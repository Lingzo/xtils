#include <chrono>
#include <thread>

#include "xtils/app/app.h"
#include "xtils/app/service.h"
#include "xtils/logging/logger.h"
#include "xtils/tasks/event.h"

class SimpleService : public xtils::Service {
 public:
  SimpleService() : xtils::Service("SimpleService") {}

  void init() override {
    LogI("Compenets Init");
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
  }

  void deinit() override { LogI("Deinit"); }
};

int main(int argc, char* argv[]) {
  xtils::App app;
  app.init(argc, argv);

  app.registor<SimpleService>();

  app.run();
  return 0;
}
