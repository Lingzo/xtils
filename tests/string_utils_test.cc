#include "xtils/utils/string_utils.h"

#include <cstdint>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// --- Character case ---

TEST_CASE("Lowercase") {
  CHECK(Lowercase('A') == 'a');
  CHECK(Lowercase('Z') == 'z');
  CHECK(Lowercase('M') == 'm');
  CHECK(Lowercase('a') == 'a');
  CHECK(Lowercase('z') == 'z');
  CHECK(Lowercase('0') == '0');
  CHECK(Lowercase(' ') == ' ');
}

TEST_CASE("Uppercase") {
  CHECK(Uppercase('a') == 'A');
  CHECK(Uppercase('z') == 'Z');
  CHECK(Uppercase('m') == 'M');
  CHECK(Uppercase('A') == 'A');
  CHECK(Uppercase('Z') == 'Z');
  CHECK(Uppercase('0') == '0');
  CHECK(Uppercase(' ') == ' ');
}

// --- CStringTo* ---

TEST_CASE("CStringToUInt32") {
  SUBCASE("valid decimal") {
    CHECK(CStringToUInt32("0").value() == 0u);
    CHECK(CStringToUInt32("123").value() == 123u);
    CHECK(CStringToUInt32("4294967295").value() == 4294967295u);
  }
  SUBCASE("valid hex") {
    CHECK(CStringToUInt32("ff", 16).value() == 255u);
    CHECK(CStringToUInt32("0", 16).value() == 0u);
  }
  SUBCASE("empty string") { CHECK(!CStringToUInt32("").has_value()); }
  SUBCASE("trailing garbage") { CHECK(!CStringToUInt32("123abc").has_value()); }
}

TEST_CASE("CStringToInt32") {
  CHECK(CStringToInt32("0").value() == 0);
  CHECK(CStringToInt32("-1").value() == -1);
  CHECK(CStringToInt32("2147483647").value() == 2147483647);
  CHECK(!CStringToInt32("").has_value());
  CHECK(!CStringToInt32("12x").has_value());
}

TEST_CASE("CStringToInt64") {
  CHECK(CStringToInt64("0").value() == 0);
  CHECK(CStringToInt64("-1").value() == -1);
  CHECK(!CStringToInt64("").has_value());
  CHECK(!CStringToInt64("abc").has_value());
}

TEST_CASE("CStringToUInt64") {
  CHECK(CStringToUInt64("0").value() == 0u);
  CHECK(CStringToUInt64("18446744073709551615").value() ==
        18446744073709551615ULL);
  CHECK(!CStringToUInt64("").has_value());
}

TEST_CASE("CStringToDouble") {
  CHECK(CStringToDouble("0").value() == doctest::Approx(0.0));
  CHECK(CStringToDouble("3.14").value() == doctest::Approx(3.14));
  CHECK(CStringToDouble("-1.5").value() == doctest::Approx(-1.5));
  CHECK(!CStringToDouble("").has_value());
  CHECK(!CStringToDouble("abc").has_value());
}

// --- StringTo* ---

TEST_CASE("StringToUInt32") {
  CHECK(StringToUInt32("42").value() == 42u);
  CHECK(StringToUInt32("ff", 16).value() == 255u);
  CHECK(!StringToUInt32("").has_value());
}

TEST_CASE("StringToInt32") {
  CHECK(StringToInt32("-42").value() == -42);
  CHECK(!StringToInt32("").has_value());
}

TEST_CASE("StringToUInt64") {
  CHECK(StringToUInt64("12345678901234").value() == 12345678901234ULL);
  CHECK(!StringToUInt64("").has_value());
}

TEST_CASE("StringToInt64") {
  CHECK(StringToInt64("-12345678901234").value() == -12345678901234LL);
  CHECK(!StringToInt64("").has_value());
}

TEST_CASE("StringToDouble") {
  CHECK(StringToDouble("2.718").value() == doctest::Approx(2.718));
  CHECK(!StringToDouble("").has_value());
}

// --- StringViewTo* ---

TEST_CASE("StringViewToInt32") {
  CHECK(StringViewToInt32("42").value() == 42);
  CHECK(StringViewToInt32("-10").value() == -10);
  CHECK(StringViewToInt32("+10").value() == 10);
  CHECK(!StringViewToInt32("").has_value());
  CHECK(!StringViewToInt32("12x").has_value());
}

TEST_CASE("StringViewToUInt32") {
  CHECK(StringViewToUInt32("42").value() == 42u);
  SUBCASE("negative wraps like strtol") {
    // -1 as int32 → cast to uint32 wraps
    auto val = StringViewToUInt32("-1");
    CHECK(val.has_value());
  }
  CHECK(!StringViewToUInt32("").has_value());
  CHECK(!StringViewToUInt32("abc").has_value());
}

