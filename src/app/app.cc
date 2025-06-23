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
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/string_utils.h"

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

App::App() {
  // Initialize the main task runner
  task_runner_ = std::make_unique<ThreadTaskRunner>(
      ThreadTaskRunner::CreateAndStart("MainTaskRunner"));
  default_config();
}

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

  // Print configuration if verbose mode is enabled
  if (config_.get_bool("verbose", false)) {
    config_.print();
  }

  // init thread pool
  int threads_size = conf().get_int("xtils.threads", 4);
  CHECK(threads_size > 0);
  for (int i = 0; i < threads_size; i++) {
    StackString<10> name("T-%02d", i);
    pool_.push_back(std::make_unique<ThreadTaskRunner>(
        ThreadTaskRunner::CreateAndStart(name.ToStr())));
  }

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

    Inspect::Create(pool_.back().get(), port);
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
  for (auto& p : components_) {
    p->ctx = this;
    p->init();
  }
  running_ = true;
  LogI("App starting main run loop...");
  while (!g_shutdown_requested) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    PostTask([]() { LogI("App is running"); });
  }

  if (conf().get_bool("xtils.inspect.enable", true)) {
    Inspect::Get().Stop();
  }

  pool_.clear();
  task_runner_.reset();
  running_ = false;
  LogI("App shutting down...");
}

void App::deinit() {
  for (auto& p : components_) {
    p->deinit();
  }
}

void App::PostTask(std::function<void()> task) {
  task_runner_->PostTask(std::move(task));
}

void App::RunBackground(std::function<void()> task,
                        std::function<void()> main) {
  static std::atomic_int idx{0};
  pool_[idx % pool_.size()]->PostTask([this, task = task, main = main]() {
    task();
    if (main) {
      task_runner_->PostTask(main);
    }
  });
  idx += 1;
}
}  // namespace xtils
