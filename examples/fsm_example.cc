/*
 * Description: Example usage of the improved FSM implementation
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 1.0.0
 */

#include <chrono>
#include <iostream>
#include <thread>

#include "xtils/fsm/fsm.h"

using namespace xtils::fsm;

// Example: Traffic Light State Machine
class TrafficLightExample {
 public:
  // Events
  enum Events : EventType {
    TIMER_EXPIRED = 1,
    PEDESTRIAN_BUTTON = 2,
    EMERGENCY_OVERRIDE = 3,
    NORMAL_OPERATION = 4
  };

  void run() {
    std::cout << "=== Traffic Light FSM Example ===" << std::endl;

    FSM fsm;
    fsm.enableThreadSafety(true);

    // Add states with callbacks
    auto red_id = fsm.addState(
        "RED",
        [](const State& state, EventType event) {
          std::cout << "Entered RED state - STOP!" << std::endl;
        },
        [](const State& state, EventType event) {
          std::cout << "Exiting RED state" << std::endl;
        });

    auto yellow_id = fsm.addState(
        "YELLOW",
        [](const State& state, EventType event) {
          std::cout << "Entered YELLOW state - CAUTION!" << std::endl;
        },
        [](const State& state, EventType event) {
          std::cout << "Exiting YELLOW state" << std::endl;
        });

    auto green_id = fsm.addState(
        "GREEN",
        [](const State& state, EventType event) {
          std::cout << "Entered GREEN state - GO!" << std::endl;
        },
        [](const State& state, EventType event) {
          std::cout << "Exiting GREEN state" << std::endl;
        });

    auto emergency_id = fsm.addState(
        "EMERGENCY",
        [](const State& state, EventType event) {
          std::cout << "Entered EMERGENCY state - FLASHING RED!" << std::endl;
        },
        [](const State& state, EventType event) {
          std::cout << "Exiting EMERGENCY state" << std::endl;
        });

    // Create transition conditions
    auto timer_condition = makeAction(
        "timer_action",
        [](const State& from, const State& to, EventType event) {
          std::cout << "Timer expired, transitioning from " << from.name()
                    << " to " << to.name() << std::endl;
        });

    auto pedestrian_guard = makeGuard(
        "pedestrian_guard",
        [](const State& from, const State& to, EventType event) {
          // Only allow pedestrian crossing when coming from GREEN
          bool allowed = from.name() == "GREEN";
          if (!allowed) {
            std::cout << "Pedestrian button pressed but not in GREEN state"
                      << std::endl;
          }
          return allowed;
        });

    auto emergency_action =
        makeAction("emergency_action",
                   [](const State& from, const State& to, EventType event) {
                     std::cout << "EMERGENCY OVERRIDE ACTIVATED!" << std::endl;
                   });

    // Add transitions - normal cycle
    fsm.addTransition("RED", "GREEN", TIMER_EXPIRED, timer_condition);
    fsm.addTransition("GREEN", "YELLOW", TIMER_EXPIRED, timer_condition);
    fsm.addTransition("YELLOW", "RED", TIMER_EXPIRED, timer_condition);

    // Add pedestrian crossing
    fsm.addTransition("GREEN", "YELLOW", PEDESTRIAN_BUTTON, pedestrian_guard);

    // Add emergency transitions from any state
    std::vector<std::string> all_states = {"RED", "GREEN", "YELLOW"};
    for (const auto& state : all_states) {
      fsm.addTransition(state, "EMERGENCY", EMERGENCY_OVERRIDE,
                        emergency_action);
    }
    fsm.addTransition(
        "EMERGENCY", "RED", NORMAL_OPERATION,
        makeAction("restore_normal",
                   [](const State& from, const State& to, EventType event) {
                     std::cout << "Restoring normal operation" << std::endl;
                   }));

    // Start FSM
    fsm.start("RED");

    // Simulate traffic light operation
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "\n--- Normal Operation ---" << std::endl;
    fsm.processEvent(TIMER_EXPIRED);  // RED -> GREEN

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fsm.processEvent(TIMER_EXPIRED);  // GREEN -> YELLOW

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fsm.processEvent(TIMER_EXPIRED);  // YELLOW -> RED

    std::cout << "\n--- Pedestrian Crossing ---" << std::endl;
    fsm.processEvent(TIMER_EXPIRED);      // RED -> GREEN
    fsm.processEvent(PEDESTRIAN_BUTTON);  // GREEN -> YELLOW (with guard)

    std::cout << "\n--- Emergency Override ---" << std::endl;
    fsm.processEvent(EMERGENCY_OVERRIDE);  // Any -> EMERGENCY
    fsm.processEvent(NORMAL_OPERATION);    // EMERGENCY -> RED

    // Display current state
    auto current_state = fsm.getCurrentStateName();
    if (current_state) {
      std::cout << "\nCurrent state: " << *current_state << std::endl;
    }

    // Generate DOT graph
    std::cout << "\n--- State Diagram (DOT format) ---" << std::endl;
    std::cout << fsm.toDotGraph() << std::endl;

    // Show history
    std::cout << "--- Transition History ---" << std::endl;
    const auto& history = fsm.getHistory();
    for (const auto& entry : history) {
      auto from_name = fsm.getStateName(entry.from_state);
      auto to_name = fsm.getStateName(entry.to_state);
      std::cout << "Event " << entry.event << ": ";
      if (from_name)
        std::cout << *from_name;
      else
        std::cout << "NONE";
      std::cout << " -> ";
      if (to_name)
        std::cout << *to_name;
      else
        std::cout << "NONE";
      std::cout << " (" << (entry.transition_occurred ? "SUCCESS" : "BLOCKED")
                << ") - " << entry.description << std::endl;
    }
  }
};

