#include <chrono>
#include <thread>

#include "xtils/app/app.h"
#include "xtils/app/service.h"
#include "xtils/config/config.h"
#include "xtils/logging/logger.h"
#include "xtils/tasks/event.h"
#include "xtils/utils/time_utils.h"

class SimpleService : public xtils::Service {
 public:
  SimpleService() : xtils::Service("simple") {
    config_.define("params", "params", 0);
    config_.define("p.level", "p.level", 2);
    config_.define("p.level.1", "p.level.1", false);
    config_.define("p.level.3", "p.level.3", "string");
  }

  void init() override {
    LogI("Compenets Init");
    LogI("params is %d", config_.get<int>("params"));
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

    using namespace xtils::time_utils;
    for (int i = 0; i <= 10; i++) {
      auto t1 = steady::GetCurrentMs();
      int ms = 1000 * i;
      ctx->delay(ms, [t1, ms] {
        LogW("Delay %dms: %d", ms, steady::GetCurrentMs() - t1);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      });
    }
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
