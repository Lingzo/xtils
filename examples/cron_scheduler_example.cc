#include <chrono>
#include <iostream>
#include <set>
#include <thread>

#include "xtils/tasks/cron_scheduler.h"

int main() {
  std::cout << "CronScheduler Example" << std::endl;

  xtils::CronScheduler scheduler;

  // --- Demonstrate every() ---
  std::cout << "\n--- Demonstrating every() ---" << std::endl;
  int counter_every = 0;
  auto task1_id = scheduler.every(xtils::CronScheduler::Seconds(2), [&]() {
    counter_every++;
    std::cout << "Task 1 (every 2s) executed. Count: " << counter_every
              << std::endl;
  });
  std::cout << "Scheduled Task 1 (every 2s) with ID: " << task1_id << std::endl;

  // --- Demonstrate cron() ---
  std::cout << "\n--- Demonstrating cron() ---" << std::endl;
  int counter_cron = 0;
  // Schedule a task to run every 5th and 15th second of every minute
  std::set<int> seconds_set = {5, 15};
  auto task2_id = scheduler.cron(
      seconds_set,  // seconds
      {},           // minutes (every minute)
      {},           // hours (every hour)
      {},           // days (every day of month)
      {},           // months (every month)
      {},           // weekdays (every day of week)
      [&]() {
        counter_cron++;
        std::cout << "Task 2 (cron: at 5s and 15s) executed. Count: "
                  << counter_cron << std::endl;
      });
  std::cout << "Scheduled Task 2 (cron: 5s, 15s) with ID: " << task2_id
            << std::endl;

  // --- Start the scheduler ---
  std::cout << "\n--- Starting scheduler ---" << std::endl;
  scheduler.start();
  std::cout << "Scheduler started. Running for 18 seconds..." << std::endl;

  std::this_thread::sleep_for(std::chrono::seconds(18));

  // --- Get Task Info ---
  std::cout << "\n--- Getting Task Info ---" << std::endl;
  if (auto info1 = scheduler.getTaskInfo(task1_id)) {
    std::cout << "Task 1 Info: ID=" << info1->id << ", Type=" << info1->type
              << ", Active=" << (info1->active ? "true" : "false")
              << ", Schedule='" << info1->schedule << "'"
              << ", Last Run="
              << (info1->lastRun ? std::ctime(&info1->lastRun) : "Never run\n");
  } else {
    std::cout << "Task 1 info not found." << std::endl;
  }

  if (auto info2 = scheduler.getTaskInfo(task2_id)) {
    std::cout << "Task 2 Info: ID=" << info2->id << ", Type=" << info2->type
              << ", Active=" << (info2->active ? "true" : "false")
              << ", Schedule='" << info2->schedule << "'"
              << ", Last Run="
              << (info2->lastRun ? std::ctime(&info2->lastRun) : "Never run\n");
  } else {
    std::cout << "Task 2 info not found." << std::endl;
  }

  // --- Cancel a task ---
  std::cout << "\n--- Cancelling Task 1 ---" << std::endl;
  if (scheduler.cancel(task1_id)) {
    std::cout << "Task 1 (ID: " << task1_id << ") cancelled successfully."
              << std::endl;
  } else {
    std::cout << "Failed to cancel Task 1." << std::endl;
  }

  // Verify cancellation
  if (auto info1_after_cancel = scheduler.getTaskInfo(task1_id)) {
    std::cout << "Task 1 (after cancel) Info: ID=" << info1_after_cancel->id
              << ", Active=" << (info1_after_cancel->active ? "true" : "false")
              << std::endl;
  }

  std::cout << "\nRunning for another 8 seconds (Task 1 should not run)..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(8));

  // --- Stop the scheduler ---
  std::cout << "\n--- Stopping scheduler ---" << std::endl;
  scheduler.stop();
  std::cout << "Scheduler stopped." << std::endl;

  std::cout << "Example finished." << std::endl;

  return 0;
}
