#include <chrono>
#include <thread>

#include "xtils/app/service.h"
#include "xtils/logging/logger.h"

class A : public xtils::Service {
  void init() override {
    LogI("Compenets Init");
    for (int i = 0; i < 10; i++)
      ctx->RunBackground(
          []() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            LogI("Run in back");
          },
          []() { LogI("Run in main"); });
  }
  void deinit() override { LogI("Deinit"); }
};

int main(int argc, char* argv[]) {
  xtils::App app;
  app.init(argc, argv);

  app.registor<A>();

  app.run();
  return 0;
}
