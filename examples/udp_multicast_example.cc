#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "xtils/net/udp_client.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;
using namespace std::chrono_literals;

// Multicast receiver listener
class MulticastReceiverListener : public UdpClientEventListener {
 public:
  MulticastReceiverListener(const std::string& name)
      : name_(name), data_received_count_(0), ready_(false) {}

  void OnDataReceived(UdpClient* client, const void* data,
                      size_t len) override {
    data_received_count_++;
    std::string received_data(static_cast<const char*>(data), len);
    std::cout << "[RECEIVER " << name_ << "] Received: " << received_data
              << " (count: " << data_received_count_ << ")" << std::endl;
    last_received_data_ = received_data;
  }

  void OnReady(UdpClient* client) override {
    ready_ = true;
    std::cout << "[RECEIVER " << name_ << "] Ready for multicast reception"
              << std::endl;
  }

  void OnClosed(UdpClient* client) override {
    ready_ = false;
    std::cout << "[RECEIVER " << name_ << "] Closed" << std::endl;
  }

  void OnError(UdpClient* client, const std::string& error) override {
    std::cout << "[RECEIVER " << name_ << "] Error: " << error << std::endl;
  }

  std::string name_;
  std::atomic<int> data_received_count_;
  std::atomic<bool> ready_;
  std::string last_received_data_;
};

// Multicast sender listener
class MulticastSenderListener : public UdpClientEventListener {
 public:
  MulticastSenderListener() : ready_(false) {}

  void OnDataReceived(UdpClient* client, const void* data,
                      size_t len) override {
    // Sender typically doesn't receive data
  }

  void OnReady(UdpClient* client) override {
    ready_ = true;
    std::cout << "[SENDER] Ready for multicast transmission" << std::endl;
  }

  void OnClosed(UdpClient* client) override {
    ready_ = false;
    std::cout << "[SENDER] Closed" << std::endl;
  }

  void OnError(UdpClient* client, const std::string& error) override {
    std::cout << "[SENDER] Error: " << error << std::endl;
  }

  std::atomic<bool> ready_;
};

void TestIPv4Multicast(TaskRunner* task_runner) {
  std::cout << "\n=== Testing IPv4 Multicast ===" << std::endl;

  const std::string multicast_group = "224.0.0.251";  // mDNS multicast address
  const uint16_t multicast_port = 12345;
  const std::string multicast_addr =
      multicast_group + ":" + std::to_string(multicast_port);

  // Create multiple receivers
  const int num_receivers = 3;
  std::vector<std::unique_ptr<MulticastReceiverListener>> receiver_listeners;
  std::vector<std::unique_ptr<UdpClient>> receivers;

  std::cout << "Creating " << num_receivers << " multicast receivers..."
            << std::endl;

  for (int i = 0; i < num_receivers; i++) {
    auto listener =
        std::make_unique<MulticastReceiverListener>("R" + std::to_string(i));
    auto receiver = std::make_unique<UdpClient>(task_runner, listener.get());

    // Open the receiver socket
    if (!receiver->Open("0.0.0.0", multicast_port)) {
      std::cout << "Failed to open receiver " << i << std::endl;
      continue;
    }

    // Enable address reuse for multicast
    receiver->SetReuseAddress(true);

    // Join the multicast group
    if (!receiver->JoinMulticastGroup(multicast_group)) {
      std::cout << "Failed to join multicast group for receiver " << i
                << std::endl;
      continue;
    }

    std::cout << "Receiver " << i << " joined multicast group "
              << multicast_group << std::endl;

    receiver_listeners.push_back(std::move(listener));
    receivers.push_back(std::move(receiver));
  }

  // Wait for receivers to be ready
  std::this_thread::sleep_for(500ms);

  // Create sender
  auto sender_listener = std::make_unique<MulticastSenderListener>();
  UdpClient sender(task_runner, sender_listener.get());

  if (!sender.Open("0.0.0.0", 0)) {  // Use any available port
    std::cout << "Failed to open sender" << std::endl;
    return;
  }

  // Wait for sender to be ready
  std::this_thread::sleep_for(200ms);

  // Configure multicast settings for sender
  sender.SetMulticastTTL(64);         // Set TTL to 64
  sender.SetMulticastLoopback(true);  // Enable loopback for testing

  std::cout << "\nSending multicast messages..." << std::endl;

  // Send multiple multicast messages
  for (int i = 1; i <= 5; i++) {
    std::string message = "Multicast message #" + std::to_string(i);

    if (sender.SendStringTo(multicast_addr, message)) {
      std::cout << "[SENDER] Sent: " << message << std::endl;
    } else {
      std::cout << "[SENDER] Failed to send: " << message << std::endl;
    }

    std::this_thread::sleep_for(200ms);
  }

  // Wait for messages to be received
  std::this_thread::sleep_for(500ms);

  // Check results
  std::cout << "\n--- Results ---" << std::endl;
  int total_received = 0;
  for (size_t i = 0; i < receiver_listeners.size(); i++) {
    int count = receiver_listeners[i]->data_received_count_;
    total_received += count;
    std::cout << "Receiver " << i << " received " << count << " messages"
              << std::endl;
  }

  std::cout << "Total messages received by all receivers: " << total_received
            << std::endl;

  // Test leaving multicast group
  std::cout << "\nTesting leave multicast group..." << std::endl;
  if (!receivers.empty() &&
      receivers[0]->LeaveMulticastGroup(multicast_group)) {
    std::cout << "Receiver 0 left multicast group" << std::endl;
  }

  // Send one more message to verify receiver 0 doesn't receive it
  std::this_thread::sleep_for(100ms);
  sender.SendStringTo(multicast_addr, "Message after leave group");
  std::this_thread::sleep_for(200ms);

  // Clean up
  for (auto& receiver : receivers) {
    receiver->Close();
  }
  sender.Close();

  std::cout << "IPv4 multicast test completed\n" << std::endl;
}

