#include "xtils/tasks/task_group.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

class TaskGroupTestFixture {
 public:
  TaskGroupTestFixture() {}

  void waitFor(int ms = 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }

  std::thread::id getMainRunnerId(TaskGroup* tg) {
    std::promise<std::thread::id> p;
    auto f = p.get_future();
    tg->PostTask([&p]() { p.set_value(std::this_thread::get_id()); });
    return f.get();
  }
};

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: PostTask runs on main_runner thread") {
  auto tg = std::make_shared<TaskGroup>(2);

  // Get the main_runner thread id
  std::thread::id main_runner_id = getMainRunnerId(tg.get());

  // Verify PostTask runs on main_runner
  std::atomic<bool> executed{false};
  std::thread::id task_thread_id;

  std::promise<void> done;
  auto done_future = done.get_future();

  tg->PostTask([&]() {
    task_thread_id = std::this_thread::get_id();
    executed = true;
    done.set_value();
  });

  done_future.wait();

  CHECK(executed);
  CHECK(task_thread_id == main_runner_id);

  tg->stop();
}

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: PostAsyncTask runs on worker thread") {
  auto tg = std::make_shared<TaskGroup>(2);

  // Get the main_runner thread id
  std::thread::id main_runner_id = getMainRunnerId(tg.get());

  // Verify PostAsyncTask runs on a different (worker) thread
  std::atomic<bool> executed{false};
  std::thread::id task_thread_id;

  std::promise<void> done;
  auto done_future = done.get_future();

  tg->PostAsyncTask([&]() {
    task_thread_id = std::this_thread::get_id();
    executed = true;
    done.set_value();
  });

  done_future.wait();

  CHECK(executed);
  CHECK(task_thread_id != main_runner_id);

  tg->stop();
}

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: Sequential shares main_runner") {
  auto async_tg = std::make_shared<TaskGroup>(2);
  auto sync_tg = TaskGroup::Sequential(async_tg->main_runner());

  // Both should use the same main_runner
  std::thread::id async_main_id = getMainRunnerId(async_tg.get());
  std::thread::id sync_main_id = getMainRunnerId(sync_tg.get());

  CHECK(async_main_id == sync_main_id);

  sync_tg->stop();
  async_tg->stop();
}

