#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <utility>

#include "xtils/tasks/task_runner.h"
#include "xtils/utils/scoped.h"
#include "xtils/utils/weak_ptr.h"

struct msghdr;

namespace xtils {

// Define the ScopedSocketHandle type.

using ScopedSocketHandle = ScopedFile;

class TaskRunner;

// Use arbitrarily high values to avoid that some code accidentally ends up
// assuming that these enum values match the sysroot's SOCK_xxx defines rather
// than using MkSockType() / MkSockFamily().
enum class SockType { kStream = 100, kDgram, kSeqPacket };
enum class SockFamily { kUnspec = 0, kUnix = 200, kInet, kInet6 };

// Controls the getsockopt(SO_PEERCRED) behavior, which allows to obtain the
// peer credentials.
enum class SockPeerCredMode {
  // Obtain the peer credentials immediately after connection and cache them.
  kReadOnConnect = 0,

  // Don't read peer credentials at all. Calls to peer_uid()/peer_pid() will
  // hit a DCHECK and return kInvalidUid/Pid in release builds.
  kIgnore = 1,

  kDefault = kReadOnConnect,
};

// Returns the socket family from the full addres that perfetto uses.
// Addr can be:
// - /path/to/socket : for linked AF_UNIX sockets.
// - @abstract_name  : for abstract AF_UNIX sockets.
// - 1.2.3.4:8080    : for Inet sockets.
// - [::1]:8080      : for Inet6 sockets.
// - vsock://-1:3000 : for VM sockets.
SockFamily GetSockFamily(const char* addr);

// Returns whether inter-process shared memory is supported for the socket.
inline bool SockShmemSupported(SockFamily sock_family) {
  return sock_family == SockFamily::kUnix;
}
inline bool SockShmemSupported(const char* addr) {
  return SockShmemSupported(GetSockFamily(addr));
}

// UnixSocketRaw is a basic wrapper around sockets. It exposes wrapper
// methods that take care of most common pitfalls (e.g., marking fd as
// O_CLOEXEC, avoiding SIGPIPE, properly handling partial writes). It is used as
// a building block for the more sophisticated UnixSocket class which depends
// on xtils::TaskRunner.
class UnixSocketRaw {
 public:
  // Creates a new unconnected unix socket.
  static UnixSocketRaw CreateMayFail(SockFamily family, SockType type);

  // Crates a pair of connected sockets.
  static std::pair<UnixSocketRaw, UnixSocketRaw> CreatePairPosix(SockFamily,
                                                                 SockType);
  // Creates an uninitialized unix socket.
  UnixSocketRaw();

  // Creates a unix socket adopting an existing file descriptor. This is
  // typically used to inherit fds from init via environment variables.
  UnixSocketRaw(ScopedSocketHandle, SockFamily, SockType);

  ~UnixSocketRaw() = default;
  UnixSocketRaw(UnixSocketRaw&&) noexcept = default;
  UnixSocketRaw& operator=(UnixSocketRaw&&) = default;

  bool Bind(const std::string& socket_name);
  bool Listen();
  bool Connect(const std::string& socket_name);
  bool SetTxTimeout(uint32_t timeout_ms);
  bool SetRxTimeout(uint32_t timeout_ms);
  void Shutdown();
  void SetBlocking(bool);
  void DcheckIsBlocking(bool expected) const;  // No-op on release and Win.
  void SetRetainOnExec(bool retain);
  std::string GetSockAddr() const;
  SockType type() const { return type_; }
  SockFamily family() const { return family_; }
  SocketHandle fd() const { return *fd_; }
  explicit operator bool() const { return !!fd_; }

  // This is the handle that passed to TaskRunner.AddFileDescriptorWatch().
  // On UNIX this is just the socket FD. On Windows, we need to create a
  // dedicated event object.
  PlatformHandle watch_handle() const { return *fd_; }

  ScopedSocketHandle ReleaseFd() { return std::move(fd_); }

  // |send_fds| and |num_fds| are ignored on Windows.
  ssize_t Send(const void* msg, size_t len, const int* send_fds = nullptr,
               size_t num_fds = 0);

  ssize_t SendStr(const std::string& str) {
    return Send(str.data(), str.size());
  }

  // |fd_vec| and |max_files| are ignored on Windows.
  ssize_t Receive(void* msg, size_t len, ScopedFile* fd_vec = nullptr,
                  size_t max_files = 0);

  // UNIX-specific helpers to deal with SCM_RIGHTS.

