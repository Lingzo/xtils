#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "xtils/system/unix_socket.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

struct UdpClientInfo {
  std::string address;
  uint16_t port;
  uint64_t last_seen_ms;

  UdpClientInfo() : port(0), last_seen_ms(0) {}
  UdpClientInfo(const std::string& addr, uint16_t p, uint64_t timestamp)
      : address(addr), port(p), last_seen_ms(timestamp) {}
};

class UdpServerEventListener {
 public:
  virtual ~UdpServerEventListener() = default;

  // Called when data is received from a client
  // client_addr format: "ip:port" for IPv4, "[ip]:port" for IPv6
  virtual void OnDataReceived(const std::string& client_addr, const void* data,
                              size_t len) = 0;

  // Called when server encounters an error
  virtual void OnServerError(const std::string& error) {}

  // Called when a new client sends data for the first time
  virtual void OnNewClient(const std::string& client_addr) {}

  // Called when a client hasn't been seen for timeout_ms
  virtual void OnClientTimeout(const std::string& client_addr) {}
};

class UdpServer : public UnixSocket::EventListener {
 public:
  explicit UdpServer(TaskRunner* task_runner, UdpServerEventListener* listener);
  ~UdpServer() override;

  // Start listening on the specified address and port
  // address can be "0.0.0.0" for IPv4 or "::" for IPv6
  bool Start(const std::string& address, uint16_t port);

  // Start listening on IPv4 and IPv6
  bool StartDualStack(uint16_t port);

  // Stop the server
  void Stop();

  // Send data to a specific client
  // client_addr format: "ip:port" for IPv4, "[ip]:port" for IPv6
  bool SendTo(const std::string& client_addr, const void* data, size_t len);
  bool SendStringTo(const std::string& client_addr, const std::string& data);

  // Broadcast data to all known clients
  void Broadcast(const void* data, size_t len);
  void BroadcastString(const std::string& data);

  // Get the number of known clients
  size_t GetClientCount() const;

  // Get list of known client addresses
  std::vector<std::string> GetClientAddresses() const;

  // Check if server is running
  bool IsRunning() const;

  // Set client timeout in milliseconds (0 = no timeout)
  void SetClientTimeout(uint32_t timeout_ms);

  // Remove inactive clients (those not seen for timeout_ms)
  void CleanupInactiveClients();

  // Set maximum UDP packet size
  void SetMaxPacketSize(size_t max_size);

  // Get server bind address
  std::string GetBindAddress() const;

 private:
  // UnixSocket::EventListener implementation
  void OnNewIncomingConnection(
      UnixSocket* self, std::unique_ptr<UnixSocket> new_connection) override {}
  void OnConnect(UnixSocket* self, bool connected) override;
  void OnDisconnect(UnixSocket* self) override;
  void OnDataAvailable(UnixSocket* self) override;

  void UpdateClientInfo(const std::string& client_addr);
  uint64_t GetCurrentTimeMs() const;
  std::string FormatAddress(const std::string& ip, uint16_t port,
                            SockFamily family) const;

  TaskRunner* task_runner_;
  UdpServerEventListener* listener_;
  std::unique_ptr<UnixSocket> ipv4_socket_;
  std::unique_ptr<UnixSocket> ipv6_socket_;
  std::unordered_map<std::string, UdpClientInfo> clients_;
  bool running_;
  uint32_t client_timeout_ms_;
  size_t max_packet_size_;
  std::string bind_address_;
  uint16_t bind_port_;
};

}  // namespace xtils
