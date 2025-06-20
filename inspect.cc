#include "inspect.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "string_utils.h"
#include "string_view.h"
#include "task_runner.h"

namespace {
namespace fs = std::filesystem;
std::string formatSeconds(uint64_t seconds) {
  uint64_t days = seconds / 86400;
  seconds %= 86400;
  uint64_t hours = seconds / 3600;
  seconds %= 3600;
  uint64_t minutes = seconds / 60;
  seconds %= 60;

  std::ostringstream oss;
  if (days > 0) oss << days << "d ";
  if (hours > 0) oss << hours << "h ";
  if (minutes > 0) oss << minutes << "m ";
  oss << seconds << "s";
  return oss.str();
}
std::map<std::string, std::string> getProcessStatusMap() {
  std::map<std::string, std::string> result;
  long clk_tck = sysconf(_SC_CLK_TCK);

  // uptime
  double uptime_seconds = 0.0;
  {
    std::ifstream uptime_file("/proc/uptime");
    uptime_file >> uptime_seconds;
  }

  // /proc/self/stat
  {
    std::ifstream stat_file("/proc/self/stat");
    std::string line;
    std::getline(stat_file, line);
    std::istringstream iss(line);
    std::vector<std::string> stats;
    std::string token;
    while (iss >> token) stats.push_back(token);

    long start_time_ticks = std::stol(stats[21]);
    double start_time_sec_ago =
        uptime_seconds - (start_time_ticks / static_cast<double>(clk_tck));
    result["start_time"] =
        formatSeconds(static_cast<uint64_t>(start_time_sec_ago));

    long rss_pages = std::stol(stats[23]);
    size_t rss_kb = rss_pages * sysconf(_SC_PAGESIZE) / 1024;
    result["memory_rss_kb"] = std::to_string(rss_kb);
    result["thread_count"] = stats[19];
  }

  try {
    size_t fd_count = std::distance(fs::directory_iterator("/proc/self/fd"),
                                    fs::directory_iterator{});
    result["fd_count"] = std::to_string(fd_count);
  } catch (...) {
    result["fd_count"] = "error";
  }

  {
    std::ifstream io_file("/proc/self/io");
    std::string line;
    while (std::getline(io_file, line)) {
      if (line.find("read_bytes:") == 0) {
        std::istringstream iss(line.substr(11));
        uint64_t read_bytes;
        iss >> read_bytes;
        result["io_read_bytes"] = std::to_string(read_bytes);
      } else if (line.find("write_bytes:") == 0) {
        std::istringstream iss(line.substr(12));
        uint64_t write_bytes;
        iss >> write_bytes;
        result["io_write_bytes"] = std::to_string(write_bytes);
      }
    }
  }

  return result;
}

std::map<std::string, std::string> getSystemStatusMap() {
  std::map<std::string, std::string> result;
  long clk_tck = sysconf(_SC_CLK_TCK);

  // System boot time
  {
    std::ifstream stat_file("/proc/stat");
    std::string line;
    while (std::getline(stat_file, line)) {
      if (line.rfind("btime ", 0) == 0) {
        std::istringstream iss(line.substr(6));
        time_t boot_time;
        iss >> boot_time;
        time_t now = time(nullptr);
        result["boot_time"] =
            formatSeconds(static_cast<uint64_t>(now - boot_time));
        break;
      }
    }
  }

  // CPU time
  {
    std::ifstream stat_file("/proc/stat");
    std::string line;
    while (std::getline(stat_file, line)) {
      if (line.rfind("cpu ", 0) == 0) {
        std::istringstream iss(line);
        std::string label;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        iss >> label >> user >> nice >> system >> idle >> iowait >> irq >>
            softirq >> steal;
        uint64_t total_jiffies =
            user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t work_jiffies = user + nice + system + irq + softirq + steal;

        result["cpu_user_time"] = formatSeconds(user / clk_tck);
        result["cpu_system_time"] = formatSeconds(system / clk_tck);
        result["cpu_idle_time"] = formatSeconds(idle / clk_tck);
        result["cpu_total_time"] = formatSeconds(total_jiffies / clk_tck);
        result["cpu_work_time"] = formatSeconds(work_jiffies / clk_tck);
        break;
      }
    }
  }

  // Load average
  {
    std::ifstream load_file("/proc/loadavg");
    double l1, l5, l15;
    load_file >> l1 >> l5 >> l15;
    result["load_avg_1min"] = std::to_string(l1);
    result["load_avg_5min"] = std::to_string(l5);
    result["load_avg_15min"] = std::to_string(l15);
  }

  // Memory info
  {
    std::ifstream mem_file("/proc/meminfo");
    std::string line;
    size_t mem_total = 0, mem_available = 0;
    while (std::getline(mem_file, line)) {
      if (line.find("MemTotal:") == 0) {
        std::istringstream iss(line.substr(9));
        iss >> mem_total;
      } else if (line.find("MemAvailable:") == 0) {
        std::istringstream iss(line.substr(13));
        iss >> mem_available;
      }
    }
    result["memory_total_kb"] = std::to_string(mem_total);
    result["memory_available_kb"] = std::to_string(mem_available);
  }

  return result;
}
std::string Map2Text(const std::string& title,
                     const std::map<std::string, std::string>& m) {
  std::stringstream ss;
  ss << "=== " << title << " ===\n";
  for (const auto& [key, value] : m) {
    ss << key << " : " << value << "\n";
  }
  ss << "\n";
  return ss.str();
}
}  // namespace
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
    if (!started_ && task_runner_) {
      server_ = std::make_unique<HttpServer>(task_runner_, this);
      server_->Start(port_);
      started_ = true;

      // Register default index route
      RegisterHandler("/",
                      [this](const Inspect::Request& req) -> Inspect::Response {
                        if (req.query.find("json") != req.query.end()) {
                          return Inspect::JsonResponse(GetServerInfo());
                        }
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
    routes_[path].http_handler = handler;
  }

  void RegisterHandlerWithDescription(const std::string& path,
                                      const std::string& description,
                                      Inspect::Handler handler) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    routes_[path].description = description;
    routes_[path].http_handler = handler;
  }

  void UnregisterHandler(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    routes_.erase(path);
  }

  bool HasHandler(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = routes_.find(path);
    return it != routes_.end() && it->second.http_handler;
  }

  std::vector<std::string> GetHandlerPaths() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> paths;
    paths.reserve(routes_.size());
    for (const auto& pair : routes_) {
      if (pair.second.http_handler) {
        paths.push_back(pair.first);
      }
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

  void RegisterHandlersWithDescription(const std::string& path,
                                       const std::string& description,
                                       Inspect::Handler http_handler,
                                       Inspect::WebSocketHandler ws_handler) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    routes_[path].description = description;
    routes_[path].http_handler = http_handler;
    routes_[path].websocket_handler = ws_handler;
    routes_[path].supports_websocket = true;
  }

  bool HasWebSocketHandler(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = routes_.find(path);
    return it != routes_.end() && it->second.supports_websocket;
  }

  std::vector<std::string> GetWebSocketHandlerPaths() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> paths;
    for (const auto& pair : routes_) {
      if (pair.second.supports_websocket) {
        paths.push_back(pair.first);
      }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
  }

  size_t PublishEvent(const std::string& url, const std::string& message,
                      bool is_text = true) {
    return PublishEventWithResult(url, message, is_text).sent_count;
  }

  Inspect::PublishResult PublishEventWithResult(const std::string& url,
                                                const std::string& message,
                                                bool is_text = true) {
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
          if (is_text) {
            conn->SendWebsocketMessageText(message.data(), message.size());
          } else {
            conn->SendWebsocketMessage(message.data(), message.size());
          }
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
    for (const auto& pair : routes_) {
      base::Json route;
      route["path"] = pair.first;
      if (!pair.second.description.empty()) {
        route["description"] = pair.second.description;
      }
      route["supports_websocket"] = pair.second.supports_websocket;
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
    {
      base::Json proc_info;
      for (const auto& [k, v] : getProcessStatusMap()) {
        proc_info[k] = v;
      }
      info["proc"] = proc_info;
    }
    {
      base::Json sys_info;
      for (const auto& [k, v] : getSystemStatusMap()) {
        sys_info[k] = v;
      }
      info["sys"] = sys_info;
    }
    return info;
  }

  std::string ReplaceTemplateVars(const std::string& content) const {
    std::string result = content;
    // Replace {{PORT}} with actual port
    ReplaceAll(result, "{{PORT}}", std::to_string(port_));

    // Replace {{ROUTES}} with route list
    {
      std::string routes_html;
      for (const auto& pair : routes_) {
        if (pair.second.http_handler) {
          std::string item =
              R"(<li><a href="{{URL}}">{{URL}}</a> - {{DESC}}</li>)";
          ReplaceAll(item, "{{URL}}", pair.first);
          if (!pair.second.description.empty()) {
            ReplaceAll(item, "{{DESC}}", pair.second.description);
          } else {
            ReplaceAll(item, "{{DESC}}", "");
          }
          routes_html += item;
        }
      }
      ReplaceAll(result, "{{ROUTES}}", routes_html);
    }
    ReplaceAll(result, "{{PROC_INFO}}",
               Map2Text("ProcessInfo", getProcessStatusMap()));
    ReplaceAll(result, "{{SYS_INFO}}",
               Map2Text("SysInfo", getSystemStatusMap()));
    return result;
  }

  std::string GenerateIndexPage() const {
    std::string html = R"(<!DOCTYPE html>
          <html>
          <head>
          <title>Inspect Server</title>
          <meta charset="UTF-8">
          </head>
          <body>
          <h1>Inspect Server - Port {{PORT}}</h1>
          <h2>Available Routes</h2>
          <ul>{{ROUTES}}</ul>
          <h2>Server Info</h2>
          <p>Status: Running</p>
          <pre>{{PROC_INFO}}</pre>
          <pre>{{SYS_INFO}}</pre>
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Find the path for this connection
    auto conn_it = connection_to_url_.find(msg.conn);
    if (conn_it == connection_to_url_.end()) {
      return;  // Connection not found
    }

    std::string path = conn_it->second;

    // Check if there's a WebSocket handler for this path
    auto route_it = routes_.find(path);
    if (route_it != routes_.end()) {
      if (route_it->second.supports_websocket &&
          route_it->second.websocket_handler) {
        // Extract query parameters from the original path
        std::map<std::string, std::string> query = ParseQueryParams(path);

        // Create WebSocket request
        Inspect::WebSocketRequest ws_req;
        ws_req.path = ExtractPath(path);
        ws_req.query = query;
        ws_req.data = msg.data.ToStr();
        ws_req.is_text = msg.is_text;
        ws_req.connection = static_cast<void*>(msg.conn);

        // Call the handler and get response
        auto response = route_it->second.websocket_handler(ws_req);

        // Send response if handler returned data
        if (!response.content.empty()) {
          if (response.is_text) {
            msg.conn->SendWebsocketMessageText(response.content.data(),
                                               response.content.size());
          } else {
            msg.conn->SendWebsocketMessage(response.content.data(),
                                           response.content.size());
          }
        }
        return;
      }
    }
    LogW("can't be here");
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
    req.conn->UpgradeToWebsocket(req);
  }

  void HandleHttpRequest(const HttpRequest& http_req) {
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
      auto route_it = routes_.find(path);
      if (route_it != routes_.end()) {
        if (!http_req.is_websocket_handshake) {
          try {
            response = route_it->second.http_handler(req);
          } catch (const std::exception& e) {
            response = Inspect::ErrorResponse("Handler error: " +
                                              std::string(e.what()));
          }
        } else {
          if (route_it->second.supports_websocket) {
            HandleWebSocketUpgrade(http_req);
            return;
          } else {
            response = Inspect::NotFoundResponse("WebSocket not supported");
          }
        }
      } else {
        response = Inspect::NotFoundResponse("Route not found: " + path);
      }
    }

    // Send response
    std::list<Header> headers;
    headers.push_back({"Content-Type", StringView(response.content_type)});
    if (!cors_origin_.empty()) {
      headers.push_back(
          {"Access-Control-Allow-Origin", StringView(cors_origin_)});
      headers.push_back({StringView("Access-Control-Allow-Methods"),
                         StringView("GET, POST, OPTIONS")});
      headers.push_back({StringView("Access-Control-Allow-Headers"),
                         StringView("Content-Type, Authorization")});
    }
    http_req.conn->SendResponse(response.status.c_str(), headers,
                                StringView(response.content));
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

  std::map<std::string, Inspect::RouteInfo> routes_;
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

size_t Inspect::Publish(const std::string& url, const std::string& message,
                        bool is_text) {
  return impl_->PublishEvent(url, message, is_text);
}

size_t Inspect::Publish(const std::string& url, const base::Json& json) {
  return impl_->PublishEvent(url, json.dump(), true);
}

Inspect::PublishResult Inspect::PublishWithResult(const std::string& url,
                                                  const std::string& message,
                                                  bool is_text) {
  return impl_->PublishEventWithResult(url, message, is_text);
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

void Inspect::RouteWithHandlers(const std::string& path,
                                const std::string& description,
                                Handler http_handler,
                                WebSocketHandler ws_handler) {
  impl_->RegisterHandlersWithDescription(path, description, http_handler,
                                         ws_handler);
}

bool Inspect::HasWebSocketRoute(const std::string& path) const {
  return impl_->HasWebSocketHandler(path);
}

std::vector<std::string> Inspect::GetWebSocketRoutes() const {
  return impl_->GetWebSocketHandlerPaths();
}

}  // namespace base
