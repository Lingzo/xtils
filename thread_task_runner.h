#pragma once

#include <functional>
#include <thread>

#include "string_utils.h"
#include "unix_task_runner.h"

namespace base {
// A UnixTaskRunner backed by a dedicated task thread. Shuts down the runner and
// joins the thread upon destruction. Can be moved to transfer ownership.
//
// Guarantees that:
// * the UnixTaskRunner will be constructed and destructed on the task thread.
// * the task thread will live for the lifetime of the UnixTaskRunner.
//
class ThreadTaskRunner : public TaskRunner {
 public:
  static ThreadTaskRunner CreateAndStart(const std::string& name = "") {
    return ThreadTaskRunner(name);
  }

  ThreadTaskRunner(const ThreadTaskRunner&) = delete;
  ThreadTaskRunner& operator=(const ThreadTaskRunner&) = delete;

  ThreadTaskRunner(ThreadTaskRunner&&) noexcept;
  ThreadTaskRunner& operator=(ThreadTaskRunner&&);
  ~ThreadTaskRunner() override;

  // Warning: do not call Quit() on the returned runner pointer, the termination
  // should be handled exclusively by this class' destructor.
  UnixTaskRunner* get() const { return task_runner_; }

  // TaskRunner implementation.
  // These methods just proxy to the underlying task_runner_.
  void PostTask(std::function<void()>) override;
  void PostDelayedTask(std::function<void()>, uint32_t delay_ms) override;
  void AddFileDescriptorWatch(PlatformHandle, std::function<void()>) override;
  void RemoveFileDescriptorWatch(PlatformHandle) override;
  bool RunsTasksOnCurrentThread() const override;

 private:
  explicit ThreadTaskRunner(const std::string& name);
  void RunTaskThread(std::function<void(UnixTaskRunner*)> initializer);

  std::thread thread_;
  std::string name_;
  UnixTaskRunner* task_runner_ = nullptr;
};

}  // namespace base
