#include "xtils/system/unix_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cinttypes>
#include <memory>

#include "xtils/logging/logger.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/utils/string_utils.h"

namespace xtils {

// The CMSG_* macros use NULL instead of nullptr.
// Note: MSVC doesn't have #pragma GCC diagnostic, hence the if __GNUC__.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif

namespace {
inline bool IsAgain(int err) { return err == EAGAIN || err == EWOULDBLOCK; }

// Android takes an int instead of socklen_t for the control buffer size.
using CBufLenType = socklen_t;

// A wrapper around variable-size sockaddr structs.
// This is solving the following problem: when calling connect() or bind(), the
// caller needs to take care to allocate the right struct (sockaddr_un for
// AF_UNIX, sockaddr_in for AF_INET).   Those structs have different sizes and,
// more importantly, are bigger than the xtils struct sockaddr.
struct SockaddrAny {
  SockaddrAny() : size() {}
  SockaddrAny(const void* addr, socklen_t sz)
      : data(new char[static_cast<size_t>(sz)]), size(sz) {
    memcpy(data.get(), addr, static_cast<size_t>(size));
  }

  const struct sockaddr* addr() const {
    return reinterpret_cast<const struct sockaddr*>(data.get());
  }

  std::unique_ptr<char[]> data;
  socklen_t size;
};

inline int MkSockFamily(SockFamily family) {
  switch (family) {
    case SockFamily::kUnix:
      return AF_UNIX;
    case SockFamily::kInet:
      return AF_INET;
    case SockFamily::kInet6:
      return AF_INET6;
    case SockFamily::kUnspec:
      return AF_UNSPEC;
  }
  CHECK(false);  // For GCC.
  return 0;
}

inline int MkSockType(SockType type) {
#if defined(SOCK_CLOEXEC)
  constexpr int kSockCloExec = SOCK_CLOEXEC;
#else
  constexpr int kSockCloExec = 0;
#endif
  switch (type) {
    case SockType::kStream:
      return SOCK_STREAM | kSockCloExec;
    case SockType::kDgram:
      return SOCK_DGRAM | kSockCloExec;
    case SockType::kSeqPacket:
      return SOCK_SEQPACKET | kSockCloExec;
  }
  CHECK(false);  // For GCC.
  return 0;
}

SockaddrAny MakeSockAddr(SockFamily family, const std::string& socket_name) {
  switch (family) {
    case SockFamily::kUnix: {
      struct sockaddr_un saddr{};
      const size_t name_len = socket_name.size();
      if (name_len + 1 /* for trailing \0 */ >= sizeof(saddr.sun_path)) {
        errno = ENAMETOOLONG;
        return SockaddrAny();
      }
      memcpy(saddr.sun_path, socket_name.data(), name_len);
      if (saddr.sun_path[0] == '@') {
        saddr.sun_path[0] = '\0';
      }
      saddr.sun_family = AF_UNIX;
      auto size = static_cast<socklen_t>(
          __builtin_offsetof(sockaddr_un, sun_path) + name_len + 1);

      // Abstract sockets do NOT require a trailing null terminator (which is
      // instead mandatory for filesystem sockets). Any byte up to `size`,
      // including '\0' will become part of the socket name.
      if (saddr.sun_path[0] == '\0') --size;
      // CHECK(static_cast<size_t>(size) <= sizeof(saddr));
      return SockaddrAny(&saddr, size);
    }
    case SockFamily::kInet: {
      auto parts = SplitString(socket_name, ":");
      // CHECK(parts.size() == 2);
      struct addrinfo* addr_info = nullptr;
      struct addrinfo hints{};
      hints.ai_family = AF_INET;
      int ret =
          getaddrinfo(parts[0].c_str(), parts[1].c_str(), &hints, &addr_info);
      if (ret < 0 || addr_info == nullptr) {
        freeaddrinfo(addr_info);
        return SockaddrAny();
      }
      SockaddrAny res(addr_info->ai_addr,
                      static_cast<socklen_t>(addr_info->ai_addrlen));
      freeaddrinfo(addr_info);
      return res;
    }
    case SockFamily::kInet6: {
      auto parts = SplitString(socket_name, "]");
      // CHECK(parts.size() == 2);
      auto address = SplitString(parts[0], "[");
      // CHECK(address.size() == 1);
      auto port = SplitString(parts[1], ":");
      // CHECK(port.size() == 1);
      struct addrinfo* addr_info = nullptr;
      struct addrinfo hints{};
      hints.ai_family = AF_INET6;
      int ret =
          getaddrinfo(address[0].c_str(), port[0].c_str(), &hints, &addr_info);
      if (ret < 0 || addr_info == nullptr) {
        freeaddrinfo(addr_info);
        return SockaddrAny();
      }
      SockaddrAny res(addr_info->ai_addr,
                      static_cast<socklen_t>(addr_info->ai_addrlen));
      freeaddrinfo(addr_info);
      return res;
    }
    case SockFamily::kUnspec:
      errno = ENOTSOCK;
      return SockaddrAny();
  }
  CHECK(false);  // For GCC.
  return SockaddrAny();
}

ScopedSocketHandle CreateSocketHandle(SockFamily family, SockType type) {
  return ScopedSocketHandle(socket(MkSockFamily(family), MkSockType(type), 0));
}

}  // namespace

SockFamily GetSockFamily(const char* addr) {
  if (strlen(addr) == 0) return SockFamily::kUnspec;

  if (addr[0] == '@') return SockFamily::kUnix;  // Abstract AF_UNIX sockets.

  // If `addr` ends in :NNNN it's either a kInet or kInet6 socket.
  const char* col = strrchr(addr, ':');
  if (col && CStringToInt32(col + 1).has_value()) {
    return addr[0] == '[' ? SockFamily::kInet6 : SockFamily::kInet;
  }

  return SockFamily::kUnix;  // For anything else assume it's a linked AF_UNIX.
}

// +-----------------------+
// | UnixSocketRaw methods |
// +-----------------------+

// static
void UnixSocketRaw::ShiftMsgHdrPosix(size_t n, struct msghdr* msg) {
  using LenType = decltype(msg->msg_iovlen);  // Mac and Linux don't agree.
  for (LenType i = 0; i < msg->msg_iovlen; ++i) {
    struct iovec* vec = &msg->msg_iov[i];
    if (n < vec->iov_len) {
      // We sent a part of this iovec.
      vec->iov_base = reinterpret_cast<char*>(vec->iov_base) + n;
      vec->iov_len -= n;
      msg->msg_iov = vec;
      msg->msg_iovlen -= i;
      return;
    }
    // We sent the whole iovec.
    n -= vec->iov_len;
  }
  // We sent all the iovecs.
  // CHECK(n == 0);
  msg->msg_iovlen = 0;
  msg->msg_iov = nullptr;
}

// static
std::pair<UnixSocketRaw, UnixSocketRaw> UnixSocketRaw::CreatePairPosix(
    SockFamily family, SockType type) {
  int fds[2];
  if (socketpair(MkSockFamily(family), MkSockType(type), 0, fds) != 0) {
    return std::make_pair(UnixSocketRaw(), UnixSocketRaw());
  }
  return std::make_pair(UnixSocketRaw(ScopedFile(fds[0]), family, type),
                        UnixSocketRaw(ScopedFile(fds[1]), family, type));
}

// static
UnixSocketRaw UnixSocketRaw::CreateMayFail(SockFamily family, SockType type) {
  auto fd = CreateSocketHandle(family, type);
  if (!fd) return UnixSocketRaw();
  return UnixSocketRaw(std::move(fd), family, type);
}

UnixSocketRaw::UnixSocketRaw() = default;

UnixSocketRaw::UnixSocketRaw(SockFamily family, SockType type)
    : UnixSocketRaw(CreateSocketHandle(family, type), family, type) {}

UnixSocketRaw::UnixSocketRaw(ScopedSocketHandle fd, SockFamily family,
                             SockType type)
    : fd_(std::move(fd)), family_(family), type_(type) {
  if (family == SockFamily::kInet || family == SockFamily::kInet6) {
    int flag = 1;
    // The reinterpret_cast<const char*> is needed for Windows, where the 4th
    // arg is a const char* (on other POSIX system is a const void*).
    CHECK(!setsockopt(*fd_, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&flag), sizeof(flag)));
    // Disable Nagle's algorithm, optimize for low-latency.
    // See https://github.com/google/perfetto/issues/70.
    setsockopt(*fd_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
  }

  // There is no reason why a socket should outlive the process in case of
  // exec() by default, this is just working around a broken unix design.
  SetRetainOnExec(false);
}

void UnixSocketRaw::SetBlocking(bool is_blocking) {
  // DCHECK(fd_);
  int flags = fcntl(*fd_, F_GETFL, 0);
  if (!is_blocking) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~static_cast<int>(O_NONBLOCK);
  }
  int fcntl_res = fcntl(*fd_, F_SETFL, flags);
  // CHECK(fcntl_res == 0);
}

