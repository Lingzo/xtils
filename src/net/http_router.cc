#include "xtils/net/http_router.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>

#include "xtils/net/http_common.h"

namespace xtils {

// RouteParams implementation

void RouteParams::Add(const std::string& name, const std::string& value) {
  params_[name] = value;
}

std::string RouteParams::Get(const std::string& name) const {
  auto it = params_.find(name);
  return (it != params_.end()) ? it->second : "";
}

bool RouteParams::Has(const std::string& name) const {
  return params_.find(name) != params_.end();
}

// QueryParams implementation

QueryParams::QueryParams(StringView query_string) { Parse(query_string); }

void QueryParams::Parse(StringView query_string) {
  if (query_string.empty()) return;

  std::string query_str(query_string.data(), query_string.size());
  std::stringstream ss(query_str);
  std::string pair;

  while (std::getline(ss, pair, '&')) {
    size_t eq_pos = pair.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = HttpUtils::UrlDecode(pair.substr(0, eq_pos));
      std::string value = HttpUtils::UrlDecode(pair.substr(eq_pos + 1));
      params_.emplace(key, value);
    } else {
      std::string key = HttpUtils::UrlDecode(pair);
      params_.emplace(key, "");
    }
  }
}

std::string QueryParams::Get(const std::string& name) const {
  auto it = params_.find(name);
  return (it != params_.end()) ? it->second : "";
}

std::string QueryParams::Get(const std::string& name,
                             const std::string& default_val) const {
  auto it = params_.find(name);
  return (it != params_.end()) ? it->second : default_val;
}

bool QueryParams::Has(const std::string& name) const {
  return params_.find(name) != params_.end();
}

std::vector<std::string> QueryParams::GetAll(const std::string& name) const {
  std::vector<std::string> values;
  auto range = params_.equal_range(name);
  for (auto it = range.first; it != range.second; ++it) {
    values.push_back(it->second);
  }
  return values;
}

// HttpRequestContext implementation

HttpRequestContext::HttpRequestContext(const HttpRequest& req) : request(&req) {
  // Parse query string from URI
  std::string uri_str(req.uri.data(), req.uri.size());
  size_t query_pos = uri_str.find('?');
  if (query_pos != std::string::npos) {
    StringView query_view(uri_str.data() + query_pos + 1,
                          uri_str.size() - query_pos - 1);
    query = QueryParams(query_view);
  }

  // Build headers map for easy access
  for (const auto& header : req.headers) {
    if (header.name.empty()) break;  // End of valid headers
    std::string name(header.name.data(), header.name.size());
    std::string value(header.value.data(), header.value.size());
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    headers_map[name] = value;
  }
}

std::string HttpRequestContext::GetHeader(const std::string& name) const {
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 ::tolower);
  auto it = headers_map.find(lower_name);
  return (it != headers_map.end()) ? it->second : "";
}

std::string HttpRequestContext::GetBody() const {
  return std::string(request->body.data(), request->body.size());
}

bool HttpRequestContext::IsJson() const {
  std::string content_type = GetHeader("content-type");
  return content_type.find("application/json") != std::string::npos;
}

bool HttpRequestContext::IsForm() const {
  std::string content_type = GetHeader("content-type");
  return content_type.find("application/x-www-form-urlencoded") !=
         std::string::npos;
}

bool HttpRequestContext::IsMultipart() const {
  std::string content_type = GetHeader("content-type");
  return content_type.find("multipart/form-data") != std::string::npos;
}

std::string HttpRequestContext::GetClientIP() const {
  // Simple implementation - in production you'd check X-Forwarded-For, etc.
  return "127.0.0.1";
}

// HttpResponse implementation

HttpResponse& HttpResponse::Status(int code) {
  std::map<int, std::string> status_codes = {
      {200, "200 OK"},
      {201, "201 Created"},
      {204, "204 No Content"},
      {301, "301 Moved Permanently"},
      {302, "302 Found"},
      {400, "400 Bad Request"},
      {401, "401 Unauthorized"},
      {403, "403 Forbidden"},
      {404, "404 Not Found"},
      {405, "405 Method Not Allowed"},
      {500, "500 Internal Server Error"}};

  auto it = status_codes.find(code);
  if (it != status_codes.end()) {
    status_ = it->second;
  } else {
    status_ = std::to_string(code) + " Unknown";
  }
  return *this;
}

HttpResponse& HttpResponse::Status(const std::string& status) {
  status_ = status;
  return *this;
}

HttpResponse& HttpResponse::Header(const std::string& name,
                                   const std::string& value) {
  headers_.emplace_back(name, value);
  return *this;
}

