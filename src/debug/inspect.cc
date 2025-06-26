#include "xtils/debug/inspect.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "xtils/logging/logger.h"
#include "xtils/net/http_server.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/utils/file_utils.h"
#include "xtils/utils/json.h"
#include "xtils/utils/micros.h"
#include "xtils/utils/string_utils.h"
#include "xtils/utils/string_view.h"

namespace {
std::string formatSeconds(uint64_t seconds) {
  uint64_t days = seconds / 86400;
  seconds %= 86400;
  uint64_t hours = seconds / 3600;
  seconds %= 3600;
  uint64_t minutes = seconds / 60;
  seconds %= 60;

  std::stringstream ss;
  if (days > 0) ss << days << "d ";
  if (hours > 0) ss << hours << "h ";
  if (minutes > 0) ss << minutes << "m ";
  ss << seconds << "s";
  return ss.str();
}
std::map<std::string, std::string> getProcessStatusMap() {
  std::map<std::string, std::string> result;
  long clk_tck = sysconf(_SC_CLK_TCK);

  // uptime
  double uptime_seconds = 0.0;
  {
    std::string line;
    file_utils::read("/proc/uptime", &line);
    std::stringstream ss(line);
    ss >> uptime_seconds;
  }

  // /proc/self/stat
  {
    std::string line;
    file_utils::read("/proc/self/stat", &line);
    std::stringstream ss(line);
    std::vector<std::string> stats;
    std::string token;
    while (ss >> token) stats.push_back(token);

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
    size_t fd_count = file_utils::list_files("/proc/self/fd").size();
    result["fd_count"] = std::to_string(fd_count);
  } catch (...) {
    result["fd_count"] = "error";
  }

  {
    std::vector<std::string> lines;
    file_utils::read_lines("/proc/self/io", &lines);
    for (auto& line : lines) {
      if (line.find("read_bytes:") == 0) {
        std::stringstream iss(line.substr(11));
        uint64_t read_bytes;
        iss >> read_bytes;
        result["io_read_bytes"] = std::to_string(read_bytes);
      } else if (line.find("write_bytes:") == 0) {
        std::stringstream iss(line.substr(12));
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
  // system time
  auto timestamp = std::time(nullptr);
  result["sys_timestamp"] = std::to_string(timestamp);
  std::tm* local_tm = std::localtime(&timestamp);
  char buffer[64] = {0};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_tm);
  result["sys_localtime"] = buffer;
  long clk_tck = sysconf(_SC_CLK_TCK);

  // System boot time
  {
    std::vector<std::string> lines;
    file_utils::read_lines("/proc/stat", &lines);
    for (auto& line : lines) {
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

    // CPU time
    for (auto& line : lines) {
      if (line.rfind("cpu ", 0) == 0) {
        std::stringstream ss(line);
        std::string label;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        ss >> label >> user >> nice >> system >> idle >> iowait >> irq >>
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
    std::string line;
    file_utils::read("/proc/loadavg", &line);
    std::stringstream ss(line);
    double l1, l5, l15;
    ss >> l1 >> l5 >> l15;
    result["load_avg_1min"] = std::to_string(l1);
    result["load_avg_5min"] = std::to_string(l5);
    result["load_avg_15min"] = std::to_string(l15);
  }

  // Memory info
  {
    std::vector<std::string> lines;
    file_utils::read_lines("/proc/meminfo", &lines);
    size_t mem_total = 0, mem_available = 0;
    for (auto& line : lines) {
      if (line.find("MemTotal:") == 0) {
        std::stringstream ss(line.substr(9));
        ss >> mem_total;
      } else if (line.find("MemAvailable:") == 0) {
        std::stringstream ss(line.substr(13));
        ss >> mem_available;
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
    ss << key << ": " << value << "\n";
  }
  ss << "\n";
  return ss.str();
}
}  // namespace
namespace xtils {

// Private implementation class
class Impl : public HttpRequestHandler {
 public:
  Impl() {}

  ~Impl() { Stop(); }

  void Init(TaskRunner* task_runner, const std::string& ip, int port) {
    task_runner_ = task_runner;
    port_ = port;
    ip_ = ip;
    if (!started_ && task_runner_) {
      server_ = std::make_unique<HttpServer>(task_runner_, this);
      started_ = server_->Start(ip, port);

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
      websocket_connections_.clear();
      connection_to_url_.clear();
      started_ = false;
      server_.reset();
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

  xtils::Json GetServerInfo() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    xtils::Json info;
    info["port"] = port_;
    info["ip"] = ip_;
    info["running"] = IsRunning();
    xtils::Json::array_t routes_array;
    for (const auto& pair : routes_) {
      xtils::Json route;
      route["path"] = pair.first;
      if (!pair.second.description.empty()) {
        route["description"] = pair.second.description;
      }
      route["supports_websocket"] = pair.second.supports_websocket;
      routes_array.push_back(route);
    }
    info["routes"] = xtils::Json(routes_array);

    xtils::Json::array_t ws_array;
    for (const auto& pair : websocket_connections_) {
      xtils::Json ws;
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
    info["websockets"] = xtils::Json(ws_array);
    {
      xtils::Json proc_info;
      for (const auto& [k, v] : getProcessStatusMap()) {
        proc_info[k] = v;
      }
      info["proc"] = proc_info;
    }
    {
      xtils::Json sys_info;
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
              R"(<li><a href="{{URL}}">{{URL}}</a> {{DESC}}</li>)";
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
          <meta http-equiv="refresh" content="10">
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
  std::string ip_;
  bool started_;
  std::string cors_origin_;

  std::map<std::string, Inspect::RouteInfo> routes_;
  std::map<std::string, std::vector<HttpServerConnection*>>
      websocket_connections_;
  std::map<HttpServerConnection*, std::string> connection_to_url_;
};

static Impl* impl_ = nullptr;
// Inspect class implementation
Inspect::Inspect() { impl_ = new Impl(); }

Inspect::~Inspect() { SAFE_DELETE_OBJ(impl_); }

void Inspect::Init(TaskRunner* task_runner, const std::string& ip, int port) {
  if (impl_) impl_->Init(task_runner, ip, port);
}

Inspect& Inspect::Get() {
  static Inspect ins;
  return ins;
}

void Inspect::Stop() {
  if (impl_) impl_->Stop();
}

bool Inspect::IsRunning() const {
  CHECK(impl_ != nullptr);
  return impl_->IsRunning();
}

void Inspect::RouteWithDescription(const std::string& path,
                                   const std::string& description,
                                   Handler handler) {
  CHECK(impl_ != nullptr);
  impl_->RegisterHandlerWithDescription(path, description, std::move(handler));
}

void Inspect::Static(const std::string& path, const std::string& content,
                     const std::string& content_type) {
  CHECK(impl_ != nullptr);
  impl_->RegisterHandler(path,
                         [content, content_type](const Request&) -> Response {
                           return Response(content, content_type);
                         });
}

void Inspect::Unregister(const std::string& path) {
  CHECK(impl_ != nullptr);
  impl_->UnregisterHandler(path);
}

bool Inspect::HasRoute(const std::string& path) const {
  CHECK(impl_ != nullptr);
  return impl_->HasHandler(path);
}

size_t Inspect::Publish(const std::string& url, const std::string& message,
                        bool is_text) {
  CHECK(impl_ != nullptr);
  return impl_->PublishEvent(url, message, is_text);
}

size_t Inspect::Publish(const std::string& url, const xtils::Json& json) {
  CHECK(impl_ != nullptr);
  return impl_->PublishEvent(url, json.dump(), true);
}

Inspect::PublishResult Inspect::PublishWithResult(const std::string& url,
                                                  const std::string& message,
                                                  bool is_text) {
  CHECK(impl_ != nullptr);
  return impl_->PublishEventWithResult(url, message, is_text);
}

bool Inspect::HasSubscribers(const std::string& url) const {
  CHECK(impl_ != nullptr);
  return impl_->HasEventSubscribers(url);
}

size_t Inspect::GetSubscriberCount(const std::string& url) const {
  CHECK(impl_ != nullptr);
  return impl_->GetEventSubscriberCount(url);
}

Inspect::Response Inspect::JsonResponse(const xtils::Json& json,
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
  xtils::Json error;
  error["error"] = message;
  error["status"] = status;
  return Response(error.dump(), "application/json", status);
}

Inspect::Response Inspect::NotFoundResponse(const std::string& message) {
  std::string msg = message.empty() ? "Not Found" : message;
  xtils::Json error;
  error["error"] = msg;
  error["status"] = "404 Not Found";
  return Response(error.dump(), "application/json", "404 Not Found");
}

xtils::Json Inspect::GetServerInfo() const { return impl_->GetServerInfo(); }

void Inspect::SetCORS(const std::string& allow_origin) {
  CHECK(impl_ != nullptr);
  impl_->SetCORS(allow_origin);
}

std::vector<std::string> Inspect::GetRoutes() const {
  CHECK(impl_ != nullptr);
  return impl_->GetHandlerPaths();
}

void Inspect::RouteWithHandlers(const std::string& path,
                                const std::string& description,
                                Handler http_handler,
                                WebSocketHandler ws_handler) {
  CHECK(impl_ != nullptr);
  impl_->RegisterHandlersWithDescription(path, description, http_handler,
                                         ws_handler);
}

bool Inspect::HasWebSocketRoute(const std::string& path) const {
  CHECK(impl_ != nullptr);
  return impl_->HasWebSocketHandler(path);
}

std::vector<std::string> Inspect::GetWebSocketRoutes() const {
  CHECK(impl_ != nullptr);
  return impl_->GetWebSocketHandlerPaths();
}

}  // namespace xtils