void UnixSocketRaw::SetRetainOnExec(bool retain) {
  // DCHECK(fd_);
  int flags = fcntl(*fd_, F_GETFD, 0);
  if (retain) {
    flags &= ~static_cast<int>(FD_CLOEXEC);
  } else {
    flags |= FD_CLOEXEC;
  }
  int fcntl_res = fcntl(*fd_, F_SETFD, flags);
  // CHECK(fcntl_res == 0);
}

void UnixSocketRaw::DcheckIsBlocking(bool expected) const {
  // DCHECK(fd_);
  bool is_blocking = (fcntl(*fd_, F_GETFL, 0) & O_NONBLOCK) == 0;
  // DCHECK(is_blocking == expected);
}

bool UnixSocketRaw::Bind(const std::string& socket_name) {
  // DCHECK(fd_);
  SockaddrAny addr = MakeSockAddr(family_, socket_name);
  if (addr.size == 0) return false;

  if (bind(*fd_, addr.addr(), addr.size)) {
    // DPLOG("bind(%s)", socket_name.c_str());
    return false;
  }

  return true;
}

bool UnixSocketRaw::Listen() {
  // DCHECK(fd_);
  // DCHECK(type_ == SockType::kStream || type_ ==
  // SockType::kSeqPacket);
  return listen(*fd_, SOMAXCONN) == 0;
}

