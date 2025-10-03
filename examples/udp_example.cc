#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/udp_client.h"
#include "xtils/net/udp_server.h"
#include "xtils/system/signal_handler.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;
using namespace std::chrono_literals;

// Test server listener implementation
class TestUdpServerListener : public UdpServerEventListener {
 public:
  TestUdpServerListener() : data_received_count_(0), client_count_(0) {}

  void OnDataReceived(const std::string& client_addr, const void* data,
                      size_t len) override {
    data_received_count_ += len;
    std::string received_data(static_cast<const char*>(data), len);
    std::cout << "[SERVER] Received from " << client_addr << ": "
              << received_data << std::endl;

    // Store the data for verification
    last_received_data_ = received_data;
    last_client_addr_ = client_addr;

    // Handle special commands
    if (received_data == "PING") {
      // Echo back PONG
      server_->SendStringTo(client_addr, "PONG");
    } else if (received_data == "BROADCAST_TEST") {
      // Broadcast a message to all clients
      broadcast_test_requested_ = true;
    } else if (received_data == "SHUTDOWN") {
      shutdown_requested_ = true;
    } else {
      // Echo back with prefix
      std::string response = "ECHO: " + received_data;
      server_->SendStringTo(client_addr, response);
    }
  }

  void OnNewClient(const std::string& client_addr) override {
    client_count_++;
    std::cout << "[SERVER] New client connected: " << client_addr
              << " (Total: " << client_count_ << ")" << std::endl;

    // Send welcome message
    std::string welcome =
        "Welcome! You are client #" + std::to_string(client_count_);
    server_->SendStringTo(client_addr, welcome);
  }

  void OnClientTimeout(const std::string& client_addr) override {
    client_count_--;
    std::cout << "[SERVER] Client timed out: " << client_addr
              << " (Remaining: " << client_count_ << ")" << std::endl;
  }

  void OnServerError(const std::string& error) override {
    std::cout << "[SERVER] Error: " << error << std::endl;
  }

  void SetServer(UdpServer* server) { server_ = server; }

  std::atomic<int> data_received_count_;
  std::atomic<int> client_count_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> broadcast_test_requested_{false};
  std::string last_received_data_;
  std::string last_client_addr_;
  UdpServer* server_ = nullptr;
};

// Test client listener implementation
class TestUdpClientListener : public UdpClientEventListener {
 public:
  TestUdpClientListener(const std::string& name)
      : name_(name), data_received_count_(0), ready_(false) {}

  void OnDataReceived(UdpClient* client, const void* data,
                      size_t len) override {
    data_received_count_ += len;
    std::string received_data(static_cast<const char*>(data), len);
    std::cout << "[CLIENT " << name_ << "] Received: " << received_data
              << std::endl;
    last_received_data_ = received_data;
  }

  void OnReady(UdpClient* client) override {
    ready_ = true;
    std::cout << "[CLIENT " << name_ << "] Ready for communication"
              << std::endl;
  }

  void OnClosed(UdpClient* client) override {
    ready_ = false;
    std::cout << "[CLIENT " << name_ << "] Closed" << std::endl;
  }

  void OnError(UdpClient* client, const std::string& error) override {
    std::cout << "[CLIENT " << name_ << "] Error: " << error << std::endl;
  }

  std::string name_;
  std::atomic<int> data_received_count_;
  std::atomic<bool> ready_;
  std::string last_received_data_;
};

