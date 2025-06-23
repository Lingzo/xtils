/*
 * Description: Example demonstrating the optimized logger usage
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
 * - Simple example showing logger usage
 * - Performance comparison with basic timing
 * - Demonstrates async vs sync logging
 */

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "xtils/logging/logger.h"

// Define a custom log tag for this example
#undef LOG_TAG_STRING
#define LOG_TAG_STRING "EXAMPLE"

void basic_logging_example() {
  std::cout << "=== Basic Logging Example ===" << std::endl;

  // Basic logging with different levels
  LogT("This is a trace message (only in DEBUG builds)");
  LogD("This is a debug message");
  LogI("This is an info message");
  LogW("This is a warning message");
  LogE("This is an error message");

  // Logging with custom logger instance
  auto* logger = logger::default_logger();
  L_INFO(logger, "Custom logger instance message");

  // Formatted logging
  int value = 42;
  const char* text = "world";
  LogI("Formatted message: value=%d, text=%s", value, text);

  // Special purpose macros
  LogTodo();
  LogThis();
}

void performance_example() {
  std::cout << "\n=== Performance Example ===" << std::endl;

  const int NUM_MESSAGES = 10000;
  auto* logger = logger::default_logger();

  // Set to INFO level to avoid debug message overhead
  logger::set_level(logger, logger::INFO);

  // Measure performance of async logging
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < NUM_MESSAGES; ++i) {
    LogI("Performance test message #%d", i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "Logged " << NUM_MESSAGES << " messages in " << duration.count()
            << " microseconds" << std::endl;
  std::cout << "Average: " << (double)duration.count() / NUM_MESSAGES
            << " microseconds per message" << std::endl;

  // Allow time for async processing to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check for dropped messages
  std::cout << "Dropped messages: " << logger->get_dropped_count() << std::endl;
}

void level_filtering_example() {
  std::cout << "\n=== Level Filtering Example ===" << std::endl;

  auto* logger = logger::default_logger();

  // Set to WARN level - only WARN and ERROR messages will be shown
  logger::set_level(logger, logger::WARN);
  std::cout << "Set log level to WARN" << std::endl;

  LogD("This debug message should NOT appear");
  LogI("This info message should NOT appear");
  LogW("This warning message SHOULD appear");
  LogE("This error message SHOULD appear");

  // Reset to INFO level
  logger::set_level(logger, logger::INFO);
  std::cout << "Reset log level to INFO" << std::endl;

  LogD("This debug message should NOT appear");
  LogI("This info message SHOULD appear");
  LogW("This warning message SHOULD appear");
}

void threading_example() {
  std::cout << "\n=== Threading Example ===" << std::endl;

  const int NUM_THREADS = 4;
  const int MESSAGES_PER_THREAD = 1000;

  std::vector<std::thread> threads;

  // Create multiple threads that log concurrently
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([t, MESSAGES_PER_THREAD]() {
      for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
        LogI("Thread %d, message %d", t, i);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  std::cout << "All threads completed logging" << std::endl;

  // Allow time for async processing
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void error_handling_example() {
  std::cout << "\n=== Error Handling Example ===" << std::endl;

  bool condition = false;

  // CHECK macro - logs warning but continues execution
  CHECK(condition, "This condition failed but program continues");

  // DCHECK - same as CHECK
  DCHECK(condition);

  std::cout << "Program continued after CHECK failure" << std::endl;

  // Note: FATAL macro would terminate the program, so we don't demonstrate it
  LogE("This is how you would log a fatal error without terminating");
}

int main() {
  std::cout << "Logger Example Program" << std::endl;
  std::cout << "======================" << std::endl;

  // Initialize logger (happens automatically on first use)
  auto* logger = logger::default_logger();

  // Run various examples
  basic_logging_example();
  performance_example();
  level_filtering_example();
  threading_example();
  error_handling_example();

  // Flush all pending messages before exit
  logger->flush();

  // Allow time for final flush
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "\nExample completed successfully!" << std::endl;
  return 0;
}
