#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "xtils/system/unix_socket.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

class TcpServerConnection;

class TcpServerEventListener {
 public:
  virtual ~TcpServerEventListener() = default;

  // Called when a new client connects
  virtual void OnClientConnected(TcpServerConnection* conn) = 0;

  // Called when data is available from a client
  virtual void OnDataReceived(TcpServerConnection* conn, const void* data,
                              size_t len) = 0;

  // Called when a client disconnects
  virtual void OnClientDisconnected(TcpServerConnection* conn) = 0;

  // Called when server encounters an error
  virtual void OnServerError(const std::string& error) {}
};

class TcpServerConnection {
 public:
  explicit TcpServerConnection(std::unique_ptr<UnixSocket> socket);
  ~TcpServerConnection();

  // Send data to the client
  bool Send(const void* data, size_t len);
  bool SendString(const std::string& data);

  // Close this connection
  void Close();

  // Get client address information
  std::string GetPeerAddress() const;

  // Check if connection is still active
  bool IsConnected() const;

  // Get the underlying socket file descriptor
  SocketHandle GetSocketFd() const;

 private:
  friend class TcpServer;

  std::unique_ptr<UnixSocket> socket_;
  bool connected_;
};

class TcpServer : public UnixSocket::EventListener {
 public:
  explicit TcpServer(TaskRunner* task_runner, TcpServerEventListener* listener);
  ~TcpServer() override;

  // Start listening on the specified address and port
  // address can be "0.0.0.0" for IPv4 or "::" for IPv6
  bool Start(const std::string& address, uint16_t port);

  // Start listening on IPv4 and IPv6
  bool StartDualStack(uint16_t port);

  // Stop the server and close all connections
  void Stop();

  // Get the number of active connections
  size_t GetConnectionCount() const;

  // Close a specific connection
  void CloseConnection(TcpServerConnection* conn);

  // Broadcast data to all connected clients
  void Broadcast(const void* data, size_t len);
  void BroadcastString(const std::string& data);

  // Check if server is running
  bool IsRunning() const;

 private:
  // UnixSocket::EventListener implementation
  void OnNewIncomingConnection(
      UnixSocket* self, std::unique_ptr<UnixSocket> new_connection) override;
  void OnConnect(UnixSocket* self, bool connected) override;
  void OnDisconnect(UnixSocket* self) override;
  void OnDataAvailable(UnixSocket* self) override;

  void RemoveConnection(TcpServerConnection* conn);

  TaskRunner* task_runner_;
  TcpServerEventListener* listener_;
  std::unique_ptr<UnixSocket> ipv4_socket_;
  std::unique_ptr<UnixSocket> ipv6_socket_;
  std::list<std::unique_ptr<TcpServerConnection>> connections_;
  bool running_;
};

}  // namespace xtils
