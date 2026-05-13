#include "xtils/fsm/behavior_tree.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// ============================================================================
// AnyData
// ============================================================================

TEST_CASE("AnyData: make and get") {
  SUBCASE("int") {
    auto data = make_any_data<int>(42);
    CHECK(data.get<int>() != nullptr);
    CHECK(*data.get<int>() == 42);
    CHECK(data.get<std::string>() == nullptr);
  }

  SUBCASE("string") {
    auto data = make_any_data<std::string>("hello");
    CHECK(data.get<std::string>() != nullptr);
    CHECK(*data.get<std::string>() == "hello");
    CHECK(data.get<int>() == nullptr);
  }

  SUBCASE("default constructed") {
    AnyData data;
    CHECK(data.get<int>() == nullptr);
  }
}

// ============================================================================
// AnyMap (Blackboard)
// ============================================================================

TEST_CASE("AnyMap: basic operations") {
  AnyMap bb;

  SUBCASE("set and get") {
    bb.set<int>("x", 42);
    auto val = bb.get<int>("x");
    CHECK(val.has_value());
    CHECK(val.value() == 42);
  }

  SUBCASE("get missing key") {
    auto val = bb.get<int>("missing");
    CHECK_FALSE(val.has_value());
  }

  SUBCASE("get wrong type") {
    bb.set<int>("x", 42);
    auto val = bb.get<std::string>("x");
    CHECK_FALSE(val.has_value());
  }

  SUBCASE("has") {
    CHECK_FALSE(bb.has("x"));
    bb.set<int>("x", 1);
    CHECK(bb.has("x"));
  }

  SUBCASE("try_str") {
    bb.set<std::string>("s", "hello");
    std::string out;
    CHECK(bb.try_str("s", out));
    CHECK(out == "hello");

    // Non-string
    bb.set<int>("i", 1);
    CHECK_FALSE(bb.try_str("i", out));

    // Missing
    CHECK_FALSE(bb.try_str("missing", out));
  }

  SUBCASE("size and keys") {
    CHECK(bb.size() == 0);
    bb.set<int>("a", 1);
    bb.set<int>("b", 2);
    CHECK(bb.size() == 2);

    auto k = bb.keys();
    CHECK(k.size() == 2);
  }

  SUBCASE("clear") {
    bb.set<int>("a", 1);
    bb.set<int>("b", 2);
    bb.clear();
    CHECK(bb.size() == 0);
    CHECK_FALSE(bb.has("a"));
  }

  SUBCASE("iteration") {
    bb.set<int>("a", 1);
    bb.set<int>("b", 2);
    int count = 0;
    for (auto& [k, v] : bb) {
      (void)k;
      (void)v;
      count++;
    }
    CHECK(count == 2);
  }

  SUBCASE("overwrite") {
    bb.set<int>("x", 1);
    bb.set<int>("x", 2);
    CHECK(bb.get<int>("x").value() == 2);
    CHECK(bb.size() == 1);
  }
}

// ============================================================================
// AlwaysSuccess / AlwaysFailure
// ============================================================================

TEST_CASE("AlwaysSuccess") {
  AlwaysSuccess node("as");
  CHECK(node.getType() == Type::Action);
  CHECK(node.getName() == "as");
  CHECK(node.getStatus() == Status::Idle);

  auto s = node.tick();
  CHECK(s == Status::Success);
  CHECK(node.getStatus() == Status::Success);
}

TEST_CASE("AlwaysFailure") {
  AlwaysFailure node;
  CHECK(node.tick() == Status::Failure);
  CHECK(node.getStatus() == Status::Failure);
}

// ============================================================================
// Node reset
// ============================================================================

TEST_CASE("Node: reset returns to Idle") {
  AlwaysSuccess node;
  node.tick();
  CHECK(node.getStatus() == Status::Success);
  node.reset();
  CHECK(node.getStatus() == Status::Idle);
}

// ============================================================================
// SimpleAction
// ============================================================================

