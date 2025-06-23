#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "xtils/utils/scoped.h"

namespace xtils {
using TimeMillis = std::chrono::milliseconds;
// Used only to add more details to crash reporting.
enum class WatchdogCrashReason {
  kUnspecified = 0,
  kCpuGuardrail = 1,
  kMemGuardrail = 2,
  kTaskRunnerHung = 3,
  kTraceDidntStop = 4,
};

// Make the limits more relaxed on desktop, where multi-GB traces are likely.
// Multi-GB traces can take bursts of cpu time to write into disk at the end of
// the trace.
constexpr uint32_t kWatchdogDefaultCpuLimit = 90;
constexpr uint32_t kWatchdogDefaultCpuWindow = 10 * 60 * 1000;  // 10 minutes.

// The default memory margin we give to our processes. This is used as as a
// constant to put on top of the trace buffers.
constexpr uint64_t kWatchdogDefaultMemorySlack = 32 * 1024 * 1024;  // 32 MiB.
constexpr uint32_t kWatchdogDefaultMemoryWindow = 30 * 1000;  // 30 seconds.
struct ProcStat {
  unsigned long int utime = 0l;
  unsigned long int stime = 0l;
  long int rss_pages = -1l;
};

bool ReadProcStat(int fd, ProcStat* out);

// Ensures that the calling program does not exceed certain hard limits on
// resource usage e.g. time, memory and CPU. If exceeded, the program is
// crashed.
class Watchdog {
 public:
  struct TimerData {
    TimeMillis deadline{};  // Absolute deadline, CLOCK_MONOTONIC.
    int thread_id = 0;      // The tid we'll send a SIGABRT to on expiry.
    WatchdogCrashReason crash_reason{};  // Becomes a crash key.

    TimerData() = default;
    TimerData(TimeMillis d, int t) : deadline(d), thread_id(t) {}
    bool operator<(const TimerData& x) const {
      return std::tie(deadline, thread_id) < std::tie(x.deadline, x.thread_id);
    }
    bool operator==(const TimerData& x) const {
      return std::tie(deadline, thread_id) == std::tie(x.deadline, x.thread_id);
    }
  };

  // Handle to the timer set to crash the program. If the handle is dropped,
  // the timer is removed so the program does not crash.
  class Timer {
   public:
    ~Timer();
    Timer(Timer&&) noexcept;

   private:
    friend class Watchdog;

    explicit Timer(Watchdog*, uint32_t ms, WatchdogCrashReason);
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // In production this is always Watchdog::GetInstance(), which is long
    // lived. However unittests use a non-global instance.
    Watchdog* watchdog_ = nullptr;
    TimerData timer_data_;
  };
  virtual ~Watchdog();

  static Watchdog* GetInstance();

  // Sets a timer which will crash the program in |ms| milliseconds if the
  // returned handle is not destroyed before this point.
  // WatchdogCrashReason is used only to set a crash key in the case of a crash,
  // to disambiguate different timer types.
  Timer CreateFatalTimer(uint32_t ms, WatchdogCrashReason);

  // Starts the watchdog thread which monitors the memory and CPU usage
  // of the program.
  void Start();

  // Sets a limit on the memory (defined as the RSS) used by the program
  // averaged over the last |window_ms| milliseconds. If |kb| is 0, any
  // existing limit is removed.
  // Note: |window_ms| has to be a multiple of |polling_interval_ms_|.
  void SetMemoryLimit(uint64_t bytes, uint32_t window_ms);

  // Sets a limit on the CPU usage used by the program averaged over the last
  // |window_ms| milliseconds. If |percentage| is 0, any existing limit is
  // removed.
  // Note: |window_ms| has to be a multiple of |polling_interval_ms_|.
  void SetCpuLimit(uint32_t percentage, uint32_t window_ms);

 private:
  // Represents a ring buffer in which integer values can be stored.
  class WindowedInterval {
   public:
    // Pushes a new value into a ring buffer wrapping if necessary and returns
    // whether the ring buffer is full.
    bool Push(uint64_t sample);

    // Returns the mean of the values in the buffer.
    double Mean() const;

    // Clears the ring buffer while keeping the existing size.
    void Clear();

    // Resets the size of the buffer as well as clearing it.
    void Reset(size_t new_size);

    // Gets the oldest value inserted in the buffer. The buffer must be full
    // (i.e. Push returned true) before this method can be called.
    uint64_t OldestWhenFull() const {
      // CHECK(filled_);
      return buffer_[position_];
    }

    // Gets the newest value inserted in the buffer. The buffer must be full
    // (i.e. Push returned true) before this method can be called.
    uint64_t NewestWhenFull() const {
      // CHECK(filled_);
      return buffer_[(position_ + size_ - 1) % size_];
    }

    // Returns the size of the ring buffer.
    size_t size() const { return size_; }

   private:
    bool filled_ = false;
    size_t position_ = 0;
    size_t size_ = 0;
    std::unique_ptr<uint64_t[]> buffer_;
  };

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog(Watchdog&&) = delete;
  Watchdog& operator=(Watchdog&&) = delete;

  // Main method for the watchdog thread.
  void ThreadMain();

  // Check each type of resource every |polling_interval_ms_| miillis.
  // Returns true if the threshold is exceeded and the process should be killed.
  bool CheckMemory_Locked(uint64_t rss_bytes);
  bool CheckCpu_Locked(uint64_t cpu_time);

  void AddFatalTimer(TimerData);
  void RemoveFatalTimer(TimerData);
  void RearmTimerFd_Locked();
  void SerializeLogsAndKillThread(int tid, WatchdogCrashReason);

  // Computes the time interval spanned by a given ring buffer with respect
  // to |polling_interval_ms_|.
  uint32_t WindowTimeForRingBuffer(const WindowedInterval& window);

  const uint32_t polling_interval_ms_;
  std::atomic_bool enabled_{false};
  std::thread thread_;
  ScopedPlatformHandle timer_fd_;

  std::mutex mutex_;

  uint64_t memory_limit_bytes_ = 0;
  WindowedInterval memory_window_bytes_;

  uint32_t cpu_limit_percentage_ = 0;
  WindowedInterval cpu_window_time_ticks_;

  // Outstanding timers created via CreateFatalTimer() and not yet destroyed.
  // The vector is not sorted. In most cases there are only 1-2 timers, we can
  // afford O(N) operations.
  // All the timers in the list share the same |timer_fd_|, which is keeped
  // armed on the min(timers_) through RearmTimerFd_Locked().
  std::vector<TimerData> timers_;

 protected:
  // Protected for testing.
  explicit Watchdog(uint32_t polling_interval_ms);

  bool disable_kill_failsafe_for_testing_ = false;
};

inline void RunTaskWithWatchdogGuard(const std::function<void()>& task) {
  // The longest duration allowed for a single task within the TaskRunner.
  // Exceeding this limit will trigger program termination.
  constexpr int64_t kWatchdogMillis = 180000;  // 180s

  Watchdog::Timer handle = xtils::Watchdog::GetInstance()->CreateFatalTimer(
      kWatchdogMillis, WatchdogCrashReason::kTaskRunnerHung);
  task();

  // Suppress unused variable warnings in the client library amalgamated build.
  (void)kWatchdogDefaultCpuLimit;
  (void)kWatchdogDefaultCpuWindow;
  (void)kWatchdogDefaultMemorySlack;
  (void)kWatchdogDefaultMemoryWindow;
}
}  // namespace xtils
