#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/tcp_client.h"
#include "xtils/net/tcp_server.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;
using namespace std::chrono_literals;

// Test server implementation
class TestServerListener : public TcpServerEventListener {
 public:
  TestServerListener() : client_count_(0), data_received_count_(0) {}

  void OnClientConnected(TcpServerConnection* conn) override {
    client_count_++;
    std::cout << "[SERVER] Client connected from " << conn->GetPeerAddress()
              << " (Total clients: " << client_count_ << ")" << std::endl;

    // Send welcome message
    std::string welcome =
        "Welcome to test server! Your ID is " + std::to_string(client_count_);
    conn->SendString(welcome);
  }

  void OnDataReceived(TcpServerConnection* conn, const void* data,
                      size_t len) override {
    data_received_count_++;
    std::string received_data(static_cast<const char*>(data), len);
    std::cout << "[SERVER] Received from " << conn->GetPeerAddress() << ": "
              << received_data << std::endl;

    // Echo back the data with prefix
    std::string response = "ECHO: " + received_data;
    conn->SendString(response);

    // Special commands
    if (received_data == "SHUTDOWN") {
      std::cout << "[SERVER] Shutdown command received" << std::endl;
      shutdown_requested_ = true;
    } else if (received_data == "BROADCAST_TEST") {
      broadcast_test_requested_ = true;
    }
  }

  void OnClientDisconnected(TcpServerConnection* conn) override {
    client_count_--;
    std::cout << "[SERVER] Client disconnected from " << conn->GetPeerAddress()
              << " (Total clients: " << client_count_ << ")" << std::endl;
  }

  void OnServerError(const std::string& error) override {
    std::cout << "[SERVER] Error: " << error << std::endl;
  }

  std::atomic<int> client_count_;
  std::atomic<int> data_received_count_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> broadcast_test_requested_{false};
};

// Test client implementation
class TestClientListener : public TcpClientEventListener {
 public:
  explicit TestClientListener(const std::string& name)
      : name_(name), connected_(false), data_received_count_(0) {}

  void OnConnected(TcpClient* client, bool success) override {
    if (success) {
      connected_ = true;
      std::cout << "[CLIENT:" << name_ << "] Connected to "
                << client->GetServerAddress() << std::endl;
    } else {
      std::cout << "[CLIENT:" << name_ << "] Failed to connect" << std::endl;
    }
    client->SendString("Hello Server!");
  }

  void OnDataReceived(TcpClient* client, const void* data,
                      size_t len) override {
    data_received_count_++;
    std::string received_data(static_cast<const char*>(data), len);
    std::cout << "[CLIENT:" << name_ << "] Received: " << received_data
              << std::endl;

    last_received_data_ = received_data;
  }

  void OnDisconnected(TcpClient* client) override {
    connected_ = false;
    std::cout << "[CLIENT:" << name_ << "] Disconnected" << std::endl;
  }

  void OnError(TcpClient* client, const std::string& error) override {
    std::cout << "[CLIENT:" << name_ << "] Error: " << error << std::endl;
  }

  std::string name_;
  std::atomic<bool> connected_;
  std::atomic<int> data_received_count_;
  std::string last_received_data_;
};

// Helper function to check IPv6 support
bool CheckIPv6Support() {
  // Try to create an IPv6 socket to check if IPv6 is available
  int sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock == -1) {
    return false;
  }
  close(sock);
  return true;
}

