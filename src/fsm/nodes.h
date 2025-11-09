#pragma once
#include <algorithm>
#include <random>

#include "xtils/fsm/behavior_tree.h"
#include "xtils/utils/exception.h"
#include "xtils/utils/time_utils.h"

namespace xtils {

class RandomSelector : public Composite {
 public:
  RandomSelector(const std::string& name) : Composite(name) {}

  xtils::Status OnStart() override {
    if (children.empty()) {
      return xtils::Status::Failure;
    }
    // Shuffle indices
    indices_.clear();
    for (size_t i = 0; i < children.size(); ++i) {
      indices_.push_back(i);
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(indices_.begin(), indices_.end(), g);
    current_ = 0;
    return xtils::Status::Running;
  }
  xtils::Status OnTick() override {
    while (current_ < static_cast<int>(indices_.size())) {
      int idx = indices_[current_];
      auto status = children[idx]->tick();
      if (status == xtils::Status::Running) {
        return xtils::Status::Running;
      }
      if (status == xtils::Status::Success) {
        current_ = -1;
        return xtils::Status::Success;
      }
      // Failure, try next child
      current_++;
    }
    current_ = -1;
    return xtils::Status::Failure;
  }

 private:
  std::vector<int> indices_;
};

// Decorator that fails if child does not complete within timeout
class Timeout : public Decorator {
 public:
  Timeout(const std::string& name) : Decorator(name) {}
  static Ports getPots() { return {xtils::InputPort<double>("timeout_ms")}; }

  xtils::Status OnStart() override {
    start_time_ = xtils::steady::GetCurrentMs();
    auto timeout_opt = getInput<double>("timeout_ms");
    if (!timeout_opt) {
      throw xtils::runtime_error("Timeout decorator requires timeout_ms input");
    }
    timeout_ms_ = *timeout_opt;
    return xtils::Status::Running;
  }

  xtils::Status OnTick() override {
    auto now = xtils::steady::GetCurrentMs();
    if (now - start_time_ >= timeout_ms_) {
      start_time_ = 0;
      return xtils::Status::Failure;
    }
    auto status = children[0]->tick();
    if (status != xtils::Status::Running) {
      start_time_ = 0;
    }
    return status;
  }

 private:
  int timeout_ms_;
  uint64_t start_time_{0};
};

class Retry : public Decorator {
 public:
  Retry(const std::string& name) : Decorator(name) {}
  static Ports getPorts() { return {xtils::InputPort<int>("max_retries")}; }
  xtils::Status OnStart() override {
    attempt_count_ = 0;
    auto max_retries_opt = getInput<int>("max_retries");
    if (!max_retries_opt) {
      throw xtils::runtime_error("Retry decorator requires max_retries input");
    }
    max_retries_ = *max_retries_opt;
    return xtils::Status::Running;
  }
  xtils::Status OnTick() override {
    while (attempt_count_ < max_retries_) {
      auto status = children[0]->tick();
      if (status == xtils::Status::Success) {
        return xtils::Status::Success;
      }
      attempt_count_++;
      children[0]->reset();
    }
    return xtils::Status::Failure;
  }

 private:
  int max_retries_;
  int attempt_count_{0};
};

class Repeater : public Decorator {
 public:
  Repeater(const std::string& name) : Decorator(name) {}
  static Ports getPorts() { return {xtils::InputPort<int>("repeat_count")}; }
  xtils::Status OnStart() override {
    current_count_ = 0;
    auto repeat_count_opt = getInput<int>("repeat_count");
    if (!repeat_count_opt) {
      throw xtils::runtime_error(
          "Repeater decorator requires repeat_count input");
    }
    repeat_count_ = *repeat_count_opt;
    return xtils::Status::Running;
  }
  xtils::Status OnTick() override {
    while (current_count_ < repeat_count_) {
      auto status = children[0]->tick();
      if (status == xtils::Status::Running) {
        return xtils::Status::Running;
      }
      if (status == xtils::Status::Failure) {
        return xtils::Status::Failure;
      }
      current_count_++;
      children[0]->reset();
    }
    return xtils::Status::Success;
  }

 private:
  int repeat_count_;
  int current_count_{0};
};

// Fallback composite node
class Fallback : public Composite {
 public:
  Fallback(const std::string& name) : Composite(name) {}

  xtils::Status OnStart() override {
    current_ = 0;
    return xtils::Status::Running;
  }
  xtils::Status OnTick() override {
    while (current_ < static_cast<int>(children.size())) {
      auto status = children[current_]->tick();
      if (status == xtils::Status::Running) {
        return xtils::Status::Running;
      }
      if (status == xtils::Status::Failure) {
        current_++;
        continue;
      }
      // Success
      current_ = -1;
      return xtils::Status::Success;
    }
    current_ = -1;
    return xtils::Status::Failure;
  }
};

class Wait : public ActionNode {
 public:
  Wait(const std::string& name) : ActionNode(name) {}
  static Ports getPorts() { return {xtils::InputPort<double>("wait_ms")}; }

  xtils::Status OnStart() override {
    auto wait_ms_opt = getInput<double>("wait_ms");
    if (!wait_ms_opt) {
      throw xtils::runtime_error("Wait action requires wait_ms input");
    }
    wait_ms_ = *wait_ms_opt;
    start_time_ = xtils::steady::GetCurrentMs();
    return xtils::Status::Running;
  }

  xtils::Status OnTick() override {
    auto now = xtils::steady::GetCurrentMs();
    if (now - start_time_ >= wait_ms_) {
      return xtils::Status::Success;
    }
    return xtils::Status::Running;
  }

 private:
  int wait_ms_;
  uint64_t start_time_{0};
};
}  // namespace xtils
