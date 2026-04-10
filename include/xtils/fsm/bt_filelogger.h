/*
 * Description: DOC
 *
 * Copyright (c) 2024 - 2028 Albert Lv <altair.albert@gmail.com>
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 0.0.0
 *
 * Changelog:
 */

#include <xtils/utils/time_utils.h>

#include <fstream>
#include <string>

#include "behavior_tree.h"

namespace xtils {
class BtFileLogger : public BtLogger {
 public:
  explicit BtFileLogger(const std::string& path) { open(path); }
  bool open(const std::string& path) {
    out_ = std::fstream(path, std::ios_base::trunc | std::ios_base::out |
                                  std::ios_base::binary);
    if (out_.is_open()) return true;
    return false;
  }
  void update(const Json& tree) override {
    if (out_.good()) out_ << tree.dump() << std::endl;
  }
  void record(const Node& n, Status from, Status to) override {
    if (out_.good())
      out_ << n.getId() << ":" << system::GetCurrentUtcMs() << " "
           << n.getName() << " [" << to_string(from) << " -> " << to_string(to)
           << "]" << std::endl;
  }

 private:
  std::string to_string(const Status& s) {
    switch (s) {
      case Status::Success:
        return "Success";
      case Status::Failure:
        return "Failure";
      case Status::Idle:
        return "Idel";
      case Status::Running:
        return "Running";
      default:
        return "Unknown";
    }
  }
  std::fstream out_;
};

}  // namespace xtils
