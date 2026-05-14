/*
 * BtInspectLogger - Real-time WebSocket logger for online behavior tree
 * analysis.
 *
 * Pushes JSON messages via Inspect::Publish() to connected WebSocket clients.
 * Batches per-node transitions within a tick and flushes them in onTickEnd()
 * to minimize WebSocket traffic.
 *
 * Copyright (c) 2024 - 2028 Albert Lv <altair.albert@gmail.com>
 */
#pragma once

#ifndef INSPECT_DISABLE

#include <string>
#include <vector>

#include "xtils/debug/inspect.h"
#include "xtils/fsm/behavior_tree.h"
#include "xtils/utils/time_utils.h"

namespace xtils {

class BtInspectLogger : public BtLogger {
 public:
  /// @param ws_path  WebSocket path to publish on, e.g. "/ws/bt"
  explicit BtInspectLogger(const std::string& ws_path = "/ws/bt")
      : ws_path_(ws_path) {
    Inspect::Get().WebSocket(ws_path_, "Behavior tree live logger",
                             [](const Inspect::Request&, Inspect::Response&) {
                               // Accept connections; no per-message handling
                             });
  }

  void update(const Json& tree) override {
    Json msg;
    msg["type"] = "tree";
    msg["ts"] = system::GetCurrentUtcMs();
    msg["data"] = tree;
    Inspect::Get().Publish(ws_path_, msg);
  }

  void onTickBegin(uint64_t tick) override {
    current_tick_ = tick;
    tick_transitions_.clear();
  }

  void record(const Node& n, Status from, Status to) override {
    Json t;
    t["nid"] = n.getId();
    t["name"] = n.getName();
    t["from"] = StatusToStr(from);
    t["to"] = StatusToStr(to);
    tick_transitions_.push_back(std::move(t));
  }

  void onTickEnd(uint64_t tick, Status result) override {
    // Skip publishing if nobody is listening
    if (!Inspect::Get().HasSubscribers(ws_path_)) return;

    Json msg;
    msg["type"] = "tick";
    msg["ts"] = system::GetCurrentUtcMs();
    msg["tick"] = tick;
    msg["result"] = StatusToStr(result);

    Json::array_t arr;
    arr.reserve(tick_transitions_.size());
    for (auto& t : tick_transitions_) arr.push_back(std::move(t));
    msg["transitions"] = std::move(arr);

    Inspect::Get().Publish(ws_path_, msg);
    tick_transitions_.clear();
  }

 private:
  static const char* StatusToStr(Status s) {
    switch (s) {
      case Status::Success:
        return "Success";
      case Status::Failure:
        return "Failure";
      case Status::Idle:
        return "Idle";
      case Status::Running:
        return "Running";
      default:
        return "Unknown";
    }
  }

  std::string ws_path_;
  std::vector<Json> tick_transitions_;
};

}  // namespace xtils

#endif  // INSPECT_DISABLE
