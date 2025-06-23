#include "xtils/fsm/fsm.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils::fsm;

// Test Events
enum TestEvents : EventType {
  EVENT_A = 1,
  EVENT_B = 2,
  EVENT_C = 3,
  EVENT_TIMER = 4,
  EVENT_INVALID = 999
};

TEST_CASE("FSM Basic State Management") {
  FSM fsm;

  SUBCASE("Add states") {
    auto state1_id = fsm.addState("State1");
    auto state2_id = fsm.addState("State2");

    CHECK(state1_id != state2_id);
    CHECK(fsm.getStateId("State1") == state1_id);
    CHECK(fsm.getStateId("State2") == state2_id);
    CHECK(fsm.getStateName(state1_id) == "State1");
    CHECK(fsm.getStateName(state2_id) == "State2");
  }

  SUBCASE("Duplicate state names should throw") {
    fsm.addState("DuplicateState");
    CHECK_THROWS_AS(fsm.addState("DuplicateState"), FSMException);
  }

  SUBCASE("Get non-existent state should throw") {
    CHECK_THROWS_AS(fsm.getStateId("NonExistent"), StateNotFoundException);
  }

  SUBCASE("Get state name for invalid ID") {
    CHECK_FALSE(fsm.getStateName(999999).has_value());
  }
}

TEST_CASE("FSM State Callbacks") {
  FSM fsm;

  bool on_enter_called = false;
  bool on_exit_called = false;
  EventType received_event = 0;

  SUBCASE("State with callbacks") {
    auto state_id = fsm.addState(
        "CallbackState",
        [&](const State& state, EventType event) {
          on_enter_called = true;
          received_event = event;
        },
        [&](const State& state, EventType event) { on_exit_called = true; });

    auto state2_id = fsm.addState("State2");

    fsm.addTransition("CallbackState", "State2", EVENT_A);

    fsm.start("CallbackState");
    CHECK(on_enter_called);
    CHECK(received_event == 0);  // Start event

    on_enter_called = false;
    fsm.processEvent(EVENT_A);
    CHECK(on_exit_called);
  }
}

TEST_CASE("FSM Transitions") {
  FSM fsm;

  fsm.addState("State1");
  fsm.addState("State2");
  fsm.addState("State3");

  SUBCASE("Basic transition") {
    fsm.addTransition("State1", "State2", EVENT_A);
    fsm.start("State1");

    CHECK(fsm.isInState("State1"));
    CHECK_FALSE(fsm.isInState("State2"));

    fsm.processEvent(EVENT_A);

    CHECK_FALSE(fsm.isInState("State1"));
    CHECK(fsm.isInState("State2"));
    CHECK(fsm.getCurrentStateName() == "State2");
  }

  SUBCASE("Multiple transitions from same state") {
    fsm.addTransition("State1", "State2", EVENT_A);
    fsm.addTransition("State1", "State3", EVENT_B);
    fsm.start("State1");

    fsm.processEvent(EVENT_B);
    CHECK(fsm.isInState("State3"));

    fsm.reset("State1");
    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("State2"));
  }

  SUBCASE("No valid transition") {
    fsm.addTransition("State1", "State2", EVENT_A);
    fsm.start("State1");

    fsm.processEvent(EVENT_B);       // No transition for this event
    CHECK(fsm.isInState("State1"));  // Should stay in same state
  }

  SUBCASE("Transition with multiple events") {
    std::vector<EventType> events = {EVENT_A, EVENT_B, EVENT_C};
    fsm.addTransition("State1", "State2", events);
    fsm.start("State1");

    fsm.processEvent(EVENT_B);
    CHECK(fsm.isInState("State2"));

    fsm.reset("State1");
    fsm.processEvent(EVENT_C);
    CHECK(fsm.isInState("State2"));
  }
}

