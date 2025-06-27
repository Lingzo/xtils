#pragma once

#include <chrono>
#include <cstdint>

namespace xtils {
namespace time_utils {

// Type aliases for different time representations
using SteadyTimePoint = std::chrono::steady_clock::time_point;
using SystemTimePoint = std::chrono::system_clock::time_point;
using Milliseconds = std::chrono::milliseconds;

// Steady clock utilities (monotonic time, good for timers)
namespace steady {
// Get current steady time point
SteadyTimePoint Now();

// Convert steady time point to milliseconds since steady clock epoch
uint64_t ToMs(const SteadyTimePoint& tp);

// Convert milliseconds since steady clock epoch to time point
SteadyTimePoint FromMs(uint64_t ms);

// Get current time in milliseconds since steady clock epoch
uint64_t GetCurrentMs();

// Calculate delay from now to target time (returns 0 if target is in the past)
uint32_t CalculateDelayMs(const SteadyTimePoint& target_time);

// Add milliseconds to a time point
SteadyTimePoint AddMs(const SteadyTimePoint& tp, uint32_t ms);
}  // namespace steady

// System clock utilities (wall clock time, good for absolute timestamps)
namespace system {
// Get current system time point
SystemTimePoint Now();

// Convert system time point to milliseconds since Unix epoch
uint64_t ToMs(const SystemTimePoint& tp);

// Convert milliseconds since Unix epoch to system time point
SystemTimePoint FromMs(uint64_t ms);

// Get current UTC time in milliseconds since Unix epoch
uint64_t GetCurrentUtcMs();

// Convert system time to steady time (approximation)
SteadyTimePoint ToSteadyTime(const SystemTimePoint& system_time);

// Convert steady time to system time (approximation)
SystemTimePoint FromSteadyTime(const SteadyTimePoint& steady_time);

// Add milliseconds to a system time point
SystemTimePoint AddMs(const SystemTimePoint& tp, uint32_t ms);
}  // namespace system

// Common utilities
namespace common {
// Check if a time point is in the past
bool IsInPast(const SteadyTimePoint& tp);
bool IsInPast(const SystemTimePoint& tp);

// Get time difference in milliseconds (returns 0 if tp1 <= tp2)
uint64_t TimeDiffMs(const SteadyTimePoint& tp1, const SteadyTimePoint& tp2);
uint64_t TimeDiffMs(const SystemTimePoint& tp1, const SystemTimePoint& tp2);

// Clamp delay to uint32_t range
uint32_t ClampDelayMs(int64_t delay_ms);
}  // namespace common

}  // namespace time_utils
}  // namespace xtils
