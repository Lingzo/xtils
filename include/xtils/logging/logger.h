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

#include <cassert>
#include <cstring>
#include <string>

#ifndef LOG_TAG_STRING
#define LOG_TAG_STRING "default"
#endif

namespace logger {
struct source_loc {
  const char *file_name;
  const int line;
  const char *function_name;
};
enum log_level { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, MAX };
constexpr const char *level_name[] = {"T", "D", "I", "W", "E"};
static_assert(log_level::MAX == sizeof(level_name) / sizeof(level_name[0]),
              "log_level::MAX need equal sizeof(level_name)");

constexpr const char *to_string(log_level level) { return level_name[level]; }

class Logger;
Logger *default_logger();
void set_level(Logger *logger, log_level level);
}  // namespace logger
constexpr const char *get_filename(const char *path) {
  const char *last_slash = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      last_slash = p + 1;
    }
  }
  return last_slash;
}
namespace xtils {
inline std::string GetStackTrace() {return {};}
}
void _write_log(logger::Logger *log, const char *name,
                const logger::source_loc &lc, logger::log_level level,
                const char *fmt, ...);

// clang-format off
#define __SOURCE_NAME__ (get_filename(__FILE__))
#define __SOURCE_LOC__  (logger::source_loc{__SOURCE_NAME__, __LINE__, __FUNCTION__})
#define __LOG(logger, level, ...) _write_log(logger, LOG_TAG_STRING, __SOURCE_LOC__, level, __VA_ARGS__)

#ifdef DEBUG
#define L_TRACE(logger, ...) __LOG(logger, logger::TRACE, __VA_ARGS__)
#define LogT(...) __LOG(logger::default_logger(), logger::TRACE, __VA_ARGS__)
#else
#define L_TRACE(logger, ...)
#define LogT(...)
#endif

#define L_DEBUG(logger, ...) __LOG(logger, logger::DEBUG, __VA_ARGS__)
#define L_INFO(logger, ...) __LOG(logger, logger::INFO, __VA_ARGS__)
#define L_WARN(logger, ...) __LOG(logger, logger::WARN, __VA_ARGS__)
#define L_ERROR(logger, ...) __LOG(logger, logger::ERROR, __VA_ARGS__)

#define LogD(...) __LOG(logger::default_logger(), logger::DEBUG, __VA_ARGS__)
#define LogI(...) __LOG(logger::default_logger(), logger::INFO, __VA_ARGS__)
#define LogW(...) __LOG(logger::default_logger(), logger::WARN, __VA_ARGS__)
#define LogE(...) __LOG(logger::default_logger(), logger::ERROR, __VA_ARGS__)

#define LogTodo() LogW("======>> TODO <<=====")
#define LogThis() LogI("======>> THIS <<=====")


#define CHECK(expr, ...)                                     \
  do {                                                       \
    if (!(expr)) {                                           \
      LogW("Assert -- " #expr " -- " #__VA_ARGS__);          \
      fprintf(stderr, "%s", xtils::GetStackTrace().c_str()); \
    }                                                        \
  } while (0)
#define DCHECK(expr) CHECK(expr)
#define FATAL(x, ...)       \
  do {                      \
    LogW(x, ##__VA_ARGS__); \
    abort();                \
  } while (0);
// clang-format on
