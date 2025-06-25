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

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace xtils {
namespace fsm {

// Type definitions
using EventType = std::int32_t;
using StateId = std::int32_t;

// Forward declarations
class State;
class FSM;

// Exception classes
class FSMException : public std::runtime_error {
 public:
  explicit FSMException(const std::string& message)
      : std::runtime_error(message) {}
};

class StateNotFoundException : public FSMException {
 public:
  explicit StateNotFoundException(const std::string& state_name)
      : FSMException("State not found: " + state_name) {}
};

class InvalidTransitionException : public FSMException {
 public:
  InvalidTransitionException(const std::string& from, const std::string& to,
                             EventType event)
      : FSMException("Invalid transition from '" + from + "' to '" + to +
                     "' on event " + std::to_string(event)) {}
};

// Callback function types
using StateCallback = std::function<void(const State&, EventType)>;
using TransitionGuard =
    std::function<bool(const State&, const State&, EventType)>;
using TransitionAction =
    std::function<void(const State&, const State&, EventType)>;

// Transition condition combining guard and action
class TransitionCondition {
 public:
  explicit TransitionCondition(const std::string& name = "") : name_(name) {}

  TransitionCondition(const std::string& name, TransitionGuard guard)
      : name_(name), guard_(std::move(guard)) {}

  TransitionCondition(const std::string& name, TransitionAction action)
      : name_(name), action_(std::move(action)) {}

  TransitionCondition(const std::string& name, TransitionGuard guard,
                      TransitionAction action)
      : name_(name), guard_(std::move(guard)), action_(std::move(action)) {}

  // Check if transition is allowed
  bool canTransition(const State& from, const State& to,
                     EventType event) const {
    return !guard_ || guard_(from, to, event);
  }

  // Execute transition action
  void executeAction(const State& from, const State& to,
                     EventType event) const {
    if (action_) {
      action_(from, to, event);
    }
  }

  const std::string& name() const { return name_; }

 private:
  std::string name_;
  TransitionGuard guard_;
  TransitionAction action_;
};

// Transition target
struct TransitionTarget {
  StateId target_state_id;
  std::shared_ptr<TransitionCondition> condition;

  TransitionTarget(StateId id,
                   std::shared_ptr<TransitionCondition> cond = nullptr)
      : target_state_id(id), condition(std::move(cond)) {}
};

// State class
class State {
 public:
  explicit State(std::string name)
      : name_(std::move(name)), id_(generateId()) {}

  State(std::string name, StateCallback on_enter)
      : name_(std::move(name)),
        id_(generateId()),
        on_enter_(std::move(on_enter)) {}

  State(std::string name, StateCallback on_enter, StateCallback on_exit)
      : name_(std::move(name)),
        id_(generateId()),
        on_enter_(std::move(on_enter)),
        on_exit_(std::move(on_exit)) {}

  State(std::string name, StateCallback on_enter, StateCallback on_exit,
        StateCallback on_update)
      : name_(std::move(name)),
        id_(generateId()),
        on_enter_(std::move(on_enter)),
        on_exit_(std::move(on_exit)),
        on_update_(std::move(on_update)) {}

  virtual ~State() = default;

  // Non-copyable but movable
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = default;
  State& operator=(State&&) = default;

  // Getters
  const std::string& name() const { return name_; }
  StateId id() const { return id_; }

  // State lifecycle callbacks
  virtual void onEnter(EventType event) {
    if (on_enter_) on_enter_(*this, event);
  }

  virtual void onExit(EventType event) {
    if (on_exit_) on_exit_(*this, event);
  }

  virtual void onUpdate(EventType event) {
    if (on_update_) on_update_(*this, event);
  }

 private:
  friend class FSM;

  static StateId generateId() {
    static StateId next_id = 0;
    return ++next_id;
  }

  std::string name_;
  StateId id_;
  StateCallback on_enter_;
  StateCallback on_exit_;
  StateCallback on_update_;

  // Transitions: event -> list of possible targets
  std::unordered_map<EventType, std::vector<TransitionTarget>> transitions_;
};

// History entry for debugging and logging
struct HistoryEntry {
  std::chrono::steady_clock::time_point timestamp;
  StateId from_state;
  StateId to_state;
  EventType event;
  bool transition_occurred;
  std::string description;

  HistoryEntry(StateId from, StateId to, EventType evt, bool transitioned,
               std::string desc = "")
      : timestamp(std::chrono::steady_clock::now()),
        from_state(from),
        to_state(to),
        event(evt),
        transition_occurred(transitioned),
        description(std::move(desc)) {}
};

// Main FSM class
class FSM {
 public:
  static constexpr std::size_t DEFAULT_MAX_HISTORY = 100;

  explicit FSM(std::size_t max_history = DEFAULT_MAX_HISTORY)
      : max_history_size_(max_history) {}

  ~FSM() = default;

  // Non-copyable but movable
  FSM(const FSM&) = delete;
  FSM& operator=(const FSM&) = delete;

  // State management
  StateId addState(std::unique_ptr<State> state);
  StateId addState(const std::string& name);
  StateId addState(const std::string& name, StateCallback on_enter);
  StateId addState(const std::string& name, StateCallback on_enter,
                   StateCallback on_exit);
  template <typename T>
  StateId addState() {
    return addState(std::move(std::make_unique<State>()));
  }

  // Transition management
  void addTransition(const std::string& from, const std::string& to,
                     EventType event,
                     std::shared_ptr<TransitionCondition> condition = nullptr);
  void addTransition(StateId from, StateId to, EventType event,
                     std::shared_ptr<TransitionCondition> condition = nullptr);
  void addTransition(const std::string& from, const std::string& to,
                     const std::vector<EventType>& events,
                     std::shared_ptr<TransitionCondition> condition = nullptr);

  // FSM control
  void start(const std::string& initial_state);
  void start(StateId initial_state_id);
  void reset(const std::string& state);
  void reset(StateId state_id);
  void processEvent(EventType event);

  // State queries
  bool isInState(const std::string& state_name) const;
  bool isInState(StateId state_id) const;
  std::optional<std::string> getCurrentStateName() const;
  std::optional<StateId> getCurrentStateId() const;

  // Utility functions
  StateId getStateId(const std::string& name) const;
  std::optional<std::string> getStateName(StateId id) const;
  std::string toDotGraph() const;

  // History and debugging
  const std::vector<HistoryEntry>& getHistory() const;
  void clearHistory();
  void setMaxHistorySize(std::size_t size);

  // Thread safety
  void enableThreadSafety(bool enable = true) { thread_safe_ = enable; }

 private:
  mutable std::mutex mutex_;
  bool thread_safe_ = false;

  std::unordered_map<StateId, std::unique_ptr<State>> states_;
  std::unordered_map<std::string, StateId> name_to_id_;

  StateId current_state_id_ = 0;
  StateId previous_state_id_ = 0;
  bool is_started_ = false;

  std::vector<HistoryEntry> history_;
  std::size_t max_history_size_;

  // Helper methods
  void addToHistory(StateId from, StateId to, EventType event,
                    bool transitioned, const std::string& desc = "");
  State* getState(StateId id) const;
  State* getState(const std::string& name) const;

  template <typename Func>
  auto withLock(Func&& func) const -> decltype(func()) {
    if (thread_safe_) {
      std::lock_guard<std::mutex> lock(mutex_);
      return func();
    } else {
      return func();
    }
  }
};

// Helper functions for creating conditions
inline std::shared_ptr<TransitionCondition> makeGuard(const std::string& name,
                                                      TransitionGuard guard) {
  return std::make_shared<TransitionCondition>(name, std::move(guard));
}

inline std::shared_ptr<TransitionCondition> makeAction(
    const std::string& name, TransitionAction action) {
  return std::make_shared<TransitionCondition>(name, std::move(action));
}

inline std::shared_ptr<TransitionCondition> makeCondition(
    const std::string& name, TransitionGuard guard, TransitionAction action) {
  return std::make_shared<TransitionCondition>(name, std::move(guard),
                                               std::move(action));
}

}  // namespace fsm
}  // namespace xtils
