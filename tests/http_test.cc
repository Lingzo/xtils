#include "xtils/net/http_router.h"
#include "xtils/net/http_server.h"
#include "xtils/tasks/thread_task_runner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "xtils/net/tcp_client.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// Simple synchronous HTTP request/response via raw TCP.
// We use TcpClient to send an HTTP request and read the response.
struct SimpleHttpResponse {
  int status_code = 0;
  std::string status_line;
  std::string body;
  std::string raw;
};

class RawHttpClient : public TcpClientEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;
  std::atomic<bool> connect_done{false};
  std::atomic<bool> connect_ok{false};
  std::atomic<bool> response_done{false};
  std::atomic<bool> disconnected{false};
  std::string recv_buf;

  void OnConnected(bool success) override {
    std::lock_guard<std::mutex> lock(mu);
    connect_ok = success;
    connect_done = true;
    cv.notify_all();
  }

  void OnDataReceived(const void* data, size_t len) override {
    std::lock_guard<std::mutex> lock(mu);
    recv_buf.append(static_cast<const char*>(data), len);
    // Check if we have a complete HTTP response
    // Look for Content-Length to know when body is complete
    auto hdr_end = recv_buf.find("\r\n\r\n");
    if (hdr_end != std::string::npos) {
      std::string headers = recv_buf.substr(0, hdr_end);
      size_t body_start = hdr_end + 4;
      auto cl_pos = headers.find("Content-Length: ");
      if (cl_pos != std::string::npos) {
        size_t cl_val_start = cl_pos + 16;
        size_t cl_val_end = headers.find("\r\n", cl_val_start);
        std::string cl_str = headers.substr(
            cl_val_start,
            cl_val_end == std::string::npos ? std::string::npos
                                            : cl_val_end - cl_val_start);
        size_t content_length = std::stoul(cl_str);
        if (recv_buf.size() >= body_start + content_length) {
          response_done = true;
          cv.notify_all();
        }
      } else {
        // No content length, might be zero-length body (204, etc.)
        // or Connection: close
        response_done = true;
        cv.notify_all();
      }
    }
  }

  void OnDisconnected() override {
    std::lock_guard<std::mutex> lock(mu);
    disconnected = true;
    response_done = true;
    cv.notify_all();
  }

  SimpleHttpResponse ParseResponse() {
    SimpleHttpResponse resp;
    resp.raw = recv_buf;
    auto hdr_end = recv_buf.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return resp;

    // Parse status line
    auto first_line_end = recv_buf.find("\r\n");
    if (first_line_end != std::string::npos) {
      resp.status_line = recv_buf.substr(0, first_line_end);
      // Parse "HTTP/1.1 200 OK"
      auto space1 = resp.status_line.find(' ');
      if (space1 != std::string::npos) {
        auto space2 = resp.status_line.find(' ', space1 + 1);
        std::string code_str = resp.status_line.substr(
            space1 + 1,
            space2 == std::string::npos ? std::string::npos
                                        : space2 - space1 - 1);
        try {
          resp.status_code = std::stoi(code_str);
        } catch (...) {
        }
      }
    }

    resp.body = recv_buf.substr(hdr_end + 4);
    return resp;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mu);
    connect_done = false;
    connect_ok = false;
    response_done = false;
    disconnected = false;
    recv_buf.clear();
  }
};

// Helper: send HTTP request and get response
static SimpleHttpResponse DoHttpRequest(ThreadTaskRunner& tr, uint16_t port,
                                        const std::string& request_text,
                                        int timeout_ms = 3000) {
  RawHttpClient listener;
  TcpClient client(&tr, &listener);

  if (!client.Connect("127.0.0.1", port)) {
    return {};
  }

  // Wait for connection
  {
    std::unique_lock<std::mutex> lock(listener.mu);
    listener.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [&] { return listener.connect_done.load(); });
  }
  if (!listener.connect_ok) return {};

  // Send HTTP request
  client.SendString(request_text);

  // Wait for response
  {
    std::unique_lock<std::mutex> lock(listener.mu);
    listener.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [&] { return listener.response_done.load(); });
  }

  auto resp = listener.ParseResponse();
  client.Disconnect();
  return resp;
}