bool UnixSocketRaw::Connect(const std::string& socket_name) {
  // DCHECK(fd_);
  SockaddrAny addr = MakeSockAddr(family_, socket_name);
  if (addr.size == 0) return false;

  int res = connect(*fd_, addr.addr(), addr.size);
  bool continue_async = errno == EINPROGRESS;
  bool is_blocking_call = false;
  if (is_blocking_call && res < 0 && continue_async) {
    pollfd pfd{*fd_, POLLOUT, 0};
    if (poll(&pfd, 1 /*nfds*/, 3000 /*timeout*/) <= 0) return false;
    return (pfd.revents & POLLOUT) != 0;
  }
  if (res && !continue_async) return false;

  return true;
}

void UnixSocketRaw::Shutdown() {
  shutdown(*fd_, SHUT_RDWR);
  fd_.reset();
}

// For the interested reader, Linux kernel dive to verify this is not only a
// theoretical possibility: sock_stream_sendmsg, if sock_alloc_send_pskb returns
// NULL [1] (which it does when it gets interrupted [2]), returns early with the
// amount of bytes already sent.
//
// [1]:
// https://elixir.bootlin.com/linux/v4.18.10/source/net/unix/af_unix.c#L1872
// [2]: https://elixir.bootlin.com/linux/v4.18.10/source/net/core/sock.c#L2101
ssize_t UnixSocketRaw::SendMsgAllPosix(struct msghdr* msg) {
  // This does not make sense on non-blocking sockets.
  // DCHECK(fd_);

  const bool is_blocking_with_timeout =
      tx_timeout_ms_ > 0 && ((fcntl(*fd_, F_GETFL, 0) & O_NONBLOCK) == 0);
  const int64_t start_ms = GetWallTimeMs().count();

  // Waits until some space is available in the tx buffer.
  // Returns true if some buffer space is available, false if times out.
  auto poll_or_timeout = [&] {
    // DCHECK(is_blocking_with_timeout);
    const int64_t deadline = start_ms + tx_timeout_ms_;
    const int64_t now_ms = GetWallTimeMs().count();
    if (now_ms >= deadline) return false;  // Timed out
    const int timeout_ms = static_cast<int>(deadline - now_ms);
    pollfd pfd{*fd_, POLLOUT, 0};
    return poll(&pfd, 1, timeout_ms) > 0;
  };

  int send_flags = MSG_NOSIGNAL | (is_blocking_with_timeout ? MSG_DONTWAIT : 0);

  ssize_t total_sent = 0;
  while (msg->msg_iov) {
    ssize_t send_res = sendmsg(*fd_, msg, send_flags);
    if (send_res == -1 && IsAgain(errno)) {
      if (is_blocking_with_timeout && poll_or_timeout()) {
        continue;  // Tx buffer unblocked, repeat the loop.
      }
      return total_sent;
    } else if (send_res <= 0) {
      return send_res;  // An error occurred.
    } else {
      total_sent += send_res;
      ShiftMsgHdrPosix(static_cast<size_t>(send_res), msg);
      // Only send the ancillary data with the first sendmsg call.
      msg->msg_control = nullptr;
      msg->msg_controllen = 0;
    }
  }
  return total_sent;
}

