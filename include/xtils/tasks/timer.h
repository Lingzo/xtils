#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "xtils/tasks/task_group.h"
#include "xtils/utils/time_utils.h"
#include "xtils/utils/weak_ptr.h"

namespace xtils {

// Timer ID type for managing timers
using TimerId = uint64_t;
constexpr TimerId kInvalidTimerId = 0;

// Timer callback function type
using TimerCallback = std::function<void()>;

// Timer types
enum class TimerType {
  kOneShot,   // Execute once
  kRepeating  // Execute repeatedly
};

// Base timer information structure
struct TimerInfo {
  TimerId id;
  TimerCallback callback;
  TimerType type;
  uint32_t interval_ms;  // For repeating timers
  bool cancelled;

  TimerInfo(TimerId timer_id, TimerCallback cb, TimerType timer_type,
            uint32_t interval)
      : id(timer_id),
        callback(std::move(cb)),
        type(timer_type),
        interval_ms(interval),
        cancelled(false) {}
};

// Timer info for steady clock based timers
struct SteadyTimerInfo : public TimerInfo {
  time_utils::SteadyTimePoint next_execution;

  SteadyTimerInfo(TimerId timer_id, TimerCallback cb, TimerType timer_type,
                  uint32_t interval, time_utils::SteadyTimePoint next_time)
      : TimerInfo(timer_id, std::move(cb), timer_type, interval),
        next_execution(next_time) {}
};

// Timer info for system clock based timers
struct SystemTimerInfo : public TimerInfo {
  time_utils::SystemTimePoint next_execution;

  SystemTimerInfo(TimerId timer_id, TimerCallback cb, TimerType timer_type,
                  uint32_t interval, time_utils::SystemTimePoint next_time)
      : TimerInfo(timer_id, std::move(cb), timer_type, interval),
        next_execution(next_time) {}
};

// Base Timer class template
template <typename TimePoint, typename TimerInfoType>
class BaseTimer {
 public:
  explicit BaseTimer(TaskGroup* task_group)
      : task_group_(task_group), weak_factor_(this) {}

  virtual ~BaseTimer() { CancelAllTimers(); }

  // Set a relative timer - execute after specified milliseconds from now
  TimerId SetRelativeTimer(uint32_t delay_ms, TimerCallback callback,
                           TimerType type = TimerType::kOneShot);

  // Set an absolute timer - execute at specified time point
  TimerId SetAbsoluteTimer(const TimePoint& when, TimerCallback callback,
                           TimerType type = TimerType::kOneShot);

  // Set a repeating timer - execute every interval_ms milliseconds
  TimerId SetRepeatingTimer(uint32_t interval_ms, TimerCallback callback);

  // Cancel a timer
  bool CancelTimer(TimerId timer_id);

  // Cancel all timers
  void CancelAllTimers();

  // Get the number of active timers
  size_t GetActiveTimerCount() const;

 protected:
  virtual TimePoint GetCurrentTime() const = 0;
  virtual uint32_t CalculateDelayMs(const TimePoint& target_time) const = 0;
  virtual TimePoint AddMs(const TimePoint& tp, uint32_t ms) const = 0;

 private:
  void ExecuteTimer(TimerId timer_id);
  void ScheduleTimerExecution(std::shared_ptr<TimerInfoType> timer_info);
  void ScheduleRepeatingTimer(std::shared_ptr<TimerInfoType> timer_info);
  TimerId GenerateTimerId();

 private:
  TaskGroup* task_group_;  // Not owned
  mutable std::mutex timers_mutex_;
  std::map<TimerId, std::shared_ptr<TimerInfoType>> active_timers_;
  std::atomic<TimerId> next_timer_id_{1};
  WeakPtrFactory<BaseTimer> weak_factor_;
};

// Steady clock based timer (monotonic time, good for relative timers)
class SteadyTimer
    : public BaseTimer<time_utils::SteadyTimePoint, SteadyTimerInfo> {
 public:
  explicit SteadyTimer(TaskGroup* task_group);

  // Set absolute timer using steady clock timestamp (ms since steady clock
  // epoch)
  TimerId SetAbsoluteTimer(uint64_t timestamp_ms, TimerCallback callback,
                           TimerType type = TimerType::kOneShot);

  // Get current steady clock timestamp in milliseconds
  static uint64_t GetCurrentTimestampMs();

  // Get current steady clock time point
  static time_utils::SteadyTimePoint GetCurrentTimePoint();

 protected:
  time_utils::SteadyTimePoint GetCurrentTime() const override;
  uint32_t CalculateDelayMs(
      const time_utils::SteadyTimePoint& target_time) const override;
  time_utils::SteadyTimePoint AddMs(const time_utils::SteadyTimePoint& tp,
                                    uint32_t ms) const override;
};

// System clock based timer (wall clock time, good for UTC/absolute time)
class SystemTimer
    : public BaseTimer<time_utils::SystemTimePoint, SystemTimerInfo> {
 public:
  explicit SystemTimer(TaskGroup* task_group);

  // Set absolute timer using UTC timestamp (ms since Unix epoch)
  TimerId SetAbsoluteUtcTimer(uint64_t utc_timestamp_ms, TimerCallback callback,
                              TimerType type = TimerType::kOneShot);

  // Get current UTC timestamp in milliseconds since Unix epoch
  static uint64_t GetCurrentUtcTimestampMs();

  // Get current system clock time point
  static time_utils::SystemTimePoint GetCurrentTimePoint();

 protected:
  time_utils::SystemTimePoint GetCurrentTime() const override;
  uint32_t CalculateDelayMs(
      const time_utils::SystemTimePoint& target_time) const override;
  time_utils::SystemTimePoint AddMs(const time_utils::SystemTimePoint& tp,
                                    uint32_t ms) const override;
};

// Convenience aliases
using MonotonicTimer = SteadyTimer;  // For clarity in usage
using UtcTimer = SystemTimer;        // For clarity in usage

}  // namespace xtils