void TestBroadcast(TaskRunner* task_runner) {
  std::cout << "\n=== Testing UDP Broadcast ===" << std::endl;

  const uint16_t broadcast_port = 12346;
  const std::string broadcast_addr =
      "255.255.255.255:" + std::to_string(broadcast_port);

  // Create broadcast receiver
  auto receiver_listener =
      std::make_unique<MulticastReceiverListener>("Broadcast");
  UdpClient receiver(task_runner, receiver_listener.get());

  if (!receiver.Open("0.0.0.0", broadcast_port)) {
    std::cout << "Failed to open broadcast receiver" << std::endl;
    return;
  }

  receiver.SetReuseAddress(true);

  // Create broadcast sender
  auto sender_listener = std::make_unique<MulticastSenderListener>();
  UdpClient sender(task_runner, sender_listener.get());

  if (!sender.Open("0.0.0.0", 0)) {
    std::cout << "Failed to open broadcast sender" << std::endl;
    return;
  }

  // Enable broadcast on sender
  sender.SetBroadcast(true);

  std::this_thread::sleep_for(200ms);

  std::cout << "Sending broadcast messages..." << std::endl;

  // Send broadcast messages
  for (int i = 1; i <= 3; i++) {
    std::string message = "Broadcast message #" + std::to_string(i);

    if (sender.SendStringTo(broadcast_addr, message)) {
      std::cout << "[BROADCAST SENDER] Sent: " << message << std::endl;
    } else {
      std::cout << "[BROADCAST SENDER] Failed to send: " << message
                << std::endl;
    }

    std::this_thread::sleep_for(300ms);
  }

  // Wait for messages
  std::this_thread::sleep_for(500ms);

  std::cout << "Broadcast receiver got "
            << receiver_listener->data_received_count_ << " messages"
            << std::endl;

  receiver.Close();
  sender.Close();

  std::cout << "Broadcast test completed\n" << std::endl;
}

void TestSocketOptions(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Socket Options ===" << std::endl;

  auto listener = std::make_unique<MulticastSenderListener>();
  UdpClient client(task_runner, listener.get());

  if (!client.Open("0.0.0.0", 0)) {
    std::cout << "Failed to open client for socket options test" << std::endl;
    return;
  }

  std::cout << "Testing socket options..." << std::endl;

  // Test various socket options
  client.SetBroadcast(true);
  std::cout << "✓ SetBroadcast(true)" << std::endl;

  client.SetReuseAddress(true);
  std::cout << "✓ SetReuseAddress(true)" << std::endl;

  client.SetMulticastTTL(32);
  std::cout << "✓ SetMulticastTTL(32)" << std::endl;

  client.SetMulticastLoopback(false);
  std::cout << "✓ SetMulticastLoopback(false)" << std::endl;

  // Test timeouts
  client.SetSendTimeout(1000);     // 1 second
  client.SetReceiveTimeout(1000);  // 1 second
  std::cout << "✓ Set send/receive timeouts" << std::endl;

  // Test max packet size
  client.SetMaxPacketSize(8192);
  std::cout << "✓ SetMaxPacketSize(8192)" << std::endl;

  client.Close();
  std::cout << "Socket options test completed\n" << std::endl;
}

void TestErrorHandling(TaskRunner* task_runner) {
  std::cout << "\n=== Testing Error Handling ===" << std::endl;

  auto listener = std::make_unique<MulticastReceiverListener>("ErrorTest");
  UdpClient client(task_runner, listener.get());

  // Test joining multicast group without opening socket
  std::cout << "Testing join multicast without open socket..." << std::endl;
  bool result = client.JoinMulticastGroup("224.0.0.1");
  std::cout << "Join result (should be false): " << (result ? "true" : "false")
            << std::endl;

  // Open socket
  if (!client.Open("0.0.0.0", 0)) {
    std::cout << "Failed to open client" << std::endl;
    return;
  }

  // Test invalid multicast addresses
  std::cout << "Testing invalid multicast address..." << std::endl;
  result = client.JoinMulticastGroup("999.999.999.999");
  std::cout << "Join invalid address result (should be false): "
            << (result ? "true" : "false") << std::endl;

  result = client.JoinMulticastGroup("");
  std::cout << "Join empty address result (should be false): "
            << (result ? "true" : "false") << std::endl;

  client.Close();
  std::cout << "Error handling test completed\n" << std::endl;
}

int main() {
  std::cout << "UDP Multicast and Broadcast Example" << std::endl;
  std::cout << "====================================" << std::endl;

  // Create task runner
  auto task_runner = ThreadTaskRunner::CreateAndStart();

  try {
    // Run multicast and broadcast tests
    TestSocketOptions(task_runner.get());
    TestErrorHandling(task_runner.get());
    TestBroadcast(task_runner.get());
    TestIPv4Multicast(task_runner.get());

    std::cout << "\n=== All Multicast Tests Completed ===" << std::endl;
    std::cout << "\nNote: Some tests may require root privileges or specific"
              << std::endl;
    std::cout << "network configuration to work properly on your system."
              << std::endl;

  } catch (const std::exception& e) {
    std::cout << "Exception occurred: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
