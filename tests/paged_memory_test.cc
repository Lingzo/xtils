#include "xtils/system/paged_memory.h"

#include <cstdint>
#include <cstring>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("PagedMemory: default constructed is invalid") {
  PagedMemory pm;
  CHECK_FALSE(pm.IsValid());
  CHECK(pm.Get() == nullptr);
  CHECK(pm.size() == 0);
}

// ============================================================================
// Allocate default
// ============================================================================

TEST_CASE("PagedMemory: Allocate default") {
  auto pm = PagedMemory::Allocate(4096);
  CHECK(pm.IsValid());
  CHECK(pm.Get() != nullptr);
  CHECK(pm.size() == 4096);

  // Memory should be zeroed
  auto* p = static_cast<char*>(pm.Get());
  for (size_t i = 0; i < 4096; ++i) {
    CHECK(p[i] == 0);
  }
}

// ============================================================================
// Allocate with kMayFail
// ============================================================================

TEST_CASE("PagedMemory: Allocate with kMayFail") {
  auto pm = PagedMemory::Allocate(4096, PagedMemory::kMayFail);
  CHECK(pm.IsValid());
  CHECK(pm.size() == 4096);
}

// ============================================================================
// Allocate with kDontCommit
// ============================================================================

TEST_CASE("PagedMemory: Allocate with kDontCommit") {
  auto pm = PagedMemory::Allocate(4096, PagedMemory::kDontCommit);
  CHECK(pm.IsValid());
  CHECK(pm.size() == 4096);
}

// ============================================================================
// Small allocation (less than page size)
// ============================================================================

TEST_CASE("PagedMemory: small allocation") {
  auto pm = PagedMemory::Allocate(100);
  CHECK(pm.IsValid());
  CHECK(pm.size() == 100);
  CHECK(pm.Get() != nullptr);
}

// ============================================================================
// Read/write within bounds
// ============================================================================

TEST_CASE("PagedMemory: read and write") {
  auto pm = PagedMemory::Allocate(4096);
  CHECK(pm.IsValid());

  auto* p = static_cast<uint8_t*>(pm.Get());

  // Write pattern
  for (size_t i = 0; i < 4096; ++i) {
    p[i] = static_cast<uint8_t>(i & 0xFF);
  }

  // Read back
  for (size_t i = 0; i < 4096; ++i) {
    CHECK(p[i] == static_cast<uint8_t>(i & 0xFF));
  }
}

// ============================================================================
// Move constructor
// ============================================================================

TEST_CASE("PagedMemory: move constructor") {
  auto pm1 = PagedMemory::Allocate(4096);
  CHECK(pm1.IsValid());
  void* orig_ptr = pm1.Get();

  PagedMemory pm2(std::move(pm1));
  CHECK(pm2.IsValid());
  CHECK(pm2.Get() == orig_ptr);
  CHECK(pm2.size() == 4096);

  // Source should be invalid after move
  CHECK_FALSE(pm1.IsValid());
  CHECK(pm1.Get() == nullptr);
}

// ============================================================================
// Move assignment
// ============================================================================

TEST_CASE("PagedMemory: move assignment") {
  auto pm1 = PagedMemory::Allocate(4096);
  CHECK(pm1.IsValid());
  void* orig_ptr = pm1.Get();

  PagedMemory pm2;
  pm2 = std::move(pm1);
  CHECK(pm2.IsValid());
  CHECK(pm2.Get() == orig_ptr);
  CHECK(pm2.size() == 4096);

  CHECK_FALSE(pm1.IsValid());
  CHECK(pm1.Get() == nullptr);
}

// ============================================================================
// AdviseDontNeed
// ============================================================================

TEST_CASE("PagedMemory: AdviseDontNeed") {
  auto pm = PagedMemory::Allocate(4096);
  CHECK(pm.IsValid());

  CHECK(pm.AdviseDontNeed(pm.Get(), pm.size()));
}

// ============================================================================
// Large allocation
// ============================================================================

TEST_CASE("PagedMemory: large allocation") {
  const size_t sz = 1024 * 1024;  // 1 MB
  auto pm = PagedMemory::Allocate(sz);
  CHECK(pm.IsValid());
  CHECK(pm.size() == sz);

  // Write and read at boundaries
  auto* p = static_cast<char*>(pm.Get());
  p[0] = 'A';
  p[sz - 1] = 'Z';
  CHECK(p[0] == 'A');
  CHECK(p[sz - 1] == 'Z');
}

// ============================================================================
// Destructor releases memory (no crash)
// ============================================================================

TEST_CASE("PagedMemory: destructor") {
  {
    auto pm = PagedMemory::Allocate(4096);
    CHECK(pm.IsValid());
    // pm destroyed here
  }
  // No crash means success
  CHECK(true);
}

int main() {
  doctest::Context context;
  int result = context.run();
  return result;
}
