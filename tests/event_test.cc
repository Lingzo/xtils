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

// Test event IDs
enum TestEventIds : EventId {
  EVENT_SIMPLE = 1,
  EVENT_STRING_DATA = 2,
  EVENT_INT_DATA = PARALLEL_EVENT(3),
  EVENT_CUSTOM_DATA = 4,
  EVENT_PARALLEL_TEST = PARALLEL_EVENT(10),
  EVENT_ORDERED_TEST = ORDERED_EVENT(11)
};

// Custom test data structure
struct CustomData {
  int value;
  std::string name;

  bool operator==(const CustomData& other) const {
    return value == other.value && name == other.name;
  }
};

class EventManagerTest {
 public:
  EventManagerTest() : task_group_(std::make_unique<TaskGroup>(2)) {}

  std::unique_ptr<TaskGroup> task_group_;

  // Helper to wait for async operations
  void waitForTasks(int ms = 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
};

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Basic Construction") {
  SUBCASE("Create EventManager with TaskGroup") {
    EventManager em(task_group_.get());
    // Should not throw and should be ready to use
    CHECK(true);  // EventManager created successfully
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "Event Structure Tests") {
  SUBCASE("Event with simple data") {
    Event event{EVENT_SIMPLE, std::string("test")};

    CHECK(event.id == EVENT_SIMPLE);
    CHECK(event.is<std::string>());
    CHECK_FALSE(event.is<int>());
    CHECK(event.as<std::string>() == "test");
  }

  SUBCASE("Event type checking and casting") {
    Event int_event{EVENT_INT_DATA, 42};
    Event string_event{EVENT_STRING_DATA, std::string("hello")};

    CHECK(int_event.is<int>());
    CHECK(int_event.as<int>() == 42);

    CHECK(string_event.is<std::string>());
    CHECK(string_event.as<std::string>() == "hello");

    // Cross-type checks should fail
    CHECK_FALSE(int_event.is<std::string>());
    CHECK_FALSE(string_event.is<int>());
  }

  SUBCASE("Event try_as safe casting") {
    Event event{EVENT_INT_DATA, 123};

    auto int_value = event.try_as<int>();
    CHECK(int_value.has_value());
    CHECK(int_value.value() == 123);

    auto string_value = event.try_as<std::string>();
    CHECK_FALSE(string_value.has_value());
  }

  SUBCASE("Event with custom data structure") {
    CustomData data{42, "test_name"};
    Event event{EVENT_CUSTOM_DATA, data};

    CHECK(event.is<CustomData>());
    auto retrieved = event.as<CustomData>();
    CHECK(retrieved.value == 42);
    CHECK(retrieved.name == "test_name");
    CHECK(retrieved == data);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Connection and Emission") {
  EventManager em(task_group_.get());
  std::atomic<bool> callback_called{false};
  std::atomic<int> received_value{0};

  SUBCASE("Basic event connection and emission") {
    em.connect(EVENT_INT_DATA, [&](const Event& e) {
      if (e.is<int>()) {
        received_value = e.as<int>();
        callback_called = true;
      }
    });

    em.emit(EVENT_INT_DATA, 100);
    waitForTasks();

    CHECK(callback_called);
    CHECK(received_value == 100);
  }

  SUBCASE("Multiple callbacks for same event") {
    std::atomic<int> callback1_count{0};
    std::atomic<int> callback2_count{0};

    em.connect(EVENT_SIMPLE, [&](const Event& e) { callback1_count++; });

    em.connect(EVENT_SIMPLE, [&](const Event& e) { callback2_count++; });

    em.emit(EVENT_SIMPLE, std::string("test"));
    waitForTasks();

    CHECK(callback1_count == 1);
    CHECK(callback2_count == 1);
  }

  SUBCASE("Multiple emissions to same event") {
    std::atomic<int> total_calls{0};

    em.connect(EVENT_INT_DATA, [&](const Event& e) { total_calls++; });

    em.emit(EVENT_INT_DATA, 1);
    em.emit(EVENT_INT_DATA, 2);
    em.emit(EVENT_INT_DATA, 3);
    waitForTasks();

    CHECK(total_calls == 3);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager with Different Data Types") {
  EventManager em(task_group_.get());

  SUBCASE("String data events") {
    std::string received_string;
    std::atomic<bool> string_received{false};

    em.connect(EVENT_STRING_DATA, [&](const Event& e) {
      if (e.is<std::string>()) {
        received_string = e.as<std::string>();
        string_received = true;
      }
    });

    em.emit(EVENT_STRING_DATA, std::string("Hello World"));
    waitForTasks();

    CHECK(string_received);
    CHECK(received_string == "Hello World");
  }

  SUBCASE("Custom struct data events") {
    CustomData received_data{0, ""};
    std::atomic<bool> data_received{false};

    em.connect(EVENT_CUSTOM_DATA, [&](const Event& e) {
      if (e.is<CustomData>()) {
        received_data = e.as<CustomData>();
        data_received = true;
      }
    });

    CustomData test_data{999, "custom_test"};
    em.emit(EVENT_CUSTOM_DATA, test_data);
    waitForTasks();

    CHECK(data_received);
    CHECK(received_data == test_data);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "Parallel vs Ordered Events") {
  EventManager em(task_group_.get());

  SUBCASE("Parallel event processing") {
    std::atomic<int> parallel_count{0};
    std::vector<std::thread::id> thread_ids;
    std::mutex thread_ids_mutex;

    // Register multiple callbacks for parallel event
    for (int i = 0; i < 3; ++i) {
      em.connect(EVENT_PARALLEL_TEST, [&](const Event& e) {
        parallel_count++;
        std::lock_guard<std::mutex> lock(thread_ids_mutex);
        thread_ids.push_back(std::this_thread::get_id());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      });
    }

    em.emit(EVENT_PARALLEL_TEST, std::string("parallel"));
    waitForTasks(200);  // Give more time for parallel execution

    CHECK(parallel_count == 3);
    // Note: In a real parallel implementation, we might see different thread
    // IDs
  }

  SUBCASE("Ordered event processing") {
    std::atomic<int> ordered_count{0};
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Register multiple callbacks for ordered event
    for (int i = 0; i < 3; ++i) {
      em.connect(EVENT_ORDERED_TEST, [&, i](const Event& e) {
        ordered_count++;
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(i);
      });
    }

    em.emit(EVENT_ORDERED_TEST, std::string("ordered"));
    waitForTasks();

    CHECK(ordered_count == 3);
    CHECK(execution_order.size() == 3);
    // In ordered execution, callbacks should execute in registration order
    for (size_t i = 0; i < execution_order.size(); ++i) {
      CHECK(execution_order[i] == static_cast<int>(i));
    }
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "Event ID Macros") {
  SUBCASE("ORDERED_EVENT macro") {
    EventId ordered_id = ORDERED_EVENT(5);
    CHECK(ordered_id == 5);
  }

  SUBCASE("PARALLEL_EVENT macro") {
    EventId parallel_id = PARALLEL_EVENT(5);
    CHECK(parallel_id == (PARALLEL_PREFIX | 5));
    CHECK(parallel_id != 5);
  }

  SUBCASE("isParallelEvent function") {
    EventId ordered_id = ORDERED_EVENT(10);
    EventId parallel_id = PARALLEL_EVENT(10);

    // Note: isParallelEvent is not in header, but we can test the macro
    // behavior
    CHECK((PARALLEL_PREFIX & parallel_id) == PARALLEL_PREFIX);
    CHECK((PARALLEL_PREFIX & ordered_id) != PARALLEL_PREFIX);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Error Handling") {
  EventManager em(task_group_.get());

  SUBCASE("Emit event with no registered callbacks") {
    // Should not crash or throw
    em.emit(EVENT_SIMPLE, std::string("no_listeners"));
    waitForTasks();

    // If we get here without crashing, the test passes
    CHECK(true);
  }

  SUBCASE("Callback throws exception") {
    std::atomic<bool> safe_callback_called{false};

    // Register a callback that throws
    em.connect(EVENT_INT_DATA, [](const Event& e) {
      throw std::runtime_error("Test exception");
    });

    // Register a safe callback
    em.connect(EVENT_INT_DATA,
               [&](const Event& e) { safe_callback_called = true; });

    // Emit event - should not crash the system
    em.emit(EVENT_INT_DATA, 42);
    waitForTasks();

    // The safe callback should still be called
    // Note: This depends on the implementation's exception handling
    CHECK(safe_callback_called);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Thread Safety") {
  EventManager em(task_group_.get());
  std::atomic<int> total_events{0};

  SUBCASE("Concurrent connections and emissions") {
    // Register callback
    em.connect(EVENT_INT_DATA, [&](const Event& e) { total_events++; });

    // Start multiple threads emitting events
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const int events_per_thread = 10;

    for (int t = 0; t < num_threads; ++t) {
      threads.emplace_back([&, t]() {
        for (int i = 0; i < events_per_thread; ++i) {
          em.emit(EVENT_INT_DATA, t * events_per_thread + i);
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
      thread.join();
    }

    // Wait for all events to be processed
    waitForTasks(300);

    CHECK(total_events == num_threads * events_per_thread);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Template Emit Method") {
  EventManager em(task_group_.get());

  SUBCASE("Template emit with various types") {
    std::atomic<bool> int_received{false};
    std::atomic<bool> string_received{false};
    std::atomic<bool> custom_received{false};

    int received_int = 0;
    std::string received_string;
    CustomData received_custom{0, ""};

    em.connect(EVENT_INT_DATA, [&](const Event& e) {
      if (e.is<int>()) {
        received_int = e.as<int>();
        int_received = true;
      }
    });

    em.connect(EVENT_STRING_DATA, [&](const Event& e) {
      if (e.is<std::string>()) {
        received_string = e.as<std::string>();
        string_received = true;
      }
    });

    em.connect(EVENT_CUSTOM_DATA, [&](const Event& e) {
      if (e.is<CustomData>()) {
        received_custom = e.as<CustomData>();
        custom_received = true;
      }
    });

    // Use template emit method
    em.emit<int>(EVENT_INT_DATA, 777);
    em.emit<std::string>(EVENT_STRING_DATA, "template_test");

    CustomData test_custom{888, "template_custom"};
    em.emit<CustomData>(EVENT_CUSTOM_DATA, test_custom);

    waitForTasks();

    CHECK(int_received);
    CHECK(received_int == 777);

    CHECK(string_received);
    CHECK(received_string == "template_test");

    CHECK(custom_received);
    CHECK(received_custom == test_custom);
  }
}

TEST_CASE_FIXTURE(EventManagerTest, "EventManager Memory Management") {
  SUBCASE("EventManager lifecycle with TaskGroup") {
    auto local_task_group = std::make_unique<TaskGroup>(1);
    std::atomic<bool> callback_executed{false};

    {
      EventManager em(local_task_group.get());

      em.connect(EVENT_SIMPLE,
                 [&](const Event& e) { callback_executed = true; });

      em.emit(EVENT_SIMPLE, std::string("lifecycle_test"));
      waitForTasks();
    }  // EventManager goes out of scope

    CHECK(callback_executed);
    // TaskGroup should still be valid
    CHECK(local_task_group != nullptr);
  }
}

// Simple test runner
int main() {
  doctest::Context context;

  // Run tests
  int result = context.run();

  if (result == 0) {
    std::cout << "All EventManager tests passed!" << std::endl;
  } else {
    std::cout << "Some EventManager tests failed!" << std::endl;
  }

  return result;
}
