#pragma once

#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "xtils/net/http_common.h"
#include "xtils/net/http_server.h"
#include "xtils/utils/string_view.h"

namespace xtils {

// Route parameters extracted from URL
class RouteParams {
 public:
  RouteParams() = default;

  // Add a parameter
  void Add(const std::string& name, const std::string& value);

  // Get parameter value
  std::string Get(const std::string& name) const;
  std::string operator[](const std::string& name) const { return Get(name); }

  // Check if parameter exists
  bool Has(const std::string& name) const;

  // Get all parameters
  const std::map<std::string, std::string>& GetAll() const { return params_; }

  // Clear all parameters
  void Clear() { params_.clear(); }

  // Get parameter count
  size_t Count() const { return params_.size(); }

 private:
  std::map<std::string, std::string> params_;
};

// Query parameters from URL
class QueryParams {
 public:
  explicit QueryParams(StringView query_string);
  QueryParams() = default;

  // Get parameter value
  std::string Get(const std::string& name) const;
  std::string operator[](const std::string& name) const { return Get(name); }

  // Get parameter with default value
  std::string Get(const std::string& name,
                  const std::string& default_val) const;

  // Check if parameter exists
  bool Has(const std::string& name) const;

  // Get all values for a parameter (for parameters that appear multiple times)
  std::vector<std::string> GetAll(const std::string& name) const;

  // Get all parameters
  const std::multimap<std::string, std::string>& GetAll() const {
    return params_;
  }

 private:
  void Parse(StringView query_string);
  std::multimap<std::string, std::string> params_;
};

// Enhanced HTTP request context
struct HttpRequestContext {
  const HttpRequest* request;
  RouteParams params;
  QueryParams query;
  std::map<std::string, std::string> headers_map;

  explicit HttpRequestContext(const HttpRequest& req);

  // Convenience methods
  std::string GetHeader(const std::string& name) const;
  std::string GetParam(const std::string& name) const {
    return params.Get(name);
  }
  std::string GetQuery(const std::string& name) const {
    return query.Get(name);
  }

  // Get request body as string
  std::string GetBody() const;

  // Check content type
  bool IsJson() const;
  bool IsForm() const;
  bool IsMultipart() const;

  // Get client IP (simplified)
  std::string GetClientIP() const;
};

// HTTP response builder
class HttpResponse {
 public:
  HttpResponse() = default;

  // Set status code
  HttpResponse& Status(int code);
  HttpResponse& Status(const std::string& status);

  // Set headers
  HttpResponse& Header(const std::string& name, const std::string& value);
  HttpResponse& Headers(const HttpHeaders& headers);

  // Set content type
  HttpResponse& ContentType(const std::string& content_type);

  // Set body
  HttpResponse& Body(const std::string& body);
  HttpResponse& Json(const std::string& json);
  HttpResponse& Html(const std::string& html);
  HttpResponse& Text(const std::string& text);

  // File response
  HttpResponse& File(const std::string& file_path);
  HttpResponse& Download(const std::string& file_path,
                         const std::string& filename = "");

  // Redirect
  HttpResponse& Redirect(const std::string& url, int code = 302);

  // Build and send response
  void Send(HttpServerConnection* conn);

  // Static helper methods
  static void SendJson(HttpServerConnection* conn, const std::string& json,
                       int status = 200);
  static void SendError(HttpServerConnection* conn, int status,
                        const std::string& message = "");
  static void SendFile(HttpServerConnection* conn,
                       const std::string& file_path);

 private:
  std::string status_ = "200 OK";
  HttpHeaders headers_;
  std::string body_;
  std::string file_path_;
  bool is_file_response_ = false;
};

// Route handler function type
using RouteHandler =
    std::function<void(const HttpRequestContext&, HttpResponse&)>;

// Middleware function type
using MiddlewareHandler =
    std::function<bool(const HttpRequestContext&, HttpResponse&)>;

// Route definition
class Router {
 public:
  Router(HttpMethod method, const std::string& pattern, RouteHandler handler);

  // Check if this route matches the request
  bool Matches(HttpMethod method, const std::string& path,
               RouteParams& params) const;

  // Execute the route handler
  void Execute(const HttpRequestContext& context, HttpResponse& response) const;

  // Get route info
  HttpMethod GetMethod() const { return method_; }
  const std::string& GetPattern() const { return pattern_; }

 private:
  HttpMethod method_;
  std::string pattern_;
  std::regex regex_;
  std::vector<std::string> param_names_;
  RouteHandler handler_;
  bool is_regex_;

  void CompilePattern();
  void ExtractParamNames();
};

// Static file server
class StaticFileServer {
 public:
  StaticFileServer(const std::string& root_directory,
                   const std::string& url_prefix = "/static");

  // Check if request is for a static file
  bool CanHandle(const std::string& path) const;

