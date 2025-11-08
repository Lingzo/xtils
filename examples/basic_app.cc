#include <chrono>
#include <memory>
#include <thread>
#include <vector>

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
    LogI("Compenets Init %s", xtils::type_name<SimpleService>());
    LogI("params is %d", config.get<int>("params"));
    for (int i = 0; i < 10; i++)
      ctx->PostAsyncTask(
          []() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            LogI("Run in back");
          },
          []() { LogI("Run in main"); });
    enum EventIds : xtils::EventId { EVENT_TEST = 1 };
    ctx->connect(EVENT_TEST, [this](const xtils::EventId& e) {
      LogI("On Event %d", e);
      std::this_thread::sleep_for(std::chrono::seconds(5));
    });

    ctx->PostAsyncTask([this] {
      TRACE_SCOPE("Task");
      fib(10);
      LogThis();
    });

    using namespace xtils;
    for (int i = 0; i <= 10; i++) {
      ctx->emit(EVENT_TEST);
      auto t1 = steady::GetCurrentMs();
      int ms = 1000 * i;
      ctx->delay(ms, [t1, ms] {
        LogW("Delay %dms: %d", ms, steady::GetCurrentMs() - t1);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      });
    }
    INSPECT_ROUTE("/basic_app/trace", "get trace info",
                  [](const Inspect::Request& req, Inspect::Response& resp) {
                    std::string tracer;
                    TRACE_DATA(&tracer);
                    resp = Inspect::Text(tracer);
                  });
  }

  void deinit() override { LogI("Deinit"); }
};

// call by xtils
void app_main(xtils::App& ctx, const std::vector<std::string>& args) {
  // setup service
  ctx.registor(std::make_shared<SimpleService>());
}
