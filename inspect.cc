#include "inspect.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "task_runner.h"

namespace base {

// Private implementation class
class Inspect::Impl : public HttpRequestHandler {
 public:
  Impl() : task_runner_(nullptr), port_(8080), started_(false) {}

  ~Impl() { Stop(); }

  void Initialize(TaskRunner* task_runner, int port) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    task_runner_ = task_runner;
    port_ = port;
  }

  void Start() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!started_ && task_runner_) {
      server_ = std::make_unique<HttpServer>(task_runner_, this);
      server_->Start(port_);
      started_ = true;

      // Register default index route
      RegisterHandler("/",
                      [this](const Inspect::Request&) -> Inspect::Response {
                        return Inspect::HtmlResponse(GenerateIndexPage());
                      });
    }
  }

  void Stop() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (started_ && server_) {
      server_.reset();
      started_ = false;
      websocket_connections_.clear();
      connection_to_url_.clear();
    }
  }

  bool IsRunning() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return started_ && server_ != nullptr;
  }

  void RegisterHandler(const std::string& path, Inspect::Handler handler) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    handlers_[path] = std::move(handler);
  }

  void RegisterHandlerWithDescription(const std::string& path,
                                      const std::string& description,
                                      Inspect::Handler handler) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    handlers_[path] = std::move(handler);
    if (!description.empty()) {
      route_descriptions_[path] = description;
    }
  }

  void UnregisterHandler(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    handlers_.erase(path);
    route_descriptions_.erase(path);
  }

  bool HasHandler(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return handlers_.find(path) != handlers_.end();
  }

  std::vector<std::string> GetHandlerPaths() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> paths;
    paths.reserve(handlers_.size());
    for (const auto& pair : handlers_) {
      paths.push_back(pair.first);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
  }

  std::vector<std::string> GetWebSocketUrls() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> urls;
    urls.reserve(websocket_connections_.size());
    for (const auto& pair : websocket_connections_) {
      urls.push_back(pair.first);
    }
    std::sort(urls.begin(), urls.end());
    return urls;
  }

  size_t PublishEvent(const std::string& url, const std::string& message) {
    return PublishEventWithResult(url, message).sent_count;
  }

  Inspect::PublishResult PublishEventWithResult(const std::string& url,
                                                const std::string& message) {
    Inspect::PublishResult result;

    auto it = websocket_connections_.find(url);
    if (it == websocket_connections_.end()) {
      return result;  // No subscribers
    }

    result.has_subscribers = true;
    auto& connections = it->second;

    // Remove inactive connections first
    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     [](HttpServerConnection* conn) {
                                       return conn == nullptr;
                                     }),
                      connections.end());

    // Send message to all active connections
    for (auto* conn : connections) {
      try {
        if (conn) {
          conn->SendWebsocketMessage(message.data(), message.size());
          result.sent_count++;
        }
      } catch (const std::exception& e) {
        result.failed_count++;
        if (result.error.empty()) {
          result.error = e.what();
        }
      }
    }

    return result;
  }

  bool HasEventSubscribers(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = websocket_connections_.find(url);
    if (it == websocket_connections_.end()) {
      return false;
    }

    // Check if any connections are active
    for (auto* conn : it->second) {
      if (conn != nullptr) {
        return true;
      }
    }
    return false;
  }

  size_t GetEventSubscriberCount(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = websocket_connections_.find(url);
    if (it == websocket_connections_.end()) {
      return 0;
    }

    size_t count = 0;
    for (auto* conn : it->second) {
      if (conn != nullptr) {
        count++;
      }
    }
    return count;
  }

  base::Json GetServerInfo() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    base::Json info;
    info["port"] = port_;
    info["running"] = IsRunning();
    base::Json::array_t routes_array;
    for (const auto& pair : handlers_) {
      base::Json route;
      route["path"] = pair.first;
      auto desc_it = route_descriptions_.find(pair.first);
      if (desc_it != route_descriptions_.end()) {
        route["description"] = desc_it->second;
      }
      routes_array.push_back(route);
    }
    info["routes"] = base::Json(routes_array);

    base::Json::array_t ws_array;
    for (const auto& pair : websocket_connections_) {
      base::Json ws;
      ws["url"] = pair.first;
      size_t active_count = 0;
      for (auto* conn : pair.second) {
        if (conn != nullptr) {
          active_count++;
        }
      }
      ws["subscribers"] = active_count;
      ws_array.push_back(ws);
    }
    info["websockets"] = base::Json(ws_array);

    return info;
  }

  std::string ReplaceTemplateVars(const std::string& content) const {
    std::string result = content;
    // Replace {{PORT}} with actual port
    size_t pos = 0;
    while ((pos = result.find("{{PORT}}", pos)) != std::string::npos) {
      result.replace(pos, 8, std::to_string(port_));
      pos += std::to_string(port_).length();
    }

    // Replace {{ROUTES}} with route list
    pos = 0;
    while ((pos = result.find("{{ROUTES}}", pos)) != std::string::npos) {
      std::string routes_html;
      for (const auto& pair : handlers_) {
        routes_html +=
            "<li><a href=\"" + pair.first + "\">" + pair.first + "</a>";
        auto desc_it = route_descriptions_.find(pair.first);
        if (desc_it != route_descriptions_.end() && !desc_it->second.empty()) {
          routes_html += " - " + desc_it->second;
        }
        routes_html += "</li>";
      }
      result.replace(pos, 10, routes_html);
      pos += routes_html.length();
    }

    return result;
  }

  std::string GenerateIndexPage() const {
    std::string html = R"(<!DOCTYPE html>
<html>
<head><title>Inspect Server</title></head>
<body>
<h1>Inspect Server - Port {{PORT}}</h1>
<h2>Available Routes</h2>
<ul>{{ROUTES}}</ul>
<h2>Server Info</h2>
<p>Status: Running</p>
<p>Routes: )" + std::to_string(handlers_.size()) +
                       R"(</p>