  // Re-enter sendmsg until all the data has been sent or an error occurs.
  // TODO(fmayer): Figure out how to do timeouts here for heapprofd.
  ssize_t SendMsgAllPosix(struct msghdr* msg);

  // Exposed for testing only.
  // Update msghdr so subsequent sendmsg will send data that remains after n
  // bytes have already been sent.
  static void ShiftMsgHdrPosix(size_t n, struct msghdr* msg);

 private:
  UnixSocketRaw(SockFamily, SockType);

  UnixSocketRaw(const UnixSocketRaw&) = delete;
  UnixSocketRaw& operator=(const UnixSocketRaw&) = delete;

  ScopedSocketHandle fd_;
  SockFamily family_ = SockFamily::kUnix;
  SockType type_ = SockType::kStream;
  uint32_t tx_timeout_ms_ = 0;
};

// A non-blocking UNIX domain socket. Allows also to transfer file descriptors.
// None of the methods in this class are blocking.
// The main design goal is making strong guarantees on the EventListener
// callbacks, in order to avoid ending in some undefined state.
// In case of any error it will aggressively just shut down the socket and
// notify the failure with OnConnect(false) or OnDisconnect() depending on the
// state of the socket (see below).
// EventListener callbacks stop happening as soon as the instance is destroyed.
//
// Lifecycle of a client socket:
//
//                           Connect()
//                               |
//            +------------------+------------------+
//            | (success)                           | (failure or Shutdown())
//            V                                     V
//     OnConnect(true)                         OnConnect(false)
//            |
//            V
//    OnDataAvailable()
//            |
//            V
//     OnDisconnect()  (failure or shutdown)
//
//
// Lifecycle of a server socket:
//
//                          Listen()  --> returns false in case of errors.
//                             |
//                             V
//              OnNewIncomingConnection(new_socket)
//
//          (|new_socket| inherits the same EventListener)
//                             |
//                             V
//                     OnDataAvailable()
//                             | (failure or Shutdown())
//                             V
//                       OnDisconnect()
class UnixSocket {
 public:
  class EventListener {
   public:
    EventListener() = default;
    virtual ~EventListener();

    EventListener(const EventListener&) = delete;
    EventListener& operator=(const EventListener&) = delete;

    EventListener(EventListener&&) noexcept = default;
    EventListener& operator=(EventListener&&) noexcept = default;

    // After Listen().
    // |self| may be null if the connection was not accepted via a listen
    // socket.
    virtual void OnNewIncomingConnection(
        UnixSocket* self, std::unique_ptr<UnixSocket> new_connection);

    // After Connect(), whether successful or not.
    virtual void OnConnect(UnixSocket* self, bool connected);

    // After a successful Connect() or OnNewIncomingConnection(). Either the
    // other endpoint did disconnect or some other error happened.
    virtual void OnDisconnect(UnixSocket* self);

    // Whenever there is data available to Receive(). Note that spurious FD
    // watch events are possible, so it is possible that Receive() soon after
    // OnDataAvailable() returns 0 (just ignore those).
    virtual void OnDataAvailable(UnixSocket* self);
  };

  enum class State {
    kDisconnected = 0,  // Failed connection, peer disconnection or Shutdown().
    kConnecting,  // Soon after Connect(), before it either succeeds or fails.
    kConnected,   // After a successful Connect().
    kListening    // After Listen(), until Shutdown().
  };

  // Creates a socket and starts listening. If SockFamily::kUnix and
  // |socket_name| starts with a '@', an abstract UNIX dmoain socket will be
  // created instead of a filesystem-linked UNIX socket (Linux/Android only).
  // If SockFamily::kInet, |socket_name| is host:port (e.g., "1.2.3.4:8000").
  // If SockFamily::kInet6, |socket_name| is [host]:port (e.g., "[::1]:8000").
  // Returns nullptr if the socket creation or bind fails. If listening fails,
  // (e.g. if another socket with the same name is already listening) the
  // returned socket will have is_listening() == false.
  static std::unique_ptr<UnixSocket> Listen(const std::string& socket_name,
                                            EventListener*, TaskRunner*,
                                            SockFamily, SockType);

  // Attaches to a pre-existing socket. The socket must have been created in
  // SOCK_STREAM mode and the caller must have called bind() on it.
  static std::unique_ptr<UnixSocket> Listen(ScopedSocketHandle, EventListener*,
                                            TaskRunner*, SockFamily, SockType);

