/*
 * Description: Lightweight logger header with macro definitions
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 1.0.0
 *
 * Changelog:
 * - Simplified header with only essential declarations
 * - Fixed move semantics for better performance
 * - Optimized macro definitions
 */

#pragma once

#include <memory>
#include <string>

#include "xtils/logging/sink.h"
#include "xtils/system/signal_handler.h"

#ifndef LOG_TAG_STRING
#define LOG_TAG_STRING "default"
#endif

namespace logger {

struct source_loc {
  const char* file_name;
  int line;
  const char* function_name;

  source_loc() : file_name(""), line(0), function_name("") {}
  source_loc(const char* f, int l, const char* func)
      : file_name(f), line(l), function_name(func) {}
};

enum log_level { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, MAX };

constexpr const char* level_name[] = {"T", "D", "I", "W", "E"};
static_assert(log_level::MAX == sizeof(level_name) / sizeof(level_name[0]),
              "log_level::MAX need equal sizeof(level_name)");

constexpr const char* to_string(log_level level) { return level_name[level]; }

// Logger class with async processing
class Logger {
 public:
  Logger();
  ~Logger();

  // Non-copyable, non-movable
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void setLevel(log_level level);
  log_level level() const;

  // Core logging functions
  void write_log_async(const char* tag, const source_loc& loc, log_level level,
                       const std::string& message);
  void write_log_sync(const char* tag, const source_loc& loc, log_level level,
                      const std::string& message);

  void addSink(std::unique_ptr<Sink> s);

  void flush();
  void shutdown();

  // Statistics
  size_t get_dropped_count() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Simple interface functions
Logger* default_logger();
void set_level(Logger* logger, log_level level);

}  // namespace logger

// Optimized filename extraction at compile time
constexpr const char* get_filename(const char* path) {
  const char* last_slash = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      last_slash = p + 1;
    }
  }
  return last_slash;
}

// Core logging function - implemented in logger.cc
void _write_log(logger::Logger* log, const char* name,
                const logger::source_loc& lc, logger::log_level level,
                const char* fmt, ...);

// Optimized macro definitions
#define __SOURCE_NAME__ (get_filename(__FILE__))
#define __SOURCE_LOC__ \
  (logger::source_loc{__SOURCE_NAME__, __LINE__, __FUNCTION__})
#define __LOG(logger, level, ...) \
  _write_log(logger, LOG_TAG_STRING, __SOURCE_LOC__, level, __VA_ARGS__)

// Conditional trace logging for debug builds
#ifdef DEBUG
#define L_TRACE(logger, ...) __LOG(logger, logger::TRACE, __VA_ARGS__)
#define LogT(...) __LOG(logger::default_logger(), logger::TRACE, __VA_ARGS__)
#else
#define L_TRACE(logger, ...)
#define LogT(...)
#endif

// Standard logging macros
#define L_DEBUG(logger, ...) __LOG(logger, logger::DEBUG, __VA_ARGS__)
#define L_INFO(logger, ...) __LOG(logger, logger::INFO, __VA_ARGS__)
#define L_WARN(logger, ...) __LOG(logger, logger::WARN, __VA_ARGS__)
#define L_ERROR(logger, ...) __LOG(logger, logger::ERROR, __VA_ARGS__)

// Convenience macros using default logger
#define LogD(...) __LOG(logger::default_logger(), logger::DEBUG, __VA_ARGS__)
#define LogI(...) __LOG(logger::default_logger(), logger::INFO, __VA_ARGS__)
#define LogW(...) __LOG(logger::default_logger(), logger::WARN, __VA_ARGS__)
#define LogE(...) __LOG(logger::default_logger(), logger::ERROR, __VA_ARGS__)

// Special purpose macros
#define LogTodo() LogW("======>> TODO <<=====")
#define LogThis() LogI("======>> THIS <<=====")

// Assertion and fatal error macros
#define CHECK(expr)                                 \
  do {                                              \
    if (!(expr)) {                                  \
      LogE("Assert -- " #expr " -- \n%s",           \
           xtils::system::GetStackTrace().c_str()); \
      xtils::system::SignalHandler::Shutdown();     \
    }                                               \
  } while (0)

#define DCHECK(expr) CHECK(expr)

#define FATAL(x, ...)       \
  do {                      \
    LogW(x, ##__VA_ARGS__); \
    abort();                \
  } while (0);