void TestBasicServerClient(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Basic UDP Server-Client Communication ==="
            << std::endl;

  // Create server
  auto server_listener = std::make_unique<TestUdpServerListener>();
  UdpServer server(task_runner, server_listener.get());
  server_listener->SetServer(&server);

  // Start server on localhost:8080
  if (!server.Start("127.0.0.1", 8080)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }

  std::cout << "Server started on " << server.GetBindAddress() << std::endl;

  // Wait a bit for server to be ready
  std::this_thread::sleep_for(100ms);

  // Create client
  auto client_listener = std::make_unique<TestUdpClientListener>("Basic");
  UdpClient client(task_runner, client_listener.get());

  if (!client.Open("127.0.0.1", 8080)) {
    std::cout << "Failed to open client" << std::endl;
    return;
  }

  // Wait for client to be ready
  int attempts = 0;
  while (!client_listener->ready_ && attempts++ < 50) {
    std::this_thread::sleep_for(10ms);
  }

  if (!client_listener->ready_) {
    std::cout << "Client not ready after timeout" << std::endl;
    return;
  }

  std::cout << "Client ready at " << client.GetLocalAddress() << std::endl;

  // Test basic communication
  std::string server_addr = "127.0.0.1:8080";

  // Send PING
  if (client.SendString("PING")) {
    std::cout << "Sent PING to server" << std::endl;
  }

  // Wait for response
  std::this_thread::sleep_for(100ms);

  // Send a test message
  std::string test_msg = "Hello from UDP client!";
  if (client.SendString(test_msg)) {
    std::cout << "Sent test message: " << test_msg << std::endl;
  }

  // Wait for response
  std::this_thread::sleep_for(100ms);

  // Verify communication
  if (server_listener->data_received_count_ >= 2) {
    std::cout << "✓ Server received data from client" << std::endl;
  } else {
    std::cout << "✗ Server did not receive expected data" << std::endl;
  }

  if (client_listener->data_received_count_ >= 2) {
    std::cout << "✓ Client received responses from server" << std::endl;
  } else {
    std::cout << "✗ Client did not receive expected responses" << std::endl;
  }

  client.Close();
  server.Stop();
  std::cout << "Basic test completed\n" << std::endl;
}

void TestMultipleClients(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Multiple UDP Clients ===" << std::endl;

  // Create server
  auto server_listener = std::make_unique<TestUdpServerListener>();
  UdpServer server(task_runner, server_listener.get());
  server_listener->SetServer(&server);

  if (!server.Start("127.0.0.1", 8081)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }

  std::cout << "Server started on " << server.GetBindAddress() << std::endl;
  std::this_thread::sleep_for(100ms);

  // Create multiple clients
  const int num_clients = 3;
  std::vector<std::unique_ptr<TestUdpClientListener>> client_listeners;
  std::vector<std::unique_ptr<UdpClient>> clients;

  for (int i = 0; i < num_clients; i++) {
    auto listener =
        std::make_unique<TestUdpClientListener>("Client" + std::to_string(i));
    auto client = std::make_unique<UdpClient>(task_runner, listener.get());

    if (!client->Open("127.0.0.1", 8081)) {
      std::cout << "Failed to open client " << i << std::endl;
      continue;
    }

    client_listeners.push_back(std::move(listener));
    clients.push_back(std::move(client));
  }
  if (clients.size() < num_clients) {
    std::cout << "Failed to start all clients" << std::endl;
    return;
  }

  // Wait for all clients to be ready
  std::this_thread::sleep_for(200ms);

  // Each client sends a message
  for (size_t i = 0; i < clients.size(); i++) {
    std::string msg = "Hello from client " + std::to_string(i);
    if (clients[i]->SendString(msg)) {
      std::cout << "Client " << i << " sent: " << msg << std::endl;
    }
  }

  // Wait for responses
  std::this_thread::sleep_for(200ms);

  // Test broadcast
  std::cout << "\nTesting broadcast..." << std::endl;
  clients[0]->SendString("BROADCAST_TEST");
  std::this_thread::sleep_for(1000ms);

  // Check if broadcast was requested
  if (server_listener->broadcast_test_requested_) {
    server.BroadcastString("Broadcast message to all clients!");
    std::this_thread::sleep_for(100ms);
  }

  // Verify results
  std::cout << "\nServer client count: " << server.GetClientCount()
            << std::endl;
  std::cout << "Server received " << server_listener->data_received_count_
            << " messages" << std::endl;

  int total_client_responses = 0;
  for (const auto& listener : client_listeners) {
    total_client_responses += listener->data_received_count_;
  }
  std::cout << "Clients received total " << total_client_responses
            << " responses" << std::endl;

  // Clean up
  for (auto& client : clients) {
    client->Close();
  }
  server.Stop();
  std::cout << "Multiple clients test completed\n" << std::endl;
}

