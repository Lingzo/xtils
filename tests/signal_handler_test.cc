#include "xtils/system/signal_handler.h"

#include <atomic>
#include <chrono>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils::system;

TEST_CASE("SignalHandler: Initialize and not shutdown") {
  SignalHandler::Initialize();
  CHECK_FALSE(SignalHandler::IsShutdownRequested());
  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Shutdown sets flag") {
  SignalHandler::Initialize();
  CHECK_FALSE(SignalHandler::IsShutdownRequested());

  SignalHandler::Shutdown();
  CHECK(SignalHandler::IsShutdownRequested());

  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Shutdown callback via Initialize") {
  std::atomic<bool> called{false};

  SignalHandler::Initialize([&called]() { called = true; });
  CHECK_FALSE(called);

  SignalHandler::Shutdown();
  CHECK(called);
  CHECK(SignalHandler::IsShutdownRequested());

  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Multiple callbacks via Initialize") {
  // Only one callback can be registered via Initialize (the first call).
  // Subsequent Initialize calls are no-ops since already initialized.
  std::atomic<int> count{0};

  SignalHandler::Initialize([&count]() { count++; });
  SignalHandler::Shutdown();
  CHECK(count == 1);

  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Double initialize is idempotent") {
  SignalHandler::Initialize();
  SignalHandler::Initialize();  // Should be a no-op
  CHECK_FALSE(SignalHandler::IsShutdownRequested());

  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Cleanup then re-initialize") {
  SignalHandler::Initialize();
  SignalHandler::Shutdown();
  CHECK(SignalHandler::IsShutdownRequested());

  SignalHandler::Cleanup();

  // Re-initialize should reset the shutdown flag
  SignalHandler::Initialize();
  CHECK_FALSE(SignalHandler::IsShutdownRequested());

  SignalHandler::Cleanup();
}

TEST_CASE("SignalHandler: Cleanup without initialize is safe") {
  SignalHandler::Cleanup();  // Should not crash
}

