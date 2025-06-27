#pragma once

#include <atomic>
#include <cstdint>
#include <list>

#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/thread_safe.h"
#include "xtils/utils/weak_ptr.h"

namespace xtils {
class TaskGroup {
 public:
  explicit TaskGroup(int size);
  ~TaskGroup();

  TaskGroup() = delete;
  TaskGroup(const TaskGroup&) = delete;
  TaskGroup& operator=(const TaskGroup&) = delete;
  TaskGroup(TaskGroup&&) = delete;

  void PostTask(Task task);
  void PostDelayedTask(Task task, uint32_t ms);
  void PostAsyncTask(Task task, uint32_t ms = 0);

  TaskRunner* main();
  TaskRunner* slave();

  bool is_busy();
  int size();

 private:
  void runLoop(int id);
  void loopExited(int id);

 private:
  std::atomic_bool quit_{false};
  ThreadSafe<std::list<Task>> tasks_;
  std::list<std::thread> threads_;
  std::atomic_int exit_id_{-1};
  ThreadTaskRunner main_runner_;
  ThreadTaskRunner slave_runner_;
  WeakPtrFactory<TaskGroup> weak_factory_;
};
}  // namespace xtils