  // Serve static file
  void ServeFile(const HttpRequestContext& context,
                 HttpResponse& response) const;

  // Set configuration
  void SetCacheControl(const std::string& cache_control) {
    cache_control_ = cache_control;
  }
  void EnableDirectoryListing(bool enable) { directory_listing_ = enable; }
  void SetIndexFile(const std::string& index_file) { index_file_ = index_file; }

 private:
  std::string root_directory_;
  std::string url_prefix_;
  std::string cache_control_;
  std::string index_file_;
  bool directory_listing_;

  std::string GetMimeType(const std::string& file_extension) const;
  std::string GetFilePath(const std::string& url_path) const;
  bool IsValidPath(const std::string& file_path) const;
  void ServeDirectoryListing(const std::string& directory_path,
                             HttpResponse& response) const;
};

// HTTP Router
class HttpRouter {
 public:
  HttpRouter() = default;
  ~HttpRouter() = default;

  // Add routes
  void Get(const std::string& pattern, RouteHandler handler);
  void Post(const std::string& pattern, RouteHandler handler);
  void Put(const std::string& pattern, RouteHandler handler);
  void Delete(const std::string& pattern, RouteHandler handler);
  void Patch(const std::string& pattern, RouteHandler handler);
  void Options(const std::string& pattern, RouteHandler handler);
  void Head(const std::string& pattern, RouteHandler handler);
  void Any(const std::string& pattern, RouteHandler handler);
  void Route(HttpMethod method, const std::string& pattern,
             RouteHandler handler);

  // Add middleware
  void Use(MiddlewareHandler middleware);
  void Use(const std::string& path_prefix, MiddlewareHandler middleware);

  // Static file serving
  void Static(const std::string& url_prefix, const std::string& directory);
  void ServeStatic(const std::string& directory) {
    Static("/static", directory);
  }

  // Route group (for organizing routes with common prefix)
  class RouteGroup {
   public:
    RouteGroup(HttpRouter* router, const std::string& prefix)
        : router_(router), prefix_(prefix) {}

    void Get(const std::string& pattern, RouteHandler handler) {
      router_->Get(prefix_ + pattern, handler);
    }
    void Post(const std::string& pattern, RouteHandler handler) {
      router_->Post(prefix_ + pattern, handler);
    }
    void Put(const std::string& pattern, RouteHandler handler) {
      router_->Put(prefix_ + pattern, handler);
    }
    void Delete(const std::string& pattern, RouteHandler handler) {
      router_->Delete(prefix_ + pattern, handler);
    }

   private:
    HttpRouter* router_;
    std::string prefix_;
  };

  RouteGroup Group(const std::string& prefix) {
    return RouteGroup(this, prefix);
  }

  // Handle HTTP request
  bool HandleRequest(const HttpRequest& request);

  // Set error handlers
  void SetNotFoundHandler(RouteHandler handler) {
    not_found_handler_ = handler;
  }
  void SetErrorHandler(std::function<void(const HttpRequestContext&,
                                          HttpResponse&, const std::string&)>
                           handler) {
    error_handler_ = handler;
  }

  // Enable CORS
  void EnableCors(const std::string& origin = "*",
                  const std::string& methods = "GET,POST,PUT,DELETE,OPTIONS");

 private:
  std::vector<std::unique_ptr<Router>> routes_;
  std::vector<std::pair<std::string, MiddlewareHandler>>
      middlewares_;  // path_prefix, handler
  std::vector<std::unique_ptr<StaticFileServer>> static_servers_;
  RouteHandler not_found_handler_;
  std::function<void(const HttpRequestContext&, HttpResponse&,
                     const std::string&)>
      error_handler_;

  bool cors_enabled_ = false;
  std::string cors_origin_;
  std::string cors_methods_;

  HttpMethod StringToMethod(const std::string& method) const;
  std::string MethodToString(HttpMethod method) const;
  void HandleCorsPreflightRequest(const HttpRequestContext& context,
                                  HttpResponse& response);
  bool RunMiddlewares(const HttpRequestContext& context,
                      HttpResponse& response);
};

// Router-based HTTP request handler
class RouterHttpRequestHandler : public HttpRequestHandler {
 public:
  explicit RouterHttpRequestHandler(std::unique_ptr<HttpRouter> router)
      : router_(std::move(router)) {}

  void OnHttpRequest(const HttpRequest& request) override {
    if (!router_->HandleRequest(request)) {
      // Fallback to default behavior
      HttpHeaders headers;
      headers.emplace_back("Content-Type", "text/html; charset=utf-8");
      request.conn->SendResponseAndClose(
          "404 Not Found", headers,
          "<html><body><h1>404 Not Found</h1></body></html>");
    }
  }

  HttpRouter* GetRouter() { return router_.get(); }

 private:
  std::unique_ptr<HttpRouter> router_;
};

}  // namespace xtils