TEST_CASE("StringViewToInt64") {
  CHECK(StringViewToInt64("999999999999").value() == 999999999999LL);
  CHECK(StringViewToInt64("+5").value() == 5LL);
  CHECK(!StringViewToInt64("").has_value());
}

TEST_CASE("StringViewToUInt64") {
  CHECK(StringViewToUInt64("999999999999").value() == 999999999999ULL);
  CHECK(StringViewToUInt64("-1").has_value());  // wraps
  CHECK(!StringViewToUInt64("").has_value());
}

// --- StringCopy ---

TEST_CASE("StringCopy") {
  SUBCASE("normal copy") {
    char dst[16] = {};
    StringCopy(dst, "hello", sizeof(dst));
    CHECK(std::string(dst) == "hello");
  }
  SUBCASE("truncation") {
    char dst[4] = {};
    StringCopy(dst, "hello world", sizeof(dst));
    CHECK(std::string(dst) == "hel");
    CHECK(dst[3] == '\0');
  }
  SUBCASE("exact fit") {
    char dst[6] = {};
    StringCopy(dst, "hello", sizeof(dst));
    CHECK(std::string(dst) == "hello");
  }
  SUBCASE("zero dst_size is a no-op") {
    char dst[4] = {'x', 'y', 'z', '\0'};
    StringCopy(dst, "abc", 0);
    CHECK(dst[0] == 'x');  // unchanged
  }
}

// --- StackString ---

TEST_CASE("StackString") {
  SUBCASE("basic format") {
    StackString<32> s("hello %d", 42);
    CHECK(std::string(s.c_str()) == "hello 42");
    CHECK(s.len() == 8);
    CHECK(s.string_view() == "hello 42");
    CHECK(s.ToStr() == "hello 42");
  }
  SUBCASE("truncation") {
    StackString<8> s("long string %d", 123456);
    CHECK(s.len() == 7);  // truncated to 7 chars + null
    CHECK(s.c_str()[7] == '\0');
  }
}

// --- StartsWith / EndsWith ---

TEST_CASE("StartsWith") {
  using std::string;
  CHECK(StartsWith(string("hello world"), string("hello")));
  CHECK(StartsWith(string("hello"), string("hello")));
  CHECK(StartsWith(string("hello"), string("")));
  CHECK_FALSE(StartsWith(string("hello"), string("hello world longer")));
  CHECK_FALSE(StartsWith(string("hello"), string("world")));
  CHECK(StartsWith(string(""), string("")));
}

TEST_CASE("EndsWith") {
  using std::string;
  CHECK(EndsWith(string("hello world"), string("world")));
  CHECK(EndsWith(string("hello"), string("hello")));
  CHECK(EndsWith(string("hello"), string("")));
  CHECK_FALSE(EndsWith(string("hello"), string("hello world longer")));
  CHECK_FALSE(EndsWith(string("hello"), string("xyz")));
  CHECK(EndsWith(string(""), string("")));
}

// --- StartsWithAny ---

TEST_CASE("StartsWithAny") {
  CHECK(StartsWithAny("http://example.com", {"http://", "https://"}));
  CHECK(StartsWithAny("https://example.com", {"http://", "https://"}));
  CHECK_FALSE(StartsWithAny("ftp://example.com", {"http://", "https://"}));
  CHECK_FALSE(StartsWithAny("hello", {}));
}

// --- Contains ---

TEST_CASE("Contains string") {
  CHECK(Contains(std::string("hello world"), std::string("world")));
  CHECK(Contains(std::string("hello"), std::string("")));
  CHECK_FALSE(Contains(std::string("hello"), std::string("xyz")));
  CHECK_FALSE(Contains(std::string(""), std::string("a")));
}

TEST_CASE("Contains char") {
  CHECK(Contains(std::string("hello"), 'e'));
  CHECK_FALSE(Contains(std::string("hello"), 'z'));
  CHECK_FALSE(Contains(std::string(""), 'a'));
}

// --- Find ---

TEST_CASE("Find") {
  CHECK(Find("world", "hello world") == 6);
  CHECK(Find("hello", "hello world") == 0);
  CHECK(Find("xyz", "hello world") == std::string::npos);
  CHECK(Find("", "hello") == 0);
  CHECK(Find("longerthanthe", "short") == std::string::npos);
}

// --- CaseInsensitiveEqual ---

TEST_CASE("CaseInsensitiveEqual") {
  CHECK(CaseInsensitiveEqual("Hello", "hello"));
  CHECK(CaseInsensitiveEqual("ABC", "abc"));
  CHECK(CaseInsensitiveEqual("", ""));
  CHECK_FALSE(CaseInsensitiveEqual("abc", "abcd"));
  CHECK_FALSE(CaseInsensitiveEqual("abc", "xyz"));
}

