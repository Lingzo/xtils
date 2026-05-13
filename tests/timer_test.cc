#include "xtils/tasks/timer.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// Helper: wait for a future with timeout, return true if ready
template <typename T>
bool WaitFor(std::future<T>& f, int ms = 2000) {
  return f.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready;
}

// --- SteadyTimer ---

TEST_CASE("SteadyTimer: SetRelativeTimer one-shot") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  auto id = timer.SetRelativeTimer(50, [&p]() { p.set_value(); });
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));

  tg->Stop();
}

TEST_CASE("SteadyTimer: SetRelativeTimer repeating") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::atomic<int> count{0};
  std::promise<void> p;
  auto f = p.get_future();

  auto id = timer.SetRelativeTimer(
      50,
      [&count, &p]() {
        if (++count >= 3) {
          try {
            p.set_value();
          } catch (...) {
          }
        }
      },
      TimerType::kRepeating);
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));
  CHECK(count >= 3);

  timer.CancelTimer(id);
  tg->Stop();
}

TEST_CASE("SteadyTimer: SetAbsoluteTimer timestamp") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  auto target = SteadyTimer::GetCurrentTimestampMs() + 50;
  auto id = timer.SetAbsoluteTimer(target, [&p]() { p.set_value(); });
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));

  tg->Stop();
}

TEST_CASE("SteadyTimer: CancelTimer") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::atomic<bool> fired{false};

  auto id = timer.SetRelativeTimer(200, [&fired]() { fired = true; });
  CHECK(id != kInvalidTimerId);
  CHECK(timer.CancelTimer(id));

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  CHECK_FALSE(fired);

  tg->Stop();
}

TEST_CASE("SteadyTimer: CancelAllTimers") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::atomic<int> count{0};

  timer.SetRelativeTimer(200, [&count]() { count++; });
  timer.SetRelativeTimer(200, [&count]() { count++; });
  timer.SetRelativeTimer(200, [&count]() { count++; });

  timer.CancelAllTimers();

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  CHECK(count == 0);

  tg->Stop();
}

TEST_CASE("SteadyTimer: GetActiveTimerCount") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  CHECK(timer.GetActiveTimerCount() == 0);

  auto id1 = timer.SetRelativeTimer(5000, []() {});
  CHECK(timer.GetActiveTimerCount() == 1);

  auto id2 = timer.SetRelativeTimer(5000, []() {});
  CHECK(timer.GetActiveTimerCount() == 2);

  timer.CancelTimer(id1);
  CHECK(timer.GetActiveTimerCount() == 1);

  timer.CancelTimer(id2);
  CHECK(timer.GetActiveTimerCount() == 0);

  tg->Stop();
}

TEST_CASE("SteadyTimer: null callback") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  auto id = timer.SetRelativeTimer(50, nullptr);
  CHECK(id == kInvalidTimerId);

  tg->Stop();
}

TEST_CASE("SteadyTimer: SetRepeatingTimer with 0 interval") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  auto id = timer.SetRepeatingTimer(0, []() {});
  CHECK(id == kInvalidTimerId);

  tg->Stop();
}

TEST_CASE("SteadyTimer: one-shot removes from active count") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  timer.SetRelativeTimer(50, [&p]() { p.set_value(); });
  CHECK(timer.GetActiveTimerCount() == 1);

  CHECK(WaitFor(f));
  // Give a moment for cleanup
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(timer.GetActiveTimerCount() == 0);

  tg->Stop();
}

// --- SystemTimer ---

TEST_CASE("SystemTimer: SetRelativeTimer one-shot") {
  auto tg = std::make_shared<TaskGroup>(2);
  SystemTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  auto id = timer.SetRelativeTimer(50, [&p]() { p.set_value(); });
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));

  tg->Stop();
}

TEST_CASE("SystemTimer: SetAbsoluteUtcTimer") {
  auto tg = std::make_shared<TaskGroup>(2);
  SystemTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  auto target = SystemTimer::GetCurrentUtcTimestampMs() + 50;
  auto id = timer.SetAbsoluteUtcTimer(target, [&p]() { p.set_value(); });
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));

  tg->Stop();
}

TEST_CASE("SystemTimer: SetRepeatingTimer") {
  auto tg = std::make_shared<TaskGroup>(2);
  SystemTimer timer(tg.get());

  std::atomic<int> count{0};
  std::promise<void> p;
  auto f = p.get_future();

  auto id = timer.SetRepeatingTimer(50, [&count, &p]() {
    if (++count >= 3) {
      try {
        p.set_value();
      } catch (...) {
      }
    }
  });
  CHECK(id != kInvalidTimerId);

  CHECK(WaitFor(f));
  CHECK(count >= 3);

  timer.CancelTimer(id);
  tg->Stop();
}

TEST_CASE("SystemTimer: CancelTimer") {
  auto tg = std::make_shared<TaskGroup>(2);
  SystemTimer timer(tg.get());

  std::atomic<bool> fired{false};

  auto id = timer.SetRelativeTimer(200, [&fired]() { fired = true; });
  CHECK(timer.CancelTimer(id));

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  CHECK_FALSE(fired);

  tg->Stop();
}

// --- Destruction safety ---

TEST_CASE("Timer destruction while callbacks pending") {
  auto tg = std::make_shared<TaskGroup>(2);

  {
    SteadyTimer timer(tg.get());
    timer.SetRelativeTimer(200, []() {});
    timer.SetRelativeTimer(300, []() {});
    // Timer destroyed here with pending callbacks
  }

  // Wait a bit to ensure no crash from dangling callbacks
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  tg->Stop();
}

// --- Multiple concurrent timers ---

TEST_CASE("Multiple concurrent SteadyTimers") {
  auto tg = std::make_shared<TaskGroup>(4);
  SteadyTimer timer(tg.get());

  const int num_timers = 5;
  std::atomic<int> count{0};
  std::promise<void> p;
  auto f = p.get_future();

  for (int i = 0; i < num_timers; ++i) {
    timer.SetRelativeTimer(50 + i * 10, [&count, &p, num_timers]() {
      if (++count >= num_timers) {
        try {
          p.set_value();
        } catch (...) {
        }
      }
    });
  }

  CHECK(WaitFor(f));
  CHECK(count >= num_timers);

  tg->Stop();
}

// --- Exception in callback ---

TEST_CASE("Exception in timer callback does not crash") {
  auto tg = std::make_shared<TaskGroup>(2);
  SteadyTimer timer(tg.get());

  std::promise<void> p;
  auto f = p.get_future();

  // First timer throws
  timer.SetRelativeTimer(50, []() { throw std::runtime_error("test"); });

  // Second timer should still fire
  timer.SetRelativeTimer(100, [&p]() { p.set_value(); });

  CHECK(WaitFor(f));

  tg->Stop();
}

int main() {
  doctest::Context context;
  int result = context.run();
  return result;
}
