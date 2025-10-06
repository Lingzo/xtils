#include <ctime>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "xtils/tasks/cron_scheduler.h"

// Helper function to get the current time in system_clock
xtils::CronScheduler::TimePoint getCurrentTime() {
  return xtils::CronScheduler::Clock::now();
}

// Helper to advance time in test mode and trigger checks
void advanceTimeAndTrigger(xtils::CronScheduler& scheduler,
                           xtils::CronScheduler::TimePoint& now,
                           xtils::CronScheduler::Seconds duration,
                           std::atomic<int>& run_count) {
  for (int i = 0; i < duration.count(); ++i) {
    now += xtils::CronScheduler::Seconds(1);
    scheduler.triggerCheck(now);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1));  // Allow tasks to process
  }
}

TEST_CASE("CronScheduler: Every interval tasks in test mode") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;
  std::vector<xtils::CronScheduler::TimePoint> run_times;

  auto task_id = scheduler.every(xtils::CronScheduler::Seconds(2), [&]() {
    counter++;
    run_times.push_back(getCurrentTime());
  });

  CHECK(task_id > 0);

  xtils::CronScheduler::TimePoint now = getCurrentTime();
  scheduler.triggerCheck(now);  // Initial check should not run it immediately

  CHECK(counter == 0);

  // Advance time by 1 second, should not run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 0);

  // Advance time by another 1 second (total 2s), should run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 1);
  CHECK(run_times.size() == 1);

  // Advance time by 2 seconds, should run again
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(2),
                        counter);
  CHECK(counter == 2);
  CHECK(run_times.size() == 2);

  // Check task info
  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->id == task_id);
  CHECK(info->type == "Interval");
  CHECK(info->active == true);
  CHECK(info->schedule == "every 2s");
  CHECK(info->lastRun != 0);  // Should have run at least once

  scheduler.stop();
}

TEST_CASE("CronScheduler: Cron tasks in test mode") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;
  std::vector<xtils::CronScheduler::TimePoint> run_times;

  // Schedule to run at 5 and 10 seconds past the minute
  std::set<int> seconds_to_run = {5, 10};
  auto current_time_point = getCurrentTime();
  auto task_id = scheduler.cron(seconds_to_run, {}, {}, {}, {}, {}, [&]() {
    counter++;
    run_times.push_back(getCurrentTime());
  });

  CHECK(task_id > 0);

  // Initialize now to a minute boundary (e.g., xx:yy:00)
  xtils::CronScheduler::TimePoint now = getCurrentTime();

  std::tm tm_now = xtils::CronScheduler::toLocalTm(current_time_point, 0);
  now = std::chrono::time_point_cast<xtils::CronScheduler::Minutes>(
      current_time_point);
  if (tm_now.tm_sec >= 5) {  // make sure we're at the next minute boundary
    now = std::chrono::time_point_cast<xtils::CronScheduler::Minutes>(
        current_time_point + std::chrono::minutes(1));
  }
  tm_now = xtils::CronScheduler::toLocalTm(now, 0);

  scheduler.triggerCheck(now);
  CHECK(counter == 0);

  // Advance to 4 seconds past the minute, no run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(4),
                        counter);
  CHECK(counter == 0);

  // Advance to 5 seconds past the minute, should run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 1);
  CHECK(run_times.size() == 1);

  // Advance to 9 seconds past the minute, no run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(4),
                        counter);
  CHECK(counter == 1);

  // Advance to 10 seconds past the minute, should run
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 2);
  CHECK(run_times.size() == 2);

  // Advance to 15 seconds past the minute, no more runs this minute
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(5),
                        counter);
  CHECK(counter == 2);

  // Advance to next minute and to 5 seconds past, should run again
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(50),
                        counter);  // Go to next minute + 5s
  CHECK(counter == 3);
  CHECK(run_times.size() == 3);

  // Check task info
  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->id == task_id);
  CHECK(info->type == "Cron");
  CHECK(info->active == true);
  CHECK(info->schedule == "cron 5,10 * * * * *");
  CHECK(info->lastRun != 0);

  scheduler.stop();
}

TEST_CASE("CronScheduler: Cancel task") {
  xtils::CronScheduler scheduler(0, true);  // testMode = true
  std::atomic<int> counter = 0;

  auto task_id =
      scheduler.every(xtils::CronScheduler::Seconds(1), [&]() { counter++; });

  CHECK(task_id > 0);

  xtils::CronScheduler::TimePoint now = getCurrentTime();
  scheduler.triggerCheck(now);
  CHECK(counter == 0);

  // Let it run once
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(1),
                        counter);
  CHECK(counter == 1);

  // Cancel the task
  bool cancelled = scheduler.cancel(task_id);
  CHECK(cancelled == true);

  // Verify task info shows inactive
  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->active == false);

  // Advance time, it should not run again
  advanceTimeAndTrigger(scheduler, now, xtils::CronScheduler::Seconds(5),
                        counter);
  CHECK(counter == 1);  // Counter should still be 1

  scheduler.stop();
}

