/*
 * Description: Improved Finite State Machine Implementation
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 1.0.0
 *
 * Changelog:
 * - Fixed naming inconsistencies and typos
 * - Improved error handling with exceptions
 * - Enhanced thread safety
 * - Added state ID system for better performance
 * - Improved API design and usability
 */

#include "xtils/fsm/fsm.h"

#include <sstream>

namespace xtils {
namespace fsm {

// FSM Implementation

StateId FSM::addState(std::unique_ptr<State> state) {
  return withLock([&]() -> StateId {
    if (!state) {
      throw FSMException("Cannot add null state");
    }

    const std::string& name = state->name();
    StateId id = state->id();

    // Check for name conflicts
    if (name_to_id_.find(name) != name_to_id_.end()) {
      throw FSMException("State with name '" + name + "' already exists");
    }

    // Check for ID conflicts (shouldn't happen with proper ID generation)
    if (states_.find(id) != states_.end()) {
      throw FSMException("State with ID " + std::to_string(id) +
                         " already exists");
    }

    name_to_id_[name] = id;
    states_[id] = std::move(state);

    return id;
  });
}

StateId FSM::addState(const std::string& name) {
  return addState(std::make_unique<State>(name));
}

StateId FSM::addState(const std::string& name, StateCallback on_enter) {
  return addState(std::make_unique<State>(name, std::move(on_enter)));
}

StateId FSM::addState(const std::string& name, StateCallback on_enter,
                      StateCallback on_exit) {
  return addState(
      std::make_unique<State>(name, std::move(on_enter), std::move(on_exit)));
}

void FSM::addTransition(const std::string& from, const std::string& to,
                        EventType event,
                        std::shared_ptr<TransitionCondition> condition) {
  StateId from_id = getStateId(from);
  StateId to_id = getStateId(to);
  addTransition(from_id, to_id, event, std::move(condition));
}

void FSM::addTransition(StateId from, StateId to, EventType event,
                        std::shared_ptr<TransitionCondition> condition) {
  withLock([&]() {
    State* from_state = getState(from);
    State* to_state = getState(to);

    if (!from_state) {
      throw StateNotFoundException("State ID " + std::to_string(from));
    }
    if (!to_state) {
      throw StateNotFoundException("State ID " + std::to_string(to));
    }

    // Add transition to the from state
    auto& transitions = from_state->transitions_[event];
    transitions.emplace_back(to, std::move(condition));
  });
}

void FSM::addTransition(const std::string& from, const std::string& to,
                        const std::vector<EventType>& events,
                        std::shared_ptr<TransitionCondition> condition) {
  StateId from_id = getStateId(from);
  StateId to_id = getStateId(to);

  for (EventType event : events) {
    // Create a copy of the condition for each event
    auto condition_copy =
        condition ? std::make_shared<TransitionCondition>(*condition) : nullptr;
    addTransition(from_id, to_id, event, std::move(condition_copy));
  }
}

void FSM::start(const std::string& initial_state) {
  StateId id = getStateId(initial_state);
  start(id);
}

void FSM::start(StateId initial_state_id) {
  withLock([&]() {
    State* state = getState(initial_state_id);
    if (!state) {
      throw StateNotFoundException("State ID " +
                                   std::to_string(initial_state_id));
    }

    current_state_id_ = initial_state_id;
    previous_state_id_ = 0;  // No previous state on start
    is_started_ = true;

    // Call onEnter for initial state
    state->onEnter(0);  // Event 0 for initialization

    addToHistory(0, current_state_id_, 0, true, "FSM started");
  });
}

void FSM::reset(const std::string& state) {
  StateId id = getStateId(state);
  reset(id);
}

void FSM::reset(StateId state_id) {
  withLock([&]() {
    State* state = getState(state_id);
    if (!state) {
      throw StateNotFoundException("State ID " + std::to_string(state_id));
    }

    // Exit current state if FSM is running
    if (is_started_ && current_state_id_ != 0) {
      State* current_state = getState(current_state_id_);
      if (current_state) {
        current_state->onExit(0);  // Event 0 for reset
      }
    }

    previous_state_id_ = current_state_id_;
    current_state_id_ = state_id;
    is_started_ = true;

    // Enter new state
    state->onEnter(0);  // Event 0 for reset

    addToHistory(previous_state_id_, current_state_id_, 0, true, "FSM reset");
  });
}

void FSM::processEvent(EventType event) {
  withLock([&]() {
    if (!is_started_) {
      throw FSMException("FSM is not started");
    }

    State* current_state = getState(current_state_id_);
    if (!current_state) {
      throw FSMException("Current state not found");
    }

    // Check for transitions on this event
    auto transitions_it = current_state->transitions_.find(event);
    if (transitions_it == current_state->transitions_.end()) {
      // No transitions for this event, just update current state
      current_state->onUpdate(event);
      addToHistory(current_state_id_, current_state_id_, event, false,
                   "No transition");
      return;
    }

    // Try each transition until one succeeds
    const auto& transitions = transitions_it->second;
    for (const auto& transition : transitions) {
      State* target_state = getState(transition.target_state_id);
      if (!target_state) {
        continue;  // Skip invalid transitions
      }

      // Check transition condition (guard)
      if (transition.condition && !transition.condition->canTransition(
                                      *current_state, *target_state, event)) {
        continue;  // Guard prevented transition
      }

      // Execute transition
      current_state->onExit(event);

      // Execute transition action
      if (transition.condition) {
        transition.condition->executeAction(*current_state, *target_state,
                                            event);
      }

      // Update state
      previous_state_id_ = current_state_id_;
      current_state_id_ = transition.target_state_id;

      // Enter new state
      target_state->onEnter(event);

      std::string desc =
          transition.condition ? transition.condition->name() : "transition";
      addToHistory(previous_state_id_, current_state_id_, event, true, desc);
      return;
    }

    // No valid transition found, update current state
    current_state->onUpdate(event);
    addToHistory(current_state_id_, current_state_id_, event, false,
                 "Transition blocked");
  });
}

bool FSM::isInState(const std::string& state_name) const {
  return withLock([&]() {
    auto it = name_to_id_.find(state_name);
    return it != name_to_id_.end() && it->second == current_state_id_;
  });
}

bool FSM::isInState(StateId state_id) const {
  return withLock([&]() { return current_state_id_ == state_id; });
}

std::optional<std::string> FSM::getCurrentStateName() const {
  return withLock([&]() -> std::optional<std::string> {
    State* state = getState(current_state_id_);
    return state ? std::make_optional(state->name()) : std::nullopt;
  });
}

std::optional<StateId> FSM::getCurrentStateId() const {
  return withLock([&]() -> std::optional<StateId> {
    return is_started_ ? std::make_optional(current_state_id_) : std::nullopt;
  });
}

StateId FSM::getStateId(const std::string& name) const {
  return withLock([&]() -> StateId {
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
      throw StateNotFoundException(name);
    }
    return it->second;
  });
}

std::optional<std::string> FSM::getStateName(StateId id) const {
  return withLock([&]() -> std::optional<std::string> {
    State* state = getState(id);
    return state ? std::make_optional(state->name()) : std::nullopt;
  });
}

std::string FSM::toDotGraph() const {
  return withLock([&]() -> std::string {
    std::stringstream ss;
    ss << "digraph FSM {\n";
    ss << "  rankdir=LR;\n";

    // Add states
    for (const auto& [id, state] : states_) {
      ss << "  \"" << state->name() << "\"";
      if (id == current_state_id_) {
        ss << " [style=filled,fillcolor=lightblue]";
      }
      ss << ";\n";
    }

    // Add transitions
    for (const auto& [id, state] : states_) {
      for (const auto& [event, transitions] : state->transitions_) {
        for (const auto& transition : transitions) {
          State* target = getState(transition.target_state_id);
          if (target) {
            ss << "  \"" << state->name() << "\" -> \"" << target->name()
               << "\" [label=\"" << event;
            if (transition.condition && !transition.condition->name().empty()) {
              ss << "\\n" << transition.condition->name();
            }
            ss << "\"];\n";
          }
        }
      }
    }

    ss << "}\n";
    return ss.str();
  });
}

const std::vector<HistoryEntry>& FSM::getHistory() const {
  return withLock(
      [&]() -> const std::vector<HistoryEntry>& { return history_; });
}

void FSM::clearHistory() {
  withLock([&]() { history_.clear(); });
}

void FSM::setMaxHistorySize(std::size_t size) {
  withLock([&]() {
    max_history_size_ = size;
    while (history_.size() > max_history_size_) {
      history_.erase(history_.begin());
    }
  });
}

// Private helper methods

void FSM::addToHistory(StateId from, StateId to, EventType event,
                       bool transitioned, const std::string& desc) {
  // This method should only be called from within withLock
  history_.emplace_back(from, to, event, transitioned, desc);

  // Maintain history size limit
  while (history_.size() > max_history_size_) {
    history_.erase(history_.begin());
  }
}

State* FSM::getState(StateId id) const {
  // This method should only be called from within withLock
  auto it = states_.find(id);
  return it != states_.end() ? it->second.get() : nullptr;
}

State* FSM::getState(const std::string& name) const {
  // This method should only be called from within withLock
  auto it = name_to_id_.find(name);
  if (it == name_to_id_.end()) {
    return nullptr;
  }
  return getState(it->second);
}

}  // namespace fsm
}  // namespace xtils
