#include "xtils/net/udp_client.h"
#include "xtils/net/udp_server.h"
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

template <typename Pred>
bool WaitUntil(std::mutex& m, std::condition_variable& cv, Pred pred,
               int ms = 2000) {
  std::unique_lock<std::mutex> lock(m);
  return cv.wait_for(lock, std::chrono::milliseconds(ms), pred);
}

// --- Server listener ---

class TestUdpServer : public UdpServerEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;

  std::vector<std::pair<std::string, std::string>> received;  // addr, data
  std::string last_client_addr;
  std::atomic<int> new_client_count{0};
  UdpServer* server = nullptr;

  void OnDataReceived(const std::string& client_addr, const void* data,
                      size_t len) override {
    std::string msg(static_cast<const char*>(data), len);
    {
      std::lock_guard<std::mutex> lock(mu);
      received.emplace_back(client_addr, msg);
      last_client_addr = client_addr;
    }
    // Echo with prefix
    if (server) {
      server->SendStringTo(client_addr, "ECHO:" + msg);
    }
    cv.notify_all();
  }

  void OnNewClient(const std::string& client_addr) override {
    new_client_count++;
    cv.notify_all();
  }
};

// --- Client listener ---

class TestUdpClient : public UdpClientEventListener {
 public:
  std::mutex mu;
  std::condition_variable cv;

  std::string last_data;
  std::vector<std::string> all_data;
  std::atomic<bool> ready{false};

  void OnDataReceived(UdpClient* client, const void* data,
                      size_t len) override {
    std::string msg(static_cast<const char*>(data), len);
    {
      std::lock_guard<std::mutex> lock(mu);
      last_data = msg;
      all_data.push_back(msg);
    }
    cv.notify_all();
  }

  void OnReady(UdpClient* client) override {
    ready = true;
    cv.notify_all();
  }
};

// Helper: get a bound UdpServer on an ephemeral port, return the port.
// The server binds on port 0, then we parse the address from GetBindAddress().
static uint16_t StartUdpServer(UdpServer& server, const std::string& ip) {
  // Try port 0 for ephemeral
  if (server.Start(ip, 0)) {
    std::string bind_addr = server.GetBindAddress();
    // Parse port from "ip:port"
    auto colon = bind_addr.rfind(':');
    if (colon != std::string::npos) {
      try {
        return static_cast<uint16_t>(
            std::stoul(bind_addr.substr(colon + 1)));
      } catch (...) {
      }
    }
  }
  // Fallback: try fixed ports
  for (uint16_t p = 19600; p < 19700; ++p) {
    if (server.Start(ip, p)) return p;
  }
  return 0;
}

// --- Tests ---

TEST_CASE("UDP: single client send and echo") {
  auto tr = ThreadTaskRunner::CreateAndStart("udp_test");

  TestUdpServer srv_listener;
  UdpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  uint16_t port = StartUdpServer(server, "127.0.0.1");
  REQUIRE(port != 0);
  CHECK(server.IsRunning());

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TestUdpClient cli_listener;
  UdpClient client(&tr, &cli_listener);
  REQUIRE(client.Open("127.0.0.1", 0));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Send data to server
  std::string server_addr = "127.0.0.1:" + std::to_string(port);
  client.SendStringTo(server_addr, "PING");

  // Wait for echo
  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return !cli_listener.last_data.empty(); }));
  CHECK(cli_listener.last_data == "ECHO:PING");

  // Server should have received the data
  {
    std::lock_guard<std::mutex> lock(srv_listener.mu);
    REQUIRE(srv_listener.received.size() >= 1);
    CHECK(srv_listener.received[0].second == "PING");
  }

  client.Close();
  server.Stop();
}

