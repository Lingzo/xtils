#include <fcntl.h>
#include <openssl/err.h>

#include <cstring>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/transport/tls_transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

TlsTransport::TlsTransport(TaskRunner* runner)
    : runner_(runner), ctx_(nullptr), ssl_(nullptr), state_(TLSState::kIdle) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);

  static bool openssl_inited = false;
  if (!openssl_inited) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    openssl_inited = true;
  }
}

TlsTransport::~TlsTransport() { Close(); }

bool TlsTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
  if (state_ != TLSState::kIdle) return false;

  host_ = url.host;
  ctx_ = ctx;
  state_ = TLSState::kIdle;
  return tcp_->ConnectToHost(url.host, url.port);
}

void TlsTransport::OnConnected(TcpClient*, bool success) {
  if (!success) {
    Fail("TCP connect failed");
    return;
  }

  ssl_ = SSL_new(reinterpret_cast<OpenSslContext*>(ctx_.get())->ctx());
  if (!ssl_) {
    Fail("SSL_new failed");
    return;
  }
  int fd = tcp_->GetSocketFd();
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  SSL_set_fd(ssl_, tcp_->GetSocketFd());
  SSL_set_tlsext_host_name(ssl_, host_.c_str());
  SSL_set_connect_state(ssl_);

  state_ = TLSState::kHandshaking;
  ContinueHandshake();
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TlsTransport::ContinueHandshake() {
  int ret = SSL_connect(ssl_);
  if (ret == 1) {
    state_ = TLSState::kConnected;
    if (on_connected_) on_connected_(true);
    return;
  }

  char reason[256];
  unsigned long e = ERR_get_error();
  ERR_error_string_n(e, reason, sizeof(reason));
  Fail(reason);
}

void TlsTransport::OnDataReceived(TcpClient*, const void*, size_t) {
  LogW("TlsTransport::OnDataReceived should not be called");
}

bool TlsTransport::OnReadable() {
  if (state_ == TLSState::kHandshaking) {
    ContinueHandshake();
    return true;
  } else if (state_ == TLSState::kConnected) {
    DrainSSLRead();
    return true;
  }
  return false;
}

void TlsTransport::DrainSSLRead() {
  uint8_t buf[4096];

  while (true) {
    int n = SSL_read(ssl_, buf, sizeof(buf));
    if (n > 0) {
      if (on_data_) on_data_(buf, static_cast<size_t>(n));
      continue;
    }

    int err = SSL_get_error(ssl_, n);
    if (err == SSL_ERROR_WANT_READ) break;

    if (state_ == TLSState::kConnected) {
      char reason[256];
      ERR_error_string_n(err, reason, sizeof(reason));
      Fail(reason);
    }
    break;
  }
}

bool TlsTransport::Send(const void* data, size_t len) {
  if (state_ != TLSState::kConnected) return false;

  int ret = SSL_write(ssl_, data, static_cast<int>(len));
  if (ret <= 0) {
    Fail("SSL_write failed");
    return false;
  }
  return true;
}

void TlsTransport::OnDisconnected(TcpClient*) {
  if (state_ != TLSState::kError && on_disconnected_) {
    on_disconnected_();
  }
  Close();
}

void TlsTransport::OnError(TcpClient*, const std::string& error) {
  Fail(error);
}

void TlsTransport::Fail(const std::string& reason) {
  state_ = TLSState::kError;
  if (on_error_) on_error_(reason);
}

void TlsTransport::Close() {
  if (tcp_) {
    tcp_->Disconnect();
  }
  if (ssl_) {
    SSL_set_quiet_shutdown(ssl_, 1);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }

  state_ = TLSState::kIdle;
}

}  // namespace xtils