// Test functions
void TestBasicServerClient(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Basic Server-Client Communication ==="
            << std::endl;

  // Create server
  TestServerListener server_listener;
  TcpServer server(task_runner, &server_listener);

  // Start server on localhost
  if (!server.Start("127.0.0.1", 8888)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }
  std::cout << "Server started on 127.0.0.1:8888" << std::endl;
  std::this_thread::sleep_for(100ms);

  // Create client
  TestClientListener client_listener("Client1");
  TcpClient client(task_runner, &client_listener);

  // Set client options
  client.SetConnectTimeout(5000);
  client.SetSendTimeout(3000);
  client.SetReceiveTimeout(3000);
  client.SetKeepAlive(true);
  client.SetNoDelay(true);

  // Connect to server
  std::cout << "Connecting to server..." << std::endl;
  bool connect_initiated = client.Connect("127.0.0.1", 8888);
  std::cout << "Connect() returned: " << (connect_initiated ? "true" : "false")
            << std::endl;

  if (!connect_initiated) {
    std::cout << "Failed to initiate connection to server" << std::endl;
    return;
  }

  // Wait for connection
  std::this_thread::sleep_for(300ms);

  if (!client_listener.connected_) {
    std::cout << "Failed to establish connection to server" << std::endl;
    return;
  }
  std::cout << "✓ Successfully connected to server" << std::endl;

  // Test data exchange
  std::vector<std::string> test_messages = {
      "Hello Server!", "How are you?", "Testing TCP communication",
      "Message with numbers: 12345", "Special chars: !@#$%^&*()"};

  for (const auto& msg : test_messages) {
    client.SendString(msg);
    std::this_thread::sleep_for(50ms);
  }

  std::this_thread::sleep_for(500ms);

  std::cout << "Basic test completed. Messages sent: " << test_messages.size()
            << ", Server received: " << server_listener.data_received_count_
            << std::endl;

  client.Disconnect();
  server.Stop();
}

void TestMultipleClients(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Multiple Clients ===" << std::endl;

  TestServerListener server_listener;
  TcpServer server(task_runner, &server_listener);

  if (!server.Start("127.0.0.1", 8889)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }
  std::cout << "Server started on 127.0.0.1:8889" << std::endl;
  std::this_thread::sleep_for(100ms);

  // Create multiple clients
  const int num_clients = 5;
  std::vector<std::unique_ptr<TestClientListener>> client_listeners;
  std::vector<std::unique_ptr<TcpClient>> clients;

  for (int i = 0; i < num_clients; i++) {
    auto listener =
        std::make_unique<TestClientListener>("Client" + std::to_string(i + 1));
    auto client = std::make_unique<TcpClient>(task_runner, listener.get());

    if (client->Connect("127.0.0.1", 8889)) {
      std::cout << "Client " << (i + 1) << " connection initiated" << std::endl;
    }

    client_listeners.push_back(std::move(listener));
    clients.push_back(std::move(client));
    std::this_thread::sleep_for(50ms);
  }

  std::this_thread::sleep_for(500ms);
  std::cout << "Server connection count: " << server.GetConnectionCount()
            << std::endl;

  // Each client sends some data
  for (int i = 0; i < num_clients; i++) {
    std::string msg = "Message from client " + std::to_string(i + 1);
    clients[i]->SendString(msg);
    std::this_thread::sleep_for(50ms);
  }

  std::this_thread::sleep_for(500ms);

  // Test broadcast
  std::cout << "Testing broadcast..." << std::endl;
  server.BroadcastString("BROADCAST: Hello all clients!");
  std::this_thread::sleep_for(300ms);

  // Disconnect clients one by one
  for (int i = 0; i < num_clients; i++) {
    clients[i]->Disconnect();
    std::this_thread::sleep_for(100ms);
  }

  std::cout << "Multiple clients test completed. Final connection count: "
            << server.GetConnectionCount() << std::endl;

  server.Stop();
}

