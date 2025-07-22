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

#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <string>

namespace logger {
struct Sink {
  virtual ~Sink() {}
  virtual void write(const char* buf, std::size_t start,
                         std::size_t len) = 0;  // noblocking write
  virtual void flush() = 0;
};
class ConsoleSink : public Sink {
 public:
  void write(const char* buf, std::size_t start, std::size_t len) override;
  void flush() override;
};
class FileSink : public Sink {
 public:
  FileSink(const std::string& path, std::size_t max_bytes,
           std::size_t max_items);
  ~FileSink();
  void write(const char* buf, std::size_t start, std::size_t len) override;
  void flush() override;

 private:
  class Impl;
  Impl* impl;
};
}  // namespace logger
