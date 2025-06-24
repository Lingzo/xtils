/*
 * Description: Simplified logger implementation with async processing
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
 * - Simplified implementation avoiding complex templates
 * - Async processing with simple queue
 * - Optimized time formatting and string operations
 * - Better error handling and thread safety
 */

#include "xtils/logging/logger.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

#include "xtils/logging/sink.h"

#define MAX_LINE_LOG_SIZE (1024 * 10)
#define DATE_BUF_SIZE 26  // yyyy-mm-dd hh:mm:ss.xxxxxx

namespace {

// Color codes for terminal output
constexpr const char* RESET_COLOR = "\033[0m";
constexpr const char* COLORS[] = {
    "\033[37m",  // white - TRACE
    "\033[36m",  // cyan - DEBUG
    "\033[32m",  // green - INFO
    "\033[33m",  // yellow - WARN
    "\033[31m",  // red - ERROR
};

constexpr int COLOR_STR_SIZE = 5;

// Thread-local time formatter for better performance
thread_local struct {
  char date_buffer[DATE_BUF_SIZE + 1] = {'\0'};
  time_t last_sec = 0;

  const char* format_time() {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);

    if (last_sec != tp.tv_sec) {
      struct tm t;
      localtime_r(&tp.tv_sec, &t);
      strftime(date_buffer, DATE_BUF_SIZE, "%Y-%m-%d %H:%M:%S", &t);
      last_sec = tp.tv_sec;
    }

    // Add milliseconds
    snprintf(date_buffer + 19, 8, ".%03d", (int)(tp.tv_nsec / 1e6));
    return date_buffer;
  }
} time_formatter;

}  // namespace

namespace logger {

// Simple log entry structure
struct LogEntry {
  std::string timestamp;
  log_level level;
  std::string tag;
  std::string function_name;
  std::string file_name;
  int line;
  std::string message;

  LogEntry() = default;
  LogEntry(log_level lvl, const char* t, const source_loc& loc,
           const std::string& msg)
      : timestamp(time_formatter.format_time()),
        level(lvl),
        tag(t ? t : ""),
        function_name(loc.function_name ? loc.function_name : ""),
        file_name(loc.file_name ? loc.file_name : ""),
        line(loc.line),
        message(msg) {}
};

// Logger implementation class
class Logger::Impl {
 public:
  explicit Impl()
      : level_(INFO), shutdown_requested_(false), dropped_messages_(0) {
    // Start worker thread
    worker_thread_ = std::thread(&Impl::worker_thread, this);
  }

  ~Impl() { shutdown(); }

  void setLevel(log_level level) {
    std::lock_guard<std::mutex> lock(level_mutex_);
    level_ = level;
  }

  log_level level() const {
    std::lock_guard<std::mutex> lock(level_mutex_);
    return level_;
  }

  void write_log_async(const char* tag, const source_loc& loc, log_level level,
                       const std::string& message) {
    if (level < this->level()) return;

    LogEntry entry(level, tag, loc, message);

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (log_queue_.size() >= MAX_QUEUE_SIZE) {
        dropped_messages_++;
        return;
      }
      log_queue_.push(std::move(entry));
    }

    cv_.notify_one();
  }

  void write_log_sync(const char* tag, const source_loc& loc, log_level level,
                      const std::string& message) {
    if (level < this->level()) return;

    LogEntry entry(level, tag, loc, message);
    process_log_entry(entry);
  }