ssize_t UnixSocketRaw::Send(const void* msg, size_t len, const int* send_fds,
                            size_t num_fds) {
  DCHECK(fd_);
  msghdr msg_hdr = {};
  iovec iov = {const_cast<void*>(msg), len};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (num_fds > 0) {
    const auto raw_ctl_data_sz = num_fds * sizeof(int);
    const CBufLenType control_buf_len =
        static_cast<CBufLenType>(CMSG_SPACE(raw_ctl_data_sz));
    CHECK(control_buf_len <= sizeof(control_buf));
    memset(control_buf, 0, sizeof(control_buf));
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen = control_buf_len;  // used by CMSG_FIRSTHDR
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = static_cast<CBufLenType>(CMSG_LEN(raw_ctl_data_sz));
    memcpy(CMSG_DATA(cmsg), send_fds, num_fds * sizeof(int));
    // note: if we were to send multiple cmsghdr structures, then
    // msg_hdr.msg_controllen would need to be adjusted, see "man 3 cmsg".
  }

  return SendMsgAllPosix(&msg_hdr);
}

ssize_t UnixSocketRaw::Receive(void* msg, size_t len, ScopedFile* fd_vec,
                               size_t max_files) {
  DCHECK(fd_);
  msghdr msg_hdr = {};
  iovec iov = {msg, len};
  msg_hdr.msg_iov = &iov;
  msg_hdr.msg_iovlen = 1;
  alignas(cmsghdr) char control_buf[256];

  if (max_files > 0) {
    msg_hdr.msg_control = control_buf;
    msg_hdr.msg_controllen =
        static_cast<CBufLenType>(CMSG_SPACE(max_files * sizeof(int)));
    CHECK(msg_hdr.msg_controllen <= sizeof(control_buf));
  }
  const ssize_t sz = recvmsg(*fd_, &msg_hdr, 0);
  if (sz <= 0) {
    return sz;
  }
  CHECK(static_cast<size_t>(sz) <= len);

  int* fds = nullptr;
  uint32_t fds_len = 0;

  if (max_files > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg_hdr); cmsg;
         cmsg = CMSG_NXTHDR(&msg_hdr, cmsg)) {
      const size_t payload_len = cmsg->cmsg_len - CMSG_LEN(0);
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        DCHECK(payload_len % sizeof(int) == 0u);
        CHECK(fds == nullptr);
        fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        fds_len = static_cast<uint32_t>(payload_len / sizeof(int));
      }
    }
  }

  if (msg_hdr.msg_flags & MSG_TRUNC || msg_hdr.msg_flags & MSG_CTRUNC) {
    for (size_t i = 0; fds && i < fds_len; ++i) close(fds[i]);
    errno = EMSGSIZE;
    return -1;
  }

  for (size_t i = 0; fds && i < fds_len; ++i) {
    if (i < max_files)
      fd_vec[i].reset(fds[i]);
    else
      close(fds[i]);
  }

  return sz;
}

bool UnixSocketRaw::SetTxTimeout(uint32_t timeout_ms) {
  DCHECK(fd_);
  // On Unix-based systems, SO_SNDTIMEO isn't used for Send() because it's
  // unreliable (b/193234818). Instead we use non-blocking sendmsg() + poll().
  // See SendMsgAllPosix(). We still make the setsockopt call because
  // SO_SNDTIMEO also affects connect().
  tx_timeout_ms_ = timeout_ms;
  struct timeval timeout{};
  uint32_t timeout_sec = timeout_ms / 1000;
  timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_sec);
  timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(
      (timeout_ms - (timeout_sec * 1000)) * 1000);

  return setsockopt(*fd_, SOL_SOCKET, SO_SNDTIMEO,
                    reinterpret_cast<const char*>(&timeout),
                    sizeof(timeout)) == 0;
}

