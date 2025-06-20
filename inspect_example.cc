#include <atomic>
#include <ctime>
#include <thread>

#include "inspect.h"
#include "json.h"
#include "logger.h"
#include "thread_task_runner.h"

using namespace base;

// Global counter for demonstration
static std::atomic<int> global_counter{0};

// Example handler functions
Inspect::Response HandleHello(const Inspect::Request& req) {
  base::Json response;
  response["message"] = "Hello, World!";
  response["path"] = req.path;
  response["method"] = req.method;

  // Include query parameters if any
  if (!req.query.empty()) {
    base::Json query_json;
    for (const auto& [key, value] : req.query) {
      query_json[key] = value;
    }
    response["query"] = query_json;
  }

  return Inspect::JsonResponse(response);
}

Inspect::Response HandleUserInfo(const Inspect::Request& req) {
  // Extract user ID from query parameters
  auto it = req.query.find("id");
  if (it == req.query.end()) {
    return Inspect::ErrorResponse("Missing 'id' parameter", "400 Bad Request");
  }

  std::string user_id = it->second;

  base::Json user_info;
  user_info["id"] = user_id;
  user_info["name"] = "User " + user_id;
  user_info["email"] = "user" + user_id + "@example.com";
  user_info["active"] = true;

  return Inspect::JsonResponse(user_info);
}

Inspect::Response HandleStatus(const Inspect::Request& req) {
  base::Json status;
  status["server"] = "running";
  status["timestamp"] = std::time(nullptr);
  status["version"] = "1.0.0";
  status["global_counter"] = global_counter.load();

  // Get server info from Inspect
  status["inspect_info"] = Inspect::Get().GetServerInfo();

  return Inspect::JsonResponse(status);
}

Inspect::Response HandleEcho(const Inspect::Request& req) {
  if (req.method == "POST") {
    base::Json echo_response;
    echo_response["echo"] = req.body;
    echo_response["content_length"] = req.body.length();
    return Inspect::JsonResponse(echo_response);
  } else {
    return Inspect::ErrorResponse("Only POST method supported",
                                  "405 Method Not Allowed");
  }
}