  void addSink(std::unique_ptr<Sink> s) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.emplace_back(std::move(s));
  }

  void flush() {
    // Process all remaining entries synchronously
    std::queue<LogEntry> temp_queue;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      temp_queue = std::move(log_queue_);
      log_queue_ = std::queue<LogEntry>();
    }

    while (!temp_queue.empty()) {
      process_log_entry(temp_queue.front());
      temp_queue.pop();
    }

    // Flush all sinks
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
      if (sink) {
        sink->flush();
      }
    }
  }

  void shutdown() {
    if (!shutdown_requested_.exchange(true)) {
      cv_.notify_all();
      if (worker_thread_.joinable()) {
        worker_thread_.join();
      }
    }
  }

  size_t get_dropped_count() const { return dropped_messages_.load(); }

 private:
  static constexpr size_t MAX_QUEUE_SIZE = 4096;

  void worker_thread() {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    while (!shutdown_requested_.load() || !log_queue_.empty()) {
      cv_.wait(lock, [this] {
        return shutdown_requested_.load() || !log_queue_.empty();
      });

      while (!log_queue_.empty()) {
        LogEntry entry = std::move(log_queue_.front());
        log_queue_.pop();

        lock.unlock();
        process_log_entry(entry);
        lock.lock();
      }
    }
  }

  void process_log_entry(const LogEntry& entry) {
    std::string formatted_message = format_log_message(entry);

    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
      if (sink) {
        sink->write_log(formatted_message.c_str(), 0, formatted_message.size());
      }
    }
  }

  std::string format_log_message(const LogEntry& entry) {
    const char* filename = entry.file_name.c_str();
    bool use_color = isatty(STDOUT_FILENO);

    std::ostringstream oss;

    if (use_color) {
      oss << COLORS[entry.level];
    }

    oss << entry.timestamp << " [" << to_string(entry.level) << "] "
        << entry.tag << " " << entry.function_name << " " << entry.message
        << " ---- " << filename << ":" << entry.line;

    if (use_color) {
      oss << RESET_COLOR;
    }

    oss << "\n";
    return oss.str();
  }

  mutable std::mutex level_mutex_;
  log_level level_;

  std::mutex queue_mutex_;
  std::queue<LogEntry> log_queue_;
  std::condition_variable cv_;

  std::mutex sinks_mutex_;
  std::vector<std::unique_ptr<Sink>> sinks_;

  std::atomic<bool> shutdown_requested_;
  std::thread worker_thread_;
  std::atomic<size_t> dropped_messages_;
};

Logger::Logger() : impl_(std::make_unique<Impl>()) {}

Logger::~Logger() = default;

void Logger::setLevel(log_level level) { impl_->setLevel(level); }

log_level Logger::level() const { return impl_->level(); }

void Logger::write_log_async(const char* tag, const source_loc& loc,
                             log_level level, const std::string& message) {
  impl_->write_log_async(tag, loc, level, message);
}

void Logger::write_log_sync(const char* tag, const source_loc& loc,
                            log_level level, const std::string& message) {
  impl_->write_log_sync(tag, loc, level, message);
}
void Logger::addSink(std::unique_ptr<Sink> s) { impl_->addSink(std::move(s)); }

void Logger::flush() { impl_->flush(); }

void Logger::shutdown() { impl_->shutdown(); }

size_t Logger::get_dropped_count() const { return impl_->get_dropped_count(); }

// Global logger instance
Logger* default_logger() {
  static Logger logger;
  return &logger;
}

void set_level(Logger* logger, log_level level) {
  if (logger) {
    logger->setLevel(level);
  }
}

}  // namespace logger

// Optimized log writing function - main entry point for all logging
void _write_log(logger::Logger* log, const char* name,
                const logger::source_loc& lc, logger::log_level level,
                const char* fmt, ...) {
  if (!log || log->level() > level) return;

  // Use thread-local buffer to avoid repeated allocations
  static thread_local std::string message_buffer;
  message_buffer.clear();
  message_buffer.reserve(MAX_LINE_LOG_SIZE);

  // Format message efficiently
  va_list args;
  va_start(args, fmt);

  char temp_buffer[MAX_LINE_LOG_SIZE];
  int len = vsnprintf(temp_buffer, sizeof(temp_buffer), fmt, args);

  if (len > 0) {
    if (len >= MAX_LINE_LOG_SIZE) {
      message_buffer.assign(temp_buffer, MAX_LINE_LOG_SIZE - 1);
      message_buffer.append("...TRUNCATED");
    } else {
      message_buffer.assign(temp_buffer, len);
    }
  }

  va_end(args);

  // Use async logging for better performance, except for INFO and above
  if (level >= logger::INFO) {
    log->write_log_sync(name, lc, level, message_buffer);
  } else {
    log->write_log_async(name, lc, level, message_buffer);
  }
}