bool UnixSocketRaw::SetRxTimeout(uint32_t timeout_ms) {
  DCHECK(fd_);
  struct timeval timeout{};
  uint32_t timeout_sec = timeout_ms / 1000;
  timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_sec);
  timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(
      (timeout_ms - (timeout_sec * 1000)) * 1000);
  return setsockopt(*fd_, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&timeout),
                    sizeof(timeout)) == 0;
}

std::string UnixSocketRaw::GetSockAddr() const {
  struct sockaddr_storage stg{};
  socklen_t slen = sizeof(stg);
  int ret = getsockname(*fd_, reinterpret_cast<struct sockaddr*>(&stg), &slen);
  if (ret < 0) {
    LogT("Failed to get socket address");
    return {};
  }
  char addr[255]{};

  if (stg.ss_family == AF_UNIX) {
    auto* saddr = reinterpret_cast<struct sockaddr_un*>(&stg);
    static_assert(sizeof(addr) >= sizeof(saddr->sun_path), "addr too small");
    memcpy(addr, saddr->sun_path, sizeof(saddr->sun_path));
    addr[0] = addr[0] == '\0' ? '@' : addr[0];
    addr[sizeof(saddr->sun_path) - 1] = '\0';
    return std::string(addr);
  }

  if (stg.ss_family == AF_INET) {
    auto* saddr = reinterpret_cast<struct sockaddr_in*>(&stg);
    inet_ntop(AF_INET, &saddr->sin_addr, addr, sizeof(addr));
    uint16_t port = ntohs(saddr->sin_port);
    xtils::StackString<255> addr_and_port("%s:%" PRIu16, addr, port);
    return addr_and_port.ToStr();
  }

  if (stg.ss_family == AF_INET6) {
    auto* saddr = reinterpret_cast<struct sockaddr_in6*>(&stg);
    CHECK(inet_ntop(AF_INET6, &saddr->sin6_addr, addr, sizeof(addr)));
    auto port = ntohs(saddr->sin6_port);
    xtils::StackString<255> addr_and_port("[%s]:%" PRIu16, addr, port);
    return addr_and_port.ToStr();
  }

  LogT("GetSockAddr() unsupported on family %d", stg.ss_family);
  return {};
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// +--------------------+
// | UnixSocket methods |
// +--------------------+

// static
std::unique_ptr<UnixSocket> UnixSocket::Listen(const std::string& socket_name,
                                               EventListener* event_listener,
                                               TaskRunner* task_runner,
                                               SockFamily sock_family,
                                               SockType sock_type) {
  auto sock_raw = UnixSocketRaw::CreateMayFail(sock_family, sock_type);
  if (!sock_raw || !sock_raw.Bind(socket_name)) return nullptr;

  // Forward the call to the Listen() overload below.
  return Listen(sock_raw.ReleaseFd(), event_listener, task_runner, sock_family,
                sock_type);
}

// static
std::unique_ptr<UnixSocket> UnixSocket::Listen(ScopedSocketHandle fd,
                                               EventListener* event_listener,
                                               TaskRunner* task_runner,
                                               SockFamily sock_family,
                                               SockType sock_type) {
  return std::unique_ptr<UnixSocket>(new UnixSocket(
      event_listener, task_runner, std::move(fd), State::kListening,
      sock_family, sock_type, SockPeerCredMode::kDefault));
}

// static
std::unique_ptr<UnixSocket> UnixSocket::Connect(
    const std::string& socket_name, EventListener* event_listener,
    TaskRunner* task_runner, SockFamily sock_family, SockType sock_type,
    SockPeerCredMode peer_cred_mode) {
  std::unique_ptr<UnixSocket> sock(new UnixSocket(
      event_listener, task_runner, sock_family, sock_type, peer_cred_mode));
  sock->DoConnect(socket_name);
  return sock;
}

// static
std::unique_ptr<UnixSocket> UnixSocket::AdoptConnected(
    ScopedSocketHandle fd, EventListener* event_listener,
    TaskRunner* task_runner, SockFamily sock_family, SockType sock_type,
    SockPeerCredMode peer_cred_mode) {
  return std::unique_ptr<UnixSocket>(new UnixSocket(
      event_listener, task_runner, std::move(fd), State::kConnected,
      sock_family, sock_type, peer_cred_mode));
}

UnixSocket::UnixSocket(EventListener* event_listener, TaskRunner* task_runner,
                       SockFamily sock_family, SockType sock_type,
                       SockPeerCredMode peer_cred_mode)
    : UnixSocket(event_listener, task_runner, ScopedSocketHandle(),
                 State::kDisconnected, sock_family, sock_type, peer_cred_mode) {
}

UnixSocket::UnixSocket(EventListener* event_listener, TaskRunner* task_runner,
                       ScopedSocketHandle adopt_fd, State adopt_state,
                       SockFamily sock_family, SockType sock_type,
                       SockPeerCredMode peer_cred_mode)
    : peer_cred_mode_(peer_cred_mode),
      event_listener_(event_listener),
      task_runner_(task_runner),
      weak_ptr_factory_(this) {
  state_ = State::kDisconnected;
  if (adopt_state == State::kDisconnected) {
    sock_raw_ = UnixSocketRaw::CreateMayFail(sock_family, sock_type);
    if (!sock_raw_) return;
  } else if (adopt_state == State::kConnected) {
    sock_raw_ = UnixSocketRaw(std::move(adopt_fd), sock_family, sock_type);
    state_ = State::kConnected;
    if (peer_cred_mode_ == SockPeerCredMode::kReadOnConnect)
      ReadPeerCredentialsPosix();
  } else if (adopt_state == State::kListening) {
    // We get here from Listen().

    // |adopt_fd| might genuinely be invalid if the bind() failed.
    if (!adopt_fd) return;

    sock_raw_ = UnixSocketRaw(std::move(adopt_fd), sock_family, sock_type);
    if (!sock_raw_.Listen()) {
      return;
    }
    state_ = State::kListening;
  } else {
    // FATAL("Unexpected adopt_state");  // Unfeasible.
  }

  CHECK(sock_raw_);

  sock_raw_.SetBlocking(false);

  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner_->AddFileDescriptorWatch(sock_raw_.watch_handle(), [weak_ptr] {
    if (weak_ptr) {
      weak_ptr->OnEvent();
    }
  });
}

UnixSocket::~UnixSocket() {
  // The implicit dtor of |weak_ptr_factory_| will no-op pending callbacks.
  Shutdown(true);
}

UnixSocketRaw UnixSocket::ReleaseSocket() {
  // This will invalidate any pending calls to OnEvent.
  state_ = State::kDisconnected;
  if (sock_raw_)
    task_runner_->RemoveFileDescriptorWatch(sock_raw_.watch_handle());

  return std::move(sock_raw_);
}

// Called only by the Connect() static constructor.
void UnixSocket::DoConnect(const std::string& socket_name) {
  // DCHECK(state_ == State::kDisconnected);

  // This is the only thing that can gracefully fail in the ctor.
  if (!sock_raw_) return NotifyConnectionState(false);

  if (!sock_raw_.Connect(socket_name)) return NotifyConnectionState(false);

  // At this point either connect() succeeded or started asynchronously
  // (errno = EINPROGRESS).
  state_ = State::kConnecting;

  // Even if the socket is non-blocking, connecting to a UNIX socket can be
  // acknowledged straight away rather than returning EINPROGRESS.
  // The decision here is to deal with the two cases uniformly, at the cost of
  // delaying the straight-away-connect() case by one task, to avoid depending
  // on implementation details of UNIX socket on the various OSes.
  // Posting the OnEvent() below emulates a wakeup of the FD watch. OnEvent(),
  // which knows how to deal with spurious wakeups, will poll the SO_ERROR and
  // evolve, if necessary, the state into either kConnected or kDisconnected.
  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_ptr] {
    if (weak_ptr) {
      weak_ptr->OnEvent();
    }
  });
}

