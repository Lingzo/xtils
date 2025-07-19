#include "xtils/utils/time_utils.h"

#include <algorithm>
#include <limits>

namespace xtils {
namespace time {

// Steady clock utilities implementation
namespace steady {

SteadyTimePoint Now() { return std::chrono::steady_clock::now(); }

uint64_t ToMs(const SteadyTimePoint& tp) {
  auto duration = tp.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

SteadyTimePoint FromMs(uint64_t ms) {
  return SteadyTimePoint(std::chrono::milliseconds(ms));
}

uint64_t GetCurrentMs() { return ToMs(Now()); }

uint32_t CalculateDelayMs(const SteadyTimePoint& target_time) {
  auto now = Now();
  if (target_time <= now) {
    return 0;
  }

  auto delay =
      std::chrono::duration_cast<std::chrono::milliseconds>(target_time - now);
  return common::ClampDelayMs(delay.count());
}

SteadyTimePoint AddMs(const SteadyTimePoint& tp, uint32_t ms) {
  return tp + std::chrono::milliseconds(ms);
}

}  // namespace steady

// System clock utilities implementation
namespace system {

SystemTimePoint Now() { return std::chrono::system_clock::now(); }

uint64_t ToMs(const SystemTimePoint& tp) {
  auto duration = tp.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

SystemTimePoint FromMs(uint64_t ms) {
  return SystemTimePoint(std::chrono::milliseconds(ms));
}

uint64_t GetCurrentUtcMs() { return ToMs(Now()); }

SteadyTimePoint ToSteadyTime(const SystemTimePoint& system_time) {
  // This is an approximation since we can't directly convert between
  // system_clock and steady_clock. We calculate the offset and add it
  // to the current steady_clock time.
  auto system_now = Now();
  auto steady_now = steady::Now();

  auto offset = system_time - system_now;
  return steady_now + offset;
}

SystemTimePoint FromSteadyTime(const SteadyTimePoint& steady_time) {
  // This is an approximation - calculate offset from current times
  auto system_now = Now();
  auto steady_now = steady::Now();

  auto offset = steady_time - steady_now;
  return system_now + offset;
}

SystemTimePoint AddMs(const SystemTimePoint& tp, uint32_t ms) {
  return tp + std::chrono::milliseconds(ms);
}

}  // namespace system

// Common utilities implementation
namespace common {

bool IsInPast(const SteadyTimePoint& tp) { return tp <= steady::Now(); }

bool IsInPast(const SystemTimePoint& tp) { return tp <= system::Now(); }

uint64_t TimeDiffMs(const SteadyTimePoint& tp1, const SteadyTimePoint& tp2) {
  if (tp1 <= tp2) {
    return 0;
  }
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp1 - tp2);
  return static_cast<uint64_t>(diff.count());
}

uint64_t TimeDiffMs(const SystemTimePoint& tp1, const SystemTimePoint& tp2) {
  if (tp1 <= tp2) {
    return 0;
  }
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp1 - tp2);
  return static_cast<uint64_t>(diff.count());
}

uint32_t ClampDelayMs(int64_t delay_ms) {
  if (delay_ms <= 0) {
    return 0;
  }
  if (delay_ms > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(delay_ms);
}

}  // namespace common

}  // namespace time_utils
}  // namespace xtils
