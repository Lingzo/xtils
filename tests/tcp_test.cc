#include "xtils/net/tcp_client.h"
#include "xtils/net/tcp_server.h"
#include "xtils/tasks/thread_task_runner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// Helper: wait on a condition variable with timeout
template <typename Pred>
bool WaitUntil(std::mutex& m, std::condition_variable& cv, Pred pred,
               int ms = 2000) {
  std::unique_lock<std::mutex> lock(m);
  return cv.wait_for(lock, std::chrono::milliseconds(ms), pred);
}

// Helper: find a port that works for binding, starting from base_port
static uint16_t FindPort(ThreadTaskRunner& tr, uint16_t base_port) {
  for (uint16_t p = base_port; p < base_port + 100; ++p) {
    // Quick check by creating a raw socket
    auto raw =
        UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
    if (!raw) continue;
    std::string addr = "127.0.0.1:" + std::to_string(p);
    if (raw.Bind(addr)) {
      return p;
    }
  }
  return 0;
}

// --- Server listener ---

class TestServer : public TcpServerEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;

  std::atomic<int> connected_count{0};
  std::atomic<int> disconnected_count{0};
  std::string last_data;
  std::vector<std::string> all_data;
  TcpServerConnection* last_conn = nullptr;
  TcpServer* server = nullptr;

  void OnClientConnected(TcpServerConnection* conn) override {
    std::lock_guard<std::mutex> lock(mu);
    connected_count++;
    last_conn = conn;
    cv.notify_all();
  }

  void OnDataReceived(TcpServerConnection* conn, const void* data,
                      size_t len) override {
    std::string msg(static_cast<const char*>(data), len);
    {
      std::lock_guard<std::mutex> lock(mu);
      last_data = msg;
      all_data.push_back(msg);
    }
    // Echo with prefix
    conn->SendString("ECHO:" + msg);
    cv.notify_all();
  }

  void OnClientDisconnected(TcpServerConnection* conn) override {
    std::lock_guard<std::mutex> lock(mu);
    disconnected_count++;
    cv.notify_all();
  }
};

// --- Client listener ---

class TestClient : public TcpClientEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;

  std::atomic<bool> connect_result{false};
  std::atomic<bool> connect_done{false};
  std::atomic<bool> disconnected{false};
  std::string last_data;
  std::vector<std::string> all_data;

  void OnConnected(bool success) override {
    std::lock_guard<std::mutex> lock(mu);
    connect_result = success;
    connect_done = true;
    cv.notify_all();
  }

  void OnDataReceived(const void* data, size_t len) override {
    std::string msg(static_cast<const char*>(data), len);
    {
      std::lock_guard<std::mutex> lock(mu);
      last_data = msg;
      all_data.push_back(msg);
    }
    cv.notify_all();
  }

  void OnDisconnected() override {
    std::lock_guard<std::mutex> lock(mu);
    disconnected = true;
    cv.notify_all();
  }
};

// --- Tests ---

TEST_CASE("TCP: single client connect, send, echo") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_test");
  uint16_t port = FindPort(tr, 19100);
  REQUIRE(port != 0);

  TestServer srv_listener;
  TcpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  REQUIRE(server.Start("127.0.0.1", port));
  CHECK(server.IsRunning());

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TestClient cli_listener;
  TcpClient client(&tr, &cli_listener);
  REQUIRE(client.Connect("127.0.0.1", port));

  // Wait for connection
  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return cli_listener.connect_done.load(); }));
  CHECK(cli_listener.connect_result);
  CHECK(client.IsConnected());

  // Send data
  client.SendString("hello");

  // Wait for echo
  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return !cli_listener.last_data.empty(); }));
  CHECK(cli_listener.last_data == "ECHO:hello");

  client.Disconnect();
  server.Stop();
}