void TestClientTimeout(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Client Timeout ===" << std::endl;

  auto server_listener = std::make_unique<TestUdpServerListener>();
  UdpServer server(task_runner, server_listener.get());
  server_listener->SetServer(&server);

  // Set a short timeout for testing
  server.SetClientTimeout(500);  // 500ms timeout

  if (!server.Start("127.0.0.1", 8082)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }

  std::this_thread::sleep_for(100ms);

  // Create a client and send one message
  auto client_listener = std::make_unique<TestUdpClientListener>("Timeout");
  UdpClient client(task_runner, client_listener.get());

  if (!client.Open("127.0.0.1", 8082)) {
    std::cout << "Failed to open client" << std::endl;
    return;
  }

  client.SendString("Hello, I will go inactive!");

  std::this_thread::sleep_for(200ms);
  std::cout << "Client count after initial message: " << server.GetClientCount()
            << std::endl;

  // Wait for timeout to occur
  std::cout << "Waiting for client timeout..." << std::endl;
  std::this_thread::sleep_for(600ms);

  // Trigger cleanup
  server.CleanupInactiveClients();
  std::this_thread::sleep_for(100ms);

  std::cout << "Client count after timeout cleanup: " << server.GetClientCount()
            << std::endl;

  client.Close();
  server.Stop();
  std::cout << "Client timeout test completed\n" << std::endl;
}

void TestErrorHandling(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Error Handling ===" << std::endl;

  // Test client connecting to non-existent server
  auto client_listener = std::make_unique<TestUdpClientListener>("Error");
  UdpClient client(task_runner, client_listener.get());

  if (!client.Open("127.0.0.1", 8081)) {
    std::cout << "Failed to open client for error test" << std::endl;
    return;
  }

  // Try to send to non-existent server (this won't generate an error in UDP)
  std::cout << "Sending to non-existent server..." << std::endl;
  bool sent =
      client.SendStringTo("127.0.0.1:9999", "Hello non-existent server");
  std::cout << "Send result: " << (sent ? "Success" : "Failed") << std::endl;

  // Test invalid addresses
  std::cout << "Testing invalid bind address..." << std::endl;
  UdpClient invalid_client(task_runner, client_listener.get());
  bool opened = invalid_client.Open("999.999.999.999", 0);
  std::cout << "Invalid address open result: "
            << (opened ? "Success" : "Failed") << std::endl;

  client.Close();
  std::cout << "Error handling test completed\n" << std::endl;
}

void TestLargeData(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Large Data Transfer ===" << std::endl;

  auto server_listener = std::make_unique<TestUdpServerListener>();
  UdpServer server(task_runner, server_listener.get());
  server_listener->SetServer(&server);

  // Set a larger packet size
  server.SetMaxPacketSize(8192);

  if (!server.Start("127.0.0.1", 8083)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }

  std::this_thread::sleep_for(100ms);

  auto client_listener = std::make_unique<TestUdpClientListener>("LargeData");
  UdpClient client(task_runner, client_listener.get());
  client.SetMaxPacketSize(8192);

  if (!client.Open("127.0.0.1", 8083)) {
    std::cout << "Failed to open client" << std::endl;
    return;
  }

  // Create a large message (but within UDP limits)
  std::string large_msg(4000, 'A');  // 4KB of 'A' characters
  large_msg = "LARGE_DATA_TEST:" + large_msg;

  std::cout << "Sending large message (" << large_msg.size() << " bytes)..."
            << std::endl;
  bool sent = client.SendString(large_msg);
  std::cout << "Large data send result: " << (sent ? "Success" : "Failed")
            << std::endl;

  std::this_thread::sleep_for(2000ms);

  if (server_listener->data_received_count_ > 0) {
    std::cout << "✓ Server received large data" << std::endl;
  } else {
    std::cout << "✗ Server did not receive large data" << std::endl;
  }

  client.Close();
  server.Stop();
  std::cout << "Large data test completed\n" << std::endl;
}

int main() {
  system::SignalHandler::Initialize();
  std::cout << "UDP Server/Client Example" << std::endl;
  std::cout << "=========================" << std::endl;

  // Create task runner
  auto task_runner = ThreadTaskRunner::CreateAndStart();

  try {
    // Run various UDP tests
    TestBasicServerClient(task_runner.get());
    TestMultipleClients(task_runner.get());
    TestClientTimeout(task_runner.get());
    TestErrorHandling(task_runner.get());
    TestLargeData(task_runner.get());

    std::cout << "\n=== All UDP Tests Completed ===" << std::endl;

  } catch (const std::exception& e) {
    std::cout << "Exception occurred: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
