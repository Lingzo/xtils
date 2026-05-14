#include "xtils/utils/time_utils.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// --- steady ---

TEST_CASE("steady::Now") {
  auto t1 = steady::Now();
  auto t2 = steady::Now();
  CHECK(t2 >= t1);
  CHECK(steady::ToMs(t1) > 0);
}

TEST_CASE("steady::ToMs/FromMs roundtrip") {
  auto tp = steady::Now();
  uint64_t ms = steady::ToMs(tp);
  auto tp2 = steady::FromMs(ms);
  // Within 1ms tolerance
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - tp2);
  CHECK(std::abs(diff.count()) <= 1);
}

TEST_CASE("steady::GetCurrentMs") {
  uint64_t ms1 = steady::GetCurrentMs();
  CHECK(ms1 > 0);
  uint64_t ms2 = steady::GetCurrentMs();
  CHECK(ms2 >= ms1);
}

TEST_CASE("steady::CalculateDelayMs") {
  SUBCASE("future time") {
    auto future = steady::AddMs(steady::Now(), 500);
    auto delay = steady::CalculateDelayMs(future);
    CHECK(delay > 0);
    CHECK(delay <= 500);
  }
  SUBCASE("past time") {
    auto past = steady::FromMs(0);
    CHECK(steady::CalculateDelayMs(past) == 0);
  }
}

TEST_CASE("steady::AddMs") {
  auto tp = steady::Now();
  auto tp2 = steady::AddMs(tp, 100);
  CHECK(tp2 > tp);
  auto diff =
      std::chrono::duration_cast<std::chrono::milliseconds>(tp2 - tp).count();
  CHECK(diff == 100);
}

// --- system ---

TEST_CASE("system::Now") {
  auto t1 = system::Now();
  CHECK(system::ToMs(t1) > 0);
  // Should be a reasonable epoch value (after 2020-01-01)
  CHECK(system::ToMs(t1) > 1577836800000ULL);
}

TEST_CASE("system::ToMs/FromMs roundtrip") {
  auto tp = system::Now();
  uint64_t ms = system::ToMs(tp);
  auto tp2 = system::FromMs(ms);
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(tp - tp2);
  CHECK(std::abs(diff.count()) <= 1);
}

TEST_CASE("system::GetCurrentUtcMs") {
  uint64_t ms = system::GetCurrentUtcMs();
  CHECK(ms > 1577836800000ULL);  // after 2020-01-01
}

TEST_CASE("system::ToSteadyTime/FromSteadyTime") {
  // Approximate roundtrip
  auto sys_now = system::Now();
  auto steady_converted = system::ToSteadyTime(sys_now);
  auto sys_back = system::FromSteadyTime(steady_converted);

  auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     sys_now - sys_back)
                     .count();
  // Within 10ms tolerance (approximation)
  CHECK(std::abs(diff_ms) <= 10);
}

TEST_CASE("system::AddMs") {
  auto tp = system::Now();
  auto tp2 = system::AddMs(tp, 100);
  CHECK(tp2 > tp);
  auto diff =
      std::chrono::duration_cast<std::chrono::milliseconds>(tp2 - tp).count();
  CHECK(diff == 100);
}

// --- common ---

TEST_CASE("common::IsInPast steady") {
  auto past = steady::FromMs(0);
  CHECK(common::IsInPast(past));

  auto future = steady::AddMs(steady::Now(), 10000);
  CHECK_FALSE(common::IsInPast(future));
}

TEST_CASE("common::IsInPast system") {
  auto past = system::FromMs(0);
  CHECK(common::IsInPast(past));

  auto future = system::AddMs(system::Now(), 10000);
  CHECK_FALSE(common::IsInPast(future));
}

TEST_CASE("common::TimeDiffMs steady") {
  auto t1 = steady::Now();
  auto t2 = steady::AddMs(t1, 200);
  CHECK(common::TimeDiffMs(t2, t1) == 200);
  CHECK(common::TimeDiffMs(t1, t2) == 0);  // t1 <= t2
  CHECK(common::TimeDiffMs(t1, t1) == 0);
}

TEST_CASE("common::TimeDiffMs system") {
  auto t1 = system::Now();
  auto t2 = system::AddMs(t1, 200);
  CHECK(common::TimeDiffMs(t2, t1) == 200);
  CHECK(common::TimeDiffMs(t1, t2) == 0);
  CHECK(common::TimeDiffMs(t1, t1) == 0);
}

TEST_CASE("common::ClampDelayMs") {
  CHECK(common::ClampDelayMs(-1) == 0);
  CHECK(common::ClampDelayMs(0) == 0);
  CHECK(common::ClampDelayMs(100) == 100);
  CHECK(common::ClampDelayMs(static_cast<int64_t>(
            std::numeric_limits<uint32_t>::max()) +
                              1) == std::numeric_limits<uint32_t>::max());
  CHECK(common::ClampDelayMs(std::numeric_limits<uint32_t>::max()) ==
        std::numeric_limits<uint32_t>::max());
}

