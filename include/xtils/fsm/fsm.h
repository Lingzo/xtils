/*
 * Description: DOC
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 0.0.0
 *
 * Changelog:
 */

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

using Event = std::int32_t;
class State;
class FSM;
using CondIf = std::function<bool(State&, State&, Event)>;
struct Condition {
  explicit Condition(const std::string& name, CondIf cb)
      : name_(name), cond_if_(cb) {}
  bool operator()(State& cur, State& next, Event e) const {
    if (cond_if_) return std::invoke(cond_if_, cur, next, e);
    return true;
  }
  const std::string name_;
  CondIf cond_if_;
};

inline std::shared_ptr<Condition> make_action(
    const std::string& name,
    std::function<bool(State&, State&, Event)> action) {
  return std::make_shared<Condition>(name, action);
}

using StatePtr = std::shared_ptr<State>;
struct ConditonPair {
  std::shared_ptr<Condition> action;
  std::shared_ptr<State> state;
};
class State;
using Callback = std::function<void(State&, Event)>;
class State {
 public:
  explicit State(const std::string name) : name_(name) {}
  explicit State(const std::string name, Callback entry)
      : entry_(entry), name_(name) {}
  explicit State(const std::string name, Callback pre, Callback entry,
                 Callback post)
      : pre_(pre), entry_(entry), post_(post), name_(name) {}
  virtual ~State() {}
  virtual void initialize() {}

 private:
  friend FSM;
  Callback pre_{nullptr};
  Callback entry_{nullptr};
  Callback post_{nullptr};
  const std::string name_;
  std::unordered_map<Event, ConditonPair> conds_{};
};

class FSM {
 public:
  void add(std::initializer_list<std::shared_ptr<State>> states);
  void transfer(const std::string& form, const std::string& to, Event e,
                std::shared_ptr<Condition> cond);
  void transfer(const std::string& form, const std::string& to,
                std::vector<Event> events, std::shared_ptr<Condition> cond);

  void update(Event e);

  void reset(const std::string& to);

  std::string dump();

  std::list<std::string> history();

 private:
  StatePtr last_state_{nullptr};
  StatePtr cur_state_{nullptr};
  const int MAX_HISTORY_SIZE = 10;
  std::mutex mtx_;
  std::list<std::string> history_;
  std::unordered_map<std::string, StatePtr> states_{};
};