void TestIPv6(TaskRunner* task_runner) {
  std::cout << "\n=== Testing IPv6 Support ===" << std::endl;

  // Check if IPv6 is supported on this system
  if (!CheckIPv6Support()) {
    std::cout << "IPv6 not supported on this system, skipping IPv6 tests"
              << std::endl;
    return;
  }

  TestServerListener server_listener;
  TcpServer server(task_runner, &server_listener);

  // Try dual stack (IPv4 and IPv6)
  if (!server.StartDualStack(8890)) {
    std::cout << "Failed to start dual stack server, trying IPv6 only..."
              << std::endl;
    if (!server.Start("::1", 8890)) {
      std::cout << "Failed to start IPv6 server - IPv6 may not be enabled"
                << std::endl;
      return;
    }
    std::cout << "Server started on IPv6 only (::1:8890)" << std::endl;
  } else {
    std::cout << "Server started on dual stack port 8890" << std::endl;
  }
  std::this_thread::sleep_for(200ms);

  // Test IPv6 client
  // Test IPv6 client
  TestClientListener ipv6_listener("IPv6Client");
  TcpClient ipv6_client(task_runner, &ipv6_listener);

  std::cout << "Testing IPv6 client connection to ::1:8890..." << std::endl;
  ipv6_client.SetConnectTimeout(3000);

  bool connect_initiated = ipv6_client.Connect("::1", 8890);
  std::cout << "IPv6 connect() returned: "
            << (connect_initiated ? "true" : "false") << std::endl;

  if (connect_initiated) {
    // Wait for connection result
    std::this_thread::sleep_for(500ms);

    if (ipv6_listener.connected_) {
      std::cout << "✓ IPv6 client connected successfully" << std::endl;
      ipv6_client.SendString("Hello from IPv6 client!");
      std::this_thread::sleep_for(500ms);
    } else {
      std::cout << "✗ IPv6 connection failed after initiation (connection "
                   "timeout or refused)"
                << std::endl;
      std::cout << "  Client state: "
                << static_cast<int>(ipv6_client.GetState()) << std::endl;
    }
    ipv6_client.Disconnect();
  } else {
    std::cout << "✗ IPv6 connection initiation failed immediately" << std::endl;
  }

  std::this_thread::sleep_for(200ms);

  // Test IPv4 client to dual stack server
  TestClientListener ipv4_listener("IPv4Client");
  TcpClient ipv4_client(task_runner, &ipv4_listener);

  std::cout << "Testing IPv4 client connection to dual stack server..."
            << std::endl;
  ipv4_client.SetConnectTimeout(3000);

  bool ipv4_connect_initiated = ipv4_client.Connect("127.0.0.1", 8890);
  std::cout << "IPv4 connect() returned: "
            << (ipv4_connect_initiated ? "true" : "false") << std::endl;

  if (ipv4_connect_initiated) {
    // Wait for connection result
    std::this_thread::sleep_for(500ms);

    if (ipv4_listener.connected_) {
      std::cout << "✓ IPv4 client connected to dual stack server" << std::endl;
      ipv4_client.SendString("Hello from IPv4 client!");
      std::this_thread::sleep_for(300ms);
    } else {
      std::cout << "✗ IPv4 connection to dual stack server failed" << std::endl;
      std::cout << "  Client state: "
                << static_cast<int>(ipv4_client.GetState()) << std::endl;
    }
    ipv4_client.Disconnect();
  } else {
    std::cout << "✗ IPv4 connection initiation to dual stack server failed"
              << std::endl;
  }

  std::this_thread::sleep_for(300ms);
  server.Stop();
}

void TestHostnameResolution(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Hostname Resolution ===" << std::endl;

  TestServerListener server_listener;
  TcpServer server(task_runner, &server_listener);

  if (!server.Start("127.0.0.1", 8891)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }
  std::cout << "Server started on 127.0.0.1:8891" << std::endl;
  std::this_thread::sleep_for(100ms);

  TestClientListener client_listener("HostnameClient");
  TcpClient client(task_runner, &client_listener);

  std::cout << "Testing hostname resolution connection to localhost:8891..."
            << std::endl;
  client.SetConnectTimeout(3000);

  bool hostname_connect_initiated = client.ConnectToHost("localhost", 8891);
  std::cout << "ConnectToHost() returned: "
            << (hostname_connect_initiated ? "true" : "false") << std::endl;

  if (hostname_connect_initiated) {
    // Wait for connection result
    std::this_thread::sleep_for(500ms);

    if (client_listener.connected_) {
      std::cout << "✓ Connected using hostname resolution" << std::endl;
      client.SendString("Connected via hostname!");
      std::this_thread::sleep_for(300ms);
    } else {
      std::cout << "✗ Hostname resolution connection failed after initiation"
                << std::endl;
      std::cout << "  Client state: " << static_cast<int>(client.GetState())
                << std::endl;
    }
    client.Disconnect();
  } else {
    std::cout << "✗ Hostname resolution connection initiation failed"
              << std::endl;
  }

  std::this_thread::sleep_for(200ms);
  server.Stop();
}