TEST_CASE("SimpleAction") {
  SUBCASE("returns Success") {
    SimpleAction sa([]() { return Status::Success; });
    CHECK(sa.tick() == Status::Success);
  }

  SUBCASE("returns Failure") {
    SimpleAction sa([]() { return Status::Failure; });
    CHECK(sa.tick() == Status::Failure);
  }

  SUBCASE("returns Running then Success") {
    int count = 0;
    SimpleAction sa([&count]() {
      return (++count < 3) ? Status::Running : Status::Success;
    });
    CHECK(sa.tick() == Status::Running);
    CHECK(sa.tick() == Status::Running);
    CHECK(sa.tick() == Status::Success);
  }
}

// ============================================================================
// Sequence
// ============================================================================

TEST_CASE("Sequence") {
  SUBCASE("all succeed") {
    auto seq = std::make_shared<Sequence>("seq");
    seq->addChild(std::make_shared<AlwaysSuccess>());
    seq->addChild(std::make_shared<AlwaysSuccess>());
    seq->addChild(std::make_shared<AlwaysSuccess>());
    CHECK(seq->tick() == Status::Success);
  }

  SUBCASE("first failure stops") {
    auto seq = std::make_shared<Sequence>("seq");
    seq->addChild(std::make_shared<AlwaysSuccess>());
    seq->addChild(std::make_shared<AlwaysFailure>());
    seq->addChild(std::make_shared<AlwaysSuccess>());
    CHECK(seq->tick() == Status::Failure);
  }

  SUBCASE("empty sequence") {
    auto seq = std::make_shared<Sequence>("seq");
    CHECK(seq->tick() == Status::Success);
  }
}

// ============================================================================
// Selector
// ============================================================================

TEST_CASE("Selector") {
  SUBCASE("first success") {
    auto sel = std::make_shared<Selector>("sel");
    sel->addChild(std::make_shared<AlwaysFailure>());
    sel->addChild(std::make_shared<AlwaysSuccess>());
    sel->addChild(std::make_shared<AlwaysFailure>());
    CHECK(sel->tick() == Status::Success);
  }

  SUBCASE("all fail") {
    auto sel = std::make_shared<Selector>("sel");
    sel->addChild(std::make_shared<AlwaysFailure>());
    sel->addChild(std::make_shared<AlwaysFailure>());
    sel->addChild(std::make_shared<AlwaysFailure>());
    CHECK(sel->tick() == Status::Failure);
  }

  SUBCASE("empty selector") {
    auto sel = std::make_shared<Selector>("sel");
    CHECK(sel->tick() == Status::Failure);
  }
}

// ============================================================================
// Inverter
// ============================================================================

TEST_CASE("Inverter") {
  SUBCASE("inverts success to failure") {
    auto inv = std::make_shared<Inverter>("inv");
    inv->setChild(std::make_shared<AlwaysSuccess>());
    CHECK(inv->tick() == Status::Failure);
  }

  SUBCASE("inverts failure to success") {
    auto inv = std::make_shared<Inverter>("inv");
    inv->setChild(std::make_shared<AlwaysFailure>());
    CHECK(inv->tick() == Status::Success);
  }

  SUBCASE("no child returns failure") {
    auto inv = std::make_shared<Inverter>("inv");
    CHECK(inv->tick() == Status::Failure);
  }
}

// ============================================================================
// BtTree: basic tick, reset, blackboard, pause/resume, events
// ============================================================================

TEST_CASE("BtTree: basic tick") {
  auto root = std::make_shared<AlwaysSuccess>();
  auto tree = std::make_shared<BtTree>(root, "test_tree");

  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtTree: reset") {
  auto root = std::make_shared<AlwaysSuccess>();
  auto tree = std::make_shared<BtTree>(root, "test_tree");

  tree->tick();
  tree->reset();
  // After reset, root is Idle again; next tick should succeed
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtTree: blackboard access") {
  auto root = std::make_shared<AlwaysSuccess>();
  auto tree = std::make_shared<BtTree>(root, "test_tree");

  tree->blackboard().set<int>("x", 42);
  CHECK(tree->blackboard().get<int>("x").value() == 42);
}

