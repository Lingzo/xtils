#include "xtils/app/app.h"

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "xtils/app/auto-gen.h"
#include "xtils/app/service.h"
#include "xtils/debug/inspect.h"
#include "xtils/debug/tracer.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/sink.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"
#include "xtils/utils/file_utils.h"
#include "xtils/utils/json.h"
#include "xtils/utils/string_utils.h"
#include "xtils/utils/time_utils.h"

namespace xtils {

App::App() { default_config(); }
App::~App() {
  if (main_.joinable()) {
    main_.join();
  }
}

App *App::Ins() {
  static App app;
  return &app;
}

void App::default_config() {
  config_.Define("xtils.threads", "threds numbers", 4)
      .Define("xtils.inspect.enable", "enable inspect or not", true)
      .Define("xtils.inspect.port", "inspect prot", 9090)
      .Define("xtils.inspect.addr", "inspect address", "0.0.0.0")
      .Define("xtils.inspect.cors", "inspect cross addr", "*")
      .Define("xtils.log.file.name", "log file name,default in current dir",
              "./log/app.log")
      .Define("xtils.log.file.max_bytes", "log file size, default 4M",
              4 * 1024 * 1024)
      .Define("xtils.log.file.max_items", "max file number, app.max.log", 5)
      .Define("xtils.log.file.enable", "log to file or not", false)
      .Define("xtils.log.level",
              "log level: 0 trace, 1 debug, 2 info, 3 warn, 4 error", 1)
      .Define("xtils.log.console.enable", "log to console or not", true);
}
void App::parse_args(const std::vector<std::string> &args, bool allow_exit) {
  if (!config_.ParseArgs(args, allow_exit)) {
    std::cerr << "Failed to parse command line arguments" << std::endl;
    std::cerr << config_.Help() << std::endl;
    exit(1);
  }

  // Validate configuration
  if (!config_.Validate()) {
    std::cerr << "Configuration validation failed:" << std::endl;
    auto missing = config_.MissingRequired();
    for (const auto &key : missing) {
      std::cerr << "  Missing required parameter: " << key << std::endl;
    }
    std::cerr << config_.Help() << std::endl;
    exit(1);
  }
}
void App::Init(const std::vector<std::string> &args) {
  TRACE_SCOPE("App:init");
  // Setup signal handlers for graceful shutdown
  system::SignalHandler::Initialize();

  args_ = args;  // cache command line arguments
  // Parse command line arguments and load configuration
  parse_args(args, false);  // don't exit on help

  init_log();

  // init thread pool
  int threads_size = Conf().GetInt("xtils.threads").value();
  CHECK(threads_size > 1);
  async_tg_ = std::make_unique<TaskGroup>(threads_size);
  // init event manager
  em_ = std::make_unique<EventManager>(
      TaskGroup::Sequential(async_tg_->MainRunner()));
  timer_ = std::make_unique<SteadyTimer>(async_tg_.get());
  initialized_ = true;
}

void App::init_log() {
  TRACE_SCOPE("App:init_log");
  if (Conf().GetBool("xtils.log.console.enable").value()) {
    logger::DefaultLogger()->AddSink(std::make_unique<logger::ConsoleSink>());
  }
  if (Conf().GetBool("xtils.log.file.enable").value()) {
    std::string file = Conf().GetString("xtils.log.file.name").value();
    int max_bytes = Conf().GetInt("xtils.log.file.max_bytes").value();
    int max_items = Conf().GetInt("xtils.log.file.max_items").value();
    if (!file_utils::exists(file)) {
      bool create_dir = file_utils::mkdir(file_utils::dirname(file));
      if (!create_dir) {
        LogE("can't open log file, %s", file.c_str());
      } else {
        logger::DefaultLogger()->AddSink(
            std::make_unique<logger::FileSink>(file, max_bytes, max_items));
      }
    } else {
      logger::DefaultLogger()->AddSink(
          std::make_unique<logger::FileSink>(file, max_bytes, max_items));
    }
  }
  int log_level = Conf().GetInt("xtils.log.level").value();
  CHECK(log_level < logger::max);
  logger::SetLevel(logger::DefaultLogger(), (logger::log_level)log_level);
}

void App::init_inspect() {
#ifndef INSPECT_DISABLE
  TRACE_SCOPE("App:init_inspect");
  if (Conf().GetBool("xtils.inspect.enable").value()) {
    std::string addr = Conf().GetString("xtils.inspect.addr").value();
    int port = Conf().GetInt("xtils.inspect.port").value();
    std::string cors = Conf().GetString("xtils.inspect.cors").value();

    Inspect::Get().Init(addr, port);
    Inspect::Get().SetCORS(cors);
    LogI("inspect url http://%s:%d, cors: %s", addr.c_str(), port,
         cors.c_str());
  }

  if (Conf().GetBool("xtils.inspect.enable").value()) {
    INSPECT_ROUTE("/api/config", "config in process",
                  [this](const Inspect::Request &req, Inspect::Response &resp) {
                    resp = Inspect::Json(config_.ToJson());
                  });
    INSPECT_ROUTE("/api/tracer", "get tracer info",
                  [this](const Inspect::Request &req, Inspect::Response &resp) {
                    std::string tracer;
                    TRACE_DATA(&tracer);
                    resp = Inspect::Text(tracer);
                  });
    INSPECT_ROUTE("/api/version", "get version info",
                  [this](const Inspect::Request &req, Inspect::Response &resp) {
                    Json version;
                    version["major"] = major_;
                    version["minor"] = minor_;
                    version["patch"] = patch_;
                    version["build_time"] = build_time_;
                    resp = Inspect::Json(version);
                  });
  }
#endif
}

void App::pre_run() {
  // all config
  for (const auto &s : service_) {
    for (const auto &e : s->config.Options()) {
      const auto &opt = e.second;
      config_.Define(s->name + "." + opt.name, opt.description,
                     opt.default_value, opt.required);
    }
  }

  parse_args(args_, true);  // again with sub services

  // sub config
  for (auto &s : service_) {
    auto sub = config_.Get(s->name);
    if (sub) {
      s->config.ParseJson(*sub);
    }
  }

  print_banner();
  init_inspect();

  for (auto &p : service_) {
    p->ctx = this;
    p->Init();
    LogI("Init %s service successed!!", p->name.c_str());
  }
}

void App::RunDaemon() {
  if (running_) {
    LogW("App is already running");
    return;
  }
  main_ = std::thread(std::bind(&App::Run, this));
  bool ret = async_tg_->RunUntilCompleted([]() { return true; });
}

void App::Run() {
  CHECK(initialized_);
  if (running_) {
    LogW("App is already running");
    return;
  }
  running_ = true;
  // process service
  pre_run();
  LogI("App starting main run loop...");
  auto t1 = std::make_shared<std::atomic<int64_t>>(steady::GetCurrentMs());
  while (IsOk()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto diff_ms = steady::GetCurrentMs() - t1->load(std::memory_order_relaxed);
    if (diff_ms > 5000) {
      LogW("main threads blocked, %fms!!!", diff_ms);
    } else if (diff_ms > 2000) {
      Spawn([t1]() { t1->store(steady::GetCurrentMs(), std::memory_order_relaxed); });
    }
    if (async_tg_->IsBusy()) {
      LogW("task group is busy, maybe use more threads, cur is: %d!!!",
           async_tg_->Size());
    }
  }
  LogI("App shutting down...");
  deinit();
  LogI("Exit main");
  logger::DefaultLogger()->Shutdown();  // flush all log
  running_ = false;
}

void App::deinit() {
  TRACE_SCOPE("App::deinit");
#ifndef INSPECT_DISABLE
  if (Conf().GetBool("xtils.inspect.enable").value()) {
    Inspect::Get().Stop();
  }
#endif
  async_tg_->Stop();  // stop task group
  em_->Stop();        // stop event manager

  for (auto &p : service_) {
    p->Deinit();
  }
  em_.reset();
  timer_.reset();
  async_tg_.reset();
}

void App::Spawn(Task task) {
  async_tg_->MainRunner()->PostTask(std::move(task));
}

void App::SpawnAsync(Task task, Task main) {
  auto main_runner = async_tg_->MainRunner();
  async_tg_->PostAsyncTask(
      [task = std::move(task), main = std::move(main), main_runner]() {
        {
          TRACE_SCOPE("AsyncTask");
          task();
        }
        if (main) {
          main_runner->PostTask(std::move(main));
        }
      });
}

void App::Every(uint32_t ms, TimerCallback cb) {
  timer_->SetRepeatingTimer(ms, cb);
}

void App::Delay(uint32_t ms, TimerCallback cb) {
  timer_->SetRelativeTimer(ms, cb, TimerType::kOneShot);
}

void App::print_banner() {
  build_time_ = XTILS_BUILD_TIME;
  app_version(major_, minor_, patch_);
  const std::string fmt = R"(
================ XTILS =================
  Version : v%d.%d.%d
  Build   : %s
  Service : %s
========================================
)";
  std::stringstream ss;
  for (auto &s : service_) {
    ss << s->name << " ";
  }
  std::string names = ss.str();
  StackString<1024> banner(fmt.c_str(), major_, minor_, patch_,
                           build_time_.c_str(),
                           names.empty() ? "None" : names.c_str());
  logger::DefaultLogger()->WriteRaw(banner.c_str());
}

void App::Register(std::list<std::shared_ptr<IService>> services) {
  CHECK(!running_);
  for (auto &p : services) {
    Register(p);
  }
}

void App::Register(std::shared_ptr<IService> p) {
  CHECK(!running_);
  service_.push_back(p);
}

bool App::IsRunning() { return running_; }
}  // namespace xtils