TEST_CASE("CronScheduler: GetTaskInfo for non-existent task") {
  xtils::CronScheduler scheduler(0, true);
  auto info = scheduler.getTaskInfo(9999);  // Non-existent ID
  CHECK(!info);
  scheduler.stop();
}

TEST_CASE("CronScheduler: Start and Stop with real threads (basic check)") {
  xtils::CronScheduler scheduler;  // Real mode
  std::atomic<int> counter = 0;

  auto task_id = scheduler.every(xtils::CronScheduler::Seconds(1), [&]() {
    counter++;
    std::cout << "Real mode task running! Count: " << counter << std::endl;
  });

  scheduler.start();
  std::cout << "Scheduler started in real mode. Waiting for 3 seconds..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(3));
  scheduler.stop();

  // The counter should be at least 2 or 3, depending on exact timing
  // It's non-deterministic due to real-time scheduling, so we check for a
  // minimum
  CHECK(counter >= 2);
  CHECK(counter <= 4);  // Should not be excessively high either

  auto info = scheduler.getTaskInfo(task_id);
  REQUIRE(info);
  CHECK(info->active ==
        true);  // Task itself is still active, just scheduler stopped
  CHECK(info->lastRun != 0);  // Should have run at least once
  std::cout << "Real mode task finished. Final count: " << counter << std::endl;
}

TEST_CASE("CronScheduler: Multiple tasks and cancellation in real mode") {
  xtils::CronScheduler scheduler;
  std::atomic<int> every_counter = 0;
  std::atomic<int> cron_counter = 0;

  auto every_task_id = scheduler.every(xtils::CronScheduler::Seconds(1), [&]() {
    every_counter++;
    std::cout << "Every task real mode. Count: " << every_counter << std::endl;
  });
  auto currentTime = getCurrentTime();

  std::tm tm_now = xtils::CronScheduler::toLocalTm(currentTime, 0);
  std::set<int> seconds_for_cron = {
      2, 7};                 // Run at 2 and 7 seconds past the minute
  if (tm_now.tm_sec < 53) {  // make sure cron after time
    seconds_for_cron.clear();
    seconds_for_cron.insert(tm_now.tm_sec + 2);
    seconds_for_cron.insert(tm_now.tm_sec + 7);
  } else {
    std::this_thread::sleep_for(std::chrono::seconds(60 - tm_now.tm_sec));
  }
  auto cron_task_id =
      scheduler.cron(seconds_for_cron, {}, {}, {}, {}, {}, [&]() {
        cron_counter++;
        std::cout << "Cron task real mode. Count: " << cron_counter
                  << std::endl;
      });

  scheduler.start();
  std::cout << "Multiple tasks started in real mode. Running for 8 seconds..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(4));

  // Cancel the every task
  CHECK(scheduler.cancel(every_task_id));
  std::cout << "Cancelled every task. Running for another 4 seconds..."
            << std::endl;

  std::this_thread::sleep_for(
      std::chrono::seconds(4));  // Total 8 seconds runtime

  scheduler.stop();

  // Every task should have run about 4 times before cancellation
  CHECK(every_counter >= 3);
  CHECK(every_counter <= 5);  // Some leeway for timing

  // Cron task should have run at seconds 2 and 7 in the first minute, and
  // possibly 2 and 7 in the next if the minute rolled over
  CHECK(cron_counter >= 2);
  CHECK(cron_counter <=
        4);  // Depending on when the test started relative to minute

  auto every_info = scheduler.getTaskInfo(every_task_id);
  REQUIRE(every_info);
  CHECK(every_info->active == false);

  auto cron_info = scheduler.getTaskInfo(cron_task_id);
  REQUIRE(cron_info);
  CHECK(cron_info->active == true);  // Cron task was not cancelled
}

TEST_CASE("CronScheduler: Scheduler clean shutdown with active tasks") {
  xtils::CronScheduler scheduler;
  std::atomic<int> counter = 0;

  scheduler.every(xtils::CronScheduler::Seconds(1), [&]() { counter++; });

  scheduler.start();
  std::this_thread::sleep_for(std::chrono::seconds(2));  // Let it run a bit
  scheduler.stop();  // Should clean up gracefully

  // No specific CHECK for counter, just ensuring no crash and stop works.
  // The important part is that stop() completes without issues.
  CHECK(true);  // Placeholder check for successful execution
}