HttpResponse& HttpResponse::Headers(const HttpHeaders& headers) {
  headers_.insert(headers_.end(), headers.begin(), headers.end());
  return *this;
}

HttpResponse& HttpResponse::ContentType(const std::string& content_type) {
  return Header("Content-Type", content_type);
}

HttpResponse& HttpResponse::Body(const std::string& body) {
  body_ = body;
  is_file_response_ = false;
  return *this;
}

HttpResponse& HttpResponse::Json(const std::string& json) {
  return ContentType("application/json").Body(json);
}

HttpResponse& HttpResponse::Html(const std::string& html) {
  return ContentType("text/html; charset=utf-8").Body(html);
}

HttpResponse& HttpResponse::Text(const std::string& text) {
  return ContentType("text/plain; charset=utf-8").Body(text);
}

HttpResponse& HttpResponse::File(const std::string& file_path) {
  file_path_ = file_path;
  is_file_response_ = true;
  return *this;
}

HttpResponse& HttpResponse::Download(const std::string& file_path,
                                     const std::string& filename) {
  File(file_path);
  std::string download_filename =
      filename.empty() ? HttpUtils::GetBasename(file_path) : filename;
  return Header("Content-Disposition",
                "attachment; filename=\"" + download_filename + "\"");
}

HttpResponse& HttpResponse::Redirect(const std::string& url, int code) {
  Status(code);
  return Header("Location", url);
}

void HttpResponse::Send(HttpServerConnection* conn) {
  if (is_file_response_) {
    HttpResponse::SendFile(conn, file_path_);
  } else {
    // Convert HttpHeaders to server Header format
    HttpHeaders server_headers;
    for (const auto& h : headers_) {
      server_headers.emplace_back(h.name, h.value);
    }
    conn->SendResponse(status_.c_str(), server_headers, StringView(body_));
  }
}

void HttpResponse::SendJson(HttpServerConnection* conn, const std::string& json,
                            int status) {
  HttpHeaders server_headers;
  server_headers.emplace_back("Content-Type", "application/json");

  std::string status_str =
      std::to_string(status) + " " + HttpUtils::GetStatusMessage(status);
  conn->SendResponse(status_str.c_str(), server_headers, StringView(json));
}

void HttpResponse::SendError(HttpServerConnection* conn, int status,
                             const std::string& message) {
  std::string error_message = message;
  if (error_message.empty()) {
    error_message = HttpUtils::GetStatusMessage(status);
  }

  HttpHeaders server_headers;
  server_headers.emplace_back("Content-Type", "text/html; charset=utf-8");

  std::string html_body = "<html><body><h1>" + std::to_string(status) + " " +
                          error_message + "</h1></body></html>";
  std::string status_str = std::to_string(status) + " " + error_message;

  conn->SendResponse(status_str.c_str(), server_headers, StringView(html_body));
}

void HttpResponse::SendFile(HttpServerConnection* conn,
                            const std::string& file_path) {
  if (!HttpUtils::FileExists(file_path)) {
    HttpResponse::SendError(conn, 404, "File Not Found");
    return;
  }

  std::string content = HttpUtils::ReadFileContent(file_path);
  if (content.empty()) {
    HttpResponse::SendError(conn, 500, "Failed to read file");
    return;
  }

  std::string ext = HttpUtils::GetFileExtension(file_path);
  std::string mime_type = HttpUtils::GetMimeType(ext);

  HttpHeaders server_headers;
  server_headers.emplace_back("Content-Type", mime_type);

  conn->SendResponse("200 OK", server_headers, StringView(content));
}

// Route implementation

Router::Router(HttpMethod method, const std::string& pattern,
               RouteHandler handler)
    : method_(method), pattern_(pattern), handler_(handler), is_regex_(false) {
  CompilePattern();
  ExtractParamNames();
}

void Router::CompilePattern() {
  std::string regex_pattern = pattern_;

  // Check if pattern contains regex characters or parameter placeholders
  if (pattern_.find('{') != std::string::npos ||
      pattern_.find('*') != std::string::npos ||
      pattern_.find('^') != std::string::npos ||
      pattern_.find('$') != std::string::npos) {
    is_regex_ = true;

    // Convert {param} to named capture groups
    size_t pos = 0;
    while ((pos = regex_pattern.find('{', pos)) != std::string::npos) {
      size_t end_pos = regex_pattern.find('}', pos);
      if (end_pos != std::string::npos) {
        std::string param_name =
            regex_pattern.substr(pos + 1, end_pos - pos - 1);
        regex_pattern.replace(pos, end_pos - pos + 1, "([^/]+)");
        pos += 7;  // Length of "([^/]+)"
      } else {
        break;
      }
    }

    // Handle wildcards
    std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
    regex_pattern = "^" + regex_pattern + "$";
  } else {
    // Exact match
    regex_pattern = "^" + regex_pattern + "$";
    is_regex_ = false;
  }

  try {
    regex_ = std::regex(regex_pattern);
  } catch (const std::regex_error& e) {
    // Fallback to exact match if regex compilation fails
    regex_ = std::regex(
        "^" +
        std::regex_replace(
            pattern_, std::regex(R"([\.\^\$\*\+\?\(\)\[\]\{\}\\])"), R"(\$&)") +
        "$");
    is_regex_ = false;
  }
}