int main() {
  // Create task runner
  auto task_runner = ThreadTaskRunner::CreateAndStart("example");

  // Create and configure Inspect server
  auto& inspect = Inspect::Create(&task_runner, 8080);

  // Register routes using the new INSPECT_ROUTE macro
  INSPECT_ROUTE("/api/hello", "Returns a hello world message with request info",
                HandleHello);

  INSPECT_ROUTE("/api/user",
                "Get user information by ID (requires ?id=<user_id> parameter)",
                HandleUserInfo);

  INSPECT_ROUTE("/api/status", "Get server status and information",
                HandleStatus);

  INSPECT_ROUTE("/api/echo", "Echo back the request body (POST only)",
                HandleEcho);

  // Register a simple lambda route
  INSPECT_ROUTE("/api/time", "Get current timestamp",
                [](const Inspect::Request& req) {
                  base::Json time_response;
                  time_response["timestamp"] = std::time(nullptr);
                  time_response["iso_time"] = "2024-01-01T00:00:00Z";
                  return Inspect::JsonResponse(time_response);
                });

  // Counter API for WebSocket demonstration
  INSPECT_ROUTE("/api/counter", "Get or increment global counter",
                [](const Inspect::Request& req) {
                  if (req.method == "GET") {
                    base::Json response;
                    response["counter"] = global_counter.load();
                    return Inspect::JsonResponse(response);
                  } else if (req.method == "POST") {
                    int new_value = ++global_counter;
                    base::Json response;
                    response["counter"] = new_value;
                    response["message"] = "Counter incremented";

                    // Publish update via WebSocket
                    base::Json ws_message;
                    ws_message["type"] = "counter_update";
                    ws_message["counter"] = new_value;

                    return Inspect::JsonResponse(response);
                  }
                  return Inspect::ErrorResponse("Method not allowed",
                                                "405 Method Not Allowed");
                });

  INSPECT_DUAL_ROUTE(
      "/ping", "支持http和ws",
      [](const Inspect::Request& req) { return Inspect::TextResponse("pong"); },
      [](const Inspect::WebSocketRequest& req) { return Inspect::Response(req.data,req.is_text); });

  // Add a simple demo page
  std::string demo_html = R"(<!DOCTYPE html>
<html>
<head>
    <title>Inspect Demo</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .endpoint { background: #f5f5f5; padding: 10px; margin: 10px 0; border-radius: 5px; }
        .method { font-weight: bold; color: #007acc; }
        button { padding: 8px 16px; margin: 5px; background: #007acc; color: white; border: none; border-radius: 4px; }
    </style>
</head>
<body>
    <h1>Inspect Server Demo</h1>
    <p>Optimized Inspect server with enhanced features.</p>

    <h2>Available Endpoints:</h2>
    <div class="endpoint">
        <span class="method">GET</span> /api/hello - Hello world with request info
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/user?id=123 - Get user information
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/status - Server status and metrics
    </div>
    <div class="endpoint">
        <span class="method">GET/POST</span> /api/counter - Counter operations
    </div>
    <div class="endpoint">
        <span class="method">POST</span> /api/echo - Echo request body
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /api/time - Current timestamp
    </div>
    <div class="endpoint">
        <span class="method">GET</span> /ping - Simple ping/pong
    </div>

    <h2>Features Demonstrated:</h2>
    <ul>
        <li>Thread-safe singleton pattern</li>
        <li>INSPECT_ROUTE macros for easy registration</li>
        <li>Real-time WebSocket publishing</li>
        <li>Enhanced error handling and logging</li>
        <li>Auto-generated API documentation</li>
    </ul>
</body>
</html>)";

  INSPECT_STATIC("/demo", demo_html, "text/html");

  // Enable CORS for API endpoints
  inspect.SetCORS("*");

  // Start the server
  LogI("Starting optimized Inspect server on port 8080...");
  LogI("Visit http://localhost:8080/ for API documentation");
  LogI("Visit http://localhost:8080/demo for interactive demo");

  // Background thread for periodic WebSocket messages and stats
  std::thread background_thread([&inspect]() {
    int heartbeat_count = 0;

    while (inspect.IsRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(30));

      // Send heartbeat to any connected WebSocket clients
      if (inspect.HasSubscribers("/ping")) {
        base::Json heartbeat;
        heartbeat["type"] = "heartbeat";
        heartbeat["timestamp"] = std::time(nullptr);
        heartbeat["counter"] = global_counter.load();
        heartbeat["heartbeat_count"] = ++heartbeat_count;

        auto result = inspect.PublishWithResult("/ping", heartbeat.dump());
        if (result.sent_count > 0) {
          LogI("Heartbeat sent to %zu WebSocket subscribers",
               result.sent_count);
        }
      }

      // Log server statistics
      auto server_info = inspect.GetServerInfo();
      LogI("Server stats - Routes: %ld, WebSocket connections: %ld",
           server_info["handlers_count"].size(),
           server_info["total_websocket_connections"].size());
    }
  });

  // Keep the server running
  LogI("=== Optimized Inspect Server Running ===");
  LogI("Features demonstrated:");
  LogI("  - Thread-safe singleton pattern");
  LogI("  - INSPECT_ROUTE macros for easy registration");
  LogI("  - Real-time WebSocket publishing");
  LogI("  - Enhanced error handling and logging");
  LogI("  - Auto-generated API documentation");
  LogI("Press Ctrl+C to stop the server");

  // Simple event loop with graceful shutdown handling
  try {
    while (inspect.IsRunning()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Simulate some activity every 60 seconds
      static int activity_counter = 0;
      if (++activity_counter % 60 == 0) {
        LogI("Server uptime: %d minutes, global counter: %d",
             activity_counter / 60, global_counter.load());
      }
    }
  } catch (const std::exception& e) {
    LogE("Server error: %s", e.what());
  }

  LogI("Shutting down background thread...");
  background_thread.join();

  LogI("Inspect server example completed");
  return 0;
}