<p>WebSocket URLs: )" + std::to_string(websocket_connections_.size()) +
                       R"(</p>
</body>
</html>)";

    return ReplaceTemplateVars(html);
  }

  void SetCORS(const std::string& allow_origin) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    cors_origin_ = allow_origin;
    if (server_) {
      server_->AddAllowedOrigin(allow_origin);
    }
  }

  // HttpRequestHandler interface implementation
  void OnHttpRequest(const HttpRequest& http_req) override {
    HandleHttpRequest(http_req);
  }

  void OnWebsocketMessage(const WebsocketMessage& msg) override {
    // Handle incoming WebSocket message
    // In a real implementation, this might route messages to handlers
  }

  void OnHttpConnectionClosed(HttpServerConnection* conn) override {
    RemoveWebSocketConnection(conn);
  }

 private:
  std::string ExtractPath(const std::string& uri_str) const {
    std::string full_path(uri_str.data(), uri_str.size());
    size_t query_pos = full_path.find('?');
    return (query_pos != std::string::npos) ? full_path.substr(0, query_pos)
                                            : full_path;
  }

  std::map<std::string, std::string> ParseQueryParams(
      const std::string& uri_str) const {
    std::map<std::string, std::string> params;
    std::string full_path(uri_str.data(), uri_str.size());
    size_t query_pos = full_path.find('?');
    if (query_pos == std::string::npos) {
      return params;
    }

    std::string query = full_path.substr(query_pos + 1);
    std::istringstream ss(query);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
      size_t eq_pos = pair.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = pair.substr(0, eq_pos);
        std::string value = pair.substr(eq_pos + 1);
        params[key] = value;
      }
    }

    return params;
  }

  bool IsWebSocketUpgradeRequest(const HttpRequest& req) const {
    return req.is_websocket_handshake;
  }

  void HandleWebSocketUpgrade(const HttpRequest& req) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string path = ExtractPath(req.uri.ToStr());
    websocket_connections_[path].push_back(req.conn);
    connection_to_url_[req.conn] = path;

    // The HttpServer will handle the WebSocket upgrade response automatically
  }

  void HandleHttpRequest(const HttpRequest& http_req) {
    LogThis();
    if (IsWebSocketUpgradeRequest(http_req)) {
      HandleWebSocketUpgrade(http_req);
      return;
    }

    std::string uri_str = http_req.uri.ToStr();
    std::string path = ExtractPath(uri_str);
    auto query_params = ParseQueryParams(uri_str);

    // Create Inspect::Request
    Inspect::Request req;
    req.path = path;
    req.query = std::move(query_params);
    req.method = http_req.method.ToStr();

    // Convert body if present
    if (!http_req.body.empty()) {
      req.body = http_req.body.ToStr();
    }

    // Find handler
    Inspect::Response response;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      auto handler_it = handlers_.find(path);

      if (handler_it != handlers_.end()) {
        try {
          response = handler_it->second(req);
        } catch (const std::exception& e) {
          response =
              Inspect::ErrorResponse("Handler error: " + std::string(e.what()));
        }
      } else {
        response = Inspect::NotFoundResponse("Route not found: " + path);
      }
    }

    // Send response
    std::list<NameValue> headers;
    headers.push_back({"Content-Type", StringView(response.content_type)});
    if (!cors_origin_.empty()) {
      headers.push_back(
          {"Access-Control-Allow-Origin", StringView(cors_origin_)});
      headers.push_back({StringView("Access-Control-Allow-Methods"),
                         StringView("GET, POST, OPTIONS")});
      headers.push_back({StringView("Access-Control-Allow-Headers"),
                         StringView("Content-Type, Authorization")});
    }
    http_req.conn->SendResponse(
        response.status.c_str(), headers,
        StringView(response.content.data(), response.content.size()));
  }

  void RemoveWebSocketConnection(HttpServerConnection* conn) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto url_it = connection_to_url_.find(conn);
    if (url_it != connection_to_url_.end()) {
      const std::string& url = url_it->second;

      auto connections_it = websocket_connections_.find(url);
      if (connections_it != websocket_connections_.end()) {
        auto& connections = connections_it->second;
        connections.erase(
            std::remove(connections.begin(), connections.end(), conn),
            connections.end());

        if (connections.empty()) {
          websocket_connections_.erase(connections_it);
        }
      }

      connection_to_url_.erase(url_it);
    }
  }

  mutable std::recursive_mutex mutex_;
  std::unique_ptr<HttpServer> server_;
  TaskRunner* task_runner_;
  int port_;
  bool started_;
  std::string cors_origin_;

  std::map<std::string, Inspect::Handler> handlers_;
  std::map<std::string, std::string> route_descriptions_;
  std::map<std::string, std::vector<HttpServerConnection*>>
      websocket_connections_;
  std::map<HttpServerConnection*, std::string> connection_to_url_;
};

