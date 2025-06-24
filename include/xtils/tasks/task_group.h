#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/string_utils.h"

namespace xtils {
class TaskGroup {
 public:
  explicit TaskGroup(int size) {
    for (int i = 0; i < size; i++) {
      StackString<10> name("T-%02d", i);
      runners_.push_back(ThreadTaskRunner::CreateAndStart(name.ToStr()));
    }
    pool_size_ = runners_.size();
  }
  void PostTask(std::function<void()> task) { runners_[0].PostTask(task); }
  void PostDelayedTask(std::function<void()> task, uint32_t ms) {
    runners_[0].PostDelayedTask(task, ms);
  }

  void PostAsyncTask(std::function<void()> task) {
    runners_[getNextId()].PostTask(task);
  }

  TaskRunner* random() { return &runners_[getNextId()]; }
  TaskRunner* main() { return &runners_[0]; }

 private:
  int getNextId() {  // execpt idx=0;
    counter += 1;
    int idx = (counter % (pool_size_ - 1) + 1) % pool_size_;
    CHECK(idx > 0 && idx < pool_size_);
    return idx;
  }

 private:
  std::atomic_uint counter;
  int pool_size_;
  std::vector<ThreadTaskRunner> runners_;
};
}  // namespace xtils
