#include "xtils/tasks/event.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "xtils/tasks/task_group.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// Enum-based event IDs
enum TestEventIds : EventId {
  EVENT_INT_DATA = 1,
  EVENT_SIMPLE = 2,
  EVENT_CUSTOM_DATA = 3,
};

struct CustomData {
  int value;
  std::string name;
  bool operator==(const CustomData& o) const {
    return value == o.value && name == o.name;
  }
};

class EventTestFixture {
 public:
  EventTestFixture() : tg_(std::make_unique<TaskGroup>(2)) {}
  std::shared_ptr<TaskGroup> tg_;
  void waitForTasks(int ms = 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
};

TEST_CASE_FIXTURE(EventTestFixture, "EventManager: construction") {
  EventManager em(tg_);
  CHECK(true);
}

TEST_CASE_FIXTURE(EventTestFixture,
                  "Enum events: connect and emit (explicit template)") {
  EventManager em(tg_);

  std::atomic<bool> called{false};
  std::atomic<int> received{0};

  // connect for enum type requires explicit template parameter
  em.connect<TestEventIds>(EVENT_INT_DATA, [&](const TestEventIds& id) {
    called = true;
    received = static_cast<int>(id);
  });

  // emit for enum type should also use explicit template parameter
  em.emit<TestEventIds>(EVENT_INT_DATA);
  waitForTasks();

  CHECK(called);
  CHECK(received == static_cast<int>(EVENT_INT_DATA));

  // multiple callbacks for same enum id
  std::atomic<int> count{0};
  em.connect<TestEventIds>(EVENT_INT_DATA, [&](const TestEventIds& id) {
    (void)id;
    ++count;
  });
  em.emit<TestEventIds>(EVENT_INT_DATA);
  waitForTasks();
  CHECK(count.load() >= 1);
}

TEST_CASE_FIXTURE(EventTestFixture,
                  "Typed events: connect and emit (explicit template)") {
  EventManager em(tg_);

  SUBCASE("string payload") {
    std::atomic<bool> got{false};
    std::string value;

    // connect for non-enum type requires explicit template parameter
    em.connect<std::string>([&](const std::string& s) {
      got = true;
      value = s;
    });

    em.emit<std::string>(std::string("hello xtils"));
    waitForTasks();

    CHECK(got);
    CHECK(value == "hello xtils");
  }

  SUBCASE("custom struct payload") {
    std::atomic<bool> got{false};
    CustomData out{0, ""};

    em.connect<CustomData>([&](const CustomData& d) {
      got = true;
      out = d;
    });

    CustomData in{42, "alice"};
    em.emit<CustomData>(in);
    waitForTasks();

    CHECK(got);
    CHECK(out == in);
  }
}

TEST_CASE_FIXTURE(
    EventTestFixture,
    "Multiple callbacks and multiple emits (explicit templates)") {
  EventManager em(tg_);

  std::atomic<int> c1{0}, c2{0};

  em.connect<std::string>([&](const std::string& s) {
    (void)s;
    ++c1;
  });
  em.connect<std::string>([&](const std::string& s) {
    (void)s;
    ++c2;
  });

  em.emit<std::string>(std::string("a"));
  em.emit<std::string>(std::string("b"));
  waitForTasks();

  CHECK(c1 == 2);
  CHECK(c2 == 2);
}

TEST_CASE_FIXTURE(
    EventTestFixture,
    "Thread-safety: concurrent emits (enum, explicit templates)") {
  EventManager em(tg_);
  std::atomic<int> total{0};

  em.connect<TestEventIds>(EVENT_INT_DATA, [&](const TestEventIds& id) {
    (void)id;
    ++total;
  });

  const int threads = 4;
  const int per = 50;
  std::vector<std::thread> thr;
  thr.reserve(threads);

  for (int t = 0; t < threads; ++t) {
    thr.emplace_back([&]() {
      for (int i = 0; i < per; ++i) {
        em.emit<TestEventIds>(EVENT_INT_DATA);
      }
    });
  }

  for (auto& tt : thr) tt.join();

  // give some time for the task group to process posted tasks
  waitForTasks(300);

  CHECK(total == threads * per);
}

TEST_CASE_FIXTURE(
    EventTestFixture,
    "EventManager lifecycle with TaskGroup (explicit templates)") {
  auto local_tg = std::make_shared<TaskGroup>(1);
  std::atomic<bool> executed{false};

  {
    EventManager em(local_tg);
    em.connect<std::string>([&](const std::string& s) {
      (void)s;
      executed = true;
    });
    em.emit<std::string>(std::string("lifecycle"));
    waitForTasks();
  }  // EventManager destroyed here

  CHECK(executed);
  CHECK(local_tg != nullptr);
}

int main() {
  doctest::Context context;
  int res = context.run();
  if (res == 0) {
    std::cout << "EventManager tests passed\n";
  } else {
    std::cout << "Some EventManager tests failed\n";
  }
  return res;
}
