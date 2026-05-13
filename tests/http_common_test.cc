#include "xtils/net/http_common.h"

#include <map>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

// --- HttpUrl parsing ---

TEST_CASE("HttpUrl: full URL parsing") {
  HttpUrl url("http://example.com:8080/path?q=1#frag");
  CHECK(url.scheme == "http");
  CHECK(url.host == "example.com");
  CHECK(url.port == 8080);
  CHECK(url.path == "/path");
  CHECK(url.query == "q=1");
  CHECK(url.fragment == "frag");
  CHECK(url.IsValid());
  CHECK_FALSE(url.IsHttps());
}

TEST_CASE("HttpUrl: https default port") {
  HttpUrl url("https://example.com/secure");
  CHECK(url.scheme == "https");
  CHECK(url.host == "example.com");
  CHECK(url.port == 443);  // default for https
  CHECK(url.path == "/secure");
  CHECK(url.IsHttps());
  CHECK(url.IsValid());
}

TEST_CASE("HttpUrl: http default port") {
  HttpUrl url("http://example.com");
  CHECK(url.scheme == "http");
  CHECK(url.host == "example.com");
  CHECK(url.port == 80);  // default for http
  CHECK(url.path == "/");
  CHECK_FALSE(url.IsHttps());
}

TEST_CASE("HttpUrl: path only, no query or fragment") {
  HttpUrl url("http://localhost:3000/api/v1/data");
  CHECK(url.host == "localhost");
  CHECK(url.port == 3000);
  CHECK(url.path == "/api/v1/data");
  CHECK(url.query.empty());
  CHECK(url.fragment.empty());
}

TEST_CASE("HttpUrl: invalid URL") {
  HttpUrl url("not-a-url");
  CHECK_FALSE(url.IsValid());
}

TEST_CASE("HttpUrl: ToString round trip") {
  HttpUrl url("http://example.com:8080/path?q=1#frag");
  std::string str = url.ToString();
  CHECK(str.find("example.com") != std::string::npos);
  CHECK(str.find("8080") != std::string::npos);
  CHECK(str.find("/path") != std::string::npos);
}

TEST_CASE("HttpUrl: wss is HTTPS") {
  HttpUrl url("wss://ws.example.com/socket");
  CHECK(url.IsHttps());
}

// --- HttpMethodToString / StringToHttpMethod ---

TEST_CASE("HttpUtils: method round-trip") {
  auto check = [](HttpMethod m, const std::string& expected_str) {
    CHECK(HttpUtils::HttpMethodToString(m) == expected_str);
    CHECK(HttpUtils::StringToHttpMethod(expected_str) == m);
  };
  check(HttpMethod::kGet, "GET");
  check(HttpMethod::kPost, "POST");
  check(HttpMethod::kPut, "PUT");
  check(HttpMethod::kDelete, "DELETE");
  check(HttpMethod::kHead, "HEAD");
  check(HttpMethod::kOptions, "OPTIONS");
  check(HttpMethod::kPatch, "PATCH");
  check(HttpMethod::kTrace, "TRACE");
  check(HttpMethod::kConnect, "CONNECT");
}

TEST_CASE("HttpUtils: StringToHttpMethod case-insensitive") {
  CHECK(HttpUtils::StringToHttpMethod("get") == HttpMethod::kGet);
  CHECK(HttpUtils::StringToHttpMethod("Post") == HttpMethod::kPost);
}

// --- UrlEncode / UrlDecode ---

TEST_CASE("HttpUtils: UrlEncode basic") {
  CHECK(HttpUtils::UrlEncode("hello world") == "hello%20world");
  CHECK(HttpUtils::UrlEncode("a+b") == "a%2Bb");
  CHECK(HttpUtils::UrlEncode("foo@bar.com") == "foo%40bar.com");
}

TEST_CASE("HttpUtils: UrlEncode preserves safe chars") {
  CHECK(HttpUtils::UrlEncode("abc-123_test.file~") == "abc-123_test.file~");
}

