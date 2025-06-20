#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.h"

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

  // Response information
  struct Response {
    std::string content;
    std::string content_type;
    std::string status;

    Response(const std::string& content = "",
             const std::string& content_type = "text/plain",
             const std::string& status = "200 OK")
        : content(content), content_type(content_type), status(status) {}
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

  // WebSocket publishing
  size_t Publish(const std::string& url, const std::string& message);
  size_t Publish(const std::string& url, const base::Json& json);
  PublishResult PublishWithResult(const std::string& url,
                                  const std::string& message);

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
  std::vector<std::string> GetWebSocketUrls() const;

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

// Register simple route without description
#define INSPECT_ROUTE_SIMPLE(path, handler)                       \
  do {                                                            \
    base::Inspect::Get().RouteWithDescription(path, "", handler); \
  } while (0)

// Register static content
#define INSPECT_STATIC(path, content, content_type)           \
  do {                                                        \
    base::Inspect::Get().Static(path, content, content_type); \
  } while (0)

// Publish to WebSocket subscribers
#define INSPECT_PUBLISH(url, message) base::Inspect::Get().Publish(url, message)

}  // namespace base
