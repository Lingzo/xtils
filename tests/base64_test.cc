#include "xtils/utils/base64.h"

#include <cstdint>
#include <cstring>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// --- Size helpers ---

TEST_CASE("Base64EncSize") {
  CHECK(Base64EncSize(0) == 0);
  CHECK(Base64EncSize(1) == 4);
  CHECK(Base64EncSize(2) == 4);
  CHECK(Base64EncSize(3) == 4);
  CHECK(Base64EncSize(4) == 8);
  CHECK(Base64EncSize(5) == 8);
  CHECK(Base64EncSize(6) == 8);
}

TEST_CASE("Base64DecSize") {
  CHECK(Base64DecSize(0) == 0);
  CHECK(Base64DecSize(4) == 3);
  CHECK(Base64DecSize(8) == 6);
}

// --- Encode (buffer overload) ---

TEST_CASE("Base64Encode buffer") {
  SUBCASE("empty") {
    char dst[4];
    auto n = Base64Encode("", 0, dst, sizeof(dst));
    CHECK(n == 0);
  }
  SUBCASE("f -> Zg==") {
    char dst[4];
    auto n = Base64Encode("f", 1, dst, sizeof(dst));
    CHECK(n == 4);
    CHECK(std::string(dst, 4) == "Zg==");
  }
  SUBCASE("fo -> Zm8=") {
    char dst[4];
    auto n = Base64Encode("fo", 2, dst, sizeof(dst));
    CHECK(n == 4);
    CHECK(std::string(dst, 4) == "Zm8=");
  }
  SUBCASE("foo -> Zm9v") {
    char dst[4];
    auto n = Base64Encode("foo", 3, dst, sizeof(dst));
    CHECK(n == 4);
    CHECK(std::string(dst, 4) == "Zm9v");
  }
  SUBCASE("foob -> Zm9vYg==") {
    char dst[8];
    auto n = Base64Encode("foob", 4, dst, sizeof(dst));
    CHECK(n == 8);
    CHECK(std::string(dst, 8) == "Zm9vYg==");
  }
  SUBCASE("dst too small") {
    char dst[2];
    auto n = Base64Encode("foo", 3, dst, sizeof(dst));
    CHECK(n == -1);
  }
}

// --- Encode (string overload) ---

TEST_CASE("Base64Encode string") {
  CHECK(Base64Encode("", static_cast<size_t>(0)) == "");
  CHECK(Base64Encode("f", 1) == "Zg==");
  CHECK(Base64Encode("foo", 3) == "Zm9v");
  CHECK(Base64Encode("foob", 4) == "Zm9vYg==");
  CHECK(Base64Encode("foobar", 6) == "Zm9vYmFy");
}

// --- Encode (string_view overload) ---

TEST_CASE("Base64Encode string_view") {
  CHECK(Base64Encode(std::string_view("")) == "");
  CHECK(Base64Encode(std::string_view("f")) == "Zg==");
  CHECK(Base64Encode(std::string_view("foo")) == "Zm9v");
}

// --- Decode (buffer overload) ---

TEST_CASE("Base64Decode buffer") {
  SUBCASE("Zg== -> f") {
    uint8_t dst[4];
    auto n = Base64Decode("Zg==", 4, dst, sizeof(dst));
    CHECK(n == 1);
    CHECK(dst[0] == 'f');
  }
  SUBCASE("Zm8= -> fo") {
    uint8_t dst[4];
    auto n = Base64Decode("Zm8=", 4, dst, sizeof(dst));
    CHECK(n == 2);
    CHECK(dst[0] == 'f');
    CHECK(dst[1] == 'o');
  }
  SUBCASE("Zm9v -> foo") {
    uint8_t dst[4];
    auto n = Base64Decode("Zm9v", 4, dst, sizeof(dst));
    CHECK(n == 3);
    CHECK(std::string(reinterpret_cast<char*>(dst), 3) == "foo");
  }
  SUBCASE("URL-safe chars") {
    // '-' maps to 62 (same as '+'), '_' maps to 63 (same as '/')
    // Encode "foo" = "Zm9v", replace chars to test URL-safe decode is same
    // Let's test with a known URL-safe example
    // Standard: "a>b?" base64 = "YT5iPw=="
    // URL-safe: replace + with -, / with _
    // First encode normally, then verify URL-safe decode
    std::string encoded = Base64Encode(std::string_view("i\xb7\x7b"));
    // "i\xb7\x7b" encodes to "abc7ew==" or similar; let's just test the
    // decode of '-' and '_' characters directly
    uint8_t dst[8];
    // '+' (standard) and '-' (URL-safe) should both decode to value 62
    // '/' (standard) and '_' (URL-safe) should both decode to value 63
    // "ab+/" and "ab-_" should decode to the same result
    auto n1 = Base64Decode("ab+/", 4, dst, sizeof(dst));
    CHECK(n1 == 3);
    uint8_t expected[3];
    std::memcpy(expected, dst, 3);

    auto n2 = Base64Decode("ab-_", 4, dst, sizeof(dst));
    CHECK(n2 == 3);
    CHECK(std::memcmp(dst, expected, 3) == 0);
  }
  SUBCASE("invalid char") {
    uint8_t dst[4];
    auto n = Base64Decode("Z!==", 4, dst, sizeof(dst));
    CHECK(n == -1);
  }
  SUBCASE("dst too small") {
    uint8_t dst[1];
    auto n = Base64Decode("Zm9v", 4, dst, sizeof(dst));
    CHECK(n == -1);
  }
}

// --- Decode (string overload) ---

TEST_CASE("Base64Decode string") {
  SUBCASE("valid") {
    auto result = Base64Decode("Zm9v", 4);
    CHECK(result.has_value());
    CHECK(result.value() == "foo");
  }
  SUBCASE("invalid") {
    auto result = Base64Decode("!!!!", 4);
    CHECK_FALSE(result.has_value());
  }
}

// --- Decode (string_view overload) ---

TEST_CASE("Base64Decode string_view") {
  auto result = Base64Decode(std::string_view("Zm9v"));
  CHECK(result.has_value());
  CHECK(result.value() == "foo");

  auto bad = Base64Decode(std::string_view("!!!!"));
  CHECK_FALSE(bad.has_value());
}

// --- Roundtrip ---

TEST_CASE("Base64 roundtrip") {
  auto roundtrip = [](const std::string& input) {
    auto encoded = Base64Encode(std::string_view(input));
    auto decoded = Base64Decode(std::string_view(encoded));
    CHECK(decoded.has_value());
    CHECK(decoded.value() == input);
  };

  SUBCASE("empty") { roundtrip(""); }
  SUBCASE("short strings") {
    roundtrip("a");
    roundtrip("ab");
    roundtrip("abc");
    roundtrip("abcd");
  }
  SUBCASE("binary with null bytes") {
    std::string binary(4, '\0');
    binary[1] = '\x01';
    binary[3] = '\xff';
    roundtrip(binary);
  }
  SUBCASE("long string") {
    std::string long_str(1024, 'X');
    roundtrip(long_str);
  }
}