// Singleton management class
class InspectSingleton {
 public:
  static InspectSingleton& Instance() {
    static InspectSingleton instance;
    return instance;
  }

  Inspect& Create(TaskRunner* task_runner, int port) {
    std::call_once(create_flag_, [this, task_runner, port]() {
      instance_ = std::unique_ptr<Inspect>(new Inspect());
      instance_->impl_->Initialize(task_runner, port);
    });

    if (!instance_) {
      throw std::runtime_error("Failed to create Inspect instance");
    }

    return *instance_;
  }

  Inspect& Get() {
    if (!instance_) {
      throw std::runtime_error(
          "Inspect instance not created. Call Create() first.");
    }
    return *instance_;
  }

  bool IsCreated() const { return instance_ != nullptr; }

  void Reset() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (instance_) {
      instance_->Stop();
      instance_.reset();
    }
    // Note: std::once_flag cannot be reset, so subsequent Create() calls
    // will not work after Reset(). This is a limitation of the current design.
  }

 private:
  mutable std::recursive_mutex mutex_;
  std::unique_ptr<Inspect> instance_;
  std::once_flag create_flag_{};
};

// Inspect class implementation
Inspect::Inspect() : impl_(std::make_unique<Impl>()) {}

Inspect::~Inspect() {
  if (impl_) {
    impl_->Stop();
  }
}

Inspect& Inspect::Create(TaskRunner* task_runner, int port) {
  return InspectSingleton::Instance().Create(task_runner, port);
}

Inspect& Inspect::Get() { return InspectSingleton::Instance().Get(); }

void Inspect::Start() { impl_->Start(); }

void Inspect::Stop() { impl_->Stop(); }

bool Inspect::IsRunning() const { return impl_->IsRunning(); }

void Inspect::RouteWithDescription(const std::string& path,
                                   const std::string& description,
                                   Handler handler) {
  impl_->RegisterHandlerWithDescription(path, description, std::move(handler));
}

void Inspect::Static(const std::string& path, const std::string& content,
                     const std::string& content_type) {
  impl_->RegisterHandler(path,
                         [content, content_type](const Request&) -> Response {
                           return Response(content, content_type);
                         });
}

void Inspect::Unregister(const std::string& path) {
  impl_->UnregisterHandler(path);
}

bool Inspect::HasRoute(const std::string& path) const {
  return impl_->HasHandler(path);
}

size_t Inspect::Publish(const std::string& url, const std::string& message) {
  return impl_->PublishEvent(url, message);
}

size_t Inspect::Publish(const std::string& url, const base::Json& json) {
  return impl_->PublishEvent(url, json.dump());
}

Inspect::PublishResult Inspect::PublishWithResult(const std::string& url,
                                                  const std::string& message) {
  return impl_->PublishEventWithResult(url, message);
}

bool Inspect::HasSubscribers(const std::string& url) const {
  return impl_->HasEventSubscribers(url);
}

size_t Inspect::GetSubscriberCount(const std::string& url) const {
  return impl_->GetEventSubscriberCount(url);
}

Inspect::Response Inspect::JsonResponse(const base::Json& json,
                                        const std::string& status) {
  return Response(json.dump(), "application/json", status);
}

Inspect::Response Inspect::TextResponse(const std::string& text,
                                        const std::string& status) {
  return Response(text, "text/plain", status);
}

Inspect::Response Inspect::HtmlResponse(const std::string& html,
                                        const std::string& status) {
  return Response(html, "text/html", status);
}

Inspect::Response Inspect::ErrorResponse(const std::string& message,
                                         const std::string& status) {
  base::Json error;
  error["error"] = message;
  error["status"] = status;
  return Response(error.dump(), "application/json", status);
}

Inspect::Response Inspect::NotFoundResponse(const std::string& message) {
  std::string msg = message.empty() ? "Not Found" : message;
  base::Json error;
  error["error"] = msg;
  error["status"] = "404 Not Found";
  return Response(error.dump(), "application/json", "404 Not Found");
}

base::Json Inspect::GetServerInfo() const { return impl_->GetServerInfo(); }

void Inspect::SetCORS(const std::string& allow_origin) {
  impl_->SetCORS(allow_origin);
}

std::vector<std::string> Inspect::GetRoutes() const {
  return impl_->GetHandlerPaths();
}

std::vector<std::string> Inspect::GetWebSocketUrls() const {
  return impl_->GetWebSocketUrls();
}

}  // namespace base
