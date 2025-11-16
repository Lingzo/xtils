#include "xtils/net/http_common.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#include "xtils/utils/string_utils.h"

namespace xtils {

// HttpUrl implementation

HttpUrl::HttpUrl(const std::string& url) {
  size_t pos = 0;

  // Parse scheme
  size_t scheme_end = url.find("://", pos);
  if (scheme_end == std::string::npos) {
    return;  // Invalid URL
  }
  scheme = url.substr(pos, scheme_end);
  pos = scheme_end + 3;

  // Parse host and port
  size_t path_start = url.find('/', pos);
  size_t query_start = url.find('?', pos);
  size_t fragment_start = url.find('#', pos);

  size_t host_end = std::min({path_start, query_start, fragment_start});
  if (host_end == std::string::npos) {
    host_end = url.length();
  }

  std::string host_port = url.substr(pos, host_end - pos);
  size_t colon_pos = host_port.rfind(':');

  if (colon_pos != std::string::npos && colon_pos < host_port.length() - 1) {
    host = host_port.substr(0, colon_pos);
    port = static_cast<uint16_t>(std::stoul(host_port.substr(colon_pos + 1)));
  } else {
    host = host_port;
    port = GetDefaultPort();
  }

  pos = host_end;

  // Parse path
  if (pos < url.length() && url[pos] == '/') {
    size_t path_end = std::min(query_start, fragment_start);
    if (path_end == std::string::npos) {
      path_end = url.length();
    }
    path = url.substr(pos, path_end - pos);
    pos = path_end;
  } else {
    path = "/";
  }

  // Parse query
  if (pos < url.length() && url[pos] == '?') {
    pos++;
    size_t query_end = fragment_start;
    if (query_end == std::string::npos) {
      query_end = url.length();
    }
    query = url.substr(pos, query_end - pos);
    pos = query_end;
  }

  // Parse fragment
  if (pos < url.length() && url[pos] == '#') {
    pos++;
    fragment = url.substr(pos);
  }
}

std::string HttpUrl::ToString() const {
  std::stringstream ss;
  ss << scheme << "://" << host;

  if (port != GetDefaultPort()) {
    ss << ":" << port;
  }

  ss << path;

  if (!query.empty()) {
    ss << "?" << query;
  }

  if (!fragment.empty()) {
    ss << "#" << fragment;
  }

  return ss.str();
}

uint16_t HttpUrl::GetDefaultPort() const {
  if (scheme == "https") {
    return 443;
  } else if (scheme == "http") {
    return 80;
  }
  return 80;  // Default to HTTP
}

// HttpUtils implementation

namespace HttpUtils {

std::string HttpMethodToString(HttpMethod method) {
  switch (method) {
    case HttpMethod::kGet:
      return "GET";
    case HttpMethod::kPost:
      return "POST";
    case HttpMethod::kPut:
      return "PUT";
    case HttpMethod::kDelete:
      return "DELETE";
    case HttpMethod::kHead:
      return "HEAD";
    case HttpMethod::kOptions:
      return "OPTIONS";
    case HttpMethod::kPatch:
      return "PATCH";
    case HttpMethod::kTrace:
      return "TRACE";
    case HttpMethod::kConnect:
      return "CONNECT";
    case HttpMethod::kAny:
      return "ANY";
    default:
      return "GET";
  }
}

HttpMethod StringToHttpMethod(const std::string& method) {
  std::string upper_method = method;
  std::transform(upper_method.begin(), upper_method.end(), upper_method.begin(),
                 ::toupper);

  if (upper_method == "GET") return HttpMethod::kGet;
  if (upper_method == "POST") return HttpMethod::kPost;
  if (upper_method == "PUT") return HttpMethod::kPut;
  if (upper_method == "DELETE") return HttpMethod::kDelete;
  if (upper_method == "HEAD") return HttpMethod::kHead;
  if (upper_method == "OPTIONS") return HttpMethod::kOptions;
  if (upper_method == "PATCH") return HttpMethod::kPatch;
  if (upper_method == "TRACE") return HttpMethod::kTrace;
  if (upper_method == "CONNECT") return HttpMethod::kConnect;
  if (upper_method == "ANY") return HttpMethod::kAny;

  return HttpMethod::kGet;
}

std::string UrlEncode(const std::string& str) {
  std::ostringstream encoded;
  encoded.fill('0');
  encoded << std::hex;

  for (char c : str) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded << c;
    } else {
      encoded << std::uppercase;
      encoded << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
      encoded << std::nouppercase;
    }
  }

  return encoded.str();
}

std::string UrlDecode(const std::string& str) {
  std::stringstream ss;
  for (size_t i = 0; i < str.length(); ++i) {
    if (str[i] == '%' && i + 2 < str.length()) {
      std::string hex = str.substr(i + 1, 2);
      char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
      ss << decoded;
      i += 2;
    } else if (str[i] == '+') {
      ss << ' ';
    } else {
      ss << str[i];
    }
  }
  return ss.str();
}

std::string FormDataEncode(const std::map<std::string, std::string>& data) {
  std::stringstream ss;
  bool first = true;

  for (const auto& pair : data) {
    if (!first) {
      ss << "&";
    }
    ss << UrlEncode(pair.first) << "=" << UrlEncode(pair.second);
    first = false;
  }

  return ss.str();
}

