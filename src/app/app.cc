#include "xtils/app/app.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "xtils/app/service.h"
#include "xtils/debug/inspect.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/sink.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"
#include "xtils/utils/file_utils.h"

namespace xtils {

App::App() { default_config(); }

App::~App() {
  // Cleanup will be handled by unique_ptr destructors
}

void App::default_config() {
  config_.define("xtils.threads", "threds numbers", 4)
      .define("xtils.inspect.enable", "enable inspect or not", true)
      .define("xtils.inspect.port", "inspect prot", 9090)
      .define("xtils.inspect.addr", "inspect address", "127.0.0.1")
      .define("xtils.inspect.cors", "inspect cross addr", "*")
      .define("xtils.log.file.name", "log file name,default in current dir",
              "./app.log")
      .define("xtils.log.file.max_bytes", "log file size, default 4M",
              4 * 1024 * 1024)
      .define("xtils.log.file.max_items", "max file number, app.max.log", 5)
      .define("xtils.log.file.enable", "log to file or not", true)
      .define("xtils.log.level",
              "log level: 0 trace, 1 debug, 2 info, 3 warn, 4 error", 1)
      .define("xtils.log.console.enable", "log to console or not", true);
}

void App::init(int argc, char* argv[]) {
  // Setup signal handlers for graceful shutdown
  system::SignalHandler::Initialize();

  // Parse command line arguments and load configuration
  if (!config_.parse_args(argc, argv)) {
    std::cerr << "Failed to parse command line arguments" << std::endl;
    std::cerr << config_.help() << std::endl;
    exit(1);
  }

  // Validate configuration
  if (!config_.validate()) {
    std::cerr << "Configuration validation failed:" << std::endl;
    auto missing = config_.missing_required();
    for (const auto& key : missing) {
      std::cerr << "  Missing required parameter: " << key << std::endl;
    }
    std::cerr << config_.help() << std::endl;
    exit(1);
  }

  // init thread pool
  int threads_size = conf().get_int("xtils.threads");
  CHECK(threads_size > 1);
  tg_ = std::make_unique<TaskGroup>(threads_size);
  em_ = std::make_unique<EventManager>(tg_.get());
  timer_ = std::make_unique<SteadyTimer>(tg_.get());
  init_log();
  init_inspect();
}

void App::init_log() {
  if (conf().get_bool("xtils.log.console.enable")) {
    logger::default_logger()->addSink(std::make_unique<logger::ConsoleSink>());
  }
  if (conf().get_bool("xtils.log.file.enable")) {
    std::string file = conf().get_string("xtils.log.file.name");
    int max_bytes = conf().get_int("xtils.log.file.max_bytes");
    int max_items = conf().get_int("xtils.log.file.max_items");
    if (!file_utils::exists(file)) {
      LogI("%s == ", file_utils::dirname(file).c_str());
      bool create_dir = file_utils::mkdir(file_utils::dirname(file));
      if (!create_dir) {
        LogE("can't open log file, %s", file.c_str());
      } else {
        logger::default_logger()->addSink(
            std::make_unique<logger::FileSink>(file, max_bytes, max_items));
      }
    } else {
      logger::default_logger()->addSink(
          std::make_unique<logger::FileSink>(file, max_bytes, max_items));
    }
  }
  int log_level = conf().get_int("xtils.log.level");
  CHECK(log_level < logger::MAX);
  logger::set_level(logger::default_logger(), (logger::log_level)log_level);
}

void App::init_inspect() {
  if (conf().get_bool("xtils.inspect.enable")) {
    std::string addr = conf().get_string("xtils.inspect.addr");
    int port = conf().get_int("xtils.inspect.port");
    std::string cors = conf().get_string("xtils.inspect.cors");

    Inspect::Get().Init(tg_->slave(), addr, port);
    Inspect::Get().SetCORS(cors);
    LogI("inspect url http://%s:%d, cors: %s", addr.c_str(), port,
         cors.c_str());
  }

  if (conf().get_bool("xtils.inspect.enable")) {
    INSPECT_ROUTE("/api/config", "config in process",
                  [this](const Inspect::Request& req) {
                    return Inspect::JsonResponse(config_.to_json());
                  });
  }
}
void App::run() {
  if (running_) {
    LogW("App is already running");
    return;
  }
  for (auto& p : service_) {
    p->ctx = this;
    p->init();
    LogI("Init %s service successed!!", p->name.c_str());
  }
  running_ = true;
  LogI("App starting main run loop...");
  auto now = []() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
  };
  int64_t t1 = now();
  while (!system::SignalHandler::IsShutdownRequested()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    double diff = (now() - t1) / 1e6;
    if (diff > 5000) {
      LogW("main threads blocked, %fms!!!", diff);
    } else if (diff > 2000) {
      PostTask([&t1, now]() { t1 = now(); });
    }
    if (tg_->is_busy()) {
      LogW("task group is busy, maybe use more threads, cur is: %d!!!",
           tg_->size());
    }
  }
  running_ = false;
  LogI("App shutting down...");
  deinit();
  LogI("Exit main");
}

void App::deinit() {
  if (conf().get_bool("xtils.inspect.enable")) {
    Inspect::Get().Stop();
  }
  for (auto& p : service_) {
    p->deinit();
  }
  em_.reset();
  timer_.reset();
  tg_.reset();  // must be last

  // Cleanup signal handlers
  system::SignalHandler::Cleanup();
}

void App::PostTask(Task task) { tg_->PostTask(task); }

void App::PostAsyncTask(Task task, Task main) {
  std::weak_ptr<TaskGroup> weak_ptr = tg_;
  tg_->PostAsyncTask([task = task, main = main, weak_ptr]() {
    task();
    if (main) {
      if (auto tg = weak_ptr.lock()) {
        tg->PostTask(main);
      }
    }
  });
}

void App::emit(const Event& e) { em_->emit(e); }

void App::connect(EventId id, OnEvent cb) { em_->emit(id, cb); }

void App::every(uint32_t ms, TimerCallback cb) {
  timer_->SetRepeatingTimer(ms, cb);
}

void App::delay(uint32_t ms, TimerCallback cb) {
  timer_->SetRelativeTimer(ms, cb, TimerType::kOneShot);
}
}  // namespace xtils
