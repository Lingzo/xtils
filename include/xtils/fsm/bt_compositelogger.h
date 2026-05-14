/*
 * BtCompositeLogger - Forwards calls to multiple BtLogger instances.
 *
 * Usage:
 *   auto logger = std::make_shared<BtCompositeLogger>();
 *   logger->Add(std::make_shared<BtFileLogger>("bt.jsonl"));
 *   logger->Add(std::make_shared<BtInspectLogger>("/ws/bt"));
 *
 * Copyright (c) 2024 - 2028 Albert Lv <altair.albert@gmail.com>
 */
#pragma once

#include <memory>
#include <vector>

#include "xtils/fsm/behavior_tree.h"

namespace xtils {

class BtCompositeLogger : public BtLogger {
 public:
  void Add(std::shared_ptr<BtLogger> logger) {
    if (logger) loggers_.push_back(std::move(logger));
  }

  void update(const Json& tree) override {
    for (auto& l : loggers_) l->update(tree);
  }

  void record(const Node& n, Status from, Status to) override {
    for (auto& l : loggers_) l->record(n, from, to);
  }

  void onTickBegin(uint64_t tick) override {
    for (auto& l : loggers_) l->onTickBegin(tick);
  }

  void onTickEnd(uint64_t tick, Status result) override {
    for (auto& l : loggers_) l->onTickEnd(tick, result);
  }

 private:
  std::vector<std::shared_ptr<BtLogger>> loggers_;
};

}  // namespace xtils
