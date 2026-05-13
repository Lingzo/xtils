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
    auto state1_id = fsm.AddState("State1");
    auto state2_id = fsm.AddState("State2");

    CHECK(state1_id != state2_id);
    CHECK(fsm.GetStateId("State1").value() == state1_id);
    CHECK(fsm.GetStateId("State2").value() == state2_id);
    CHECK(fsm.GetStateName(state1_id) == "State1");
    CHECK(fsm.GetStateName(state2_id) == "State2");
  }

  SUBCASE("Duplicate state names should throw") {
    fsm.AddState("DuplicateState");
    CHECK_THROWS_AS(fsm.AddState("DuplicateState"), FSMException);
  }

  SUBCASE("Get non-existent state should throw") {
    CHECK(!fsm.GetStateId("NonExistent").has_value());
  }

  SUBCASE("Get state name for invalid ID") {
    CHECK_FALSE(fsm.GetStateName(999999).has_value());
  }
}

TEST_CASE("FSM State Callbacks") {
  FSM fsm;

  bool on_enter_called = false;
  bool on_exit_called = false;
  EventType received_event = 0;

  SUBCASE("State with callbacks") {
    auto state_id = fsm.AddState(
        "CallbackState",
        [&](const State& state, EventType event) {
          on_enter_called = true;
          received_event = event;
        },
        [&](const State& state, EventType event) { on_exit_called = true; });

    auto state2_id = fsm.AddState("State2");

    fsm.AddTransition("CallbackState", "State2", EVENT_A);

    fsm.Start("CallbackState");
    CHECK(on_enter_called);
    CHECK(received_event == 0);  // Start event

    on_enter_called = false;
    fsm.ProcessEvent(EVENT_A);
    CHECK(on_exit_called);
  }
}

TEST_CASE("FSM Transitions") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");
  fsm.AddState("State3");

  SUBCASE("Basic transition") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.Start("State1");

    CHECK(fsm.IsInState("State1"));
    CHECK_FALSE(fsm.IsInState("State2"));

    fsm.ProcessEvent(EVENT_A);

    CHECK_FALSE(fsm.IsInState("State1"));
    CHECK(fsm.IsInState("State2"));
    CHECK(fsm.GetCurrentStateName() == "State2");
  }

  SUBCASE("Multiple transitions from same state") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.AddTransition("State1", "State3", EVENT_B);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);
    CHECK(fsm.IsInState("State3"));

    fsm.Reset("State1");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State2"));
  }

  SUBCASE("No valid transition") {
    fsm.AddTransition("State1", "State2", EVENT_A);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);       // No transition for this event
    CHECK(fsm.IsInState("State1"));  // Should stay in same state
  }

  SUBCASE("Transition with multiple events") {
    std::vector<EventType> events = {EVENT_A, EVENT_B, EVENT_C};
    fsm.AddTransition("State1", "State2", events);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_B);
    CHECK(fsm.IsInState("State2"));

    fsm.Reset("State1");
    fsm.ProcessEvent(EVENT_C);
    CHECK(fsm.IsInState("State2"));
  }
}

TEST_CASE("FSM Transition Conditions") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");

  SUBCASE("Guard conditions") {
    bool allow_transition = true;

    auto guard = MakeGuard(
        "test_guard", [&](const State& from, const State& to, EventType event) {
          return allow_transition;
        });

    fsm.AddTransition("State1", "State2", EVENT_A, guard);
    fsm.Start("State1");

    // Guard allows transition
    allow_transition = true;
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State2"));

    // Reset and test blocked transition
    fsm.Reset("State1");
    allow_transition = false;
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("State1"));  // Should stay in State1
  }

  SUBCASE("Action conditions") {
    bool action_executed = false;

    auto action = MakeAction("test_action",
                             [&](const State& from, const State& to,
                                 EventType event) { action_executed = true; });

    fsm.AddTransition("State1", "State2", EVENT_A, action);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_A);
    CHECK(action_executed);
    CHECK(fsm.IsInState("State2"));
  }

  SUBCASE("Combined guard and action") {
    bool guard_checked = false;
    bool action_executed = false;

    auto condition = MakeCondition(
        "combined_condition",
        [&](const State& from, const State& to, EventType event) {
          guard_checked = true;
          return true;
        },
        [&](const State& from, const State& to, EventType event) {
          action_executed = true;
        });

    fsm.AddTransition("State1", "State2", EVENT_A, condition);
    fsm.Start("State1");

    fsm.ProcessEvent(EVENT_A);
    CHECK(guard_checked);
    CHECK(action_executed);
    CHECK(fsm.IsInState("State2"));
  }
}

