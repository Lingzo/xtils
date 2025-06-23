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

#include "xtils/logging/logger.h"

#include <time.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

#include "sink.h"

#define MAX_LINE_LOG_SIZE (1024 * 10)
#define DATE_BUF_SIZE 26  // yyyy-mm-dd hh:mm:ss.xxxxxx

namespace logger {
class Logger {
 public:
  explicit Logger() : level_(INFO) {
    sinks_.emplace_back(std::make_unique<ConsoleSink>());
    sinks_.emplace_back(
        std::make_unique<FileSink>("./a.log", 2 * 1024 * 1024, 4));
  }
  void setLevel(log_level level) { level_ = level; }
  inline log_level level() const { return level_; }
  void write_log(char* buffer, std::size_t start, std::size_t len);
  std::mutex& mtx() { return mtx_; }

 public:
  char date_buffer[DATE_BUF_SIZE + 1] = {'\0'};
  time_t last_sec = 0;

 private:
  std::mutex mtx_;
  std::vector<std::unique_ptr<Sink>> sinks_;
  log_level level_;
};

Logger* default_logger() {
  static Logger logger;
  return &logger;
}

Logger* get_logger(const char* name) { return default_logger(); }
void set_level(Logger* logger, log_level level) { logger->setLevel(level); }
void Logger::write_log(char* buffer, std::size_t start, std::size_t len) {
  for (auto& sink : sinks_) {
    sink->write_log(buffer, start, len);
  }
}
}  // namespace logger

const char* DROPED_TEXT = "\033[31m...TOO LONG...";
const char* CLEAR_COLOR = "\033[00m";
const int COLOR_STR_SIZE = 5;
const char* COLORS[] = {
    "\033[37m",  // white
    "\033[36m",  // cyan
    "\033[32m",  // green
    "\033[33m",  // yellow
    "\033[31m",  // red
};

void _write_log(logger::Logger* log, const char* name,
                const logger::source_loc& lc, logger::log_level level,
                const char* fmt, ...) {
  if (!log || log->level() > level) return;

  char buffer[MAX_LINE_LOG_SIZE + 256];

  int idx = 0;
  int n = 0;
  int date_idx = 0;
  bool color = true;
  if (color) {
    std::memcpy(buffer + idx, COLORS[level], COLOR_STR_SIZE);
    idx += COLOR_STR_SIZE;
    date_idx = idx;
  }
  idx += DATE_BUF_SIZE;

  n = snprintf(buffer + idx, 128, " %s %s %s ", to_string(level), name,
               lc.function_name);
  idx += n;

  va_list args;  // 定义可变参数列表
  va_start(args, fmt);
  n = vsnprintf(buffer + idx, MAX_LINE_LOG_SIZE, fmt, args);
  idx += n;
  va_end(args);
  if (idx > MAX_LINE_LOG_SIZE) {
    n = snprintf(buffer + idx, sizeof(buffer) - idx - 1, "%s", DROPED_TEXT);
    idx += n;
  }

  // file & line
  n = snprintf(buffer + idx, sizeof(buffer) - idx - COLOR_STR_SIZE - 1,
               " ---- %s:%d", lc.file_name, lc.line);
  idx += n;

  if (color) {
    std::memcpy(buffer + idx, CLEAR_COLOR, COLOR_STR_SIZE);
    idx += COLOR_STR_SIZE;
  }
  buffer[idx] = '\n';
  idx += 1;

  {  // time order
    std::lock_guard<std::mutex> lock(log->mtx());
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    if (log->last_sec != tp.tv_sec) {
      struct tm t;
      localtime_r(&tp.tv_sec, &t);
      strftime(log->date_buffer, DATE_BUF_SIZE, "%Y-%m-%d %H:%M:%S", &t);
      log->last_sec = tp.tv_sec;
    }
    snprintf(log->date_buffer + 19, 8, ".%03d", (int)(tp.tv_nsec / 1e6));
    std::memcpy(buffer + date_idx, log->date_buffer,
                DATE_BUF_SIZE);  // remove '\0'
    log->write_log(buffer, 0, idx);
  }
}
