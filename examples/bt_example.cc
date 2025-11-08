#include <xtils/app/service.h>
#include <xtils/fsm/behavior_tree.h>
#include <xtils/utils/file_utils.h>

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
                "ports": [{
                  "name": "delay",
                  "value": 10
                }],
                "children": {
                  "id": "MySimpleAction",
                  "type": 1
                }
              },
              {
                "id": "Inverter",
                "type": 2,
                "children": {
                  "id": "AlwaysFailure",
                  "type": 1
                }
              }
            ]
          },
          {
            "id": "ActionWithBlackboard",
            "type": 1,
            "name": "ActionWithBlackboard",
            "ports": [{"name": "example_key", "value": 100, "mode": 0, "type_name": "int"},
                      {"name": "my_object", "mode": 0, "type_name": "std::shared_ptr<MyCalss>", "value": "&my_object"}]
          }
        ]
      }
    }
)";

class MyCalss {
 public:
  MyCalss() {}
  void doSomething() {
    LogI("Doing something in MyCalss");
    helperFunction();
  }

 private:
  void helperFunction() { LogI("Helper function in MyCalss"); }
};

class ActionWithBlackboard : public xtils::ActionNode {
 public:
  ActionWithBlackboard(const std::string& name = "") : ActionNode(name) {}

  static std::vector<xtils::IPort> get_ports() {
    return {xtils::InputPort<int>("example_key"),
            xtils::InputPort<std::shared_ptr<MyCalss>>("my_object")};
  }
  xtils::Status OnTick() override {
    // Access blackboard data
    if (blackboard_) {
      auto value = blackboard_->get<int>("example_key");
      if (value) {
        LogI("Retrieved from blackboard: example_key = %d", *value);
        setOutput<int>("example_key", *value + 1);
      } else {
        LogW("example_key not found in blackboard");
      }

      auto obj = getInput<std::shared_ptr<MyCalss>>("my_object");
      if (obj && *obj) {
        (*obj)->doSomething();
      } else {
        LogW("my_object not found in blackboard");
      }
    }
    return xtils::Status::Success;
  }
};

class BtService : public xtils::Service {
 public:
  BtService() : Service("behavior_tree") {
    factory.RegisterSimpleAction(
        []() {
          LogI("Simple action executed");
          return xtils::Status::Success;
        },
        "MySimpleAction");
    factory.Register<ActionWithBlackboard>("ActionWithBlackboard");
  }
  void init() override {
    LogI("%s", factory.dump().c_str());
    file_utils::write("./bt_nodes.json", factory.dump());
    // Initialize the behavior tree service
    std::string example;
    file_utils::read("./tree.json", &example);
    if (example.empty()) {
      example = example_tree;
      LogW("Using default example tree");
    }
    LogI("\n%s\n", example.c_str());
    auto j = xtils::Json::parse(example);
    if (!j.has_value()) {
      LogE("Failed to parse behavior tree JSON");
      return;
    }
    auto tree = factory.buildFromJson(j.value());
    file_utils::write("./tree.json", tree->dumpTree().dump(2));
    LogI("\n%s", tree->dump().c_str());
    LogI("\n%s", tree->dumpTree().dump(2).c_str());
    auto& blackboard = tree->blackboard();
    auto obj = std::make_shared<MyCalss>();
    blackboard.set("example_key", 42);
    blackboard.set("my_object", obj);

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