// --- Join ---

TEST_CASE("Join") {
  CHECK(Join({"a", "b", "c"}, ",") == "a,b,c");
  CHECK(Join({"single"}, ",") == "single");
  CHECK(Join({}, ",") == "");
  CHECK(Join({"a", "b"}, "::") == "a::b");
}

// --- SplitString ---

TEST_CASE("SplitString") {
  SUBCASE("normal split") {
    auto parts = SplitString("a,b,c", ",");
    CHECK(parts.size() == 3);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
    CHECK(parts[2] == "c");
  }
  SUBCASE("no delimiter in string") {
    auto parts = SplitString("hello", ",");
    CHECK(parts.size() == 1);
    CHECK(parts[0] == "hello");
  }
  SUBCASE("trailing delimiter") {
    auto parts = SplitString("a,b,", ",");
    CHECK(parts.size() == 2);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
  }
  SUBCASE("multi-char delimiter") {
    auto parts = SplitString("a::b::c", "::");
    CHECK(parts.size() == 3);
    CHECK(parts[0] == "a");
    CHECK(parts[1] == "b");
    CHECK(parts[2] == "c");
  }
  // Skip empty delimiter test: CHECK(!delimiter.empty()) calls abort()
}

// --- TrimWhitespace ---

TEST_CASE("TrimWhitespace") {
  CHECK(TrimWhitespace("  hello  ") == "hello");
  CHECK(TrimWhitespace("\t\nhello\t\n") == "hello");
  CHECK(TrimWhitespace("   ") == "");
  CHECK(TrimWhitespace("hello") == "hello");
  CHECK(TrimWhitespace("") == "");
}

// --- StripPrefix / StripSuffix ---

TEST_CASE("StripPrefix") {
  CHECK(StripPrefix("hello world", "hello ") == "world");
  CHECK(StripPrefix("hello", "xyz") == "hello");
  CHECK(StripPrefix("hello", "") == "hello");
  CHECK(StripPrefix("", "") == "");
}

TEST_CASE("StripSuffix") {
  CHECK(StripSuffix("hello world", " world") == "hello");
  CHECK(StripSuffix("hello", "xyz") == "hello");
  CHECK(StripSuffix("hello", "") == "hello");
  CHECK(StripSuffix("", "") == "");
}

// --- ToLower / ToUpper ---

TEST_CASE("ToLower") {
  CHECK(ToLower("HELLO") == "hello");
  CHECK(ToLower("Hello World") == "hello world");
  CHECK(ToLower("hello") == "hello");
  CHECK(ToLower("") == "");
  CHECK(ToLower("123!@#") == "123!@#");
}

TEST_CASE("ToUpper") {
  CHECK(ToUpper("hello") == "HELLO");
  CHECK(ToUpper("Hello World") == "HELLO WORLD");
  CHECK(ToUpper("HELLO") == "HELLO");
  CHECK(ToUpper("") == "");
  CHECK(ToUpper("123!@#") == "123!@#");
}

// --- StripChars ---

TEST_CASE("StripChars") {
  CHECK(StripChars("hello world", " ", '_') == "hello_world");
  CHECK(StripChars("a.b.c", ".", '/') == "a/b/c");
  CHECK(StripChars("hello", "xyz", '_') == "hello");
  CHECK(StripChars("hello", "", '_') == "hello");
}

// --- ToHex ---

TEST_CASE("ToHex") {
  CHECK(ToHex("", 0) == "");
  CHECK(ToHex("\x00", 1) == "00");
  CHECK(ToHex("\xff", 1) == "ff");
  CHECK(ToHex("\xde\xad\xbe\xef", 4) == "deadbeef");
  CHECK(ToHex(std::string("AB")) == "4142");
}

// --- IntToHexString / Uint64ToHexString ---

TEST_CASE("IntToHexString") {
  CHECK(IntToHexString(0) == "0x00");
  CHECK(IntToHexString(255) == "0xff");
  CHECK(IntToHexString(0xFFFFFFFF) == "0xffffffff");
}

TEST_CASE("Uint64ToHexString") {
  CHECK(Uint64ToHexString(0) == "0x0");
  CHECK(Uint64ToHexString(0xFF) == "0xff");
}

TEST_CASE("Uint64ToHexStringNoPrefix") {
  CHECK(Uint64ToHexStringNoPrefix(0) == "0");
  CHECK(Uint64ToHexStringNoPrefix(0xFF) == "ff");
  CHECK(Uint64ToHexStringNoPrefix(0xFFFFFFFFFFFFFFFFULL) ==
        "ffffffffffffffff");
}