TEST_CASE("TCP: multiple connections and connection count") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_multi");
  uint16_t port = FindPort(tr, 19200);
  REQUIRE(port != 0);

  TestServer srv_listener;
  TcpServer server(&tr, &srv_listener);
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const int N = 3;
  std::vector<std::unique_ptr<TestClient>> listeners;
  std::vector<std::unique_ptr<TcpClient>> clients;

  for (int i = 0; i < N; ++i) {
    auto l = std::make_unique<TestClient>();
    auto c = std::make_unique<TcpClient>(&tr, l.get());
    REQUIRE(c->Connect("127.0.0.1", port));
    listeners.push_back(std::move(l));
    clients.push_back(std::move(c));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Wait for all connections
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return listeners[i]->connect_done.load(); }));
    CHECK(listeners[i]->connect_result);
  }

  // Give server a moment to process all accepts
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(server.GetConnectionCount() == N);

  // Each client sends unique message
  for (int i = 0; i < N; ++i) {
    clients[i]->SendString("msg" + std::to_string(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Wait for each client to receive echo
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return !listeners[i]->last_data.empty(); }));
    CHECK(listeners[i]->last_data == "ECHO:msg" + std::to_string(i));
  }

  // Cleanup: disconnect first, then destroy clients before listeners
  for (auto& c : clients) c->Disconnect();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  clients.clear();
  listeners.clear();
}

TEST_CASE("TCP: broadcast") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_bcast");
  uint16_t port = FindPort(tr, 19300);
  REQUIRE(port != 0);

  TestServer srv_listener;
  TcpServer server(&tr, &srv_listener);
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const int N = 3;
  std::vector<std::unique_ptr<TestClient>> listeners;
  std::vector<std::unique_ptr<TcpClient>> clients;

  for (int i = 0; i < N; ++i) {
    auto l = std::make_unique<TestClient>();
    auto c = std::make_unique<TcpClient>(&tr, l.get());
    REQUIRE(c->Connect("127.0.0.1", port));
    listeners.push_back(std::move(l));
    clients.push_back(std::move(c));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Wait for all to connect
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return listeners[i]->connect_done.load(); }));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Broadcast
  // We need to run broadcast on the task runner thread
  tr.PostTask([&server]() { server.BroadcastString("BCAST_MSG"); });

  // Each client should receive
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return !listeners[i]->last_data.empty(); }));
    CHECK(listeners[i]->last_data == "BCAST_MSG");
  }

  // Cleanup: disconnect clients first, then destroy them before listeners
  for (auto& c : clients) c->Disconnect();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server.Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  clients.clear();
  listeners.clear();
}

TEST_CASE("TCP: client disconnect notification") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_disconn");
  uint16_t port = FindPort(tr, 19400);
  REQUIRE(port != 0);

  TestServer srv_listener;
  TcpServer server(&tr, &srv_listener);
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TestClient cli_listener;
  TcpClient client(&tr, &cli_listener);
  REQUIRE(client.Connect("127.0.0.1", port));

  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return cli_listener.connect_done.load(); }));
  CHECK(cli_listener.connect_result);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(server.GetConnectionCount() == 1);

  // Disconnect client
  client.Disconnect();

  // Wait for server to detect disconnect
  CHECK(WaitUntil(srv_listener.mu, srv_listener.cv,
                  [&] { return srv_listener.disconnected_count.load() >= 1; }));

  // Give server a moment to clean up the connection list
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(server.GetConnectionCount() == 0);

  server.Stop();
}

TEST_CASE("TCP: server stop disconnects clients") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_stop");
  uint16_t port = FindPort(tr, 19500);
  REQUIRE(port != 0);

  TestServer srv_listener;
  TcpServer server(&tr, &srv_listener);
  REQUIRE(server.Start("127.0.0.1", port));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TestClient cli_listener;
  TcpClient client(&tr, &cli_listener);
  REQUIRE(client.Connect("127.0.0.1", port));

  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return cli_listener.connect_done.load(); }));

  // Stop server from task runner thread
  tr.PostTask([&server]() { server.Stop(); });

  // Client should get disconnected
  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return cli_listener.disconnected.load(); }));

  client.Disconnect();
}

TEST_CASE("TCP: connect to non-listening port fails") {
  auto tr = ThreadTaskRunner::CreateAndStart("tcp_fail");

  TestClient cli_listener;
  TcpClient client(&tr, &cli_listener);

  // Port 19999 should not be listening
  client.SetConnectTimeout(1000);
  client.Connect("127.0.0.1", 19999);

  // Should get OnConnected(false) or OnDisconnected
  bool got_failure = WaitUntil(
      cli_listener.mu, cli_listener.cv,
      [&] {
        return (cli_listener.connect_done.load() &&
                !cli_listener.connect_result.load()) ||
               cli_listener.disconnected.load();
      },
      3000);
  CHECK(got_failure);

  client.Disconnect();
}