TEST_CASE("HttpUtils: UrlDecode basic") {
  CHECK(HttpUtils::UrlDecode("hello%20world") == "hello world");
  CHECK(HttpUtils::UrlDecode("a%2Bb") == "a+b");
}

TEST_CASE("HttpUtils: UrlDecode plus as space") {
  CHECK(HttpUtils::UrlDecode("hello+world") == "hello world");
}

TEST_CASE("HttpUtils: UrlEncode/Decode round trip") {
  std::string original = "key=value&foo=bar baz";
  std::string encoded = HttpUtils::UrlEncode(original);
  std::string decoded = HttpUtils::UrlDecode(encoded);
  CHECK(decoded == original);
}

// --- FormDataEncode / ParseFormData ---

TEST_CASE("HttpUtils: FormDataEncode") {
  std::map<std::string, std::string> data = {{"a", "1"}, {"b", "2"}};
  std::string encoded = HttpUtils::FormDataEncode(data);
  CHECK(encoded == "a=1&b=2");
}

TEST_CASE("HttpUtils: ParseFormData") {
  auto parsed = HttpUtils::ParseFormData("a=1&b=2");
  CHECK(parsed.size() == 2);
  CHECK(parsed["a"] == "1");
  CHECK(parsed["b"] == "2");
}

TEST_CASE("HttpUtils: FormData round trip with encoding") {
  std::map<std::string, std::string> data = {{"key", "hello world"},
                                              {"name", "foo&bar"}};
  std::string encoded = HttpUtils::FormDataEncode(data);
  auto parsed = HttpUtils::ParseFormData(encoded);
  CHECK(parsed["key"] == "hello world");
  CHECK(parsed["name"] == "foo&bar");
}

// --- GetMimeType ---

TEST_CASE("HttpUtils: GetMimeType common types") {
  CHECK(HttpUtils::GetMimeType(".html") == "text/html");
  CHECK(HttpUtils::GetMimeType(".json") == "application/json");
  CHECK(HttpUtils::GetMimeType(".jpg") == "image/jpeg");
  CHECK(HttpUtils::GetMimeType(".png") == "image/png");
  CHECK(HttpUtils::GetMimeType(".css") == "text/css");
  CHECK(HttpUtils::GetMimeType(".js") == "application/javascript");
  CHECK(HttpUtils::GetMimeType(".txt") == "text/plain");
  CHECK(HttpUtils::GetMimeType(".pdf") == "application/pdf");
}

TEST_CASE("HttpUtils: GetMimeType unknown extension") {
  CHECK(HttpUtils::GetMimeType(".xyz") == "application/octet-stream");
}

// --- GetStatusMessage ---

TEST_CASE("HttpUtils: GetStatusMessage") {
  CHECK(HttpUtils::GetStatusMessage(200) == "OK");
  CHECK(HttpUtils::GetStatusMessage(201) == "Created");
  CHECK(HttpUtils::GetStatusMessage(204) == "No Content");
  CHECK(HttpUtils::GetStatusMessage(301) == "Moved Permanently");
  CHECK(HttpUtils::GetStatusMessage(302) == "Found");
  CHECK(HttpUtils::GetStatusMessage(304) == "Not Modified");
  CHECK(HttpUtils::GetStatusMessage(400) == "Bad Request");
  CHECK(HttpUtils::GetStatusMessage(401) == "Unauthorized");
  CHECK(HttpUtils::GetStatusMessage(403) == "Forbidden");
  CHECK(HttpUtils::GetStatusMessage(404) == "Not Found");
  CHECK(HttpUtils::GetStatusMessage(405) == "Method Not Allowed");
  CHECK(HttpUtils::GetStatusMessage(500) == "Internal Server Error");
  CHECK(HttpUtils::GetStatusMessage(999) == "Unknown");
}

// --- IsSuccessStatus / IsRedirectStatus / IsErrorStatus ---

