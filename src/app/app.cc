#include "xtils/app/app.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "xtils/app/service.h"
#include "xtils/debug/inspect.h"
#include "xtils/logging/logger.h"
#include "xtils/logging/sink.h"
#include "xtils/tasks/event.h"
#include "xtils/tasks/task_group.h"

namespace xtils {

namespace {
std::atomic<bool> g_shutdown_requested{false};

void SignalHandler(int signal) {
  std::cout << "Received signal " << signal << ", shutting down..."
            << std::endl;
  g_shutdown_requested.store(true);
}

void SetupSignalHandlers() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
#ifdef SIGQUIT
  std::signal(SIGQUIT, SignalHandler);
#endif
}

}  // namespace

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
              "app.log")
      .define("xtils.log.file.enable", "log to file or not", true)
      .define("xtils.log.level", "log level", 1)
      .define("xtils.log.console.enable", "log to console or not", true);
}

void App::init(int argc, char* argv[]) {
  // Setup signal handlers for graceful shutdown
  SetupSignalHandlers();

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
  em_ = std::make_unique<EventManager>(*tg_);
  init_log();
  init_inspect();
}

void App::init_log() {
  if (conf().get_bool("xtils.log.console.enable")) {
    logger::default_logger()->addSink(std::make_unique<logger::ConsoleSink>());
    int log_level = conf().get_int("xtils.log.level");
    CHECK(log_level < logger::MAX);
    logger::set_level(logger::default_logger(), (logger::log_level)log_level);
  }
}

void App::init_inspect() {
  if (conf().get_bool("xtils.inspect.enable")) {
    std::string addr = conf().get_string("xtils.inspect.addr");
    int port = conf().get_int("xtils.inspect.port");
    std::string cors = conf().get_string("xtils.inspect.cors");

    Inspect::Create(tg_->random(), port);
    Inspect::Get().SetCORS(cors);
    LogI("Start Inspect, http://%s:%d, cors: %s", addr.c_str(), port,
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
  std::atomic_int64_t t1 = now();
  while (!g_shutdown_requested) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    PostTask([&t1, now]() { t1 = now(); });
    double ms = (now() - t1) / 1e6;
    if (ms > 5000) {
      LogW("main threads blocked, %fms!!!", ms);
    } else {
      LogI("Main Running");
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
  tg_.reset();  // must be last
}

void App::PostTask(std::function<void()> task) { tg_->PostTask(task); }

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
}  // namespace xtils