TEST_CASE("BtTree: pause and resume") {
  auto root = std::make_shared<AlwaysSuccess>();
  auto tree = std::make_shared<BtTree>(root, "test_tree");

  CHECK_FALSE(tree->isPaused());

  tree->pause();
  CHECK(tree->isPaused());
  CHECK(tree->tick() == Status::Running);

  tree->resume();
  CHECK_FALSE(tree->isPaused());
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtTree: event system") {
  auto root = std::make_shared<AlwaysSuccess>();
  auto tree = std::make_shared<BtTree>(root, "test_tree");

  SUBCASE("sendEvent and hasEvent") {
    CHECK_FALSE(tree->hasEvent(1));
    tree->sendEvent(1);
    CHECK(tree->hasEvent(1));
    CHECK_FALSE(tree->hasEvent(2));
  }

  SUBCASE("peekEvent") {
    tree->sendEvent(1);
    auto evt = tree->peekEvent(1);
    CHECK(evt.has_value());
    CHECK(evt->type == 1);
    // peek does not consume
    CHECK(tree->hasEvent(1));
  }

  SUBCASE("consumeEvent") {
    tree->sendEvent(1);
    auto evt = tree->consumeEvent(1);
    CHECK(evt.has_value());
    CHECK(evt->type == 1);
    // consumed
    CHECK_FALSE(tree->hasEvent(1));
  }

  SUBCASE("consumeEvent missing") {
    auto evt = tree->consumeEvent(99);
    CHECK_FALSE(evt.has_value());
  }

  SUBCASE("clearEvents") {
    tree->sendEvent(1);
    tree->sendEvent(2);
    tree->clearEvents();
    CHECK_FALSE(tree->hasEvent(1));
    CHECK_FALSE(tree->hasEvent(2));
  }

  SUBCASE("sendEvent with data") {
    tree->sendEvent(1, make_any_data<int>(99));
    auto evt = tree->consumeEvent(1);
    CHECK(evt.has_value());
    CHECK(evt->data.get<int>() != nullptr);
    CHECK(*evt->data.get<int>() == 99);
  }
}

// ============================================================================
// BtTree: dump and dumpTree
// ============================================================================

TEST_CASE("BtTree: dump DOT") {
  auto seq = std::make_shared<Sequence>("seq");
  seq->addChild(std::make_shared<AlwaysSuccess>("a"));
  seq->addChild(std::make_shared<AlwaysFailure>("b"));
  auto tree = std::make_shared<BtTree>(seq, "TestTree");

  std::string dot = tree->dump();
  CHECK(dot.find("digraph") != std::string::npos);
  CHECK(dot.find("->") != std::string::npos);
}

TEST_CASE("BtTree: dumpTree JSON") {
  auto root = std::make_shared<AlwaysSuccess>("root_node");
  auto tree = std::make_shared<BtTree>(root, "TestTree");

  Json json = tree->dumpTree();
  CHECK(json.has_key("root"));
  CHECK(json.has_key("name"));
  CHECK(json.get_string("name").value() == "TestTree");
}

// ============================================================================
// BtFactory: register, buildFromJson, buildFromRegisteredTree
// ============================================================================

TEST_CASE("BtFactory: buildFromJson with AlwaysSuccess") {
  BtFactory factory;
  auto json_str = R"({"root":{"name":"AlwaysSuccess"}})";
  auto json = Json::parse(json_str);
  CHECK(json.has_value());

  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: buildFromJson with Sequence") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Sequence",
      "children": [
        {"name": "AlwaysSuccess"},
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  CHECK(json.has_value());

  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: buildFromJson with Selector") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Selector",
      "children": [
        {"name": "AlwaysFailure"},
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: buildFromJson with Inverter") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Inverter",
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Failure);
}

TEST_CASE("BtFactory: RegisterSimpleAction") {
  BtFactory factory;
  int call_count = 0;
  factory.RegisterSimpleAction(
      [&call_count]() {
        call_count++;
        return Status::Success;
      },
      "MyAction");

  auto json_str = R"({"root":{"name":"MyAction"}})";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
  CHECK(call_count == 1);
}

TEST_CASE("BtFactory: RegisterTree and buildFromRegisteredTree") {
  BtFactory factory;

  auto tree_json_str = R"({
    "name": "sub",
    "root": {"name": "AlwaysSuccess"}
  })";
  auto tree_json = Json::parse(tree_json_str);
  CHECK(tree_json.has_value());

  factory.RegisterTree("sub", *tree_json);

  auto registered = factory.getRegisteredTree("sub");
  CHECK(registered.has_value());

  auto tree = factory.buildFromRegisteredTree("sub");
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: buildFromJson unknown node throws") {
  BtFactory factory;
  auto json_str = R"({"root":{"name":"NoSuchNode"}})";
  auto json = Json::parse(json_str);
  CHECK_THROWS(factory.buildFromJson(*json));
}

