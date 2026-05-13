#include "xtils/system/unix_socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "xtils/tasks/thread_task_runner.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

using namespace xtils;

template <typename T>
bool WaitFor(std::future<T>& f, int ms = 2000) {
  return f.wait_for(std::chrono::milliseconds(ms)) ==
         std::future_status::ready;
}

// --- GetSockFamily ---

TEST_CASE("GetSockFamily: Unix paths") {
  CHECK(GetSockFamily("/tmp/my.sock") == SockFamily::kUnix);
  CHECK(GetSockFamily("relative.sock") == SockFamily::kUnix);
}

TEST_CASE("GetSockFamily: abstract Unix") {
  CHECK(GetSockFamily("@abstract_name") == SockFamily::kUnix);
}

TEST_CASE("GetSockFamily: IPv4") {
  CHECK(GetSockFamily("1.2.3.4:8080") == SockFamily::kInet);
  CHECK(GetSockFamily("127.0.0.1:0") == SockFamily::kInet);
}

TEST_CASE("GetSockFamily: IPv6") {
  CHECK(GetSockFamily("[::1]:8080") == SockFamily::kInet6);
}

TEST_CASE("GetSockFamily: empty string") {
  CHECK(GetSockFamily("") == SockFamily::kUnspec);
}

// --- UnixSocketRaw: CreatePairPosix ---

TEST_CASE("UnixSocketRaw: CreatePairPosix stream send/receive") {
  auto [a, b] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kStream);
  CHECK(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));

  const std::string msg = "hello from pair";
  ssize_t sent = a.Send(msg.data(), msg.size());
  CHECK(sent == static_cast<ssize_t>(msg.size()));

  char buf[64] = {};
  ssize_t received = b.Receive(buf, sizeof(buf));
  CHECK(received == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, received) == msg);
}

TEST_CASE("UnixSocketRaw: CreatePairPosix dgram") {
  auto [a, b] =
      UnixSocketRaw::CreatePairPosix(SockFamily::kUnix, SockType::kDgram);
  CHECK(static_cast<bool>(a));
  CHECK(static_cast<bool>(b));

  const std::string msg = "datagram test";
  a.Send(msg.data(), msg.size());

  char buf[64] = {};
  ssize_t received = b.Receive(buf, sizeof(buf));
  CHECK(received == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, received) == msg);
}

// --- UnixSocketRaw: CreateMayFail + Bind + Listen + Connect ---

TEST_CASE("UnixSocketRaw: IPv4 bind, listen, connect, send/receive") {
  // Create server socket
  auto server = UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(server));

  // Try binding on port 0 for ephemeral port
  CHECK(server.Bind("127.0.0.1:0"));
  CHECK(server.Listen());

  // Get the actual bound address (e.g., "127.0.0.1:NNNNN")
  std::string server_addr = server.GetSockAddr();
  CHECK_FALSE(server_addr.empty());

  // Create client socket and connect
  auto client = UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(client));
  CHECK(client.Connect(server_addr));

  // Accept the connection on the server side
  struct sockaddr_storage addr{};
  socklen_t addr_len = sizeof(addr);
  int client_fd =
      accept(server.fd(), reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
  CHECK(client_fd >= 0);

  // Send data from client, receive on accepted connection
  const std::string msg = "raw socket test";
  client.Send(msg.data(), msg.size());

  char buf[64] = {};
  ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
  CHECK(n == static_cast<ssize_t>(msg.size()));
  CHECK(std::string(buf, n) == msg);

  close(client_fd);
}

// --- UnixSocket with ThreadTaskRunner ---

namespace {

class TestListener : public UnixSocket::EventListener {
 public:
  std::promise<bool> connect_promise;
  std::promise<std::string> data_promise;
  std::promise<void> disconnect_promise;
  std::promise<std::unique_ptr<UnixSocket>> accept_promise;
  std::atomic<bool> connect_done{false};
  std::atomic<bool> data_done{false};

  void OnConnect(UnixSocket* self, bool connected) override {
    if (!connect_done.exchange(true)) {
      connect_promise.set_value(connected);
    }
  }

  void OnDataAvailable(UnixSocket* self) override {
    std::string s = self->ReceiveString(4096);
    if (!s.empty() && !data_done.exchange(true)) {
      data_promise.set_value(s);
    }
  }

  void OnDisconnect(UnixSocket* self) override {
    try {
      disconnect_promise.set_value();
    } catch (...) {
    }
  }

  void OnNewIncomingConnection(
      UnixSocket* self, std::unique_ptr<UnixSocket> new_conn) override {
    try {
      accept_promise.set_value(std::move(new_conn));
    } catch (...) {
    }
  }
};

}  // namespace

TEST_CASE("UnixSocket: Listen + Connect + Data with ThreadTaskRunner") {
  auto task_runner = ThreadTaskRunner::CreateAndStart("unix_sock_test");

  TestListener server_listener;
  TestListener client_listener;

  // First bind a raw socket to get ephemeral port
  auto raw = UnixSocketRaw::CreateMayFail(SockFamily::kInet, SockType::kStream);
  CHECK(static_cast<bool>(raw));
  CHECK(raw.Bind("127.0.0.1:0"));
  std::string addr = raw.GetSockAddr();
  CHECK_FALSE(addr.empty());

  // Listen using the pre-bound raw socket
  auto server =
      UnixSocket::Listen(raw.ReleaseFd(), &server_listener, &task_runner,
                         SockFamily::kInet, SockType::kStream);
  CHECK(server);
  CHECK(server->is_listening());

  // Connect client
  auto client = UnixSocket::Connect(addr, &client_listener, &task_runner,
                                    SockFamily::kInet, SockType::kStream);
  CHECK(client);

  // Wait for client connection
  auto connect_fut = client_listener.connect_promise.get_future();
  CHECK(WaitFor(connect_fut));
  CHECK(connect_fut.get() == true);

  // Wait for server to accept
  auto accept_fut = server_listener.accept_promise.get_future();
  CHECK(WaitFor(accept_fut));
  auto accepted = accept_fut.get();
  CHECK(accepted);

  // Client sends data, accepted socket should receive
  // We need a listener for the accepted connection
  TestListener accepted_listener;
  // The accepted connection inherits the server_listener, so data arrives there.
  // Actually, the accepted socket uses the server_listener's OnDataAvailable.
  // Let's send from client and receive from the accepted socket directly.
  client->SendStr("hello from client");

  // Read from accepted socket - give it a moment
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::string received = accepted->ReceiveString(4096);
  // The data might already have been consumed by the server_listener's
  // OnDataAvailable. Let's check the server_listener data_promise instead.
  auto data_fut = server_listener.data_promise.get_future();
  if (received.empty()) {
    // Data was consumed by the event listener
    CHECK(WaitFor(data_fut));
    received = data_fut.get();
  }
  CHECK(received == "hello from client");

  // Echo back
  accepted->SendStr("echo: hello from client");

  auto client_data_fut = client_listener.data_promise.get_future();
  CHECK(WaitFor(client_data_fut));
  CHECK(client_data_fut.get() == "echo: hello from client");

  // Cleanup
  client->Shutdown(false);
  accepted->Shutdown(false);
  server->Shutdown(false);
}

int main() {
  doctest::Context context;
  return context.run();
}