TEST_CASE("HttpUtils: status code classification") {
  // Success range: 200-299
  CHECK(HttpUtils::IsSuccessStatus(200));
  CHECK(HttpUtils::IsSuccessStatus(201));
  CHECK(HttpUtils::IsSuccessStatus(299));
  CHECK_FALSE(HttpUtils::IsSuccessStatus(199));
  CHECK_FALSE(HttpUtils::IsSuccessStatus(300));

  // Redirect range: 300-399
  CHECK(HttpUtils::IsRedirectStatus(301));
  CHECK(HttpUtils::IsRedirectStatus(302));
  CHECK(HttpUtils::IsRedirectStatus(399));
  CHECK_FALSE(HttpUtils::IsRedirectStatus(299));
  CHECK_FALSE(HttpUtils::IsRedirectStatus(400));

  // Error range: 400+
  CHECK(HttpUtils::IsErrorStatus(400));
  CHECK(HttpUtils::IsErrorStatus(404));
  CHECK(HttpUtils::IsErrorStatus(500));
  CHECK_FALSE(HttpUtils::IsErrorStatus(200));
  CHECK_FALSE(HttpUtils::IsErrorStatus(301));
}

// --- EscapeHtml ---

TEST_CASE("HttpUtils: EscapeHtml") {
  CHECK(HttpUtils::EscapeHtml("<script>") == "&lt;script&gt;");
  CHECK(HttpUtils::EscapeHtml("a & b") == "a &amp; b");
  CHECK(HttpUtils::EscapeHtml("\"hello\"") == "&quot;hello&quot;");
  CHECK(HttpUtils::EscapeHtml("it's") == "it&#39;s");
  CHECK(HttpUtils::EscapeHtml("plain text") == "plain text");
}

// --- GetFileExtension / GetBasename ---

TEST_CASE("HttpUtils: GetFileExtension") {
  CHECK(HttpUtils::GetFileExtension("/path/to/file.txt") == ".txt");
  CHECK(HttpUtils::GetFileExtension("image.jpg") == ".jpg");
  CHECK(HttpUtils::GetFileExtension("archive.tar.gz") == ".gz");
}

TEST_CASE("HttpUtils: GetBasename") {
  CHECK(HttpUtils::GetBasename("/path/to/file.txt") == "file.txt");
  CHECK(HttpUtils::GetBasename("file.txt") == "file.txt");
}

// --- IsValidHttpMethod ---

TEST_CASE("HttpUtils: IsValidHttpMethod") {
  CHECK(HttpUtils::IsValidHttpMethod("GET"));
  CHECK(HttpUtils::IsValidHttpMethod("POST"));
  CHECK(HttpUtils::IsValidHttpMethod("PUT"));
  CHECK(HttpUtils::IsValidHttpMethod("DELETE"));
  CHECK(HttpUtils::IsValidHttpMethod("OPTIONS"));
  CHECK_FALSE(HttpUtils::IsValidHttpMethod("INVALID"));
  CHECK_FALSE(HttpUtils::IsValidHttpMethod("get"));  // case-sensitive check
}

// --- Header utilities ---

TEST_CASE("HttpUtils: header utilities") {
  HttpHeaders headers;
  HttpUtils::AddHeader(headers, "Content-Type", "text/html");
  HttpUtils::AddHeader(headers, "X-Custom", "value1");

  CHECK(HttpUtils::HasHeader(headers, "Content-Type"));
  CHECK(HttpUtils::GetHeaderValue(headers, "Content-Type") == "text/html");
  CHECK(HttpUtils::GetHeaderValue(headers, "X-Custom") == "value1");
  CHECK_FALSE(HttpUtils::HasHeader(headers, "NonExistent"));
  CHECK(HttpUtils::GetHeaderValue(headers, "NonExistent") == "");

  // Update existing header
  HttpUtils::AddHeader(headers, "Content-Type", "application/json");
  CHECK(HttpUtils::GetHeaderValue(headers, "Content-Type") ==
        "application/json");
  CHECK(headers.size() == 2);  // Should update, not add duplicate
}

int main() {
  doctest::Context context;
  return context.run();
}
