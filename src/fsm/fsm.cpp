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

#include "fsm.h"

#include <cassert>
#include <iostream>

void FSM::add(std::initializer_list<StatePtr> states) {
  for (auto& s : states) {
    s->initialize();
    states_.emplace(s->name_, std::move(s));
  }
}

void FSM::transfer(const std::string& from, const std::string& to, Event e,
                   std::shared_ptr<Condition> action) {
  auto state1 = states_.find(from);
  auto state2 = states_.find(to);
  assert(state1 != states_.end() && state2 != states_.end());
  state1->second->conds_.emplace(e, ConditonPair{action, state2->second});
}
void FSM::transfer(const std::string& from, const std::string& to,
                   std::vector<Event> events,
                   std::shared_ptr<Condition> action) {
  auto state1 = states_.find(from);
  auto state2 = states_.find(to);
  assert(state1 != states_.end() && state2 != states_.end());
  for (auto& e : events) {
    state1->second->conds_.emplace(e, ConditonPair{action, state2->second});
  }
}

void FSM::reset(const std::string& to) {
  auto state = states_.find(to);
  assert(state != states_.end());
  last_state_ = state->second;
  cur_state_ = state->second;
}

std::string FSM::dump() {
  std::stringstream ss;
  ss << "digraph {" << std::endl;
  for (auto& it : states_) {
    std::unordered_map<std::string, std::string> state_map;
    for (auto& cond : it.second->conds_) {
      auto sub_state = state_map.find(cond.second.state->name_);
      if (sub_state == state_map.end()) {
        state_map[cond.second.state->name_] =
            cond.second.action->name_ + "(" + std::to_string(cond.first) + ")";
      } else {
        sub_state->second += ("\\n" + cond.second.action->name_ + "(" +
                              std::to_string(cond.first) + ")");
      }
    }
    for (auto& sub : state_map) {
      ss << it.first << "->" << sub.first << "[label=\"" << sub.second << "\"]"
         << std::endl;
    }
  }
  ss << "}" << std::endl;
  return ss.str();
}

void FSM::update(Event e) {
  std::stringstream ss;
  auto action = cur_state_->conds_.find(e);
  if (action == cur_state_->conds_.end()) {
    if (cur_state_->entry_) std::invoke(cur_state_->entry_, *last_state_, e);
    ss << cur_state_->name_ << "[c=1]" << std::endl;
  } else {
    auto& action_state = action->second;
    bool needTransfer = true;
    if (action_state.action) {
      needTransfer = std::invoke(*action_state.action, *cur_state_,
                                 *action_state.state, e);
    }
    if (needTransfer) {
      if (cur_state_->post_) std::invoke(cur_state_->post_, *last_state_, e);
      last_state_ = cur_state_;
      cur_state_ = action_state.state;
      if (cur_state_->pre_) std::invoke(cur_state_->pre_, *last_state_, e);
    }
    if (cur_state_->entry_) std::invoke(cur_state_->entry_, *last_state_, e);
    ss << last_state_->name_ << "->" << cur_state_->name_
       << (needTransfer ? "[c=2]" : "[c=3]") << std::endl;
  }
  {
    ss << std::chrono::system_clock::now().time_since_epoch().count()
       << std::endl;
    std::lock_guard<std::mutex> lock(mtx_);
    while (history_.size() > MAX_HISTORY_SIZE) history_.pop_front();
    history_.emplace_back(ss.str());
  }
}

std::list<std::string> FSM::history() {
  std::lock_guard<std::mutex> lock(mtx_);
  return history_;
}
