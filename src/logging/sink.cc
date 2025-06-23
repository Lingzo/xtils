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

#include "sink.h"

#include <unistd.h>

#include <cassert>
#include <filesystem>

namespace logger {

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
  void write_log(const char* buf, std::size_t start, std::size_t len) {
    if (byte_counts_ > max_bytes_) {
      rotate();
    }

    assert(logger_file_ != nullptr);
    int n = fwrite(buf + start, sizeof(char), len, logger_file_);
    assert(n == len);

    byte_counts_ += n;
  }
  
  void rotate() {
    fflush(logger_file_);
    int fd = fileno(logger_file_);
    fsync(fd);
    fclose(logger_file_);
    namespace fs = std::filesystem;
    if (max_items_ > 0) {
      auto oldFile = name_ + "." + std::to_string(max_items_);
      if (fs::exists(oldFile)) fs::remove(oldFile);
    }

    for (int i = max_items_ - 1; i >= 1; --i) {
      std::string oldFile = name_ + "." + std::to_string(i);
      std::string newFile = name_ + "." + std::to_string(i + 1);
      if (fs::exists(oldFile)) {
        fs::rename(oldFile, newFile);
      }
    }
    std::string firstBackupFile = name_ + ".1";
    if (fs::exists(name_)) {
      fs::rename(name_, firstBackupFile);
    }

    logger_file_ = fopen(name_.c_str(), "a");
    byte_counts_ = fseek(logger_file_, 0, SEEK_END);
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
void FileSink::write_log(const char* buf, std::size_t start, std::size_t len) {
  impl->write_log(buf, start, len);
}

}  // namespace logger
