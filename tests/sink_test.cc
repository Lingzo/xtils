#include "xtils/logging/sink.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "xtils/utils/file_utils.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils::logger;

static const std::string kTempFile = "/tmp/xtils_sink_test.log";

static void cleanup() {
  if (file_utils::exists(kTempFile)) {
    file_utils::remove(kTempFile);
  }
}

// ============================================================================
// ConsoleSink
// ============================================================================

TEST_CASE("ConsoleSink: write and flush") {
  ConsoleSink sink;
  const char* msg = "console_test\n";
  // Should not crash
  sink.write(msg, 0, std::strlen(msg));
  sink.flush();
}

TEST_CASE("ConsoleSink: write with offset") {
  ConsoleSink sink;
  const char* msg = "XXhello\n";
  // Write starting at offset 2
  sink.write(msg, 2, 6);
  sink.flush();
}

// ============================================================================
// FileSink: basic write and read back
// ============================================================================

TEST_CASE("FileSink: basic write") {
  cleanup();

  {
    FileSink sink(kTempFile, 1024 * 1024, 3);
    const char* msg = "hello world";
    sink.write(msg, 0, std::strlen(msg));
    sink.flush();
  }

  std::string content;
  CHECK(file_utils::read(kTempFile, &content));
  CHECK(content.find("hello world") != std::string::npos);

  cleanup();
}

TEST_CASE("FileSink: multiple writes") {
  cleanup();

  {
    FileSink sink(kTempFile, 1024 * 1024, 3);
    const char* msg1 = "line1\n";
    const char* msg2 = "line2\n";
    sink.write(msg1, 0, std::strlen(msg1));
    sink.write(msg2, 0, std::strlen(msg2));
    sink.flush();
  }

  std::string content;
  CHECK(file_utils::read(kTempFile, &content));
  CHECK(content.find("line1") != std::string::npos);
  CHECK(content.find("line2") != std::string::npos);

  cleanup();
}

TEST_CASE("FileSink: flush does not crash on empty") {
  cleanup();

  {
    FileSink sink(kTempFile, 1024, 3);
    sink.flush();
  }

  cleanup();
}

TEST_CASE("FileSink: write with offset") {
  cleanup();

  {
    FileSink sink(kTempFile, 1024 * 1024, 3);
    const char* msg = "XXdata";
    sink.write(msg, 2, 4);
    sink.flush();
  }

  std::string content;
  CHECK(file_utils::read(kTempFile, &content));
  CHECK(content == "data");

  cleanup();
}

