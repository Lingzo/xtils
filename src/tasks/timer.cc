#include "xtils/tasks/timer.h"

#include "xtils/logging/logger.h"

namespace xtils {

// BaseTimer template implementation
template <typename TimePoint, typename TimerInfoType>
TimerId BaseTimer<TimePoint, TimerInfoType>::SetRelativeTimer(
    uint32_t delay_ms, TimerCallback callback, TimerType type) {
  if (!callback) {
    LogE("Timer callback cannot be null");
    return kInvalidTimerId;
  }

  auto now = GetCurrentTime();
  auto target_time = AddMs(now, delay_ms);

  TimerId timer_id = GenerateTimerId();
  auto timer_info = std::make_shared<TimerInfoType>(
      timer_id, std::move(callback), type, delay_ms, target_time);

  {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    active_timers_[timer_id] = timer_info;
  }

  ScheduleTimerExecution(timer_info);
  return timer_id;
}

template <typename TimePoint, typename TimerInfoType>
TimerId BaseTimer<TimePoint, TimerInfoType>::SetAbsoluteTimer(
    const TimePoint& when, TimerCallback callback, TimerType type) {
  if (!callback) {
    LogE("Timer callback cannot be null");
    return kInvalidTimerId;
  }

  TimerId timer_id = GenerateTimerId();
  auto timer_info = std::make_shared<TimerInfoType>(
      timer_id, std::move(callback), type, 0, when);

  {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    active_timers_[timer_id] = timer_info;
  }

  ScheduleTimerExecution(timer_info);
  return timer_id;
}

template <typename TimePoint, typename TimerInfoType>
TimerId BaseTimer<TimePoint, TimerInfoType>::SetRepeatingTimer(
    uint32_t interval_ms, TimerCallback callback) {
  if (interval_ms == 0) {
    LogE("Repeating timer interval cannot be zero");
    return kInvalidTimerId;
  }
  return SetRelativeTimer(interval_ms, std::move(callback),
                          TimerType::kRepeating);
}

template <typename TimePoint, typename TimerInfoType>
bool BaseTimer<TimePoint, TimerInfoType>::CancelTimer(TimerId timer_id) {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  auto it = active_timers_.find(timer_id);
  if (it != active_timers_.end()) {
    it->second->cancelled = true;
    active_timers_.erase(it);
    return true;
  }
  return false;
}

template <typename TimePoint, typename TimerInfoType>
void BaseTimer<TimePoint, TimerInfoType>::CancelAllTimers() {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  for (auto& pair : active_timers_) {
    pair.second->cancelled = true;
  }
  active_timers_.clear();
}

template <typename TimePoint, typename TimerInfoType>
size_t BaseTimer<TimePoint, TimerInfoType>::GetActiveTimerCount() const {
  std::lock_guard<std::mutex> lock(timers_mutex_);
  return active_timers_.size();
}

template <typename TimePoint, typename TimerInfoType>
void BaseTimer<TimePoint, TimerInfoType>::ExecuteTimer(TimerId timer_id) {
  std::shared_ptr<TimerInfoType> timer_info;

  // Get timer info and check if it's still active
  {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    auto it = active_timers_.find(timer_id);
    if (it == active_timers_.end() || it->second->cancelled) {
      return;  // Timer was cancelled
    }
    timer_info = it->second;
  }

  // Execute the callback
  try {
    timer_info->callback();
  } catch (const std::exception& e) {
    LogE("Timer callback exception: %s", e.what());
  } catch (...) {
    LogE("Timer callback unknown exception");
  }

  // Handle repeating timers
  if (timer_info->type == TimerType::kRepeating && !timer_info->cancelled) {
    ScheduleRepeatingTimer(timer_info);
  } else {
    // Remove one-shot timer
    std::lock_guard<std::mutex> lock(timers_mutex_);
    active_timers_.erase(timer_id);
  }
}

template <typename TimePoint, typename TimerInfoType>
void BaseTimer<TimePoint, TimerInfoType>::ScheduleTimerExecution(
    std::shared_ptr<TimerInfoType> timer_info) {
  uint32_t delay_ms = CalculateDelayMs(timer_info->next_execution);

  // Use TaskGroup to schedule the timer execution
  auto weak_ptr = weak_factor_.GetWeakPtr();
  task_group_->PostAsyncTask(
      [weak_ptr, timer_id = timer_info->id]() {
        if (weak_ptr) weak_ptr->ExecuteTimer(timer_id);
      },
      delay_ms);
}

template <typename TimePoint, typename TimerInfoType>
void BaseTimer<TimePoint, TimerInfoType>::ScheduleRepeatingTimer(
    std::shared_ptr<TimerInfoType> timer_info) {
  // Update next execution time
  timer_info->next_execution =
      AddMs(timer_info->next_execution, timer_info->interval_ms);

  // Schedule next execution
  ScheduleTimerExecution(timer_info);
}

template <typename TimePoint, typename TimerInfoType>
TimerId BaseTimer<TimePoint, TimerInfoType>::GenerateTimerId() {
  return next_timer_id_.fetch_add(1);
}

// SteadyTimer implementation
SteadyTimer::SteadyTimer(TaskGroup* task_group)
    : BaseTimer<SteadyTimePoint, SteadyTimerInfo>(task_group) {}

TimerId SteadyTimer::SetAbsoluteTimer(uint64_t timestamp_ms,
                                      TimerCallback callback, TimerType type) {
  auto target_time = steady::FromMs(timestamp_ms);
  return BaseTimer<SteadyTimePoint, SteadyTimerInfo>::SetAbsoluteTimer(
      target_time, std::move(callback), type);
}

uint64_t SteadyTimer::GetCurrentTimestampMs() { return steady::GetCurrentMs(); }

SteadyTimePoint SteadyTimer::GetCurrentTimePoint() { return steady::Now(); }

SteadyTimePoint SteadyTimer::GetCurrentTime() const { return steady::Now(); }

uint32_t SteadyTimer::CalculateDelayMs(
    const SteadyTimePoint& target_time) const {
  return steady::CalculateDelayMs(target_time);
}

SteadyTimePoint SteadyTimer::AddMs(const SteadyTimePoint& tp,
                                   uint32_t ms) const {
  return steady::AddMs(tp, ms);
}

// SystemTimer implementation
SystemTimer::SystemTimer(TaskGroup* task_group)
    : BaseTimer<SystemTimePoint, SystemTimerInfo>(task_group) {}

TimerId SystemTimer::SetAbsoluteUtcTimer(uint64_t utc_timestamp_ms,
                                         TimerCallback callback,
                                         TimerType type) {
  auto target_time = system::FromMs(utc_timestamp_ms);
  return BaseTimer<SystemTimePoint, SystemTimerInfo>::SetAbsoluteTimer(
      target_time, std::move(callback), type);
}

uint64_t SystemTimer::GetCurrentUtcTimestampMs() {
  return system::GetCurrentUtcMs();
}

SystemTimePoint SystemTimer::GetCurrentTimePoint() { return system::Now(); }

SystemTimePoint SystemTimer::GetCurrentTime() const { return system::Now(); }

uint32_t SystemTimer::CalculateDelayMs(
    const SystemTimePoint& target_time) const {
  // Convert system time to steady time for scheduling
  auto steady_target = system::ToSteadyTime(target_time);
  return steady::CalculateDelayMs(steady_target);
}

SystemTimePoint SystemTimer::AddMs(const SystemTimePoint& tp,
                                   uint32_t ms) const {
  return system::AddMs(tp, ms);
}

// Explicit template instantiations
template class BaseTimer<SteadyTimePoint, SteadyTimerInfo>;
template class BaseTimer<SystemTimePoint, SystemTimerInfo>;

}  // namespace xtils
