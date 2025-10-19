#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <list>
#include <memory>
#include <thread>

#include "xtils/tasks/task_runner.h"
#include "xtils/utils/thread_safe.h"
#include "xtils/utils/weak_ptr.h"

namespace xtils {
class TaskGroup {
 public:
  static std::shared_ptr<TaskGroup> Sequential(
      std::shared_ptr<TaskRunner> runner = nullptr) {
    return std::make_shared<TaskGroup>(1, runner);
  }

  static std::shared_ptr<TaskGroup> Parallel(
      int size = std::thread::hardware_concurrency(),
      std::shared_ptr<TaskRunner> runner = nullptr) {
    return std::make_shared<TaskGroup>(0, runner);
  }

 public:
  explicit TaskGroup(int size, std::shared_ptr<TaskRunner> runner = nullptr);
  ~TaskGroup();

  TaskGroup() = delete;
  TaskGroup(const TaskGroup&) = delete;
  TaskGroup& operator=(const TaskGroup&) = delete;
  TaskGroup(TaskGroup&&) = delete;

  void PostTask(Task task);
  void PostAsyncTask(Task task, uint32_t ms = 0);

  template <typename F>
  auto RunUntilCompleted(F task) -> decltype(task()) {
    using T = decltype(task());
    auto result = std::make_shared<std::promise<T>>();

    this->PostAsyncTask([task, result]() {
      try {
        if constexpr (std::is_void_v<T>) {
          task();
          result->set_value();
        } else {
          result->set_value(task());
        }
      } catch (...) {
        result->set_exception(std::current_exception());
      }
    });

    return result->get_future().get();
  }

  bool is_busy();
  int size();

  std::shared_ptr<TaskRunner> main_runner();

 private:
  void runLoop(int id);
  void loopExited(int id);

 private:
  std::atomic_bool quit_{false};
  ThreadSafe<std::list<Task>> tasks_;
  std::list<std::thread> threads_;
  std::atomic_int exit_id_{-1};
  std::shared_ptr<TaskRunner> main_runner_;
  WeakPtrFactory<TaskGroup> weak_factory_;
  int group_id_;
  static std::atomic_int id_counter_;
};

}  // namespace xtils