TEST_CASE("FSM Control Operations") {
  FSM fsm;

  fsm.AddState("Initial");
  fsm.AddState("Running");
  fsm.AddState("Stopped");

  SUBCASE("Start FSM") {
    CHECK_FALSE(fsm.GetCurrentStateId().has_value());

    fsm.Start("Initial");
    CHECK(fsm.GetCurrentStateId().has_value());
    CHECK(fsm.IsInState("Initial"));
  }

  SUBCASE("Start with invalid state") {
    CHECK_THROWS_AS(fsm.Start("NonExistent"), StateNotFoundException);
  }

  SUBCASE("Reset FSM") {
    fsm.AddTransition("Initial", "Running", EVENT_A);
    fsm.Start("Initial");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("Running"));

    fsm.Reset("Stopped");
    CHECK(fsm.IsInState("Stopped"));
  }

  SUBCASE("Process event without starting") {
    CHECK_THROWS_AS(fsm.ProcessEvent(EVENT_A), FSMException);
  }
}

TEST_CASE("FSM History Management") {
  FSM fsm;

  fsm.AddState("State1");
  fsm.AddState("State2");
  fsm.AddTransition("State1", "State2", EVENT_A);

  SUBCASE("History tracking") {
    fsm.Start("State1");
    CHECK(fsm.GetHistory().size() == 1);  // Start event

    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.GetHistory().size() == 2);  // Transition event

    const auto& history = fsm.GetHistory();
    CHECK(history[0].transition_occurred == true);  // Start
    CHECK(history[1].transition_occurred == true);  // Transition
  }

  SUBCASE("History size limit") {
    fsm.SetMaxHistorySize(2);

    fsm.Start("State1");
    fsm.ProcessEvent(EVENT_A);        // Should have 2 entries
    fsm.ProcessEvent(EVENT_INVALID);  // Should trigger history cleanup

    CHECK(fsm.GetHistory().size() <= 2);
  }

  SUBCASE("Clear history") {
    fsm.Start("State1");
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.GetHistory().size() > 0);

    fsm.ClearHistory();
    CHECK(fsm.GetHistory().size() == 0);
  }
}

TEST_CASE("FSM DOT Graph Generation") {
  FSM fsm;

  fsm.AddState("Start");
  fsm.AddState("Process");
  fsm.AddState("End");

  fsm.AddTransition("Start", "Process", EVENT_A);
  fsm.AddTransition("Process", "End", EVENT_B);

  SUBCASE("Generate DOT graph") {
    std::string dot = fsm.ToDotGraph();

    CHECK(dot.find("digraph FSM") != std::string::npos);
    CHECK(dot.find("Start") != std::string::npos);
    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("End") != std::string::npos);
    CHECK(dot.find("->") != std::string::npos);
  }

  SUBCASE("Current state highlighting") {
    fsm.Start("Process");
    std::string dot = fsm.ToDotGraph();

    // Current state should be highlighted
    CHECK(dot.find("Process") != std::string::npos);
    CHECK(dot.find("style=filled,color=red") != std::string::npos);
  }
}

TEST_CASE("FSM Thread Safety") {
  FSM fsm;
  fsm.EnableThreadSafety(true);

  fsm.AddState("ThreadSafe1");
  fsm.AddState("ThreadSafe2");
  fsm.AddTransition("ThreadSafe1", "ThreadSafe2", EVENT_A);

  SUBCASE("Thread-safe operations") {
    fsm.Start("ThreadSafe1");
    CHECK(fsm.IsInState("ThreadSafe1"));

    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("ThreadSafe2"));

    // These operations should work without throwing in thread-safe mode
    auto current_state = fsm.GetCurrentStateName();
    CHECK(current_state.has_value());

    const auto& history = fsm.GetHistory();
    CHECK(history.size() > 0);
  }
}

