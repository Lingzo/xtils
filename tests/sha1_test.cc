#include "xtils/utils/sha1.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "xtils/utils/string_utils.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

static std::string DigestToHex(const SHA1Digest& digest) {
  return ToHex(reinterpret_cast<const char*>(digest.data()), digest.size());
}

TEST_CASE("SHA1 kSHA1Length") { CHECK(kSHA1Length == 20); }

TEST_CASE("SHA1Digest type size") {
  SHA1Digest d;
  CHECK(d.size() == 20);
  CHECK(sizeof(d) == 20);
}

TEST_CASE("SHA1 NIST vector: empty string") {
  auto digest = SHA1Hash(std::string(""));
  CHECK(DigestToHex(digest) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_CASE("SHA1 NIST vector: abc") {
  auto digest = SHA1Hash(std::string("abc"));
  CHECK(DigestToHex(digest) == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("SHA1 NIST vector: 448 bits") {
  auto digest = SHA1Hash(
      std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  CHECK(DigestToHex(digest) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("SHA1Hash data+size matches string overload") {
  std::string input = "test data";
  auto d1 = SHA1Hash(input);
  auto d2 = SHA1Hash(input.data(), input.size());
  CHECK(d1 == d2);
}

TEST_CASE("SHA1Hash binary data with null bytes") {
  const char data[] = "\x00\x01\x02\x03";
  auto digest = SHA1Hash(data, 4);
  // Just verify it produces a valid 20-byte digest
  CHECK(digest.size() == 20);
  // And it differs from empty string hash
  auto empty_digest = SHA1Hash(std::string(""));
  CHECK(digest != empty_digest);
}

TEST_CASE("SHA1 consistency") {
  SUBCASE("same input same output") {
    auto d1 = SHA1Hash(std::string("hello"));
    auto d2 = SHA1Hash(std::string("hello"));
    CHECK(d1 == d2);
  }
  SUBCASE("different input different output") {
    auto d1 = SHA1Hash(std::string("hello"));
    auto d2 = SHA1Hash(std::string("world"));
    CHECK(d1 != d2);
  }
}