void TestErrorHandling(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Error Handling ===" << std::endl;

  TestClientListener client_listener("ErrorTestClient");
  TcpClient client(task_runner, &client_listener);

  // Test connection to non-existent server
  std::cout << "Testing connection to non-existent server (127.0.0.1:9999)..."
            << std::endl;
  client.SetConnectTimeout(1000);  // Short timeout

  bool non_existent_connect = client.Connect("127.0.0.1", 9999);
  std::cout << "Connect() to non-existent server returned: "
            << (non_existent_connect ? "true" : "false") << std::endl;

  if (non_existent_connect) {
    std::cout << "Connection attempt initiated, waiting for timeout..."
              << std::endl;
    std::this_thread::sleep_for(1500ms);
    if (!client_listener.connected_) {
      std::cout << "✓ Connection correctly failed for non-existent server"
                << std::endl;
    } else {
      std::cout << "✗ Unexpected: Connection succeeded to non-existent server"
                << std::endl;
    }
  } else {
    std::cout
        << "✓ Connection initiation correctly failed for non-existent server"
        << std::endl;
  }

  // Test invalid IP address (reserved range that should fail quickly)
  std::cout << "Testing connection to invalid IP (192.0.2.1:9999)..."
            << std::endl;
  TestClientListener client_listener2("ErrorTestClient2");
  TcpClient client2(task_runner, &client_listener2);
  client2.SetConnectTimeout(500);  // Very short timeout

  bool invalid_ip_connect =
      client2.Connect("192.0.2.1", 9999);  // TEST-NET-1 reserved range
  std::cout << "Connect() to invalid IP returned: "
            << (invalid_ip_connect ? "true" : "false") << std::endl;

  if (invalid_ip_connect) {
    std::cout << "Connection attempt initiated, waiting for timeout..."
              << std::endl;
    std::this_thread::sleep_for(800ms);
    if (!client_listener2.connected_) {
      std::cout << "✓ Connection correctly failed for unreachable IP"
                << std::endl;
    } else {
      std::cout << "✗ Unexpected: Connection succeeded to unreachable IP"
                << std::endl;
    }
  } else {
    std::cout << "✓ Connection initiation failed for unreachable IP"
              << std::endl;
  }

  std::this_thread::sleep_for(10s);
}

void TestLargeData(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Large Data Transfer ===" << std::endl;

  TestServerListener server_listener;
  TcpServer server(task_runner, &server_listener);

  if (!server.Start("127.0.0.1", 8892)) {
    std::cout << "Failed to start server" << std::endl;
    return;
  }
  std::this_thread::sleep_for(100ms);

  TestClientListener client_listener("LargeDataClient");
  TcpClient client(task_runner, &client_listener);

  client.SetConnectTimeout(3000);
  if (client.Connect("127.0.0.1", 8892)) {
    // Wait for connection
    std::this_thread::sleep_for(300ms);

    if (!client_listener.connected_) {
      std::cout << "Failed to connect for large data test" << std::endl;
      server.Stop();
      return;
    }

    // Create large data (1MB)
    std::string large_data(1024 * 1024, 'A');
    large_data += " [END OF LARGE DATA]";

    std::cout << "Sending large data (" << large_data.size() << " bytes)..."
              << std::endl;

    auto start_time = std::chrono::steady_clock::now();
    if (client.SendString(large_data)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      std::cout << "Large data sent successfully in " << duration.count()
                << "ms" << std::endl;
    } else {
      std::cout << "Failed to send large data" << std::endl;
    }

    std::this_thread::sleep_for(1000ms);
  } else {
    std::cout << "Failed to initiate connection for large data test"
              << std::endl;
  }

  client.Disconnect();
  server.Stop();
}

int main() {
  std::cout << "=== TCP Server/Client Comprehensive Test ===" << std::endl;

  auto task_runner = ThreadTaskRunner::CreateAndStart();
  try {
    // Run all tests
    TestBasicServerClient(&task_runner);
    TestMultipleClients(&task_runner);
    TestIPv6(&task_runner);
    TestHostnameResolution(&task_runner);
    TestErrorHandling(&task_runner);
    // TestLargeData(&task_runner);

    std::cout << "\n=== All Tests Completed ===" << std::endl;

  } catch (const std::exception& e) {
    std::cout << "Test failed with exception: " << e.what() << std::endl;
  }

  return 0;
}
