#pragma once

#include <exception>
#include <functional>

#include "xtils/logging/logger.h"
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