// Example: Simple Door State Machine
class DoorExample {
 public:
  enum Events : EventType {
    OPEN_CMD = 10,
    CLOSE_CMD = 11,
    LOCK_CMD = 12,
    UNLOCK_CMD = 13
  };

  void run() {
    std::cout << "\n=== Door FSM Example ===" << std::endl;

    FSM fsm;

    // Add states
    fsm.addState("CLOSED");
    fsm.addState("OPEN");
    fsm.addState("LOCKED");

    // Add transitions with guards
    auto door_guard =
        makeGuard("door_security_check",
                  [](const State& from, const State& to, EventType event) {
                    if (event == OPEN_CMD && from.name() == "LOCKED") {
                      std::cout << "Cannot open locked door!" << std::endl;
                      return false;
                    }
                    return true;
                  });

    // Normal transitions
    fsm.addTransition("CLOSED", "OPEN", OPEN_CMD);
    fsm.addTransition("OPEN", "CLOSED", CLOSE_CMD);
    fsm.addTransition("CLOSED", "LOCKED", LOCK_CMD);
    fsm.addTransition("LOCKED", "CLOSED", UNLOCK_CMD);

    // Guarded transition
    fsm.addTransition("LOCKED", "OPEN", OPEN_CMD, door_guard);

    // Start FSM
    fsm.start("CLOSED");

    // Test scenarios
    std::cout << "Initial state: " << *fsm.getCurrentStateName() << std::endl;

    fsm.processEvent(OPEN_CMD);  // CLOSED -> OPEN
    std::cout << "After OPEN: " << *fsm.getCurrentStateName() << std::endl;

    fsm.processEvent(CLOSE_CMD);  // OPEN -> CLOSED
    std::cout << "After CLOSE: " << *fsm.getCurrentStateName() << std::endl;

    fsm.processEvent(LOCK_CMD);  // CLOSED -> LOCKED
    std::cout << "After LOCK: " << *fsm.getCurrentStateName() << std::endl;

    fsm.processEvent(OPEN_CMD);  // LOCKED -> LOCKED (blocked by guard)
    std::cout << "After OPEN (should fail): " << *fsm.getCurrentStateName()
              << std::endl;

    fsm.processEvent(UNLOCK_CMD);  // LOCKED -> CLOSED
    std::cout << "After UNLOCK: " << *fsm.getCurrentStateName() << std::endl;
  }
};

// Example: Game Character State Machine
class CharacterExample {
 public:
  enum Events : EventType {
    MOVE = 20,
    JUMP = 21,
    ATTACK = 22,
    TAKE_DAMAGE = 23,
    HEAL = 24,
    DIE = 25,
    RESPAWN = 26
  };