void Router::ExtractParamNames() {
  size_t pos = 0;
  while ((pos = pattern_.find('{', pos)) != std::string::npos) {
    size_t end_pos = pattern_.find('}', pos);
    if (end_pos != std::string::npos) {
      std::string param_name = pattern_.substr(pos + 1, end_pos - pos - 1);
      param_names_.push_back(param_name);
      pos = end_pos + 1;
    } else {
      break;
    }
  }
}

bool Router::Matches(HttpMethod method, const std::string& path,
                     RouteParams& params) const {
  if (method_ != HttpMethod::kAny && method_ != method) {
    return false;
  }

  std::smatch match;
  if (std::regex_match(path, match, regex_)) {
    params.Clear();
    // Extract parameters from capture groups
    for (size_t i = 1; i < match.size() && i - 1 < param_names_.size(); ++i) {
      params.Add(param_names_[i - 1], match[i].str());
    }
    return true;
  }

  return false;
}

void Router::Execute(const HttpRequestContext& context,
                     HttpResponse& response) const {
  if (handler_) {
    handler_(context, response);
  }
}

// StaticFileServer implementation

StaticFileServer::StaticFileServer(const std::string& root_directory,
                                   const std::string& url_prefix)
    : root_directory_(root_directory),
      url_prefix_(url_prefix),
      cache_control_("public, max-age=3600"),
      index_file_("index.html"),
      directory_listing_(false) {}

bool StaticFileServer::CanHandle(const std::string& path) const {
  return path.find(url_prefix_) == 0;
}

void StaticFileServer::ServeFile(const HttpRequestContext& context,
                                 HttpResponse& response) const {
  std::string file_path = GetFilePath(
      std::string(context.request->uri.data(), context.request->uri.size()));

  if (!IsValidPath(file_path)) {
    response.Status(403).Text("Forbidden");
    return;
  }

  if (!HttpUtils::FileExists(file_path)) {
    response.Status(404).Text("File Not Found");
    return;
  }

  // Check if it's a directory
  if (std::filesystem::is_directory(file_path)) {
    std::string index_path = file_path + "/" + index_file_;
    if (HttpUtils::FileExists(index_path)) {
      file_path = index_path;
    } else if (directory_listing_) {
      ServeDirectoryListing(file_path, response);
      return;
    } else {
      response.Status(403).Text("Directory listing disabled");
      return;
    }
  }

  response.Header("Cache-Control", cache_control_).File(file_path);
}

std::string StaticFileServer::GetFilePath(const std::string& url_path) const {
  std::string relative_path = url_path.substr(url_prefix_.length());
  if (relative_path.empty() || relative_path[0] != '/') {
    relative_path = "/" + relative_path;
  }

  return root_directory_ + relative_path;
}

bool StaticFileServer::IsValidPath(const std::string& file_path) const {
  // Prevent directory traversal attacks
  std::string canonical_root = std::filesystem::canonical(root_directory_);
  try {
    std::string canonical_file = std::filesystem::canonical(file_path);
    return canonical_file.find(canonical_root) == 0;
  } catch (const std::filesystem::filesystem_error&) {
    return false;
  }
}

void StaticFileServer::ServeDirectoryListing(const std::string& directory_path,
                                             HttpResponse& response) const {
  std::stringstream html;
  html << "<!DOCTYPE html><html><head><title>Directory Listing</title></head>";
  html << "<body><h1>Directory Listing</h1><ul>";

  try {
    for (const auto& entry :
         std::filesystem::directory_iterator(directory_path)) {
      std::string name = entry.path().filename().string();
      std::string url = url_prefix_ + "/" + name;
      if (entry.is_directory()) {
        name += "/";
        url += "/";
      }
      html << "<li><a href=\"" << url << "\">" << name << "</a></li>";
    }
  } catch (const std::filesystem::filesystem_error&) {
    response.Status(500).Text("Error reading directory");
    return;
  }

  html << "</ul></body></html>";
  response.Html(html.str());
}

