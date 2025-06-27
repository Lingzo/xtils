#include <chrono>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/tasks/task_group.h"
#include "xtils/tasks/timer.h"
#include "xtils/utils/time_utils.h"

using namespace xtils;

void PrintCurrentTime(const std::string& prefix) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  LogI("%s: %s", prefix.c_str(), std::ctime(&time_t));
}

void SteadyTimerExample() {
  LogI("=== Steady Timer Example ===");

  // Create TaskGroup as thread pool
  TaskGroup task_group(2);

  // Create steady timer using the task group
  SteadyTimer timer(&task_group);

  PrintCurrentTime("Start time");

  // 1. Relative timer - execute after 1 second
  timer.SetRelativeTimer(1000,
                         []() { PrintCurrentTime("Relative timer (1s)"); });

  // 2. Repeating timer - execute every 2 seconds, 3 times
  int repeat_count = 0;
  TimerId repeating_timer =
      timer.SetRepeatingTimer(2000, [&repeat_count, &timer]() {
        repeat_count++;
        PrintCurrentTime("Repeating timer");
        LogI("Repeat count: %d", repeat_count);

        if (repeat_count >= 3) {
          // Cancel after 3 executions
          LogI("Cancelling repeating timer");
          // Note: In real code, you'd need to store the timer ID
        }
      });

  // 3. Absolute timer using steady clock timestamp
  uint64_t future_timestamp =
      SteadyTimer::GetCurrentTimestampMs() + 3500;  // 3.5 seconds from now
  timer.SetAbsoluteTimer(future_timestamp, []() {
    PrintCurrentTime("Absolute steady timer (3.5s)");
  });

  // 4. One-shot timer that will be cancelled
  TimerId cancel_timer = timer.SetRelativeTimer(
      5000, []() { LogI("This should not print - timer was cancelled"); });

  // Cancel the timer after 500ms
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  if (timer.CancelTimer(cancel_timer)) {
    LogI("Successfully cancelled timer");
  }

  LogI("Active timer count: %zu", timer.GetActiveTimerCount());

  // Wait for timers to complete
  std::this_thread::sleep_for(std::chrono::seconds(8));

  // Cancel the repeating timer if still active
  timer.CancelTimer(repeating_timer);

  LogI("Final active timer count: %zu", timer.GetActiveTimerCount());
}

void SystemTimerExample() {
  LogI("\n=== System Timer (UTC) Example ===");

  // Create TaskGroup as thread pool
  TaskGroup task_group(2);

  // Create system timer using the task group
  SystemTimer utc_timer(&task_group);

  PrintCurrentTime("Start time");

  // 1. Relative timer - execute after 1.5 seconds
  utc_timer.SetRelativeTimer(
      1500, []() { PrintCurrentTime("UTC relative timer (1.5s)"); });

  // 2. Absolute UTC timer - execute 3 seconds from now
  uint64_t future_utc = SystemTimer::GetCurrentUtcTimestampMs() + 3000;
  utc_timer.SetAbsoluteUtcTimer(future_utc, [future_utc]() {
    PrintCurrentTime("Absolute UTC timer");
    LogI("Target UTC timestamp was: %llu",
         static_cast<unsigned long long>(future_utc));
    LogI("Current UTC timestamp: %llu",
         static_cast<unsigned long long>(
             SystemTimer::GetCurrentUtcTimestampMs()));
  });

  // 3. Show time conversion utilities
  auto system_now = time_utils::system::Now();
  auto steady_now = time_utils::steady::Now();

  LogI("System time (UTC) in ms: %llu",
       static_cast<unsigned long long>(time_utils::system::ToMs(system_now)));
  LogI("Steady time in ms: %llu",
       static_cast<unsigned long long>(time_utils::steady::ToMs(steady_now)));

  // 4. Repeating UTC timer
  int utc_repeat_count = 0;
  TimerId utc_repeating =
      utc_timer.SetRepeatingTimer(1000, [&utc_repeat_count]() {
        utc_repeat_count++;
        LogI("UTC repeating timer execution #%d", utc_repeat_count);
        PrintCurrentTime("UTC repeat");
      });

  // Wait and then cancel
  std::this_thread::sleep_for(std::chrono::milliseconds(4500));
  utc_timer.CancelTimer(utc_repeating);

  LogI("Cancelled UTC repeating timer");
  LogI("Final UTC timer count: %zu", utc_timer.GetActiveTimerCount());

  // Wait a bit more to see if any timers fire
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

void ComparisonExample() {
  LogI("\n=== Timer Comparison Example ===");

  TaskGroup task_group(3);
  SteadyTimer steady_timer(&task_group);
  SystemTimer system_timer(&task_group);

  PrintCurrentTime("Comparison start");

  // Set both timers to fire at roughly the same time
  uint32_t delay_ms = 2000;

  steady_timer.SetRelativeTimer(delay_ms, []() {
    PrintCurrentTime("Steady timer fired");
    LogI("Steady timestamp: %llu",
         static_cast<unsigned long long>(SteadyTimer::GetCurrentTimestampMs()));
  });

  system_timer.SetRelativeTimer(delay_ms, []() {
    PrintCurrentTime("System timer fired");
    LogI("UTC timestamp: %llu", static_cast<unsigned long long>(
                                    SystemTimer::GetCurrentUtcTimestampMs()));
  });

  // Show current timestamps
  LogI("Current steady timestamp: %llu",
       static_cast<unsigned long long>(SteadyTimer::GetCurrentTimestampMs()));
  LogI(
      "Current UTC timestamp: %llu",
      static_cast<unsigned long long>(SystemTimer::GetCurrentUtcTimestampMs()));

  std::this_thread::sleep_for(std::chrono::seconds(3));
}

int main() {
  // Initialize logging
  // logger::Initialize();  // Comment out if not available

  LogI("Timer Example Program Starting...");

  try {
    // Run steady timer examples
    SteadyTimerExample();

    // Run system timer examples
    SystemTimerExample();

    // Run comparison example
    ComparisonExample();

  } catch (const std::exception& e) {
    LogE("Exception caught: %s", e.what());
    return 1;
  }

  LogI("Timer Example Program Completed!");
  return 0;
}