void UnixSocket::ReadPeerCredentialsPosix() {
  // Peer credentials are supported only on AF_UNIX sockets.
  if (sock_raw_.family() != SockFamily::kUnix) return;
  CHECK(peer_cred_mode_ != SockPeerCredMode::kIgnore);
}

void UnixSocket::OnEvent() {
  if (state_ == State::kDisconnected)
    return;  // Some spurious event, typically queued just before Shutdown().

  if (state_ == State::kConnected)
    return event_listener_->OnDataAvailable(this);

  if (state_ == State::kConnecting) {
    // DCHECK(sock_raw_);
    int res = 0, sock_err = 0;
    bool is_error_opt_supported = true;
    if (is_error_opt_supported) {
      sock_err = EINVAL;
      socklen_t err_len = sizeof(sock_err);
      res =
          getsockopt(sock_raw_.fd(), SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
    }
    if (res == 0 && sock_err == EINPROGRESS)
      return;  // Not connected yet, just a spurious FD watch wakeup.
    if (res == 0 && sock_err == 0) {
      if (peer_cred_mode_ == SockPeerCredMode::kReadOnConnect)
        ReadPeerCredentialsPosix();
      state_ = State::kConnected;
      return event_listener_->OnConnect(this, true /* connected */);
    }
    // DLOG("Connection error: %s", strerror(sock_err));
    Shutdown(false);
    return event_listener_->OnConnect(this, false /* connected */);
  }

  // New incoming connection.
  if (state_ == State::kListening) {
    // There could be more than one incoming connection behind each FD watch
    // notification. Drain'em all.
    for (;;) {
      ScopedFile new_fd(accept(sock_raw_.fd(), nullptr, nullptr));
      if (!new_fd) return;
      std::unique_ptr<UnixSocket> new_sock(new UnixSocket(
          event_listener_, task_runner_, std::move(new_fd), State::kConnected,
          sock_raw_.family(), sock_raw_.type(), peer_cred_mode_));
      event_listener_->OnNewIncomingConnection(this, std::move(new_sock));
    }
  }
}
bool UnixSocket::Send(const void* msg, size_t len, const int* send_fds,
                      size_t num_fds) {
  if (state_ != State::kConnected) {
    errno = ENOTCONN;
    return false;
  }

  sock_raw_.SetBlocking(true);
  const ssize_t sz = sock_raw_.Send(msg, len, send_fds, num_fds);
  sock_raw_.SetBlocking(false);

  if (sz == static_cast<ssize_t>(len)) {
    return true;
  }

  // If we ever decide to support non-blocking sends again, here we should
  // watch for both EAGAIN and EWOULDBLOCK (see xtils::IsAgain()).

  // If sendmsg() succeeds but the returned size is >= 0 and < |len| it means
  // that the endpoint disconnected in the middle of the read, and we managed
  // to send only a portion of the buffer.
  // If sz < 0, either the other endpoint disconnected (ECONNRESET) or some
  // other error happened. In both cases we should just give up.
  // DPLOG("sendmsg() failed");
  Shutdown(true);
  return false;
}

void UnixSocket::Shutdown(bool notify) {
  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  if (notify) {
    if (state_ == State::kConnected) {
      task_runner_->PostTask([weak_ptr] {
        auto socket = weak_ptr.get();
        if (socket) socket->event_listener_->OnDisconnect(socket);
      });
    } else if (state_ == State::kConnecting) {
      task_runner_->PostTask([weak_ptr] {
        auto socket = weak_ptr.get();
        if (socket) socket->event_listener_->OnConnect(socket, false);
      });
    }
  }

  if (sock_raw_) {
    task_runner_->RemoveFileDescriptorWatch(sock_raw_.watch_handle());
    sock_raw_.Shutdown();
  }
  state_ = State::kDisconnected;
}

size_t UnixSocket::Receive(void* msg, size_t len, ScopedFile* fd_vec,
                           size_t max_files) {
  if (state_ != State::kConnected) return 0;

  const ssize_t sz = sock_raw_.Receive(msg, len, fd_vec, max_files);
  bool async_would_block = IsAgain(errno);
  if (sz < 0 && async_would_block) return 0;

  if (sz <= 0) {
    Shutdown(true);
    return 0;
  }
  CHECK(static_cast<size_t>(sz) <= len);
  return static_cast<size_t>(sz);
}

std::string UnixSocket::ReceiveString(size_t max_length) {
  std::unique_ptr<char[]> buf(new char[max_length + 1]);
  size_t rsize = Receive(buf.get(), max_length);
  CHECK(rsize <= max_length);
  buf[rsize] = '\0';
  return std::string(buf.get());
}

void UnixSocket::NotifyConnectionState(bool success) {
  if (!success) Shutdown(false);

  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_ptr, success] {
    auto* socket = weak_ptr.get();
    if (socket) socket->event_listener_->OnConnect(socket, success);
  });
}

UnixSocket::EventListener::~EventListener() {}
void UnixSocket::EventListener::OnNewIncomingConnection(
    UnixSocket*, std::unique_ptr<UnixSocket>) {}
void UnixSocket::EventListener::OnConnect(UnixSocket*, bool) {}
void UnixSocket::EventListener::OnDisconnect(UnixSocket*) {}
void UnixSocket::EventListener::OnDataAvailable(UnixSocket*) {}

}  // namespace xtils
