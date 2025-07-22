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

#include "xtils/logging/sink.h"

#include <unistd.h>

#include <cassert>
#include <cstdio>

#include "xtils/debug/tracer.h"
#include "xtils/utils/file_utils.h"

namespace logger {

void ConsoleSink::write(const char* buf, std::size_t start, std::size_t len) {
  int n = ::write(STDOUT_FILENO, buf + start, len);
  assert(n == len);
}

void ConsoleSink::flush() { fsync(STDOUT_FILENO); }

class FileSink::Impl {
 public:
  explicit Impl(const std::string& path, std::size_t max_bytes,
                std::size_t max_items)
      : name_(path), max_bytes_(max_bytes), max_items_(max_items) {
    logger_file_ = fopen(path.c_str(), "a");
    byte_counts_ = fseek(logger_file_, 0, SEEK_END);
  }
  ~Impl() {
    fflush(logger_file_);
    int fd = fileno(logger_file_);
    fsync(fd);
    fclose(logger_file_);
  }
  void write(const char* buf, std::size_t start, std::size_t len) {
    if (byte_counts_ > max_bytes_) {
      rotate();
    }

    assert(logger_file_ != nullptr);
    int n = fwrite(buf + start, sizeof(char), len, logger_file_);
    assert(n == len);

    byte_counts_ += n;
  }

  void rotate() {
    TRACE_SCOPE("Rotating logs");
    fflush(logger_file_);
    int fd = fileno(logger_file_);
    fsync(fd);
    fclose(logger_file_);
    namespace fs = file_utils;
    const std::string abs_path = fs::absolute_path(name_);
    const std::string stem = fs::stem(name_);
    const std::string extension = fs::extension(name_);
    const std::string prefix = fs::join_path(abs_path, stem) + ".";
    const std::string suffix = "." + extension;
    if (max_items_ > 0) {
      auto oldFile = fs::join_path(abs_path, stem) + "." +
                     std::to_string(max_items_) + "." + extension;
      if (file_utils::exists(oldFile)) file_utils::remove(oldFile);
    }

    for (int i = max_items_ - 1; i >= 1; --i) {
      auto oldFile = prefix + std::to_string(i) + suffix;
      auto newFile = prefix + std::to_string(i + 1) + suffix;
      if (fs::exists(oldFile)) {
        fs::rename(oldFile, newFile);
      }
    }
    std::string firstBackupFile = prefix + ".1." + suffix;
    if (fs::exists(name_)) {
      fs::rename(name_, firstBackupFile);
    }

    logger_file_ = fopen(name_.c_str(), "a");
    byte_counts_ = fseek(logger_file_, 0, SEEK_END);
  }
  void flush() {
    if (logger_file_) fflush(logger_file_);
  }
  std::size_t byte_counts_;
  std::string name_;
  std::size_t max_bytes_;
  std::size_t max_items_;
  FILE* logger_file_{nullptr};
};

FileSink::FileSink(const std::string& path, std::size_t max_bytes,
                   std::size_t max_items) {
  impl = new Impl(path, max_bytes, max_items);
}

FileSink::~FileSink() { delete impl; }
void FileSink::write(const char* buf, std::size_t start, std::size_t len) {
  impl->write(buf, start, len);
}

void FileSink::flush() { impl->flush(); }

}  // namespace logger