  void run() {
    std::cout << "\n=== Game Character FSM Example ===" << std::endl;

    FSM fsm;

    // Character stats
    int health = 100;
    int stamina = 100;

    // Add states with complex behaviors
    fsm.addState("IDLE", [&](const State& state, EventType event) {
      std::cout << "Character is idle (Health: " << health
                << ", Stamina: " << stamina << ")" << std::endl;
      stamina = std::min(100, stamina + 5);  // Regenerate stamina
    });

    fsm.addState(
        "MOVING",
        [&](const State& state, EventType event) {
          std::cout << "Character is moving..." << std::endl;
          stamina = std::max(0, stamina - 2);
        },
        [&](const State& state, EventType event) {
          std::cout << "Character stopped moving" << std::endl;
        });

    fsm.addState("JUMPING", [&](const State& state, EventType event) {
      std::cout << "Character is jumping!" << std::endl;
      stamina = std::max(0, stamina - 10);
    });

    fsm.addState("ATTACKING", [&](const State& state, EventType event) {
      std::cout << "Character attacks! *SWOOSH*" << std::endl;
      stamina = std::max(0, stamina - 15);
    });

    fsm.addState("DEAD", [&](const State& state, EventType event) {
      std::cout << "Character has died... Press F to pay respects" << std::endl;
      health = 0;
    });

    // Create complex transition conditions
    auto stamina_check = makeGuard(
        "stamina_check",
        [&](const State& from, const State& to, EventType event) {
          int required_stamina = 0;
          if (event == JUMP)
            required_stamina = 10;
          else if (event == ATTACK)
            required_stamina = 15;
          else if (event == MOVE)
            required_stamina = 2;

          if (stamina < required_stamina) {
            std::cout << "Not enough stamina! (Need " << required_stamina
                      << ", have " << stamina << ")" << std::endl;
            return false;
          }
          return true;
        });

    auto health_check =
        makeGuard("health_check",
                  [&](const State& from, const State& to, EventType event) {
                    if (health <= 0) {
                      std::cout << "Character has no health!" << std::endl;
                      return false;
                    }
                    return true;
                  });

    auto damage_action =
        makeAction("take_damage",
                   [&](const State& from, const State& to, EventType event) {
                     health = std::max(0, health - 25);
                     std::cout << "Character takes damage! Health: " << health
                               << std::endl;
                     if (health == 0) {
                       std::cout << "Character will die!" << std::endl;
                     }
                   });

    auto heal_action = makeAction(
        "heal", [&](const State& from, const State& to, EventType event) {
          health = std::min(100, health + 30);
          std::cout << "Character heals! Health: " << health << std::endl;
        });

    // Add transitions with multiple events and conditions
    std::vector<EventType> basic_events = {MOVE, JUMP, ATTACK};

    // From IDLE
    fsm.addTransition("IDLE", "MOVING", MOVE, stamina_check);
    fsm.addTransition("IDLE", "JUMPING", JUMP, stamina_check);
    fsm.addTransition("IDLE", "ATTACKING", ATTACK, stamina_check);

    // From MOVING - can transition to other actions
    fsm.addTransition("MOVING", "IDLE", MOVE);  // Stop moving
    fsm.addTransition("MOVING", "JUMPING", JUMP, stamina_check);
    fsm.addTransition("MOVING", "ATTACKING", ATTACK, stamina_check);

    // From action states back to IDLE
    fsm.addTransition("JUMPING", "IDLE", JUMP);      // Land
    fsm.addTransition("ATTACKING", "IDLE", ATTACK);  // Finish attack

    // Damage can happen from any living state
    std::vector<std::string> living_states = {"IDLE", "MOVING", "JUMPING",
                                              "ATTACKING"};
    for (const auto& state : living_states) {
      fsm.addTransition(state, state, TAKE_DAMAGE,
                        damage_action);  // Take damage but stay in state
      fsm.addTransition(
          state, "DEAD", DIE,
          makeCondition(
              "death_check",
              [&](const State& from, const State& to, EventType event) {
                return health <= 0;
              },
              [&](const State& from, const State& to, EventType event) {
                std::cout << "Character dies!" << std::endl;
              }));
      fsm.addTransition(state, state, HEAL, heal_action);
    }

    // Respawn from death
    fsm.addTransition(
        "DEAD", "IDLE", RESPAWN,
        makeAction("respawn", [&](const State& from, const State& to,
                                  EventType event) {
          health = 100;
          stamina = 100;
          std::cout << "Character respawns with full health and stamina!"
                    << std::endl;
        }));

    // Start the FSM
    fsm.start("IDLE");

    // Simulate gameplay
    std::cout << "\n--- Character Gameplay Simulation ---" << std::endl;

    fsm.processEvent(MOVE);    // Start moving
    fsm.processEvent(JUMP);    // Jump while moving
    fsm.processEvent(JUMP);    // Land
    fsm.processEvent(ATTACK);  // Attack
    fsm.processEvent(ATTACK);  // Finish attack

    // Try actions without enough stamina
    fsm.processEvent(JUMP);  // Should fail due to low stamina
    fsm.processEvent(MOVE);  // Should fail due to low stamina

    // Wait and regenerate stamina
    for (int i = 0; i < 10; ++i) {
      fsm.processEvent(MOVE);  // Try to trigger idle regeneration
    }

    // Take damage
    fsm.processEvent(TAKE_DAMAGE);
    fsm.processEvent(TAKE_DAMAGE);
    fsm.processEvent(TAKE_DAMAGE);
    fsm.processEvent(TAKE_DAMAGE);  // Should trigger death

    // Try to move while dead (should fail)
    fsm.processEvent(MOVE);

    // Respawn
    fsm.processEvent(RESPAWN);

    std::cout << "\nFinal state: " << *fsm.getCurrentStateName() << std::endl;
    std::cout << "Final stats - Health: " << health << ", Stamina: " << stamina
              << std::endl;
  }
};

int main() {
  try {
    TrafficLightExample traffic;
    traffic.run();

    DoorExample door;
    door.run();

    CharacterExample character;
    character.run();

    std::cout << "\n=== All examples completed successfully! ===" << std::endl;

  } catch (const FSMException& e) {
    std::cerr << "FSM Error: " << e.what() << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
