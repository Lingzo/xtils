#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <thread>

#include "xtils/app/app.h"
#include "xtils/app/service.h"
#include "xtils/config/config.h"
#include "xtils/debug/inspect.h"
#include "xtils/debug/tracer.h"
#include "xtils/logging/logger.h"
#include "xtils/tasks/event.h"
#include "xtils/utils/time_utils.h"

class SimpleService : public xtils::Service {
 public:
  SimpleService() : xtils::Service("simple") {
    config.define("params", "params", 0);
    config.define("p.level.1", "p.level.1", false);
    config.define("p.level.3", "p.level.3", "string");
  }
  int fib(int n) {
    TRACE_SCOPE("Fib");
    if (n <= 1) return n;
    std::this_thread::sleep_for(std::chrono::milliseconds(n * 10));
    return fib(n - 1) + fib(n - 2);
  }
  void init() override {
    LogI("Compenets Init");
    LogI("params is %d", config.get<int>("params"));
    for (int i = 0; i < 10; i++)
      ctx->PostAsyncTask(
          []() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            LogI("Run in back");
          },
          []() { LogI("Run in main"); });
    ctx->connect(1, [this](const xtils::Event& e) {
      LogI("On Event %d", e.id);
      std::this_thread::sleep_for(std::chrono::seconds(5));
      ctx->PostAsyncTask([this, e]() { ctx->emit(e); });
    });

    ctx->PostAsyncTask([this] {
      TRACE_SCOPE("Task");
      fib(10);
      LogThis();
    });

    using namespace xtils::time;
    for (int i = 0; i <= 10; i++) {
      auto t1 = steady::GetCurrentMs();
      int ms = 1000 * i;
      ctx->delay(ms, [t1, ms] {
        LogW("Delay %dms: %d", ms, steady::GetCurrentMs() - t1);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      });
    }
    INSPECT_ROUTE(
        "/basic_app/trace", "get trace info",
        [](const xtils::Inspect::Request& req, xtils::Inspect::Response& resp) {
          std::string tracer;
          TRACE_DATA(&tracer);
          resp.sendText(tracer);
        });
  }

  void deinit() override { LogI("Deinit"); }
};

// call by xtils
void app_version(uint32_t& major, uint32_t& minor, uint32_t& patch,
                 std::string& build_time) {
  major = 0;
  minor = 1;
  patch = 2;
  build_time = __DATE__ " " __TIME__;
}

// call by xtils
void app_main(int argc, char** argv) {
  // setup service
  setup_srv(std::make_shared<SimpleService>());
}
