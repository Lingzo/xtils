#include <xtils/app/service.h>
#include <xtils/fsm/behavior_tree.h>
#include <xtils/fsm/bt_filelogger.h>
#include <xtils/utils/file_utils.h>

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xtils/logging/logger.h"

namespace {

std::string ResolveTreeDirectory(const std::vector<std::string>& argv) {
  if (argv.size() > 1 && file_utils::is_directory(argv[1])) {
    return argv[1];
  }

  std::vector<std::string> candidates = {"./bt_trees", "./examples/bt_trees"};
  if (!argv.empty()) {
    auto bin_dir = file_utils::dirname(argv[0]);
    candidates.push_back(file_utils::join_path(bin_dir, "bt_trees"));
    candidates.push_back(
        file_utils::join_path(bin_dir, "../../examples/bt_trees"));
  }

  for (const auto& candidate : candidates) {
    if (file_utils::is_directory(candidate)) {
      return candidate;
    }
  }

  return "./bt_trees";
}

std::string ResolveMainTreeName(const std::vector<std::string>& argv) {
  return argv.size() > 2 ? argv[2] : "main";
}

}  // namespace

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
      if (value_in) {
        blackboard_->set("example_key", *value_in + 1);
      }
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
  BtService(std::string tree_dir, std::string main_tree_name)
      : Service("behavior_tree"),
        tree_dir_(std::move(tree_dir)),
        main_tree_name_(std::move(main_tree_name)) {
    factory.RegisterSimpleAction(
        []() {
          LogI("Simple action executed");
          return xtils::Status::Success;
        },
        "MySimpleAction");

    factory.Register<ActionWithBlackboard>("ActionWithBlackboard");
    factory.Register<PatrolAction>("PatrolAction");
    factory.Register<EventReceivedAction>("EventReceivedAction");
  }

  void init() override {
    LogI("=== Behavior Tree Factory Nodes ===");
    LogI("%s", factory.dump().c_str());
    file_utils::write("./bt_nodes.json", factory.dump());

    try {
      auto loaded = factory.LoadTreesFromDirectory(tree_dir_);
      LogI("Loaded %zu behavior tree files from %s", loaded, tree_dir_.c_str());
      tree_ = factory.buildFromRegisteredTree(
          main_tree_name_, nullptr,
          std::make_shared<xtils::BtFileLogger>("./bt.log"));
    } catch (const std::exception& e) {
      LogE("Failed to initialize behavior tree from %s: %s", tree_dir_.c_str(),
           e.what());
      return;
    }

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
  std::string tree_dir_;
  std::string main_tree_name_;
  xtils::BtFactory factory;
  xtils::BtTree::Ptr tree_;
};

void app_main(xtils::App& app, const std::vector<std::string>& argv) {
  app.registor(std::make_shared<BtService>(ResolveTreeDirectory(argv),
                                           ResolveMainTreeName(argv)));
}