TEST_CASE("FSM Error Handling") {
  FSM fsm;

  SUBCASE("Invalid state operations") {
    CHECK_THROWS_AS(fsm.Start("Invalid"), StateNotFoundException);
    CHECK_THROWS_AS(fsm.Reset("Invalid"), StateNotFoundException);
  }

  SUBCASE("Invalid transition operations") {
    fsm.AddState("Valid");

    // Try to add transition with invalid states
    CHECK_THROWS_AS(fsm.AddTransition("Invalid", "Valid", EVENT_A),
                    StateNotFoundException);
    CHECK_THROWS_AS(fsm.AddTransition("Valid", "Invalid", EVENT_A),
                    StateNotFoundException);
  }
}

TEST_CASE("Traffic Light Example") {
  FSM fsm;

  // Create a traffic light system
  fsm.AddState("Red");
  fsm.AddState("Yellow");
  fsm.AddState("Green");
  fsm.AddState("Emergency");

  SUBCASE("Traffic light cycle") {
    fsm.AddTransition("Red", "Green", EVENT_TIMER);
    fsm.AddTransition("Green", "Yellow", EVENT_TIMER);
    fsm.AddTransition("Yellow", "Red", EVENT_TIMER);

    // Emergency transitions from any state
    std::vector<std::string> normal_states = {"Red", "Yellow", "Green"};
    for (const auto& state : normal_states) {
      fsm.AddTransition(state, "Emergency", EVENT_C);
    }
    fsm.AddTransition("Emergency", "Red", EVENT_A);

    fsm.Start("Red");

    // Normal cycle
    fsm.ProcessEvent(EVENT_TIMER);  // Red -> Green
    CHECK(fsm.IsInState("Green"));

    fsm.ProcessEvent(EVENT_TIMER);  // Green -> Yellow
    CHECK(fsm.IsInState("Yellow"));

    fsm.ProcessEvent(EVENT_TIMER);  // Yellow -> Red
    CHECK(fsm.IsInState("Red"));

    // Emergency override
    fsm.ProcessEvent(EVENT_C);
    CHECK(fsm.IsInState("Emergency"));

    // Return to normal
    fsm.ProcessEvent(EVENT_A);
    CHECK(fsm.IsInState("Red"));
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

  fsm.AddState("Closed");
  fsm.AddState("Open");
  fsm.AddState("Locked");

  SUBCASE("Door operations") {
    // Add transitions with guards
    auto door_guard =
        MakeGuard("door_security_check",
                  [](const State& from, const State& to, EventType event) {
                    if (event == OPEN_CMD && from.name() == "Locked") {
                      return false;  // Cannot open locked door
                    }
                    return true;
                  });

    // Normal transitions
    fsm.AddTransition("Closed", "Open", OPEN_CMD);
    fsm.AddTransition("Open", "Closed", CLOSE_CMD);
    fsm.AddTransition("Closed", "Locked", LOCK_CMD);
    fsm.AddTransition("Locked", "Closed", UNLOCK_CMD);

    // Guarded transition
    fsm.AddTransition("Locked", "Open", OPEN_CMD, door_guard);

    fsm.Start("Closed");

    fsm.ProcessEvent(OPEN_CMD);  // Closed -> Open
    CHECK(fsm.IsInState("Open"));

    fsm.ProcessEvent(CLOSE_CMD);  // Open -> Closed
    CHECK(fsm.IsInState("Closed"));

    fsm.ProcessEvent(LOCK_CMD);  // Closed -> Locked
    CHECK(fsm.IsInState("Locked"));

    fsm.ProcessEvent(OPEN_CMD);  // LOCKED -> LOCKED (blocked by guard)
    CHECK(fsm.IsInState("Locked"));

    fsm.ProcessEvent(UNLOCK_CMD);  // Locked -> Closed
    CHECK(fsm.IsInState("Closed"));
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
