#include "xtils/tasks/task_group.h"

#include <exception>
#include <string>

#include "xtils/logging/logger.h"
#include "xtils/system/platform.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/string_utils.h"

namespace xtils {

void TaskGroup::runLoop(const std::string& name) {
  xtils::MaybeSetThreadName(name);
  Task task;
  while (!quit_) {
    if (tasks_.pop_wait(task)) {
      try {
        task();
      } catch (const std::exception& e) {
        LogW("task exception: %s", e.what());
      }
    }
  }
}

TaskGroup::TaskGroup(int size)
    : pool_size_(size),
      weak_factory_(this),
      main_runner_(ThreadTaskRunner::CreateAndStart("mainLoop")),
      slave_runner_(ThreadTaskRunner::CreateAndStart("slaveLoop")) {
  for (int i = 0; i < pool_size_; i++) {
    StackString<10> name("T-%02d", i);
    threads_.emplace_back([this, n = name.ToStr()]() { this->runLoop(n); });
  }
}
TaskGroup::~TaskGroup() {
  quit_ = true;
  tasks_.quit();
  for (auto& t : threads_) {
    if (t.joinable()) t.join();
  }
}
void TaskGroup::PostTask(Task task) { main_runner_.PostTask(task); }
void TaskGroup::PostDelayedTask(Task task, uint32_t ms) {
  main_runner_.PostDelayedTask(task, ms);
}
void TaskGroup::PostAsyncTask(Task task, uint32_t ms) {
  if (ms == 0) {
    tasks_.push(task);
  } else {
    auto weak = weak_factory_.GetWeakPtr();
    slave_runner_.PostDelayedTask(
        [task, weak]() {
          if (auto ptr = weak.get()) ptr->tasks_.push(task);
        },
        ms);
  }
}

TaskRunner* TaskGroup::slave() { return &slave_runner_; }
TaskRunner* TaskGroup::main() { return &main_runner_; }
}  // namespace xtils
