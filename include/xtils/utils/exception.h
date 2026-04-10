#pragma once

#include <xtils/system/signal_handler.h>

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "xtils/logging/logger.h"
namespace xtils {
class runtime_error : public std::runtime_error {
 public:
  runtime_error(const std::string& msg) : std::runtime_error(msg) {
    std::cerr << system::GetStackTrace() << std::endl;
  }
};
}  // namespace xtils

namespace utils {
inline bool Try(std::function<void()> cb, bool log = false) {
  try {
    cb();
    return true;
  } catch (const std::exception& e) {
    if (log) {
      LogI("exception %s", e.what());
    }
  } catch (...) {
    if (log) {
      LogW("unknow exception!!!");
    }
  }
  return false;
}
}  // namespace utils