// Helper: find available port
static uint16_t FindPort(uint16_t base) {
  for (uint16_t p = base; p < base + 100; ++p) {
    auto raw =
        UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
    if (!raw) continue;
    if (raw.Bind("127.0.0.1:" + std::to_string(p))) return p;
  }
  return 0;
}

// --- Tests ---

TEST_CASE("HTTP: GET route returns JSON") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_test");
  uint16_t port = FindPort(19700);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Get("/api/hello",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                resp.Json(R"({"message":"hello"})");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto resp = DoHttpRequest(
      tr, port,
      "GET /api/hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 200);
  CHECK(resp.body.find("\"message\"") != std::string::npos);
  CHECK(resp.body.find("hello") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: POST route echoes body") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_post");
  uint16_t port = FindPort(19710);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Post("/api/echo",
               [](const HttpRequestContext& ctx, HttpResponse& resp) {
                 std::string body = ctx.GetBody();
                 resp.Text(body);
               });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string body = "hello post body";
  std::string req = "POST /api/echo HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n"
                    "Connection: close\r\n"
                    "\r\n" +
                    body;

  auto resp = DoHttpRequest(tr, port, req);
  CHECK(resp.status_code == 200);
  CHECK(resp.body == body);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: route with path parameter") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_param");
  uint16_t port = FindPort(19720);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Get("/api/users/{id}",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                std::string id = ctx.GetParam("id");
                resp.Json(R"({"id":")" + id + R"("})");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto resp = DoHttpRequest(
      tr, port,
      "GET /api/users/42 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 200);
  CHECK(resp.body.find("\"id\"") != std::string::npos);
  CHECK(resp.body.find("42") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: query parameters") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_query");
  uint16_t port = FindPort(19730);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Get("/api/search",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                std::string q = ctx.GetQuery("q");
                resp.Json(R"({"query":")" + q + R"("})");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto resp = DoHttpRequest(
      tr, port,
      "GET /api/search?q=test HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 200);
  CHECK(resp.body.find("test") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: 404 for unknown route") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_404");
  uint16_t port = FindPort(19740);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Get("/api/exists",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                resp.Text("ok");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto resp = DoHttpRequest(
      tr, port,
      "GET /api/nonexistent HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 404);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: CORS preflight") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_cors");
  uint16_t port = FindPort(19750);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Get("/api/data",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                resp.Text("data");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  server.AddAllowedOrigin("*");
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // OPTIONS is handled by HttpServer itself as CORS preflight
  auto resp = DoHttpRequest(
      tr, port,
      "OPTIONS /api/data HTTP/1.1\r\nHost: localhost\r\n"
      "Origin: http://example.com\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 204);
  CHECK(resp.raw.find("Access-Control-Allow-Methods") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: POST JSON content-type detection") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_json");
  uint16_t port = FindPort(19760);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();
  router->Post("/api/json",
               [](const HttpRequestContext& ctx, HttpResponse& resp) {
                 if (ctx.IsJson()) {
                   resp.Json(R"({"received":true})");
                 } else {
                   resp.Status(400).Text("not json");
                 }
               });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string body = R"({"key":"value"})";
  std::string req = "POST /api/json HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n"
                    "Connection: close\r\n"
                    "\r\n" +
                    body;

  auto resp = DoHttpRequest(tr, port, req);
  CHECK(resp.status_code == 200);
  CHECK(resp.body.find("\"received\"") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST_CASE("HTTP: middleware") {
  auto tr = ThreadTaskRunner::CreateAndStart("http_mw");
  uint16_t port = FindPort(19770);
  REQUIRE(port != 0);

  auto router = std::make_unique<HttpRouter>();

  // Add middleware that adds a header
  router->Use([](const HttpRequestContext& ctx, HttpResponse& resp) -> bool {
    resp.Header("X-Middleware", "applied");
    return true;  // Continue to route
  });

  router->Get("/api/test",
              [](const HttpRequestContext& ctx, HttpResponse& resp) {
                resp.Text("ok");
              });

  auto handler =
      std::make_unique<RouterHttpRequestHandler>(std::move(router));
  HttpServer server(&tr, handler.get());
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto resp = DoHttpRequest(
      tr, port,
      "GET /api/test HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");

  CHECK(resp.status_code == 200);
  CHECK(resp.raw.find("X-Middleware") != std::string::npos);

  tr.PostTask([&server]() { server.Stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

