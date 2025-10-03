#include "xtils/net/udp_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <sstream>

namespace xtils {

UdpClient::UdpClient(TaskRunner* task_runner, UdpClientEventListener* listener)
    : task_runner_(task_runner),
      listener_(listener),
      state_(State::kClosed),
      server_port_(0),
      max_packet_size_(65536),
      family_(SockFamily::kInet) {}

UdpClient::~UdpClient() { Close(); }

bool UdpClient::Open(const std::string& ip, uint16_t port) {
  if (state_ != State::kClosed) {
    return false;
  }

  server_ip_ = ip.empty() ? "0.0.0.0" : ip;
  server_port_ = port;

  family_ = SockFamily::kInet;  // Default to IPv4

  // IPv6 addresses with brackets [::1] or contain multiple colons
  if (server_ip_.front() == '[' ||
      std::count(server_ip_.begin(), server_ip_.end(), ':') > 1) {
    family_ = SockFamily::kInet6;
  }

  // Create UDP socket
  auto raw_socket = UnixSocketRaw::CreateMayFail(family_, SockType::kDgram);
  if (!raw_socket) {
    HandleError("Failed to create UDP socket");
    return false;
  }

  // Connect to server if specified
  if (server_ip_ != "0.0.0.0" && server_port_ != 0) {
    std::string server_addr = FormatAddress(server_ip_, server_port_, family_);
    if (!raw_socket.Connect(server_addr)) {
      HandleError("Failed to connect UDP socket to " + server_addr);
      return false;
    }
  } else if (server_ip_ == "0.0.0.0" && server_port_ != 0) {
    std::string server_addr = FormatAddress(server_ip_, server_port_, family_);
    if (!raw_socket.Bind(server_addr)) {
      HandleError("Failed to bind UDP socket to " + server_addr);
      return false;
    }
  } else if (server_port_ == 0) {
    std::string server_addr = FormatAddress(server_ip_, server_port_, family_);
    if (!raw_socket.Bind(server_addr)) {
      HandleError("Failed to bind UDP socket to " + server_addr);
      return false;
    }
  }

  // Adopt the raw socket
  socket_ = UnixSocket::AdoptConnected(raw_socket.ReleaseFd(), this,
                                       task_runner_, family_, SockType::kDgram);
  if (!socket_) {
    HandleError("Failed to adopt UDP socket");
    return false;
  }

  SetState(State::kReady);

  if (listener_) {
    listener_->OnReady(this);
  }

  return true;
}

void UdpClient::Close() {
  if (socket_) {
    socket_->Shutdown(false);
    socket_.reset();
  }
  SetState(State::kClosed);

  if (listener_) {
    listener_->OnClosed(this);
  }
}

bool UdpClient::SendTo(const std::string& server_addr, const void* data,
                       size_t len) {
  if (state_ != State::kReady || !socket_ || len > max_packet_size_) {
    return false;
  }

  // Parse the server address
  auto [ip, port] = ParseAddress(server_addr);
  if (ip.empty() || port == 0) {
    return false;
  }

  // Create sockaddr structure based on address family
  if (family_ == SockFamily::kInet) {
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
      return false;
    }

    ssize_t sent =
        sendto(socket_->fd(), data, len, 0,
               reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(len);

  } else if (family_ == SockFamily::kInet6) {
    struct sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);

    if (inet_pton(AF_INET6, ip.c_str(), &addr.sin6_addr) != 1) {
      return false;
    }

    ssize_t sent =
        sendto(socket_->fd(), data, len, 0,
               reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(len);
  }

  return false;
}

bool UdpClient::SendStringTo(const std::string& server_addr,
                             const std::string& data) {
  return SendTo(server_addr, data.data(), data.size());
}

bool UdpClient::Send(const void* data, size_t len) {
  return socket_->Send(data, len);
}

bool UdpClient::SendString(const std::string& data) {
  return Send(data.data(), data.size());
}

size_t UdpClient::ReceiveFrom(void* buffer, size_t buffer_size,
                              std::string& sender_addr) {
  if (state_ != State::kReady || !socket_) {
    return 0;
  }

  // Use recvfrom to get both data and sender address
  struct sockaddr_storage sender_storage = {};
  socklen_t sender_len = sizeof(sender_storage);

  ssize_t bytes_read = recvfrom(
      socket_->fd(), buffer, buffer_size, 0,
      reinterpret_cast<struct sockaddr*>(&sender_storage), &sender_len);

  if (bytes_read > 0) {
    // Convert sender address to string format
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
  } else if (bytes_read < 0) {
    // Handle error cases
    return 0;
  }

  return bytes_read > 0 ? static_cast<size_t>(bytes_read) : 0;
}

size_t UdpClient::Receive(void* buffer, size_t buffer_size) {
  return socket_->Receive(buffer, buffer_size);
}

std::string UdpClient::GetLocalAddress() const {
  if (!socket_) {
    return "";
  }
  return socket_->GetSockAddr();
}

SocketHandle UdpClient::GetSocketFd() const {
  if (!socket_) {
    return -1;
  }
  return socket_->fd();
}

void UdpClient::SetSendTimeout(uint32_t timeout_ms) {
  if (socket_) {
    socket_->SetTxTimeout(timeout_ms);
  }
}

void UdpClient::SetReceiveTimeout(uint32_t timeout_ms) {
  if (socket_) {
    socket_->SetRxTimeout(timeout_ms);
  }
}

void UdpClient::SetMaxPacketSize(size_t max_size) {
  max_packet_size_ = max_size;
}

void UdpClient::SetBroadcast(bool enable) {
  if (!socket_) {
    return;
  }

  int opt = enable ? 1 : 0;
  if (setsockopt(socket_->fd(), SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) <
      0) {
    HandleError("Failed to set SO_BROADCAST option");
  }
}

void UdpClient::SetReuseAddress(bool enable) {
  if (!socket_) {
    return;
  }

  int opt = enable ? 1 : 0;
  if (setsockopt(socket_->fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    HandleError("Failed to set SO_REUSEADDR option");
  }
}

bool UdpClient::JoinMulticastGroup(const std::string& group_addr,
                                   const std::string& interface_addr) {
  if (!socket_ || group_addr.empty()) {
    return false;
  }

  if (family_ == SockFamily::kInet) {
    struct ip_mreq mreq = {};

    // Set multicast group address
    if (inet_pton(AF_INET, group_addr.c_str(), &mreq.imr_multiaddr) != 1) {
      return false;
    }

    // Set interface address (use INADDR_ANY if not specified)
    if (interface_addr.empty()) {
      mreq.imr_interface.s_addr = INADDR_ANY;
    } else {
      if (inet_pton(AF_INET, interface_addr.c_str(), &mreq.imr_interface) !=
          1) {
        return false;
      }
    }

    return setsockopt(socket_->fd(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                      sizeof(mreq)) == 0;

  } else if (family_ == SockFamily::kInet6) {
    struct ipv6_mreq mreq = {};

    // Set multicast group address
    if (inet_pton(AF_INET6, group_addr.c_str(), &mreq.ipv6mr_multiaddr) != 1) {
      return false;
    }

    // Set interface index (0 for default interface)
    mreq.ipv6mr_interface = 0;

    return setsockopt(socket_->fd(), IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq,
                      sizeof(mreq)) == 0;
  }

  return false;
}

bool UdpClient::LeaveMulticastGroup(const std::string& group_addr,
                                    const std::string& interface_addr) {
  if (!socket_ || group_addr.empty()) {
    return false;
  }

  if (family_ == SockFamily::kInet) {
    struct ip_mreq mreq = {};

    // Set multicast group address
    if (inet_pton(AF_INET, group_addr.c_str(), &mreq.imr_multiaddr) != 1) {
      return false;
    }

    // Set interface address (use INADDR_ANY if not specified)
    if (interface_addr.empty()) {
      mreq.imr_interface.s_addr = INADDR_ANY;
    } else {
      if (inet_pton(AF_INET, interface_addr.c_str(), &mreq.imr_interface) !=
          1) {
        return false;
      }
    }

    return setsockopt(socket_->fd(), IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq,
                      sizeof(mreq)) == 0;

  } else if (family_ == SockFamily::kInet6) {
    struct ipv6_mreq mreq = {};

    // Set multicast group address
    if (inet_pton(AF_INET6, group_addr.c_str(), &mreq.ipv6mr_multiaddr) != 1) {
      return false;
    }

    // Set interface index (0 for default interface)
    mreq.ipv6mr_interface = 0;

    return setsockopt(socket_->fd(), IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq,
                      sizeof(mreq)) == 0;
  }

  return false;
}

void UdpClient::SetMulticastTTL(uint8_t ttl) {
  if (!socket_) {
    return;
  }

  if (family_ == SockFamily::kInet) {
    // For IPv4, use IP_MULTICAST_TTL
    int ttl_val = static_cast<int>(ttl);
    if (setsockopt(socket_->fd(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl_val,
                   sizeof(ttl_val)) < 0) {
      HandleError("Failed to set IP_MULTICAST_TTL option");
    }
  } else if (family_ == SockFamily::kInet6) {
    // For IPv6, use IPV6_MULTICAST_HOPS
    int hops = static_cast<int>(ttl);
    if (setsockopt(socket_->fd(), IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops,
                   sizeof(hops)) < 0) {
      HandleError("Failed to set IPV6_MULTICAST_HOPS option");
    }
  }
}

void UdpClient::SetMulticastLoopback(bool enable) {
  if (!socket_) {
    return;
  }

  if (family_ == SockFamily::kInet) {
    // For IPv4, use IP_MULTICAST_LOOP
    int opt = enable ? 1 : 0;
    if (setsockopt(socket_->fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &opt,
                   sizeof(opt)) < 0) {
      HandleError("Failed to set IP_MULTICAST_LOOP option");
    }
  } else if (family_ == SockFamily::kInet6) {
    // For IPv6, use IPV6_MULTICAST_LOOP
    int opt = enable ? 1 : 0;
    if (setsockopt(socket_->fd(), IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &opt,
                   sizeof(opt)) < 0) {
      HandleError("Failed to set IPV6_MULTICAST_LOOP option");
    }
  }
}

void UdpClient::OnDisconnect(UnixSocket* self) {
  SetState(State::kClosed);

  if (listener_) {
    listener_->OnClosed(this);
  }
}

void UdpClient::OnDataAvailable(UnixSocket* self) {
  if (state_ != State::kReady || !listener_) {
    return;
  }

  // Read available data
  std::vector<uint8_t> buffer(max_packet_size_);

  size_t bytes_read = socket_->Receive(buffer.data(), buffer.size());
  if (bytes_read > 0) {
    listener_->OnDataReceived(this, buffer.data(), bytes_read);
  }
}

void UdpClient::SetState(State new_state) {
  if (state_ != new_state) {
    state_ = new_state;
  }
}

void UdpClient::HandleError(const std::string& error) {
  SetState(State::kError);

  if (listener_) {
    listener_->OnError(this, error);
  }
}

std::pair<std::string, uint16_t> UdpClient::ParseAddress(
    const std::string& address) const {
  std::string ip;
  uint16_t port = 0;

  if (address.empty()) {
    return {ip, port};
  }

  // Handle IPv6 format [ip]:port
  if (address.front() == '[') {
    size_t bracket_pos = address.find(']');
    if (bracket_pos == std::string::npos) {
      return {ip, port};  // Invalid format
    }

    ip = address.substr(1, bracket_pos - 1);

    // Look for port after the closing bracket
    if (bracket_pos + 1 < address.length() && address[bracket_pos + 1] == ':') {
      std::string port_str = address.substr(bracket_pos + 2);
      if (!port_str.empty()) {
        try {
          port = static_cast<uint16_t>(std::stoul(port_str));
        } catch (const std::exception&) {
          port = 0;  // Invalid port number
        }
      }
    }
  } else {
    // Handle IPv4 format ip:port
    size_t colon_pos = address.rfind(':');
    if (colon_pos != std::string::npos) {
      ip = address.substr(0, colon_pos);
      std::string port_str = address.substr(colon_pos + 1);
      if (!port_str.empty()) {
        try {
          port = static_cast<uint16_t>(std::stoul(port_str));
        } catch (const std::exception&) {
          port = 0;  // Invalid port number
        }
      }
    }
  }

  return {ip, port};
}

std::string UdpClient::FormatAddress(const std::string& ip, uint16_t port,
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