std::map<std::string, std::string> ParseFormData(const std::string& form_data) {
  std::map<std::string, std::string> result;
  std::stringstream ss(form_data);
  std::string pair;

  while (std::getline(ss, pair, '&')) {
    size_t eq_pos = pair.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = UrlDecode(pair.substr(0, eq_pos));
      std::string value = UrlDecode(pair.substr(eq_pos + 1));
      result[key] = value;
    }
  }

  return result;
}

std::string EscapeHtml(const std::string& text) {
  std::string escaped;
  for (char c : text) {
    switch (c) {
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '&':
        escaped += "&amp;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

std::string GetFileExtension(const std::string& filename) {
  size_t dot_pos = filename.find_last_of('.');
  if (dot_pos != std::string::npos) {
    return filename.substr(dot_pos);
  }
  return "";
}

std::string GetBasename(const std::string& path) {
  size_t slash_pos = path.find_last_of("/\\");
  if (slash_pos != std::string::npos) {
    return path.substr(slash_pos + 1);
  }
  return path;
}

bool FileExists(const std::string& path) {
  return std::filesystem::exists(path);
}

size_t GetFileSize(const std::string& path) {
  try {
    return std::filesystem::file_size(path);
  } catch (const std::filesystem::filesystem_error&) {
    return 0;
  }
}

std::string ReadFileContent(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return "";
  }

  file.seekg(0, std::ios::end);
  size_t size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string content(size, '\0');
  file.read(&content[0], size);

  return content;
}

bool IsValidHttpMethod(const std::string& method) {
  static const std::set<std::string> valid_methods = {
      "GET",     "POST", "PUT",   "DELETE", "PATCH",
      "OPTIONS", "HEAD", "TRACE", "CONNECT"};
  return valid_methods.find(method) != valid_methods.end();
}

std::string GetStatusMessage(int status_code) {
  static const std::map<int, std::string> status_messages = {
      {200, "OK"},
      {201, "Created"},
      {204, "No Content"},
      {301, "Moved Permanently"},
      {302, "Found"},
      {304, "Not Modified"},
      {400, "Bad Request"},
      {401, "Unauthorized"},
      {403, "Forbidden"},
      {404, "Not Found"},
      {405, "Method Not Allowed"},
      {500, "Internal Server Error"},
      {501, "Not Implemented"},
      {502, "Bad Gateway"},
      {503, "Service Unavailable"}};

  auto it = status_messages.find(status_code);
  if (it != status_messages.end()) {
    return it->second;
  }
  return "Unknown";
}

bool IsSuccessStatus(int status_code) {
  return status_code >= 200 && status_code < 300;
}

bool IsRedirectStatus(int status_code) {
  return status_code >= 300 && status_code < 400;
}

bool IsErrorStatus(int status_code) { return status_code >= 400; }

std::string GetHeaderValue(const HttpHeaders& headers,
                           const std::string& name) {
  for (const auto& header : headers) {
    if (CaseInsensitiveEqual(header.name, name)) {
      return header.value;
    }
  }
  return "";
}

bool HasHeader(const HttpHeaders& headers, const std::string& name) {
  return !GetHeaderValue(headers, name).empty();
}

void AddHeader(HttpHeaders& headers, const std::string& name,
               const std::string& value) {
  for (auto& header : headers) {
    if (CaseInsensitiveEqual(header.name, name)) {
      header.value = value;
      return;
    }
  }
  headers.emplace_back(name, value);
}

std::string GetMimeType(const std::string& file_extension) {
  static const std::map<std::string, std::string> mime_types = {
      {".html", "text/html"},
      {".htm", "text/html"},
      {".css", "text/css"},
      {".js", "application/javascript"},
      {".json", "application/json"},
      {".txt", "text/plain"},
      {".xml", "application/xml"},
      {".png", "image/png"},
      {".jpg", "image/jpeg"},
      {".jpeg", "image/jpeg"},
      {".gif", "image/gif"},
      {".svg", "image/svg+xml"},
      {".ico", "image/x-icon"},
      {".bmp", "image/bmp"},
      {".webp", "image/webp"},
      {".pdf", "application/pdf"},
      {".zip", "application/zip"},
      {".gz", "application/gzip"},
      {".tar", "application/x-tar"},
      {".mp3", "audio/mpeg"},
      {".mp4", "video/mp4"},
      {".avi", "video/x-msvideo"},
      {".mov", "video/quicktime"},
      {".doc", "application/msword"},
      {".docx",
       "application/"
       "vnd.openxmlformats-officedocument.wordprocessingml.document"},
      {".xls", "application/vnd.ms-excel"},
      {".xlsx",
       "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
      {".ppt", "application/vnd.ms-powerpoint"},
      {".pptx",
       "application/"
       "vnd.openxmlformats-officedocument.presentationml.presentation"}};

  auto it = mime_types.find(file_extension);
  if (it != mime_types.end()) {
    return it->second;
  }
  return "application/octet-stream";
}

}  // namespace HttpUtils

}  // namespace xtils
