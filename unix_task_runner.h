#pragma  once
#include <poll.h>
#include <sys/syscall.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

#include "event_fd.h"
#include "platform.h"
#include "scoped_file.h"
#include "task_runner.h"

namespace base {

constexpr ThreadID kDetached{};

inline ThreadID CurrentThreadId() { return pthread_self(); }

class ThreadChecker {
 public:
  ThreadChecker() { thread_id_.store(CurrentThreadId()); }
  ~ThreadChecker() = default;
  ThreadChecker(const ThreadChecker& o) { thread_id_ = o.thread_id_.load(); }
  ThreadChecker& operator=(const ThreadChecker& o) {
    thread_id_ = o.thread_id_.load();
    return *this;
  }
  bool CalledOnValidThread() const {
    auto self = CurrentThreadId();

    // Will re-attach if previously detached using DetachFromThread().
    auto prev_value = kDetached;
    if (thread_id_.compare_exchange_strong(prev_value, self)) return true;
    return prev_value == self;
  }
  void DetachFromThread() { thread_id_.store(kDetached); }

 private:
  mutable std::atomic<ThreadID> thread_id_;
};

// Runs a task runner on the current thread.
//
// Implementation note: we currently assume (and enforce in debug builds) that
// Run() is called from the thread that constructed the UnixTaskRunner. This is
// not strictly necessary, and we could instead track the thread that invokes
// Run(). However, a related property that *might* be important to enforce is
// that the destructor runs on the task-running thread. Otherwise, if there are
// still-pending tasks at the time of destruction, we would destroy those
// outside of the task thread (which might be unexpected to the caller). On the
// other hand, the std::function task interface discourages use of any
// resource-owning tasks (as the callable needs to be copyable), so this might
// not be important in practice.
//
// TODO(rsavitski): consider adding a thread-check in the destructor, after
// auditing existing usages.
// TODO(primiano): rename this to TaskRunnerImpl. The "Unix" part is misleading
// now as it supports also Windows.
class UnixTaskRunner : public TaskRunner {
 public:
  UnixTaskRunner();
  ~UnixTaskRunner() override;

  // Start executing tasks. Doesn't return until Quit() is called. Run() may be
  // called multiple times on the same task runner.
  void Run();
  void Quit();

  // Checks whether there are any pending immediate tasks to run. Note that
  // delayed tasks don't count even if they are due to run.
  bool IsIdleForTesting();

  // Pretends (for the purposes of running delayed tasks) that time advanced by
  // `ms`.
  void AdvanceTimeForTesting(uint32_t ms);

  // TaskRunner implementation:
  void PostTask(std::function<void()>) override;
  void PostDelayedTask(std::function<void()>, uint32_t delay_ms) override;
  void AddFileDescriptorWatch(PlatformHandle, std::function<void()>) override;
  void RemoveFileDescriptorWatch(PlatformHandle) override;
  bool RunsTasksOnCurrentThread() const override;

  // Returns true if the task runner is quitting, or has quit and hasn't been
  // restarted since. Exposed primarily for ThreadTaskRunner, not necessary for
  // normal use of this class.
  bool QuitCalled();

 private:
  void WakeUp();
  void UpdateWatchTasksLocked();
  int GetDelayMsToNextTaskLocked() const;
  void RunImmediateAndDelayedTask();
  void PostFileDescriptorWatches(uint64_t windows_wait_result);
  void RunFileDescriptorWatch(PlatformHandle);

  ThreadChecker thread_checker_;
  std::atomic<PlatformThreadId> created_thread_id_ = GetThreadId();

  EventFd event_;

  std::vector<struct pollfd> poll_fds_;

  std::mutex lock_;

  std::deque<std::function<void()>> immediate_tasks_;
  std::multimap<TimeMillis, std::function<void()>> delayed_tasks_;
  bool quit_ = false;
  TimeMillis advanced_time_for_testing_ = TimeMillis(0);

  struct WatchTask {
    std::function<void()> callback;
    size_t poll_fd_index;  // Index into |poll_fds_|.
  };

  std::map<PlatformHandle, WatchTask> watch_tasks_;
  bool watch_tasks_changed_ = false;
};

}  // namespace base