// HttpRouter implementation

void HttpRouter::Get(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kGet, pattern, handler));
}

void HttpRouter::Post(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kPost, pattern, handler));
}

void HttpRouter::Put(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kPut, pattern, handler));
}

void HttpRouter::Delete(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kDelete, pattern, handler));
}

void HttpRouter::Patch(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kPatch, pattern, handler));
}

void HttpRouter::Options(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(std::make_unique<::xtils::Router>(HttpMethod::kOptions,
                                                      pattern, handler));
}

void HttpRouter::Head(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kHead, pattern, handler));
}

void HttpRouter::Any(const std::string& pattern, RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(HttpMethod::kAny, pattern, handler));
}

void HttpRouter::Route(HttpMethod method, const std::string& pattern,
                       RouteHandler handler) {
  routes_.push_back(
      std::make_unique<::xtils::Router>(method, pattern, handler));
}

void HttpRouter::Use(MiddlewareHandler middleware) {
  middlewares_.emplace_back("", middleware);
}

void HttpRouter::Use(const std::string& path_prefix,
                     MiddlewareHandler middleware) {
  middlewares_.emplace_back(path_prefix, middleware);
}

void HttpRouter::Static(const std::string& url_prefix,
                        const std::string& directory) {
  static_servers_.push_back(
      std::make_unique<StaticFileServer>(directory, url_prefix));
}

bool HttpRouter::HandleRequest(const HttpRequest& request) {
  try {
    HttpRequestContext context(request);
    HttpResponse response;

    // Handle CORS preflight
    if (cors_enabled_ && std::string(request.method.data(),
                                     request.method.size()) == "OPTIONS") {
      HandleCorsPreflightRequest(context, response);
      response.Send(request.conn);
      return true;
    }

    // Run middlewares
    if (!RunMiddlewares(context, response)) {
      response.Send(request.conn);
      return true;
    }

    // Try static file servers first
    std::string path(request.uri.data(), request.uri.size());
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
      path = path.substr(0, query_pos);
    }

    for (const auto& server : static_servers_) {
      if (server->CanHandle(path)) {
        server->ServeFile(context, response);
        response.Send(request.conn);
        return true;
      }
    }

    // Try routes
    HttpMethod method = HttpUtils::StringToHttpMethod(
        std::string(request.method.data(), request.method.size()));
    RouteParams params;

    for (const auto& route : routes_) {
      if (route->Matches(method, path, params)) {
        context.params = params;
        route->Execute(context, response);

        // Add CORS headers if enabled
        if (cors_enabled_) {
          response.Header("Access-Control-Allow-Origin", cors_origin_);
          response.Header("Access-Control-Allow-Methods", cors_methods_);
        }

        response.Send(request.conn);
        return true;
      }
    }

    // No route found
    if (not_found_handler_) {
      not_found_handler_(context, response);
      response.Send(request.conn);
    } else {
      HttpResponse::SendError(request.conn, 404, "Not Found");
    }

    return true;

  } catch (const std::exception& e) {
    if (error_handler_) {
      HttpRequestContext context(request);
      HttpResponse response;
      error_handler_(context, response, e.what());
      response.Send(request.conn);
    } else {
      HttpResponse::SendError(request.conn, 500, "Internal Server Error");
    }
    return true;
  }
}

void HttpRouter::EnableCors(const std::string& origin,
                            const std::string& methods) {
  cors_enabled_ = true;
  cors_origin_ = origin;
  cors_methods_ = methods;
}

HttpMethod HttpRouter::StringToMethod(const std::string& method) const {
  return HttpUtils::StringToHttpMethod(method);
}

std::string HttpRouter::MethodToString(HttpMethod method) const {
  return HttpUtils::HttpMethodToString(method);
}

void HttpRouter::HandleCorsPreflightRequest(const HttpRequestContext& context,
                                            HttpResponse& response) {
  response.Status(204)
      .Header("Access-Control-Allow-Origin", cors_origin_)
      .Header("Access-Control-Allow-Methods", cors_methods_)
      .Header("Access-Control-Allow-Headers", "Content-Type, Authorization")
      .Header("Access-Control-Max-Age", "86400");
}

bool HttpRouter::RunMiddlewares(const HttpRequestContext& context,
                                HttpResponse& response) {
  std::string path(context.request->uri.data(), context.request->uri.size());

  for (const auto& middleware : middlewares_) {
    if (middleware.first.empty() || path.find(middleware.first) == 0) {
      if (!middleware.second(context, response)) {
        return false;  // Middleware halted processing
      }
    }
  }
  return true;
}

// HttpUtils functions are now in http_common.cc

}  // namespace xtils
