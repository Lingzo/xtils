#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/json.h"
#ifdef INSPECT_DISABLE
#define INSPECT_ROUTE(path, description, handler)
#define INSPECT_STATIC(path, content, content_type)
#define INSPECT_DUAL_ROUTE(path, description, handler)
#define INSPECT_SIMPLE(path, expr)
#define INSPECT_PUBLISH_TEXT(url, message)
#define INSPECT_PUBLISH_BINARY(url, message)
#else
// Convenience macros for route registration

// Register route with description - recommended for API documentation
#define INSPECT_ROUTE(path, description, handler)                           \
  do {                                                                      \
    xtils::Inspect::Get().RouteWithDescription(path, description, handler); \
  } while (0)

// Register static content
#define INSPECT_STATIC(path, content, content_type)            \
  do {                                                         \
    xtils::Inspect::Get().Static(path, content, content_type); \
  } while (0)

// Register combined HTTP and WebSocket handlers for the same path
#define INSPECT_DUAL_ROUTE(path, description, handler)                     \
  do {                                                                     \
    xtils::Inspect::Get().RouteWithDescription(path, description, handler, \
                                               true);                      \
  } while (0)

#define INSPECT_SIMPLE(path, expr)                                             \
  do {                                                                         \
    auto handler = [&](const Inspect::Request &req, Inspect::Response &resp) { \
      (expr);                                                                  \
      resp = Inspect::TextResponse("");                                        \
    };                                                                         \
    INSPECT_ROUTE(path, "empty response", handler);                            \
  } while (0)

// WebSocket publishing to all subscribers
#define INSPECT_PUBLISH_TEXT(url, message) \
  xtils::Inspect::Get().Publish(url, message, true)
#define INSPECT_PUBLISH_BINARY(url, message) \
  xtils::Inspect::Get().Publish(url, message, false)
#endif

namespace xtils {

/**
 * Inspect - Simplified HTTP server and WebSocket publisher
 *
 * Thread-safe singleton for HTTP API inspection and WebSocket publishing.
 * Provides easy-to-use macros and methods for route registration.
 */
class Inspect {
 public:
  // Unified request information for both HTTP and WebSocket
  struct Request {
    std::string path;
    std::map<std::string, std::string> query;
    std::string method;  // "GET", "POST", etc. for HTTP; "WS" for WebSocket
    std::string body;    // HTTP body or WebSocket data
    bool is_websocket = false;
    bool is_text = true;         // For WebSocket messages
    void *connection = nullptr;  // HttpServerConnection* - opaque pointer

    // HTTP constructor
    Request(const std::string &path = "", const std::string &method = "GET",
            const std::string &body = "")
        : path(path), method(method), body(body) {}

    // WebSocket constructor
    Request(const std::string &path, const std::string &data, bool is_text,
            void *connection)
        : path(path),
          method("WS"),
          body(data),
          is_websocket(true),
          is_text(is_text),
          connection(connection) {}
  };

  // Response information
  struct Response {
    std::string content;
    std::string content_type;
    std::string status;
    bool is_text = true;

    Response(const std::string &content = "",
             const std::string &content_type = "text/plain",
             const std::string &status = "200 OK")
        : content(content), content_type(content_type), status(status) {}
    Response(const std::string &content, bool is_text)
        : content(content), is_text(is_text) {}
    void sendJson(const Json &json) {
      content = json.dump();
      content_type = "application/json";
    }
    void sendText(const std::string &text) {
      content = text;
      content_type = "text/plain";
    }
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

  // Unified handler type
  using Handler = std::function<void(const Request &, Response &)>;

  // Route information
  struct RouteInfo {
    std::string description;
    Handler handler = nullptr;
    bool supports_websocket = false;

    RouteInfo() = default;
    RouteInfo(const std::string &desc, Handler h, bool ws_support = false)
        : description(desc), handler(h), supports_websocket(ws_support) {}
  };

  // Factory methods - Thread-safe singleton pattern
  static Inspect &Get();
  void Init(const std::string &ip = "127.0.0.1", int port = 8080);

  /**
   * @brief Stops the Inspect server, freeing resources.
   */
  void Stop();

  /**
   * @brief Checks if the Inspect server is currently running.
   * @return True if the server is running, false otherwise.
   */
  bool IsRunning() const;

  // HTTP routing
  void RouteWithDescription(const std::string &path,
                            const std::string &description, Handler handler,
                            bool supports_websocket = false);
  void Static(const std::string &path, const std::string &content,
              const std::string &content_type = "text/html");
  void Unregister(const std::string &path);
  bool HasRoute(const std::string &path) const;

  /**
   * @brief Checks if a WebSocket handler is registered for the specified path.
   * @param path The path to check.
   * @return True if a WebSocket handler is registered, false otherwise.
   */
  bool HasWebSocketRoute(const std::string &path) const;

  // WebSocket publishing (broadcast to all subscribers of a URL)
  size_t Publish(const std::string &url, const std::string &message,
                 bool is_text = true);
  size_t Publish(const std::string &url, const xtils::Json &json);
  PublishResult PublishWithResult(const std::string &url,
                                  const std::string &message,
                                  bool is_text = true);

  bool HasSubscribers(const std::string &url) const;
  size_t GetSubscriberCount(const std::string &url) const;

  // Response helpers
  static Response JsonResponse(const xtils::Json &json,
                               const std::string &status = "200 OK");
  static Response TextResponse(const std::string &text,
                               const std::string &status = "200 OK");
  static Response HtmlResponse(const std::string &html,
                               const std::string &status = "200 OK");
  static Response ErrorResponse(
      const std::string &message,
      const std::string &status = "500 Internal Server Error");
  static Response NotFoundResponse(const std::string &message = "");

  // Utilities
  xtils::Json GetServerInfo() const;
  void SetCORS(const std::string &allow_origin = "*");
  std::vector<std::string> GetRoutes() const;
  std::vector<std::string> GetWebSocketRoutes() const;

  // Non-copyable, non-movable
  Inspect(const Inspect &) = delete;
  Inspect &operator=(const Inspect &) = delete;
  Inspect(Inspect &&) = delete;
  Inspect &operator=(Inspect &&) = delete;

 private:
  Inspect();
  ThreadTaskRunner task_runner_;

 public:
  ~Inspect();
};
}  // namespace xtils
