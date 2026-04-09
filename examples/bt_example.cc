#include <xtils/app/service.h>
#include <xtils/fsm/behavior_tree.h>
#include <xtils/fsm/bt_filelogger.h>
#include <xtils/utils/file_utils.h>

#include <memory>

#include "xtils/logging/logger.h"

// Subtree: A simple patrol behavior
const std::string patrol_subtree = R"(
{
  "name": "PatrolSubtree",
  "root": {
    "name": "Sequence",
    "children": [
      {
        "name": "PatrolAction",
        "type": 1
      },
      {
        "name": "Wait",
        "ports": { "wait_ms": 500 }
      }
    ]
  }
}
)";

// Main tree with SubTree, EventGuard, and WaitForEvent
const std::string example_tree = R"(
{
  "name": "MainBehaviorTree",
  "root": {
    "name": "Selector",
    "children": [
      {
        "name": "EventGuard",
        "ports": {
          "event_type": 100,
          "interrupt_mode": 1,
          "return_status": 1
        },
        "children": [
          {
            "name": "Sequence",
            "children": [
              {
                "name": "Repeater",
                "ports": { "repeat_count": 3 },
                "children": [
                  {
                    "name": "SubTree",
                    "ports": { "tree_name": "patrol" }
                  }
                ]
              },
              {
                "name": "ActionWithBlackboard",
                "ports": {
                  "example_key": 123,
                  "my_object": "&my_object"
                }
              }
            ]
          }
        ]
      },
      {
        "name": "Sequence",
        "children": [
          {
            "name": "MySimpleAction"
          },
          {
            "name": "WaitForEvent",
            "ports": {
              "event_type": 200,
              "timeout_ms": 3000
            }
          },
          {
            "name": "EventReceivedAction"
          }
        ]
      }
    ]
  }
}
)";

class MyClass {
 public:
  MyClass() {}
  void doSomething() {
    value_++;
    LogI("Doing something in MyClass");
    helperFunction();
  }

 private:
  int value_ = 0;
  void helperFunction() { LogI("Helper function in MyClass, %d", value_); }
};

class ActionWithBlackboard : public xtils::ActionNode {
 public:
  ActionWithBlackboard(const std::string& name = "") : ActionNode(name) {}

  static xtils::Ports getPorts() {
    return {xtils::InputPort<int>("example_key"),
            xtils::InputPort<std::shared_ptr<MyClass>>("my_object")};
  }
  xtils::Status OnTick() override {
    if (blackboard_) {
      auto value = blackboard_->get<int>("example_key");
      if (value) {
        LogI("Retrieved from blackboard: example_key = %d", *value);
        setOutput<int>("example_key", *value + 1);
      } else {
        LogW("example_key not found in blackboard");
      }
      auto value_in = getInput<int>("example_key");
      blackboard_->set("example_key", *value_in + 1);
      auto obj = getInput<std::shared_ptr<MyClass>>("my_object");
      if (obj && *obj) {
        (*obj)->doSomething();
      } else {
        LogW("my_object not found in blackboard");
      }
    }
    return xtils::Status::Success;
  }
};

// Custom action for patrol behavior in subtree
class PatrolAction : public xtils::ActionNode {
 public:
  PatrolAction(const std::string& name = "") : ActionNode(name) {}

  xtils::Status OnTick() override {
    patrol_count_++;
    LogI("Patrolling... (count: %d)", patrol_count_);
    return xtils::Status::Success;
  }

 private:
  int patrol_count_ = 0;
};

// Action triggered after event is received
class EventReceivedAction : public xtils::ActionNode {
 public:
  EventReceivedAction(const std::string& name = "") : ActionNode(name) {}

  xtils::Status OnTick() override {
    LogI("Event 200 received! Executing response action.");
    return xtils::Status::Success;
  }
};

class BtService : public xtils::Service {
 public:
  BtService() : Service("behavior_tree") {
    // Register simple actions
    factory.RegisterSimpleAction(
        []() {
          LogI("Simple action executed");
          return xtils::Status::Success;
        },
        "MySimpleAction");

    // Register custom action nodes
    factory.Register<ActionWithBlackboard>("ActionWithBlackboard");
    factory.Register<PatrolAction>("PatrolAction");
    factory.Register<EventReceivedAction>("EventReceivedAction");

    // Register patrol subtree
    auto patrol_json = xtils::Json::parse(patrol_subtree);
    if (patrol_json) {
      factory.RegisterTree("patrol", patrol_json.value());
      LogI("Registered subtree: patrol");
    }
  }

  void init() override {
    LogI("=== Behavior Tree Factory Nodes ===");
    LogI("%s", factory.dump().c_str());
    file_utils::write("./bt_nodes.json", factory.dump());

    // Build main tree
    std::string tree_json_str;
    file_utils::read("./tree_new.json", &tree_json_str);
    if (tree_json_str.empty()) {
      tree_json_str = example_tree;
      LogW("Using default example tree");
    }

    auto j = xtils::Json::parse(tree_json_str);
    if (!j.has_value()) {
      LogE("Failed to parse behavior tree JSON");
      return;
    }

    tree_ = factory.buildFromJson(
        j.value(), nullptr, std::make_shared<xtils::BtFileLogger>("./bt.log"));

    file_utils::write("./tree.json", tree_->dumpTree().dump(2));
    LogI("=== Tree Structure ===\n%s", tree_->dump().c_str());

    // Initialize blackboard
    auto& blackboard = tree_->blackboard();
    blackboard.set("example_key", 42);
    blackboard.set("my_object", std::make_shared<MyClass>());

    // Main tick loop
    ctx->every(500, [this]() {
      if (tree_->isPaused()) {
        LogI("Tree is paused, skipping tick");
        return;
      }
      auto status = tree_->tick();
      LogI("Tree tick result: %d", static_cast<int>(status));
    });

    // Demo: Send interrupt event after 3 seconds
    ctx->delay(3000, [this]() {
      LogI(">>> Sending interrupt event (type=100) to stop patrol <<<");
      tree_->sendEvent(100);
    });

    // Demo: Send event 200 after 5 seconds
    ctx->delay(5000, [this]() {
      LogI(">>> Sending event (type=200) <<<");
      tree_->sendEvent(200);
    });

    // Demo: Pause tree after 7 seconds
    ctx->delay(7000, [this]() {
      LogI(">>> Pausing tree <<<");
      tree_->pause();
    });

    // Demo: Resume tree after 9 seconds
    ctx->delay(9000, [this]() {
      LogI(">>> Resuming tree <<<");
      tree_->resume();
    });

    // Demo: Log finish after 12 seconds
    ctx->delay(12000, [this]() { LogI(">>> Demo finished <<<"); });
  }

  void deinit() override { LogI("Behavior tree service stopped"); }

 private:
  xtils::BtFactory factory;
  xtils::BtTree::Ptr tree_;
};

void app_main(xtils::App& app, const std::vector<std::string>& argv) {
  app.registor(std::make_shared<BtService>());
}