TEST_CASE("FSM Transition Conditions") {
  FSM fsm;

  fsm.addState("State1");
  fsm.addState("State2");

  SUBCASE("Guard conditions") {
    bool allow_transition = true;

    auto guard = makeGuard(
        "test_guard", [&](const State& from, const State& to, EventType event) {
          return allow_transition;
        });

    fsm.addTransition("State1", "State2", EVENT_A, guard);
    fsm.start("State1");

    // Guard allows transition
    allow_transition = true;
    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("State2"));

    // Reset and test blocked transition
    fsm.reset("State1");
    allow_transition = false;
    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("State1"));  // Should stay in State1
  }

  SUBCASE("Action conditions") {
    bool action_executed = false;

    auto action = makeAction("test_action",
                             [&](const State& from, const State& to,
                                 EventType event) { action_executed = true; });

    fsm.addTransition("State1", "State2", EVENT_A, action);
    fsm.start("State1");

    fsm.processEvent(EVENT_A);
    CHECK(action_executed);
    CHECK(fsm.isInState("State2"));
  }

  SUBCASE("Combined guard and action") {
    bool guard_checked = false;
    bool action_executed = false;

    auto condition = makeCondition(
        "combined_condition",
        [&](const State& from, const State& to, EventType event) {
          guard_checked = true;
          return true;
        },
        [&](const State& from, const State& to, EventType event) {
          action_executed = true;
        });

    fsm.addTransition("State1", "State2", EVENT_A, condition);
    fsm.start("State1");

    fsm.processEvent(EVENT_A);
    CHECK(guard_checked);
    CHECK(action_executed);
    CHECK(fsm.isInState("State2"));
  }
}

TEST_CASE("FSM Control Operations") {
  FSM fsm;

  fsm.addState("Initial");
  fsm.addState("Running");
  fsm.addState("Stopped");

  SUBCASE("Start FSM") {
    CHECK_FALSE(fsm.getCurrentStateId().has_value());

    fsm.start("Initial");
    CHECK(fsm.getCurrentStateId().has_value());
    CHECK(fsm.isInState("Initial"));
  }

  SUBCASE("Start with invalid state") {
    CHECK_THROWS_AS(fsm.start("NonExistent"), StateNotFoundException);
  }

  SUBCASE("Reset FSM") {
    fsm.addTransition("Initial", "Running", EVENT_A);
    fsm.start("Initial");
    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("Running"));

    fsm.reset("Stopped");
    CHECK(fsm.isInState("Stopped"));
  }

  SUBCASE("Process event without starting") {
    CHECK_THROWS_AS(fsm.processEvent(EVENT_A), FSMException);
  }
}

TEST_CASE("FSM History Management") {
  FSM fsm;

  fsm.addState("State1");
  fsm.addState("State2");
  fsm.addTransition("State1", "State2", EVENT_A);

  SUBCASE("History tracking") {
    fsm.start("State1");
    CHECK(fsm.getHistory().size() == 1);  // Start event

    fsm.processEvent(EVENT_A);
    CHECK(fsm.getHistory().size() == 2);  // Transition event

    const auto& history = fsm.getHistory();
    CHECK(history[0].transition_occurred == true);  // Start
    CHECK(history[1].transition_occurred == true);  // Transition
  }

  SUBCASE("History size limit") {
    fsm.setMaxHistorySize(2);

    fsm.start("State1");
    fsm.processEvent(EVENT_A);        // Should have 2 entries
    fsm.processEvent(EVENT_INVALID);  // Should trigger history cleanup

    CHECK(fsm.getHistory().size() <= 2);
  }

  SUBCASE("Clear history") {
    fsm.start("State1");
    fsm.processEvent(EVENT_A);
    CHECK(fsm.getHistory().size() > 0);

    fsm.clearHistory();
    CHECK(fsm.getHistory().size() == 0);
  }
}

TEST_CASE("FSM DOT Graph Generation") {
  FSM fsm;

  fsm.addState("Start");
  fsm.addState("Process");
  fsm.addState("End");

  fsm.addTransition("Start", "Process", EVENT_A);
  fsm.addTransition("Process", "End", EVENT_B);

  SUBCASE("Generate DOT graph") {
    std::string dot = fsm.toDotGraph();

    CHECK(dot.find("digraph FSM") != std::string::npos);
    CHECK(dot.find("Start") != std::string::npos);
    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("End") != std::string::npos);
    CHECK(dot.find("->") != std::string::npos);
  }

  SUBCASE("Current state highlighting") {
    fsm.start("Process");
    std::string dot = fsm.toDotGraph();

    // Current state should be highlighted
    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("fillcolor=lightblue") != std::string::npos);
  }
}

TEST_CASE("FSM Thread Safety") {
  FSM fsm;
  fsm.enableThreadSafety(true);

  fsm.addState("ThreadSafe1");
  fsm.addState("ThreadSafe2");
  fsm.addTransition("ThreadSafe1", "ThreadSafe2", EVENT_A);

  SUBCASE("Thread-safe operations") {
    fsm.start("ThreadSafe1");
    CHECK(fsm.isInState("ThreadSafe1"));

    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("ThreadSafe2"));

    // These operations should work without throwing in thread-safe mode
    auto current_state = fsm.getCurrentStateName();
    CHECK(current_state.has_value());

    const auto& history = fsm.getHistory();
    CHECK(history.size() > 0);
  }
}