  // Creates a Unix domain socket and connects to the listening endpoint.
  // Returns always an instance. EventListener::OnConnect(bool success) will
  // be called always, whether the connection succeeded or not.
  static std::unique_ptr<UnixSocket> Connect(
      const std::string& socket_name, EventListener*, TaskRunner*, SockFamily,
      SockType, SockPeerCredMode = SockPeerCredMode::kDefault);

  // Constructs a UnixSocket using the given connected socket.
  static std::unique_ptr<UnixSocket> AdoptConnected(
      ScopedSocketHandle, EventListener*, TaskRunner*, SockFamily, SockType,
      SockPeerCredMode = SockPeerCredMode::kDefault);

  UnixSocket(const UnixSocket&) = delete;
  UnixSocket& operator=(const UnixSocket&) = delete;
  // Cannot be easily moved because of tasks from the FileDescriptorWatch.
  UnixSocket(UnixSocket&&) = delete;
  UnixSocket& operator=(UnixSocket&&) = delete;

  // This class gives the hard guarantee that no callback is called on the
  // passed EventListener immediately after the object has been destroyed.
  // Any queued callback will be silently dropped.
  ~UnixSocket();

  // Shuts down the current connection, if any. If the socket was Listen()-ing,
  // stops listening. The socket goes back to kNotInitialized state, so it can
  // be reused with Listen() or Connect().
  void Shutdown(bool notify);

  void SetTxTimeout(uint32_t timeout_ms) { sock_raw_.SetTxTimeout(timeout_ms); }
  void SetRxTimeout(uint32_t timeout_ms) { sock_raw_.SetRxTimeout(timeout_ms); }

  std::string GetSockAddr() const { return sock_raw_.GetSockAddr(); }

  // Returns true is the message was queued, false if there was no space in the
  // output buffer, in which case the client should retry or give up.
  // If any other error happens the socket will be shutdown and
  // EventListener::OnDisconnect() will be called.
  // If the socket is not connected, Send() will just return false.
  // Does not append a null string terminator to msg in any case.
  bool Send(const void* msg, size_t len, const int* send_fds, size_t num_fds);

  inline bool Send(const void* msg, size_t len, int send_fd = -1) {
    if (send_fd != -1) return Send(msg, len, &send_fd, 1);
    return Send(msg, len, nullptr, 0);
  }

  inline bool SendStr(const std::string& msg) {
    return Send(msg.data(), msg.size(), -1);
  }

  // Returns the number of bytes (<= |len|) written in |msg| or 0 if there
  // is no data in the buffer to read or an error occurs (in which case a
  // EventListener::OnDisconnect() will follow).
  // If the ScopedFile pointer is not null and a FD is received, it moves the
  // received FD into that. If a FD is received but the ScopedFile pointer is
  // null, the FD will be automatically closed.
  size_t Receive(void* msg, size_t len, ScopedFile*, size_t max_files = 1);

  inline size_t Receive(void* msg, size_t len) {
    return Receive(msg, len, nullptr, 0);
  }

  // Only for tests. This is slower than Receive() as it requires a heap
  // allocation and a copy for the std::string. Guarantees that the returned
  // string is null terminated even if the underlying message sent by the peer
  // is not.
  std::string ReceiveString(size_t max_length = 1024);

  bool is_connected() const { return state_ == State::kConnected; }
  bool is_listening() const { return state_ == State::kListening; }
  SocketHandle fd() const { return sock_raw_.fd(); }
  SockFamily family() const { return sock_raw_.family(); }

  // This makes the UnixSocket unusable.
  UnixSocketRaw ReleaseSocket();

 private:
  UnixSocket(EventListener*, TaskRunner*, SockFamily, SockType,
             SockPeerCredMode);
  UnixSocket(EventListener*, TaskRunner*, ScopedSocketHandle, State, SockFamily,
             SockType, SockPeerCredMode);

  // Called once by the corresponding public static factory methods.
  void DoConnect(const std::string& socket_name);

  void ReadPeerCredentialsPosix();

  void OnEvent();
  void NotifyConnectionState(bool success);

  UnixSocketRaw sock_raw_;
  State state_ = State::kDisconnected;
  SockPeerCredMode peer_cred_mode_ = SockPeerCredMode::kDefault;

  EventListener* const event_listener_;
  TaskRunner* const task_runner_;
  WeakPtrFactory<UnixSocket> weak_ptr_factory_;
};

}  // namespace xtils
