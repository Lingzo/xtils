#include <xtils/app/service.h>
#include <xtils/fsm/behavior_tree.h>

#include <memory>

#include "xtils/logging/logger.h"
const std::string example_tree = R"(
    {
      "root": {
        "id": "Selector",
        "type": 0,
        "children": [
          {
            "id": "Sequence",
            "type": 0,
            "children": [
              {
                "id": "Delay",
                "type": 2,
                "child": {
                  "id": "MySimpleAction",
                  "type": 1
                }
              },
              {
                "id": "Inverter",
                "type": 2,
                "child": {
                  "id": "AlwaysFailure",
                  "type": 1
                }
              }
            ]
          },
          {
            "id": "AlwaysSuccess",
            "type": 1
          }
        ]
      }
    }
)";

class BtService : public xtils::Service {
 public:
  BtService() : Service("Behavior Tree Service") {
    factory.RegisterSimpleAction(
        []() {
          LogI("Simple action executed");
          return xtils::Status::Success;
        },
        "MySimpleAction");
  }
  void init() override {
    LogI("%s", factory.dump().c_str());
    // Initialize the behavior tree service
    auto j = xtils::Json::parse(example_tree);
    auto tree = factory.buildFromJson(j.value());
    LogI("\n%s", tree->dump().c_str());
    ctx->every(1000, [this, tree]() {
      auto status = tree->tick();
      LogI("%d", status);
    });
  }

  void deinit() override {
    // Deinitialize the behavior tree service
  }

 private:
  xtils::BtFactory factory;
};

void app_main(xtils::App& app, const std::vector<std::string>& argv) {
  app.registor(std::make_shared<BtService>());
}
