#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "xtils/utils/string_view.h"

namespace xtils {

// HTTP method enumeration
enum class HttpMethod {
  kGet = 0,
  kPost,
  kPut,
  kDelete,
  kHead,
  kOptions,
  kPatch,
  kTrace,
  kConnect,
  kAny  // Matches any method (for routing)
};

// HTTP header structure
struct HttpHeader {
  std::string name;
  std::string value;

  HttpHeader() = default;
  HttpHeader(const std::string& n, const std::string& v) : name(n), value(v) {}
};

using HttpHeaders = std::vector<HttpHeader>;

// HTTP status codes
namespace HttpStatus {
constexpr int OK = 200;
constexpr int CREATED = 201;
constexpr int NO_CONTENT = 204;
constexpr int MOVED_PERMANENTLY = 301;
constexpr int FOUND = 302;
constexpr int NOT_MODIFIED = 304;
constexpr int BAD_REQUEST = 400;
constexpr int UNAUTHORIZED = 401;
constexpr int FORBIDDEN = 403;
constexpr int NOT_FOUND = 404;
constexpr int METHOD_NOT_ALLOWED = 405;
constexpr int INTERNAL_SERVER_ERROR = 500;
constexpr int NOT_IMPLEMENTED = 501;
constexpr int BAD_GATEWAY = 502;
constexpr int SERVICE_UNAVAILABLE = 503;
}  // namespace HttpStatus

// HTTP URL parser
struct HttpUrl {
  std::string scheme;  // http or https
  std::string host;
  uint16_t port = 0;  // 0 means use default port
  std::string path;
  std::string query;
  std::string fragment;

  HttpUrl() = default;
  explicit HttpUrl(const std::string& url);

  std::string ToString() const;
  uint16_t GetDefaultPort() const;
  HttpUrl base() const {
    HttpUrl base_url = *this;
    base_url.path = "/";
    base_url.query.clear();
    base_url.fragment.clear();
    return base_url;
  }
  bool IsHttps() const { return scheme == "https"; }
  bool IsValid() const { return !host.empty() && !scheme.empty(); }
  bool isSameHost(const HttpUrl& other) const {
    return host == other.host && port == other.port;
  }
};

// Common HTTP utility functions
namespace HttpUtils {

// Method conversion
std::string HttpMethodToString(HttpMethod method);
HttpMethod StringToHttpMethod(const std::string& method);

// URL encoding/decoding
std::string UrlEncode(const std::string& str);
std::string UrlDecode(const std::string& str);

// Form data handling
std::string FormDataEncode(const std::map<std::string, std::string>& data);
std::map<std::string, std::string> ParseFormData(const std::string& form_data);

// HTML escaping
std::string EscapeHtml(const std::string& text);

// File utilities
std::string GetFileExtension(const std::string& filename);
std::string GetBasename(const std::string& path);
bool FileExists(const std::string& path);
size_t GetFileSize(const std::string& path);
std::string ReadFileContent(const std::string& path);

// HTTP validation
bool IsValidHttpMethod(const std::string& method);

// Status code helpers
std::string GetStatusMessage(int status_code);
bool IsSuccessStatus(int status_code);
bool IsRedirectStatus(int status_code);
bool IsErrorStatus(int status_code);

// Header utilities
std::string GetHeaderValue(const HttpHeaders& headers, const std::string& name);
bool HasHeader(const HttpHeaders& headers, const std::string& name);
void AddHeader(HttpHeaders& headers, const std::string& name,
               const std::string& value);

// MIME type detection
std::string GetMimeType(const std::string& file_extension);

}  // namespace HttpUtils

}  // namespace xtils