TEST_CASE_FIXTURE(
    TaskGroupTestFixture,
    "TaskGroup: sync_tg PostTask and async_tg PostTask run on same thread") {
  // This simulates the App architecture: sync_tg and async_tg share main_runner
  auto async_tg = std::make_shared<TaskGroup>(4);
  auto sync_tg = TaskGroup::Sequential(async_tg->main_runner());

  std::thread::id sync_task_id;
  std::thread::id async_main_task_id;

  std::promise<void> p1, p2;
  auto f1 = p1.get_future();
  auto f2 = p2.get_future();

  // spawn() equivalent: sync_tg->PostTask
  sync_tg->PostTask([&]() {
    sync_task_id = std::this_thread::get_id();
    p1.set_value();
  });

  // Callback from spawn_async should also go to main_runner
  async_tg->PostTask([&]() {
    async_main_task_id = std::this_thread::get_id();
    p2.set_value();
  });

  f1.wait();
  f2.wait();

  // Both should run on the same main_runner thread
  CHECK(sync_task_id == async_main_task_id);

  sync_tg->stop();
  async_tg->stop();
}

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: spawn_async pattern - task on worker, callback "
                  "on main_runner") {
  // Simulate spawn_async behavior
  auto async_tg = std::make_shared<TaskGroup>(4);
  // Use shared_ptr to allow weak_ptr
  std::shared_ptr<TaskGroup> sync_tg(
      TaskGroup::Sequential(async_tg->main_runner()).release());

  std::thread::id main_runner_id = getMainRunnerId(sync_tg.get());
  std::thread::id worker_thread_id;
  std::thread::id callback_thread_id;

  std::promise<void> done;
  auto done_future = done.get_future();

  std::atomic<int> execution_order{0};
  int task_order = 0;
  int callback_order = 0;

  // Simulate spawn_async: task runs on worker, callback on main_runner
  std::weak_ptr<TaskGroup> weak_sync = sync_tg;

  async_tg->PostAsyncTask([&, weak_sync]() {
    worker_thread_id = std::this_thread::get_id();
    task_order = ++execution_order;

    // Simulate callback posting to sync_tg (main_runner)
    if (auto tg = weak_sync.lock()) {
      tg->PostTask([&]() {
        callback_thread_id = std::this_thread::get_id();
        callback_order = ++execution_order;
        done.set_value();
      });
    }
  });

  done_future.wait();

  // Task should run on worker thread (not main_runner)
  CHECK(worker_thread_id != main_runner_id);

  // Callback should run on main_runner
  CHECK(callback_thread_id == main_runner_id);

  // Task should execute before callback
  CHECK(task_order < callback_order);

  sync_tg->stop();
  async_tg->stop();
}

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: Multiple spawns maintain thread safety") {
  auto async_tg = std::make_shared<TaskGroup>(4);
  auto sync_tg = TaskGroup::Sequential(async_tg->main_runner());

  std::atomic<int> main_thread_counter{0};
  const int num_tasks = 100;

  std::thread::id main_runner_id = getMainRunnerId(sync_tg.get());

  std::atomic<int> completed{0};

  for (int i = 0; i < num_tasks; i++) {
    // Simulate spawn(): should all run on main_runner
    sync_tg->PostTask([&, main_runner_id]() {
      if (std::this_thread::get_id() == main_runner_id) {
        main_thread_counter++;
      }
      completed++;
    });
  }

  // Wait for all tasks to complete
  while (completed.load() < num_tasks) {
    waitFor(10);
  }

  // All tasks should have run on main_runner
  CHECK(main_thread_counter == num_tasks);

  sync_tg->stop();
  async_tg->stop();
}

TEST_CASE_FIXTURE(TaskGroupTestFixture,
                  "TaskGroup: spawn_async concurrent execution") {
  auto async_tg = std::make_shared<TaskGroup>(4);
  // Use shared_ptr to allow weak_ptr
  std::shared_ptr<TaskGroup> sync_tg(
      TaskGroup::Sequential(async_tg->main_runner()).release());

  std::atomic<int> concurrent_count{0};
  std::atomic<int> max_concurrent{0};
  std::atomic<int> callback_count{0};

  const int num_tasks = 20;
  std::atomic<int> completed_callbacks{0};

  std::weak_ptr<TaskGroup> weak_sync = sync_tg;

  for (int i = 0; i < num_tasks; i++) {
    async_tg->PostAsyncTask([&, weak_sync]() {
      int current = ++concurrent_count;

      // Track max concurrent tasks
      int expected = max_concurrent.load();
      while (current > expected &&
             !max_concurrent.compare_exchange_weak(expected, current)) {
      }

      // Simulate some work
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      --concurrent_count;

      // Post callback to main_runner
      if (auto tg = weak_sync.lock()) {
        tg->PostTask([&]() {
          callback_count++;
          completed_callbacks++;
        });
      }
    });
  }

  // Wait for all callbacks to complete
  while (completed_callbacks.load() < num_tasks) {
    waitFor(10);
  }

  // Should have had some concurrent execution (more than 1 at a time)
  CHECK(max_concurrent > 1);

  // All callbacks should have been executed
  CHECK(callback_count == num_tasks);

  sync_tg->stop();
  async_tg->stop();
}

int main() {
  doctest::Context context;
  int res = context.run();
  if (res == 0) {
    std::cout << "TaskGroup tests passed\n";
  } else {
    std::cout << "Some TaskGroup tests failed\n";
  }
  return res;
}