// --- ReplaceAll ---

TEST_CASE("ReplaceAll") {
  SUBCASE("single replacement") {
    std::string s = "hello world";
    CHECK(ReplaceAll(s, "world", "there") == "hello there");
  }
  SUBCASE("multiple replacements") {
    std::string s = "aaa";
    CHECK(ReplaceAll(s, "a", "bb") == "bbbbbb");
  }
  SUBCASE("no match") {
    std::string s = "hello";
    CHECK(ReplaceAll(s, "xyz", "abc") == "hello");
  }
  // Skip empty to_replace test: CHECK(!to_replace.empty()) calls abort()
}

// --- CheckAsciiAndRemoveInvalidUTF8 ---

TEST_CASE("CheckAsciiAndRemoveInvalidUTF8") {
  SUBCASE("pure ASCII") {
    std::string output;
    CHECK(CheckAsciiAndRemoveInvalidUTF8("hello 123", output));
    // output not modified for pure ASCII
  }
  SUBCASE("valid 2-byte UTF-8") {
    // U+00E9 = é = C3 A9
    std::string input = "\xc3\xa9";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    CHECK(output == input);
  }
  SUBCASE("valid 3-byte UTF-8") {
    // U+4E16 = 世 = E4 B8 96
    std::string input = "\xe4\xb8\x96";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    CHECK(output == input);
  }
  SUBCASE("valid 4-byte UTF-8") {
    // U+1F600 = 😀 = F0 9F 98 80
    std::string input = "\xf0\x9f\x98\x80";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    CHECK(output == input);
  }
  SUBCASE("invalid byte removed") {
    // 0xFF is invalid
    std::string input = "a\xff" "b";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    CHECK(output == "ab");
  }
  SUBCASE("overlong 2-byte encoding removed") {
    // C0 80 is overlong encoding of U+0000
    std::string input = "a\xc0\x80" "b";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    // Overlong bytes should be removed; continuation byte 0x80 is also invalid
    // standalone, so both are stripped
    CHECK(output.find('\xc0') == std::string::npos);
  }
  SUBCASE("surrogate removed") {
    // ED A0 80 = U+D800 (surrogate)
    std::string input = "a\xed\xa0\x80" "b";
    std::string output;
    CHECK_FALSE(CheckAsciiAndRemoveInvalidUTF8(input, output));
    CHECK(output.find('\xed') == std::string::npos);
  }
}

// --- SprintfTrunc ---

TEST_CASE("SprintfTrunc") {
  SUBCASE("normal fit") {
    char buf[32];
    size_t n = SprintfTrunc(buf, sizeof(buf), "hello %d", 42);
    CHECK(n == 8);
    CHECK(std::string(buf) == "hello 42");
  }
  SUBCASE("truncation") {
    char buf[6];
    size_t n = SprintfTrunc(buf, sizeof(buf), "hello world");
    CHECK(n == 5);  // dst_size - 1
    CHECK(buf[5] == '\0');
  }
  SUBCASE("zero dst_size") {
    char buf[4] = "abc";
    size_t n = SprintfTrunc(buf, 0, "hello");
    CHECK(n == 0);
  }
}

// --- FindLineWithOffset ---

TEST_CASE("FindLineWithOffset") {
  SUBCASE("first line") {
    auto result = FindLineWithOffset("hello\nworld", 0);
    CHECK(result.has_value());
    CHECK(result->line == "hello");
    CHECK(result->line_num == 1);
    CHECK(result->line_offset == 0);
  }
  SUBCASE("second line") {
    auto result = FindLineWithOffset("hello\nworld", 6);
    CHECK(result.has_value());
    CHECK(result->line == "world");
    CHECK(result->line_num == 2);
    CHECK(result->line_offset == 0);
  }
  SUBCASE("middle of line") {
    auto result = FindLineWithOffset("hello\nworld", 8);
    CHECK(result.has_value());
    CHECK(result->line == "world");
    CHECK(result->line_num == 2);
    CHECK(result->line_offset == 2);
  }
  SUBCASE("offset on newline returns nullopt") {
    auto result = FindLineWithOffset("hello\nworld", 5);
    CHECK_FALSE(result.has_value());
  }
  SUBCASE("offset beyond string returns nullopt") {
    auto result = FindLineWithOffset("hello", 100);
    CHECK_FALSE(result.has_value());
  }
  SUBCASE("single line no newline") {
    auto result = FindLineWithOffset("hello", 2);
    CHECK(result.has_value());
    CHECK(result->line == "hello");
    CHECK(result->line_num == 1);
    CHECK(result->line_offset == 2);
  }
}

int main() {
  doctest::Context context;
  int result = context.run();
  return result;
}