TEST_CASE("FSM Error Handling") {
  FSM fsm;

  SUBCASE("Invalid state operations") {
    CHECK_THROWS_AS(fsm.getStateId("Invalid"), StateNotFoundException);
    CHECK_THROWS_AS(fsm.start("Invalid"), StateNotFoundException);
    CHECK_THROWS_AS(fsm.reset("Invalid"), StateNotFoundException);
  }

  SUBCASE("Invalid transition operations") {
    fsm.addState("Valid");

    // Try to add transition with invalid states
    CHECK_THROWS_AS(fsm.addTransition("Invalid", "Valid", EVENT_A),
                    StateNotFoundException);
    CHECK_THROWS_AS(fsm.addTransition("Valid", "Invalid", EVENT_A),
                    StateNotFoundException);
  }
}

TEST_CASE("Traffic Light Example") {
  FSM fsm;

  // Create a traffic light system
  fsm.addState("Red");
  fsm.addState("Yellow");
  fsm.addState("Green");
  fsm.addState("Emergency");

  SUBCASE("Traffic light cycle") {
    fsm.addTransition("Red", "Green", EVENT_TIMER);
    fsm.addTransition("Green", "Yellow", EVENT_TIMER);
    fsm.addTransition("Yellow", "Red", EVENT_TIMER);

    // Emergency transitions from any state
    std::vector<std::string> normal_states = {"Red", "Yellow", "Green"};
    for (const auto& state : normal_states) {
      fsm.addTransition(state, "Emergency", EVENT_C);
    }
    fsm.addTransition("Emergency", "Red", EVENT_A);

    fsm.start("Red");

    // Normal cycle
    fsm.processEvent(EVENT_TIMER);  // Red -> Green
    CHECK(fsm.isInState("Green"));

    fsm.processEvent(EVENT_TIMER);  // Green -> Yellow
    CHECK(fsm.isInState("Yellow"));

    fsm.processEvent(EVENT_TIMER);  // Yellow -> Red
    CHECK(fsm.isInState("Red"));

    // Emergency override
    fsm.processEvent(EVENT_C);
    CHECK(fsm.isInState("Emergency"));

    // Return to normal
    fsm.processEvent(EVENT_A);
    CHECK(fsm.isInState("Red"));
  }
}

TEST_CASE("Door State Machine Example") {
  FSM fsm;

  enum DoorEvents : EventType {
    OPEN_CMD = 10,
    CLOSE_CMD = 11,
    LOCK_CMD = 12,
    UNLOCK_CMD = 13
  };

  fsm.addState("Closed");
  fsm.addState("Open");
  fsm.addState("Locked");

  SUBCASE("Door operations") {
    // Add transitions with guards
    auto door_guard =
        makeGuard("door_security_check",
                  [](const State& from, const State& to, EventType event) {
                    if (event == OPEN_CMD && from.name() == "Locked") {
                      return false;  // Cannot open locked door
                    }
                    return true;
                  });

    // Normal transitions
    fsm.addTransition("Closed", "Open", OPEN_CMD);
    fsm.addTransition("Open", "Closed", CLOSE_CMD);
    fsm.addTransition("Closed", "Locked", LOCK_CMD);
    fsm.addTransition("Locked", "Closed", UNLOCK_CMD);

    // Guarded transition
    fsm.addTransition("Locked", "Open", OPEN_CMD, door_guard);

    fsm.start("Closed");

    fsm.processEvent(OPEN_CMD);  // Closed -> Open
    CHECK(fsm.isInState("Open"));

    fsm.processEvent(CLOSE_CMD);  // Open -> Closed
    CHECK(fsm.isInState("Closed"));

    fsm.processEvent(LOCK_CMD);  // Closed -> Locked
    CHECK(fsm.isInState("Locked"));

    fsm.processEvent(OPEN_CMD);  // LOCKED -> LOCKED (blocked by guard)
    CHECK(fsm.isInState("Locked"));

    fsm.processEvent(UNLOCK_CMD);  // Locked -> Closed
    CHECK(fsm.isInState("Closed"));
  }
}

// Simple test runner
int main() {
  doctest::Context context;

  // Run tests
  int result = context.run();

  if (result == 0) {
    std::cout << "All FSM tests passed!" << std::endl;
  } else {
    std::cout << "Some FSM tests failed!" << std::endl;
  }

  return result;
}