TEST_CASE("BtFactory: dump") {
  BtFactory factory;
  std::string d = factory.dump();
  CHECK_FALSE(d.empty());
  // Should contain nodes registered by default
  auto parsed = Json::parse(d);
  CHECK(parsed.has_value());
  CHECK(parsed->has_key("nodes"));
}

// ============================================================================
// Delay decorator (time-based)
// ============================================================================

TEST_CASE("BtFactory: Delay decorator") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Delay",
      "ports": {"delay_ms": 20},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // First tick should return Running (delay not elapsed)
  CHECK(tree->tick() == Status::Running);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Wait action (time-based)
// ============================================================================

TEST_CASE("BtFactory: Wait action") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Wait",
      "ports": {"wait_ms": 20}
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Running);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Timeout decorator
// ============================================================================

TEST_CASE("BtFactory: Timeout expires") {
  BtFactory factory;
  // Timeout of 20ms wrapping Wait of 500ms -> should fail
  auto json_str = R"({
    "root": {
      "name": "Timeout",
      "ports": {"timeout_ms": 20},
      "children": [
        {"name": "Wait", "ports": {"wait_ms": 500}}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // First tick: Running
  CHECK(tree->tick() == Status::Running);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  // Should now be timed out -> Failure
  CHECK(tree->tick() == Status::Failure);
}

TEST_CASE("BtFactory: Timeout child completes in time") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Timeout",
      "ports": {"timeout_ms": 500},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Retry decorator
// ============================================================================

TEST_CASE("BtFactory: Retry on failure") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Retry",
      "ports": {"max_retries": 3},
      "children": [
        {"name": "AlwaysFailure"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // AlwaysFailure fails all retries -> final Failure
  CHECK(tree->tick() == Status::Failure);
}

TEST_CASE("BtFactory: Retry succeeds") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Retry",
      "ports": {"max_retries": 3},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Repeater decorator
// ============================================================================

TEST_CASE("BtFactory: Repeater succeeds after N repeats") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Repeater",
      "ports": {"repeat_count": 3},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: Repeater fails on child failure") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "Repeater",
      "ports": {"repeat_count": 3},
      "children": [
        {"name": "AlwaysFailure"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Failure);
}

// ============================================================================
// Fallback composite
// ============================================================================

TEST_CASE("BtFactory: Fallback") {
  BtFactory factory;

  SUBCASE("first success") {
    auto json_str = R"({
      "root": {
        "name": "Fallback",
        "children": [
          {"name": "AlwaysFailure"},
          {"name": "AlwaysSuccess"}
        ]
      }
    })";
    auto json = Json::parse(json_str);
    auto tree = factory.buildFromJson(*json);
    CHECK(tree->tick() == Status::Success);
  }

  SUBCASE("all fail") {
    auto json_str = R"({
      "root": {
        "name": "Fallback",
        "children": [
          {"name": "AlwaysFailure"},
          {"name": "AlwaysFailure"}
        ]
      }
    })";
    auto json = Json::parse(json_str);
    auto tree = factory.buildFromJson(*json);
    CHECK(tree->tick() == Status::Failure);
  }
}

// ============================================================================
// RandomSelector composite
// ============================================================================

TEST_CASE("BtFactory: RandomSelector") {
  BtFactory factory;

  SUBCASE("has one success among failures") {
    auto json_str = R"({
      "root": {
        "name": "RandomSelector",
        "children": [
          {"name": "AlwaysFailure"},
          {"name": "AlwaysFailure"},
          {"name": "AlwaysSuccess"}
        ]
      }
    })";
    auto json = Json::parse(json_str);
    auto tree = factory.buildFromJson(*json);
    CHECK(tree->tick() == Status::Success);
  }

  SUBCASE("all fail") {
    auto json_str = R"({
      "root": {
        "name": "RandomSelector",
        "children": [
          {"name": "AlwaysFailure"},
          {"name": "AlwaysFailure"}
        ]
      }
    })";
    auto json = Json::parse(json_str);
    auto tree = factory.buildFromJson(*json);
    CHECK(tree->tick() == Status::Failure);
  }
}

