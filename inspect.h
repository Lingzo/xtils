#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.h"
#include "string_view.h"

namespace base {

// Forward declarations
class TaskRunner;

/**
 * Inspect - Simplified HTTP server and WebSocket publisher
 *
 * Thread-safe singleton for HTTP API inspection and WebSocket publishing.
 * Provides easy-to-use macros and methods for route registration.
 */
class Inspect {
 public:
  // Request information
  struct Request {
    std::string path;
    std::map<std::string, std::string> query;
    std::string method;
    std::string body;
  };

  // WebSocket request information
  struct WebSocketRequest {
    std::string path;
    std::map<std::string, std::string> query;
    std::string data;
    bool is_text;
    void* connection;  // HttpServerConnection* - opaque pointer to avoid header
                       // dependency

    WebSocketRequest(const std::string& path = "", const std::string& data = "",
                     bool is_text = true, void* connection = nullptr)
        : path(path), data(data), is_text(is_text), connection(connection) {}
  };

  // Response information
  struct Response {
    std::string content;
    std::string content_type;
    std::string status;
    bool is_text = true;

    Response(const std::string& content = "",
             const std::string& content_type = "text/plain",
             const std::string& status = "200 OK")
        : content(content), content_type(content_type), status(status) {}
    Response(const std::string& content, bool is_text)
        : content(content), is_text(is_text) {}
  };

  // WebSocket publish result with detailed information
  struct PublishResult {
    size_t sent_count = 0;
    size_t failed_count = 0;
    bool has_subscribers = false;
    std::string error;

    bool IsSuccess() const { return sent_count > 0 && failed_count == 0; }
    bool HasFailures() const { return failed_count > 0; }
    bool IsEmpty() const { return sent_count == 0 && failed_count == 0; }
  };

  // Handler types
  using Handler = std::function<Response(const Request&)>;
  using WebSocketHandler = std::function<Response(const WebSocketRequest&)>;

  // Combined route information
  struct RouteInfo {
    std::string description;
    Handler http_handler = nullptr;
    WebSocketHandler websocket_handler = nullptr;
    bool supports_websocket = false;

    RouteInfo() = default;
    RouteInfo(const std::string& desc, Handler handler)
        : description(desc), http_handler(handler) {}
    RouteInfo(const std::string& desc, Handler handler,
              WebSocketHandler ws_handler)
        : description(desc),
          http_handler(handler),
          websocket_handler(ws_handler),
          supports_websocket(true) {}
  };

  // Factory methods - Thread-safe singleton pattern
  static Inspect& Create(TaskRunner* task_runner, int port = 8080);
  static Inspect& Get();

  // Server control
  void Stop();
  bool IsRunning() const;

  // HTTP routing
  void RouteWithDescription(const std::string& path,
                            const std::string& description, Handler handler);
  void Static(const std::string& path, const std::string& content,
              const std::string& content_type = "text/html");
  void Unregister(const std::string& path);
  bool HasRoute(const std::string& path) const;

  // Combined HTTP/WebSocket routing
  void RouteWithHandlers(const std::string& path,
                         const std::string& description, Handler http_handler,
                         WebSocketHandler ws_handler);

  bool HasWebSocketRoute(const std::string& path) const;

  // WebSocket publishing (broadcast to all subscribers of a URL)
  size_t Publish(const std::string& url, const std::string& message,
                 bool is_text = true);
  size_t Publish(const std::string& url, const base::Json& json);
  PublishResult PublishWithResult(const std::string& url,
                                  const std::string& message,
                                  bool is_text = true);

  bool HasSubscribers(const std::string& url) const;
  size_t GetSubscriberCount(const std::string& url) const;

  // Response helpers
  static Response JsonResponse(const base::Json& json,
                               const std::string& status = "200 OK");
  static Response TextResponse(const std::string& text,
                               const std::string& status = "200 OK");
  static Response HtmlResponse(const std::string& html,
                               const std::string& status = "200 OK");
  static Response ErrorResponse(
      const std::string& message,
      const std::string& status = "500 Internal Server Error");
  static Response NotFoundResponse(const std::string& message = "");

  // Utilities
  base::Json GetServerInfo() const;
  void SetCORS(const std::string& allow_origin = "*");
  std::vector<std::string> GetRoutes() const;
  std::vector<std::string> GetWebSocketUrls()
      const;  // Active WebSocket connections
  std::vector<std::string> GetWebSocketRoutes()
      const;  // Registered WebSocket handlers

  // Non-copyable, non-movable
  Inspect(const Inspect&) = delete;
  Inspect& operator=(const Inspect&) = delete;
  Inspect(Inspect&&) = delete;
  Inspect& operator=(Inspect&&) = delete;

 private:
  Inspect();

 public:
  ~Inspect();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  // Singleton management is now handled by InspectSingleton friend class
  friend class InspectSingleton;
};

// Convenience macros for route registration

// Register route with description - recommended for API documentation
#define INSPECT_ROUTE(path, description, handler)                          \
  do {                                                                     \
    base::Inspect::Get().RouteWithDescription(path, description, handler); \
  } while (0)

// Register static content
#define INSPECT_STATIC(path, content, content_type)           \
  do {                                                        \
    base::Inspect::Get().Static(path, content, content_type); \
  } while (0)

// Register combined HTTP and WebSocket handlers for the same path
#define INSPECT_DUAL_ROUTE(path, description, http_handler, ws_handler)     \
  do {                                                                      \
    base::Inspect::Get().RouteWithHandlers(path, description, http_handler, \
                                           ws_handler);                     \
  } while (0)

// WebSocket publishing to all subscribers
#define INSPECT_PUBLISH(url, message) \
  base::Inspect::Get().Publish(url, message, true)
#define INSPECT_PUBLISH_TEXT(url, message) \
  base::Inspect::Get().Publish(url, message, true)
#define INSPECT_PUBLISH_BINARY(url, message) \
  base::Inspect::Get().Publish(url, message, false)
}  // namespace base
