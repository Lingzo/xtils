#include "xtils/net/tcp_server.h"

#include <algorithm>
#include <sstream>

namespace xtils {

// TcpServerConnection implementation

TcpServerConnection::TcpServerConnection(std::unique_ptr<UnixSocket> socket)
    : socket_(std::move(socket)), connected_(true) {}

TcpServerConnection::~TcpServerConnection() {
  if (connected_) {
    Close();
  }
}

bool TcpServerConnection::Send(const void* data, size_t len) {
  if (!connected_ || !socket_) {
    return false;
  }

  return socket_->Send(data, len);
}

bool TcpServerConnection::SendString(const std::string& data) {
  return Send(data.data(), data.size());
}

void TcpServerConnection::Close() {
  if (socket_) {
    socket_->Shutdown(false);
  }
  connected_ = false;
}

std::string TcpServerConnection::GetPeerAddress() const {
  if (!socket_) {
    return "";
  }
  return socket_->GetSockAddr();
}

bool TcpServerConnection::IsConnected() const {
  return connected_ && socket_ && socket_->is_connected();
}

SocketHandle TcpServerConnection::GetSocketFd() const {
  if (!socket_) {
    return -1;
  }
  return socket_->fd();
}

// TcpServer implementation

TcpServer::TcpServer(TaskRunner* task_runner, TcpServerEventListener* listener)
    : task_runner_(task_runner), listener_(listener), running_(false) {}

TcpServer::~TcpServer() { Stop(); }

bool TcpServer::Start(const std::string& address, uint16_t port) {
  if (running_) {
    return false;
  }

  std::stringstream ss;
  SockFamily family;

  // Determine if this is IPv4 or IPv6 based on address format
  if (address.find(':') != std::string::npos) {
    // IPv6 address
    ss << "[" << address << "]:" << port;
    family = SockFamily::kInet6;
  } else {
    // IPv4 address
    ss << address << ":" << port;
    family = SockFamily::kInet;
  }

  std::string socket_addr = ss.str();

  auto socket = UnixSocket::Listen(socket_addr, this, task_runner_, family,
                                   SockType::kStream);
  if (!socket || !socket->is_listening()) {
    if (listener_) {
      listener_->OnServerError("Failed to start server on " + socket_addr);
    }
    return false;
  }

  if (family == SockFamily::kInet) {
    ipv4_socket_ = std::move(socket);
  } else {
    ipv6_socket_ = std::move(socket);
  }

  running_ = true;
  return true;
}

bool TcpServer::StartDualStack(uint16_t port) {
  bool ipv4_ok = Start("0.0.0.0", port);
  bool ipv6_ok = Start("::", port);

  running_ = ipv4_ok || ipv6_ok;
  return running_;
}

void TcpServer::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Close all client connections
  for (auto& conn : connections_) {
    if (conn) {
      conn->Close();
    }
  }
  connections_.clear();

  // Close listening sockets
  if (ipv4_socket_) {
    ipv4_socket_->Shutdown(false);
    ipv4_socket_.reset();
  }

  if (ipv6_socket_) {
    ipv6_socket_->Shutdown(false);
    ipv6_socket_.reset();
  }
}

size_t TcpServer::GetConnectionCount() const { return connections_.size(); }

void TcpServer::CloseConnection(TcpServerConnection* conn) {
  if (!conn) {
    return;
  }

  conn->Close();
  RemoveConnection(conn);
}

void TcpServer::Broadcast(const void* data, size_t len) {
  auto it = connections_.begin();
  while (it != connections_.end()) {
    if ((*it)->IsConnected()) {
      (*it)->Send(data, len);
      ++it;
    } else {
      // Remove disconnected connections
      it = connections_.erase(it);
    }
  }
}

void TcpServer::BroadcastString(const std::string& data) {
  Broadcast(data.data(), data.size());
}

bool TcpServer::IsRunning() const { return running_; }

void TcpServer::OnNewIncomingConnection(
    UnixSocket* self, std::unique_ptr<UnixSocket> new_connection) {
  if (!running_ || !listener_) {
    return;
  }

  auto conn = std::make_unique<TcpServerConnection>(std::move(new_connection));
  TcpServerConnection* conn_ptr = conn.get();

  connections_.push_back(std::move(conn));

  listener_->OnClientConnected(conn_ptr);
}

void TcpServer::OnConnect(UnixSocket* self, bool connected) {
  // This is for listening sockets, not client connections
  if (!connected && listener_) {
    listener_->OnServerError("Server socket connection failed");
  }
}

void TcpServer::OnDisconnect(UnixSocket* self) {
  // Find and remove the connection
  auto it =
      std::find_if(connections_.begin(), connections_.end(),
                   [self](const std::unique_ptr<TcpServerConnection>& conn) {
                     return conn->socket_.get() == self;
                   });

  if (it != connections_.end()) {
    TcpServerConnection* conn_ptr = it->get();
    if (listener_) {
      listener_->OnClientDisconnected(conn_ptr);
    }
    connections_.erase(it);
  }
}

void TcpServer::OnDataAvailable(UnixSocket* self) {
  // Find the connection corresponding to this socket
  auto it =
      std::find_if(connections_.begin(), connections_.end(),
                   [self](const std::unique_ptr<TcpServerConnection>& conn) {
                     return conn->socket_.get() == self;
                   });

  if (it == connections_.end() || !listener_) {
    return;
  }

  TcpServerConnection* conn = it->get();

  // Read available data
  constexpr size_t kBufferSize = 8192;
  uint8_t buffer[kBufferSize];

  size_t bytes_read = conn->socket_->Receive(buffer, sizeof(buffer));
  if (bytes_read > 0) {
    listener_->OnDataReceived(conn, buffer, bytes_read);
  } else {
    // Connection closed or error
    conn->connected_ = false;
    if (listener_) {
      listener_->OnClientDisconnected(conn);
    }
    connections_.erase(it);
  }
}

void TcpServer::RemoveConnection(TcpServerConnection* conn) {
  auto it = std::find_if(connections_.begin(), connections_.end(),
                         [conn](const std::unique_ptr<TcpServerConnection>& c) {
                           return c.get() == conn;
                         });

  if (it != connections_.end()) {
    connections_.erase(it);
  }
}

}  // namespace xtils