TEST_CASE("UDP: multiple clients") {
  auto tr = ThreadTaskRunner::CreateAndStart("udp_multi");

  TestUdpServer srv_listener;
  UdpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  uint16_t port = StartUdpServer(server, "127.0.0.1");
  REQUIRE(port != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const int N = 3;
  std::vector<std::unique_ptr<TestUdpClient>> listeners;
  std::vector<std::unique_ptr<UdpClient>> clients;

  std::string server_addr = "127.0.0.1:" + std::to_string(port);

  for (int i = 0; i < N; ++i) {
    auto l = std::make_unique<TestUdpClient>();
    auto c = std::make_unique<UdpClient>(&tr, l.get());
    REQUIRE(c->Open("127.0.0.1", 0));
    listeners.push_back(std::move(l));
    clients.push_back(std::move(c));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  // Each client sends unique message
  for (int i = 0; i < N; ++i) {
    clients[i]->SendStringTo(server_addr, "MSG" + std::to_string(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Wait for all echos
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return !listeners[i]->last_data.empty(); }));
    CHECK(listeners[i]->last_data == "ECHO:MSG" + std::to_string(i));
  }

  // Server should know about all clients
  CHECK(server.GetClientCount() == N);

  for (auto& c : clients) c->Close();
  server.Stop();
}

TEST_CASE("UDP: broadcast") {
  auto tr = ThreadTaskRunner::CreateAndStart("udp_bcast");

  TestUdpServer srv_listener;
  UdpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  uint16_t port = StartUdpServer(server, "127.0.0.1");
  REQUIRE(port != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const int N = 2;
  std::vector<std::unique_ptr<TestUdpClient>> listeners;
  std::vector<std::unique_ptr<UdpClient>> clients;

  std::string server_addr = "127.0.0.1:" + std::to_string(port);

  for (int i = 0; i < N; ++i) {
    auto l = std::make_unique<TestUdpClient>();
    auto c = std::make_unique<UdpClient>(&tr, l.get());
    REQUIRE(c->Open("127.0.0.1", 0));
    listeners.push_back(std::move(l));
    clients.push_back(std::move(c));
  }

  // Each client sends a message so the server knows about them
  for (int i = 0; i < N; ++i) {
    clients[i]->SendStringTo(server_addr, "HELLO" + std::to_string(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Wait for echos to arrive (server now knows clients)
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return !listeners[i]->last_data.empty(); }));
  }

  // Clear last data for broadcast test
  for (int i = 0; i < N; ++i) {
    std::lock_guard<std::mutex> lock(listeners[i]->mu);
    listeners[i]->last_data.clear();
  }

  // Broadcast from server
  tr.PostTask([&server]() { server.BroadcastString("BROADCAST"); });

  // Each client should receive
  for (int i = 0; i < N; ++i) {
    CHECK(WaitUntil(listeners[i]->mu, listeners[i]->cv,
                    [&] { return !listeners[i]->last_data.empty(); }));
    CHECK(listeners[i]->last_data == "BROADCAST");
  }

  for (auto& c : clients) c->Close();
  server.Stop();
}

TEST_CASE("UDP: client count and cleanup") {
  auto tr = ThreadTaskRunner::CreateAndStart("udp_cleanup");

  TestUdpServer srv_listener;
  UdpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  uint16_t port = StartUdpServer(server, "127.0.0.1");
  REQUIRE(port != 0);

  server.SetClientTimeout(100);  // 100ms timeout

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TestUdpClient cli_listener;
  UdpClient client(&tr, &cli_listener);
  REQUIRE(client.Open("127.0.0.1", 0));

  std::string server_addr = "127.0.0.1:" + std::to_string(port);
  client.SendStringTo(server_addr, "test");

  // Wait for data to be received
  CHECK(WaitUntil(cli_listener.mu, cli_listener.cv,
                  [&] { return !cli_listener.last_data.empty(); }));

  CHECK(server.GetClientCount() == 1);

  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Cleanup should remove inactive client
  tr.PostTask([&server]() { server.CleanupInactiveClients(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  CHECK(server.GetClientCount() == 0);

  client.Close();
  server.Stop();
}

TEST_CASE("UDP: server bind address") {
  auto tr = ThreadTaskRunner::CreateAndStart("udp_addr");

  TestUdpServer srv_listener;
  UdpServer server(&tr, &srv_listener);
  srv_listener.server = &server;
  uint16_t port = StartUdpServer(server, "127.0.0.1");
  REQUIRE(port != 0);

  std::string bind_addr = server.GetBindAddress();
  CHECK_FALSE(bind_addr.empty());
  CHECK(bind_addr.find("127.0.0.1") != std::string::npos);

  server.Stop();
}

