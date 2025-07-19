#include <chrono>
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
    config.define("p.level", "p.level", 2);
    config.define("p.level.1", "p.level.1", false);
    config.define("p.level.3", "p.level.3", "string");
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

    ctx->PostAsyncTask([] {
      std::this_thread::sleep_for(std::chrono::seconds(3));
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
          std::string trace_data = TRACE_DATA();
          resp.sendText(trace_data);
        });
  }

  void deinit() override { LogI("Deinit"); }
};

int main(int argc, char* argv[]) {
  auto app = xtils::App::instance();
  app->registor<SimpleService>();

  app->init(argc, argv);

  app->run();
  return 0;
}
