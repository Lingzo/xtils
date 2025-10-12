#include "xtils/net/tcp_client.h"

#include <sstream>

namespace xtils {

TcpClient::TcpClient(TaskRunner* task_runner, TcpClientEventListener* listener)
    : task_runner_(task_runner),
      listener_(listener),
      state_(State::kDisconnected),
      server_port_(0),
      connect_timeout_ms_(5000) {}

TcpClient::~TcpClient() { Disconnect(); }

bool TcpClient::Connect(const std::string& address, uint16_t port) {
  if (state_ != State::kDisconnected) {
    return false;
  }

  server_address_ = address;
  server_port_ = port;

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

  socket_ = UnixSocket::Connect(socket_addr, this, task_runner_, family,
                                SockType::kStream);

  if (!socket_ && !socket_->is_connected()) {
    HandleError("Failed to create socket");
    return false;
  }

  SetState(State::kConnecting);
  return true;
}

bool TcpClient::ConnectToHost(const std::string& hostname, uint16_t port) {
  // TODO: Implement hostname resolution and connection logic
  std::string address = hostname;
  return Connect(address, port);
}

void TcpClient::Disconnect() {
  if (socket_) {
    socket_->Shutdown(false);
    socket_.reset();
  }
  SetState(State::kDisconnected);
}

bool TcpClient::Send(const void* data, size_t len) {
  if (state_ != State::kConnected || !socket_) {
    return false;
  }

  return socket_->Send(data, len);
}

bool TcpClient::SendString(const std::string& data) {
  return Send(data.data(), data.size());
}

size_t TcpClient::Receive(void* buffer, size_t buffer_size) {
  if (state_ != State::kConnected || !socket_) {
    return 0;
  }

  return socket_->Receive(buffer, buffer_size);
}

std::string TcpClient::GetServerAddress() const {
  if (!socket_) {
    return "";
  }
  return socket_->GetSockAddr();
}

SocketHandle TcpClient::GetSocketFd() const {
  if (!socket_) {
    return -1;
  }
  return socket_->fd();
}

void TcpClient::SetConnectTimeout(uint32_t timeout_ms) {
  connect_timeout_ms_ = timeout_ms;
}

void TcpClient::SetSendTimeout(uint32_t timeout_ms) {
  if (socket_) {
    socket_->SetTxTimeout(timeout_ms);
  }
}

void TcpClient::SetReceiveTimeout(uint32_t timeout_ms) {
  if (socket_) {
    socket_->SetRxTimeout(timeout_ms);
  }
}

void TcpClient::SetKeepAlive(bool enable) {
  // This would require platform-specific socket option setting
  // For now, we'll leave it as a placeholder
  (void)enable;
}

void TcpClient::SetNoDelay(bool enable) {
  // This would require setting TCP_NODELAY socket option
  // For now, we'll leave it as a placeholder
  (void)enable;
}

void TcpClient::OnConnect(UnixSocket* self, bool connected) {
  if (connected) {
    SetState(State::kConnected);
  } else {
    SetState(State::kError);
    HandleError("Connection failed");
  }

  if (listener_) {
    listener_->OnConnected(this, connected);
  }
}

void TcpClient::OnDisconnect(UnixSocket* self) {
  SetState(State::kDisconnected);

  if (listener_) {
    listener_->OnDisconnected(this);
  }
}

void TcpClient::OnDataAvailable(UnixSocket* self) {
  if (state_ != State::kConnected || !listener_) {
    return;
  }

  // Read available data
  constexpr size_t kBufferSize = 8192;
  uint8_t buffer[kBufferSize];

  size_t bytes_read = socket_->Receive(buffer, sizeof(buffer));
  if (bytes_read > 0) {
    listener_->OnDataReceived(this, buffer, bytes_read);
  } else if (bytes_read < 0) {
    // Connection closed or error
    SetState(State::kDisconnected);
    if (listener_) {
      listener_->OnDisconnected(this);
    }
  }
}

void TcpClient::SetState(State new_state) {
  if (state_ != new_state) {
    state_ = new_state;
  }
}

void TcpClient::HandleError(const std::string& error) {
  SetState(State::kError);

  if (listener_) {
    listener_->OnError(this, error);
  }
}

}  // namespace xtils
