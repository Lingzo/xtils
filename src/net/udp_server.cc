#include "xtils/net/udp_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <sstream>

namespace xtils {

UdpServer::UdpServer(TaskRunner* task_runner, UdpServerEventListener* listener)
    : task_runner_(task_runner),
      listener_(listener),
      running_(false),
      client_timeout_ms_(300000),  // 5 minutes default
      max_packet_size_(65536),     // 64KB default
      bind_port_(0) {}

UdpServer::~UdpServer() { Stop(); }

bool UdpServer::Start(const std::string& address, uint16_t port) {
  if (running_) {
    return false;
  }

  bind_address_ = address;
  bind_port_ = port;

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

  // Create UDP socket using UnixSocketRaw first, then wrap it
  auto raw_socket = UnixSocketRaw::CreateMayFail(family, SockType::kDgram);
  if (!raw_socket) {
    if (listener_) {
      listener_->OnServerError("Failed to create UDP socket");
    }
    return false;
  }

  // Bind the socket
  if (!raw_socket.Bind(socket_addr)) {
    if (listener_) {
      listener_->OnServerError("Failed to bind UDP socket to " + socket_addr);
    }
    return false;
  }

  // Adopt the raw socket into a UnixSocket
  auto socket = UnixSocket::AdoptConnected(
      raw_socket.ReleaseFd(), this, task_runner_, family, SockType::kDgram);
  if (!socket) {
    if (listener_) {
      listener_->OnServerError("Failed to adopt UDP socket");
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

bool UdpServer::StartDualStack(uint16_t port) {
  bool ipv4_ok = Start("0.0.0.0", port);
  bool ipv6_ok = Start("::", port);

  running_ = ipv4_ok || ipv6_ok;
  return running_;
}

void UdpServer::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;
  clients_.clear();

  if (ipv4_socket_) {
    ipv4_socket_->Shutdown(false);
    ipv4_socket_.reset();
  }

  if (ipv6_socket_) {
    ipv6_socket_->Shutdown(false);
    ipv6_socket_.reset();
  }
}

bool UdpServer::SendTo(const std::string& client_addr, const void* data,
                       size_t len) {
  if (!running_ || len > max_packet_size_) {
    return false;
  }

  // Parse the client address
  std::string ip;
  uint16_t port = 0;
  SockFamily addr_family = SockFamily::kInet;

  if (client_addr.empty()) {
    return false;
  }

  // Handle IPv6 format [ip]:port
  if (client_addr.front() == '[') {
    size_t bracket_pos = client_addr.find(']');
    if (bracket_pos == std::string::npos) {
      return false;  // Invalid format
    }

    ip = client_addr.substr(1, bracket_pos - 1);
    addr_family = SockFamily::kInet6;

    // Look for port after the closing bracket
    if (bracket_pos + 1 < client_addr.length() &&
        client_addr[bracket_pos + 1] == ':') {
      std::string port_str = client_addr.substr(bracket_pos + 2);
      if (!port_str.empty()) {
        try {
          port = static_cast<uint16_t>(std::stoul(port_str));
        } catch (const std::exception&) {
          return false;  // Invalid port number
        }
      }
    }
  } else {
    // Handle IPv4 format ip:port
    size_t colon_pos = client_addr.rfind(':');
    if (colon_pos != std::string::npos) {
      ip = client_addr.substr(0, colon_pos);
      std::string port_str = client_addr.substr(colon_pos + 1);
      if (!port_str.empty()) {
        try {
          port = static_cast<uint16_t>(std::stoul(port_str));
        } catch (const std::exception&) {
          return false;  // Invalid port number
        }
      }
    }
  }

  if (ip.empty() || port == 0) {
    return false;
  }

  // Determine which socket to use based on address family
  UnixSocket* socket = nullptr;
  if (addr_family == SockFamily::kInet6) {
    socket = ipv6_socket_.get();
  } else {
    socket = ipv4_socket_.get();
  }

  if (!socket) {
    return false;
  }

  // Create sockaddr structure and send data
  if (addr_family == SockFamily::kInet) {
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
      return false;
    }

    ssize_t sent =
        sendto(socket->fd(), data, len, 0,
               reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(len);

  } else if (addr_family == SockFamily::kInet6) {
    struct sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);

    if (inet_pton(AF_INET6, ip.c_str(), &addr.sin6_addr) != 1) {
      return false;
    }

    ssize_t sent =
        sendto(socket->fd(), data, len, 0,
               reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(len);
  }

  return false;
}

bool UdpServer::SendStringTo(const std::string& client_addr,
                             const std::string& data) {
  return SendTo(client_addr, data.data(), data.size());
}

void UdpServer::Broadcast(const void* data, size_t len) {
  for (const auto& client : clients_) {
    SendTo(client.first, data, len);
  }
}

void UdpServer::BroadcastString(const std::string& data) {
  Broadcast(data.data(), data.size());
}

size_t UdpServer::GetClientCount() const { return clients_.size(); }

std::vector<std::string> UdpServer::GetClientAddresses() const {
  std::vector<std::string> addresses;
  addresses.reserve(clients_.size());

  for (const auto& client : clients_) {
    addresses.push_back(client.first);
  }

  return addresses;
}

bool UdpServer::IsRunning() const { return running_; }

void UdpServer::SetClientTimeout(uint32_t timeout_ms) {
  client_timeout_ms_ = timeout_ms;
}

void UdpServer::CleanupInactiveClients() {
  if (client_timeout_ms_ == 0) {
    return;  // No timeout configured
  }

  uint64_t current_time = GetCurrentTimeMs();
  auto it = clients_.begin();

  while (it != clients_.end()) {
    if ((current_time - it->second.last_seen_ms) > client_timeout_ms_) {
      std::string client_addr = it->first;
      it = clients_.erase(it);

      if (listener_) {
        listener_->OnClientTimeout(client_addr);
      }
    } else {
      ++it;
    }
  }
}

void UdpServer::SetMaxPacketSize(size_t max_size) {
  max_packet_size_ = max_size;
}

std::string UdpServer::GetBindAddress() const {
  if (ipv4_socket_) {
    return ipv4_socket_->GetSockAddr();
  } else if (ipv6_socket_) {
    return ipv6_socket_->GetSockAddr();
  }
  return "";
}

void UdpServer::OnConnect(UnixSocket* self, bool connected) {
  if (!connected && listener_) {
    listener_->OnServerError("UDP socket connection failed");
  }
}

void UdpServer::OnDisconnect(UnixSocket* self) {
  running_ = false;
  if (listener_) {
    listener_->OnServerError("UDP socket disconnected");
  }
}

void UdpServer::OnDataAvailable(UnixSocket* self) {
  if (!running_ || !listener_) {
    return;
  }

  // Read available data
  std::vector<uint8_t> buffer(max_packet_size_);

  // Use recvfrom to get both data and sender address
  struct sockaddr_storage sender_storage = {};
  socklen_t sender_len = sizeof(sender_storage);

  ssize_t bytes_read = recvfrom(
      self->fd(), buffer.data(), buffer.size(), 0,
      reinterpret_cast<struct sockaddr*>(&sender_storage), &sender_len);

  if (bytes_read > 0) {
    // Convert sender address to string format
    std::string sender_addr;
    char ip_str[INET6_ADDRSTRLEN];
    uint16_t port = 0;

    if (sender_storage.ss_family == AF_INET) {
      // IPv4 address
      struct sockaddr_in* addr_in =
          reinterpret_cast<struct sockaddr_in*>(&sender_storage);
      if (inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN)) {
        port = ntohs(addr_in->sin_port);
        sender_addr = FormatAddress(ip_str, port, SockFamily::kInet);
      }
    } else if (sender_storage.ss_family == AF_INET6) {
      // IPv6 address
      struct sockaddr_in6* addr_in6 =
          reinterpret_cast<struct sockaddr_in6*>(&sender_storage);
      if (inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN)) {
        port = ntohs(addr_in6->sin6_port);
        sender_addr = FormatAddress(ip_str, port, SockFamily::kInet6);
      }
    }

    if (!sender_addr.empty()) {
      // Update client info
      UpdateClientInfo(sender_addr);

      // Notify listener
      listener_->OnDataReceived(sender_addr, buffer.data(),
                                static_cast<size_t>(bytes_read));
    }
  }
}

void UdpServer::UpdateClientInfo(const std::string& client_addr) {
  uint64_t current_time = GetCurrentTimeMs();

  auto it = clients_.find(client_addr);
  if (it == clients_.end()) {
    // New client
    clients_[client_addr] = UdpClientInfo(client_addr, 0, current_time);
    if (listener_) {
      listener_->OnNewClient(client_addr);
    }
  } else {
    // Update existing client
    it->second.last_seen_ms = current_time;
  }
}

uint64_t UdpServer::GetCurrentTimeMs() const {
  auto now = std::chrono::steady_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

std::string UdpServer::FormatAddress(const std::string& ip, uint16_t port,
                                     SockFamily family) const {
  std::stringstream ss;
  if (family == SockFamily::kInet6) {
    ss << "[" << ip << "]:" << port;
  } else {
    ss << ip << ":" << port;
  }
  return ss.str();
}

}  // namespace xtils
