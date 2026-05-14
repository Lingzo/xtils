/*
 * BtFileLogger - Structured JSONL logger for behavior tree offline analysis.
 *
 * Each line is a self-contained JSON object with a "type" field:
 *   {"type":"tree",       "ts":..., "data":{...}}          — tree structure
 *   {"type":"tick_begin", "ts":..., "tick":N}               — tick start
 *   {"type":"transition", "ts":..., "tick":N, "nid":..., ...} — node change
 *   {"type":"tick_end",   "ts":..., "tick":N, "result":...} — tick end
 *
 * Copyright (c) 2024 - 2028 Albert Lv <altair.albert@gmail.com>
 */
#pragma once

#include <fstream>
#include <string>

#include "xtils/fsm/behavior_tree.h"
#include "xtils/utils/time_utils.h"

namespace xtils {

class BtFileLogger : public BtLogger {
 public:
  explicit BtFileLogger(const std::string& path) { Open(path); }

  bool Open(const std::string& path) {
    out_ = std::ofstream(path, std::ios::trunc | std::ios::binary);
    return out_.is_open();
  }

  void update(const Json& tree) override {
    if (!out_.good()) return;
    Json line;
    line["type"] = "tree";
    line["ts"] = system::GetCurrentUtcMs();
    line["data"] = tree;
    out_ << line.dump() << '\n';
  }

  void onTickBegin(uint64_t tick) override {
    current_tick_ = tick;
    if (!out_.good()) return;
    Json line;
    line["type"] = "tick_begin";
    line["ts"] = system::GetCurrentUtcMs();
    line["tick"] = tick;
    out_ << line.dump() << '\n';
  }

  void record(const Node& n, Status from, Status to) override {
    if (!out_.good()) return;
    Json line;
    line["type"] = "transition";
    line["ts"] = system::GetCurrentUtcMs();
    line["tick"] = current_tick_;
    line["nid"] = n.getId();
    line["name"] = n.getName();
    line["from"] = StatusToStr(from);
    line["to"] = StatusToStr(to);
    out_ << line.dump() << '\n';
  }

  void onTickEnd(uint64_t tick, Status result) override {
    if (!out_.good()) return;
    Json line;
    line["type"] = "tick_end";
    line["ts"] = system::GetCurrentUtcMs();
    line["tick"] = tick;
    line["result"] = StatusToStr(result);
    out_ << line.dump() << '\n';
    out_.flush();
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

  std::ofstream out_;
};

}  // namespace xtils
