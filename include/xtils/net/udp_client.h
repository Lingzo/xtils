#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "xtils/system/unix_socket.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

class UdpClient;

class UdpClientEventListener {
 public:
  virtual ~UdpClientEventListener() = default;

  // Called when data is received from server
  virtual void OnDataReceived(UdpClient* client, const void* data,
                              size_t len) = 0;

  // Called when client encounters an error
  virtual void OnError(UdpClient* client, const std::string& error) {}

  // Called when socket is ready for communication
  virtual void OnReady(UdpClient* client) {}

  // Called when client is closed
  virtual void OnClosed(UdpClient* client) {}
};

class UdpClient : public UnixSocket::EventListener {
 public:
  enum class State { kClosed = 0, kReady, kError };

  explicit UdpClient(TaskRunner* task_runner, UdpClientEventListener* listener);
  ~UdpClient() override;

  // Create and bind UDP socket for communication
  // local_address can be "0.0.0.0" for IPv4 or "::" for IPv6
  bool Open(const std::string& local_address = "", uint16_t local_port = 0);

  // Close the UDP socket
  void Close();

  // Send data to a specific server
  // server_addr format: "ip:port" for IPv4, "[ip]:port" for IPv6
  bool SendTo(const std::string& server_addr, const void* data, size_t len);
  bool SendStringTo(const std::string& server_addr, const std::string& data);

  bool Send(const void* data, size_t len);
  bool SendString(const std::string& data);

  // Receive data (non-blocking)
  // Returns number of bytes received, sender_addr will contain sender's address
  size_t ReceiveFrom(void* buffer, size_t buffer_size,
                     std::string& sender_addr);

  // Receive data from any sender (non-blocking)
  size_t Receive(void* buffer, size_t buffer_size);

  // Get current state
  State GetState() const { return state_; }

  // Check if client is ready for communication
  bool IsReady() const { return state_ == State::kReady; }

  // Get local bind address
  std::string GetLocalAddress() const;

  // Get the underlying socket file descriptor
  SocketHandle GetSocketFd() const;

  // Set socket options
  void SetSendTimeout(uint32_t timeout_ms);
  void SetReceiveTimeout(uint32_t timeout_ms);

  // Set maximum UDP packet size
  void SetMaxPacketSize(size_t max_size);

  // Enable/disable broadcast
  void SetBroadcast(bool enable);

  // Enable/disable address reuse
  void SetReuseAddress(bool enable);

  // Join/leave multicast group (for IPv4)
  bool JoinMulticastGroup(const std::string& group_addr,
                          const std::string& interface_addr = "");
  bool LeaveMulticastGroup(const std::string& group_addr,
                           const std::string& interface_addr = "");

  // Set multicast TTL
  void SetMulticastTTL(uint8_t ttl);

  // Enable/disable multicast loopback
  void SetMulticastLoopback(bool enable);

 private:
  // UnixSocket::EventListener implementation
  void OnConnect(UnixSocket* self, bool connected) override {}
  void OnDisconnect(UnixSocket* self) override;
  void OnDataAvailable(UnixSocket* self) override;
  void OnNewIncomingConnection(
      UnixSocket* self, std::unique_ptr<UnixSocket> new_connection) override {}

  void SetState(State new_state);
  void HandleError(const std::string& error);
  std::pair<std::string, uint16_t> ParseAddress(
      const std::string& address) const;
  std::string FormatAddress(const std::string& ip, uint16_t port,
                            SockFamily family) const;

  TaskRunner* task_runner_;
  UdpClientEventListener* listener_;
  std::unique_ptr<UnixSocket> socket_;
  State state_;
  std::string server_ip_;
  uint16_t server_port_;
  size_t max_packet_size_;
  SockFamily family_;
};

}  // namespace xtils
