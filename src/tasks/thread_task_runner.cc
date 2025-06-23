#include "xtils/tasks/thread_task_runner.h"

#include <sys/prctl.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/tasks/unix_task_runner.h"

namespace xtils {

ThreadTaskRunner::ThreadTaskRunner(ThreadTaskRunner&& other) noexcept
    : thread_(std::move(other.thread_)), task_runner_(other.task_runner_) {
  other.task_runner_ = nullptr;
}

ThreadTaskRunner& ThreadTaskRunner::operator=(ThreadTaskRunner&& other) {
  this->~ThreadTaskRunner();
  new (this) ThreadTaskRunner(std::move(other));
  return *this;
}

ThreadTaskRunner::~ThreadTaskRunner() {
  if (task_runner_) {
    CHECK(!task_runner_->QuitCalled());
    task_runner_->Quit();

    DCHECK(thread_.joinable());
  }
  if (thread_.joinable()) thread_.join();
}

ThreadTaskRunner::ThreadTaskRunner(const std::string& name) : name_(name) {
  std::mutex init_lock;
  std::condition_variable init_cv;

  std::function<void(UnixTaskRunner*)> initializer =
      [this, &init_lock, &init_cv](UnixTaskRunner* task_runner) {
        std::lock_guard<std::mutex> lock(init_lock);
        task_runner_ = task_runner;
        // Notify while still holding the lock, as init_cv ceases to exist as
        // soon as the main thread observes a non-null task_runner_, and it can
        // wake up spuriously (i.e. before the notify if we had unlocked before
        // notifying).
        init_cv.notify_one();
      };

  thread_ = std::thread(&ThreadTaskRunner::RunTaskThread, this,
                        std::move(initializer));

  std::unique_lock<std::mutex> lock(init_lock);
  init_cv.wait(lock, [this] { return !!task_runner_; });
}

void ThreadTaskRunner::RunTaskThread(
    std::function<void(UnixTaskRunner*)> initializer) {
  if (!name_.empty()) {
    xtils::MaybeSetThreadName(name_);
  }

  UnixTaskRunner task_runner;
  task_runner.PostTask(std::bind(std::move(initializer), &task_runner));
  task_runner.Run();
}

void ThreadTaskRunner::PostTask(std::function<void()> task) {
  task_runner_->PostTask(std::move(task));
}

void ThreadTaskRunner::PostDelayedTask(std::function<void()> task,
                                       uint32_t delay_ms) {
  task_runner_->PostDelayedTask(std::move(task), delay_ms);
}

void ThreadTaskRunner::AddFileDescriptorWatch(
    PlatformHandle handle, std::function<void()> watch_task) {
  task_runner_->AddFileDescriptorWatch(handle, std::move(watch_task));
}

void ThreadTaskRunner::RemoveFileDescriptorWatch(PlatformHandle handle) {
  task_runner_->RemoveFileDescriptorWatch(handle);
}

bool ThreadTaskRunner::RunsTasksOnCurrentThread() const {
  return task_runner_->RunsTasksOnCurrentThread();
}

}  // namespace xtils