// ============================================================================
// SubTree
// ============================================================================

TEST_CASE("BtFactory: SubTree with registered tree") {
  BtFactory factory;

  // Register a simple subtree
  auto sub_json_str = R"({
    "name": "my_subtree",
    "root": {"name": "AlwaysSuccess"}
  })";
  auto sub_json = Json::parse(sub_json_str);
  factory.RegisterTree("my_subtree", *sub_json);

  // Build main tree referencing the subtree
  auto json_str = R"({
    "root": {
      "name": "SubTree",
      "ports": {"tree_name": "my_subtree"}
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: SubTree with inline tree") {
  BtFactory factory;

  auto json_str = R"({
    "root": {
      "name": "SubTree",
      "subtree": {
        "root": {"name": "AlwaysSuccess"}
      }
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);
  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// WaitForEvent
// ============================================================================

TEST_CASE("BtFactory: WaitForEvent receives event") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "WaitForEvent",
      "ports": {"event_type": 1, "timeout_ms": 500}
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // First tick: Running (no event yet)
  CHECK(tree->tick() == Status::Running);

  // Send event
  tree->sendEvent(1);

  // Next tick: Success (event consumed)
  CHECK(tree->tick() == Status::Success);
}

TEST_CASE("BtFactory: WaitForEvent timeout") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "WaitForEvent",
      "ports": {"event_type": 1, "timeout_ms": 20}
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  CHECK(tree->tick() == Status::Running);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(tree->tick() == Status::Failure);
}

// ============================================================================
// EventGuard
// ============================================================================

TEST_CASE("BtFactory: EventGuard interrupts child") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "EventGuard",
      "ports": {"event_type": 1, "interrupt_mode": 0, "return_status": 1},
      "children": [
        {"name": "Wait", "ports": {"wait_ms": 1000}}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // First tick: Running (child is waiting)
  CHECK(tree->tick() == Status::Running);

  // Send interrupt event
  tree->sendEvent(1);

  // Next tick: Failure (interrupt_mode=0 deferred, return_status=1 Failure)
  CHECK(tree->tick() == Status::Failure);
}

TEST_CASE("BtFactory: EventGuard no event child completes") {
  BtFactory factory;
  auto json_str = R"({
    "root": {
      "name": "EventGuard",
      "ports": {"event_type": 1, "interrupt_mode": 0, "return_status": 1},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json);

  // No event, child completes normally
  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Ports: InputPort / OutputPort with blackboard references
// ============================================================================

TEST_CASE("BtFactory: ports with blackboard reference") {
  BtFactory factory;

  // Register a custom action that reads input port and writes output port
  bool read_ok = false;
  int read_val = 0;

  factory.RegisterSimpleAction(
      [&]() {
        // This simple action cannot read ports since it's not a real node
        // with data_ wired up. Test port wiring via factory JSON instead.
        return Status::Success;
      },
      "PortAction");

  // Test blackboard reference via Wait + blackboard set
  auto bb = std::make_shared<AnyMap>();
  bb->set<double>("my_delay", 0.0);

  auto json_str = R"({
    "root": {
      "name": "Delay",
      "ports": {"delay_ms": "&my_delay"},
      "children": [
        {"name": "AlwaysSuccess"}
      ]
    }
  })";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json, bb);

  // Delay reads "&my_delay" from blackboard (value 0.0), should pass through
  CHECK(tree->tick() == Status::Success);
}

// ============================================================================
// Shared blackboard between main tree and subtree
// ============================================================================

TEST_CASE("BtFactory: shared blackboard") {
  BtFactory factory;

  auto bb = std::make_shared<AnyMap>();
  bb->set<int>("counter", 0);

  auto json_str = R"({"root":{"name":"AlwaysSuccess"}})";
  auto json = Json::parse(json_str);
  auto tree = factory.buildFromJson(*json, bb);

  tree->blackboard().set<int>("counter", 10);
  CHECK(bb->get<int>("counter").value() == 10);
}

int main() {
  doctest::Context context;
  int result = context.run();
  return result;
}
