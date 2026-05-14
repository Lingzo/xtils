#ifdef USE_MBEDTLS

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <mbedtls/error.h>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"
#include "xtils/net/transport/mbedtls_transport.h"
#include "xtils/net/transport/transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

// ---------- MbedTlsContext ----------

MbedTlsContext::MbedTlsContext() {
  mbedtls_ssl_config_init(&conf_);
  mbedtls_x509_crt_init(&ca_cert_);
  mbedtls_x509_crt_init(&client_cert_);
  mbedtls_pk_init(&client_key_);
  mbedtls_entropy_init(&entropy_);
  mbedtls_ctr_drbg_init(&ctr_drbg_);
}

MbedTlsContext::~MbedTlsContext() {
  mbedtls_ssl_config_free(&conf_);
  if (has_ca_) mbedtls_x509_crt_free(&ca_cert_);
  if (has_client_cert_) {
    mbedtls_x509_crt_free(&client_cert_);
    mbedtls_pk_free(&client_key_);
  }
  mbedtls_ctr_drbg_free(&ctr_drbg_);
  mbedtls_entropy_free(&entropy_);
}

std::shared_ptr<MbedTlsContext> MbedTlsContext::Create(
    const TlsCertConfig& cfg) {
  auto ctx = std::shared_ptr<MbedTlsContext>(new MbedTlsContext());

  // Seed the random number generator
  int ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg_, mbedtls_entropy_func,
                                  &ctx->entropy_, nullptr, 0);
  if (ret != 0) {
    LogE("mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
    return nullptr;
  }

  // Set up the SSL configuration for a TLS client
  ret = mbedtls_ssl_config_defaults(&ctx->conf_, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    LogE("mbedtls_ssl_config_defaults failed: -0x%04x", -ret);
    return nullptr;
  }

  // Set minimum TLS version to 1.2
  mbedtls_ssl_conf_min_tls_version(&ctx->conf_,
                                   MBEDTLS_SSL_VERSION_TLS1_2);

  mbedtls_ssl_conf_rng(&ctx->conf_, mbedtls_ctr_drbg_random, &ctx->ctr_drbg_);

  // Load CA certificates
  if (!cfg.ca_file.empty()) {
    ret =
        mbedtls_x509_crt_parse_file(&ctx->ca_cert_, cfg.ca_file.c_str());
    if (ret < 0) {
      LogW("mbedtls_x509_crt_parse_file CA failed: -0x%04x", -ret);
      // Non-fatal if verify_peer is false
    } else {
      ctx->has_ca_ = true;
      mbedtls_ssl_conf_ca_chain(&ctx->conf_, &ctx->ca_cert_, nullptr);
    }
  }

  // Configure peer verification
  if (cfg.verify_peer) {
    mbedtls_ssl_conf_authmode(&ctx->conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
  } else {
    mbedtls_ssl_conf_authmode(&ctx->conf_, MBEDTLS_SSL_VERIFY_NONE);
  }

  // Load client certificate and key
  if (!cfg.cert_file.empty()) {
    ret = mbedtls_x509_crt_parse_file(&ctx->client_cert_,
                                      cfg.cert_file.c_str());
    if (ret != 0) {
      LogE("mbedtls_x509_crt_parse_file cert failed: -0x%04x", -ret);
      return nullptr;
    }

    ret = mbedtls_pk_parse_keyfile(&ctx->client_key_, cfg.key_file.c_str(),
                                   nullptr,
                                   mbedtls_ctr_drbg_random,
                                   &ctx->ctr_drbg_);
    if (ret != 0) {
      LogE("mbedtls_pk_parse_keyfile failed: -0x%04x", -ret);
      return nullptr;
    }

    ctx->has_client_cert_ = true;
    mbedtls_ssl_conf_own_cert(&ctx->conf_, &ctx->client_cert_,
                              &ctx->client_key_);
  }

  return ctx;
}

// ---------- MbedTlsTransport ----------

MbedTlsTransport::MbedTlsTransport(TaskRunner* runner,
                                     TransportEventListener* listener)
    : Transport(listener),
      runner_(runner),
      ctx_(nullptr),
      state_(TLSState::kIdle) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
}

MbedTlsTransport::~MbedTlsTransport() { Close(); }

bool MbedTlsTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
  if (state_ != TLSState::kIdle) return false;

  host_ = url.host;
  ctx_ = ctx;
  state_ = TLSState::kIdle;
  return tcp_->ConnectToHost(url.host, url.port);
}

int MbedTlsTransport::MbedSend(void* ctx, const unsigned char* buf,
                                size_t len) {
  auto* self = static_cast<MbedTlsTransport*>(ctx);
  int fd = self->tcp_->GetSocketFd();
  ssize_t ret = ::send(fd, buf, len, MSG_NOSIGNAL);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return static_cast<int>(ret);
}

int MbedTlsTransport::MbedRecv(void* ctx, unsigned char* buf, size_t len) {
  auto* self = static_cast<MbedTlsTransport*>(ctx);
  int fd = self->tcp_->GetSocketFd();
  ssize_t ret = ::recv(fd, buf, len, 0);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  if (ret == 0) {
    return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
  }
  return static_cast<int>(ret);
}

void MbedTlsTransport::OnConnected(bool success) {
  if (!success) {
    Fail("TCP connect failed");
    return;
  }

  auto* mbed_ctx = reinterpret_cast<MbedTlsContext*>(ctx_.get());

  mbedtls_ssl_init(&ssl_);
  ssl_inited_ = true;

  int ret = mbedtls_ssl_setup(&ssl_, mbed_ctx->config());
  if (ret != 0) {
    Fail("mbedtls_ssl_setup failed");
    return;
  }

  // Set SNI hostname
  ret = mbedtls_ssl_set_hostname(&ssl_, host_.c_str());
  if (ret != 0) {
    Fail("mbedtls_ssl_set_hostname failed");
    return;
  }

  // Set custom BIO callbacks using the raw TCP socket fd
  mbedtls_ssl_set_bio(&ssl_, this, MbedSend, MbedRecv, nullptr);

  // Perform handshake in blocking mode
  int fd = tcp_->GetSocketFd();
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

  state_ = TLSState::kHandshaking;
  ContinueHandshake();

  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void MbedTlsTransport::ContinueHandshake() {
  int ret = mbedtls_ssl_handshake(&ssl_);
  if (ret == 0) {
    state_ = TLSState::kConnected;
    if (listener_) listener_->OnConnected(true);
    return;
  }

  if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
      ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
    // Need more I/O, will be retried on next readable/writable event
    return;
  }

  // Real error
  char err_buf[128];
  mbedtls_strerror(ret, err_buf, sizeof(err_buf));
  Fail(std::string("TLS handshake failed: ") + err_buf);
}

void MbedTlsTransport::OnDataReceived(const void*, size_t) {
  LogW("MbedTlsTransport::OnDataReceived should not be called");
}

bool MbedTlsTransport::OnReadable() {
  if (state_ == TLSState::kHandshaking) {
    ContinueHandshake();
    return true;
  } else if (state_ == TLSState::kConnected) {
    DrainRead();
    return true;
  }
  return false;
}

void MbedTlsTransport::DrainRead() {
  uint8_t buf[4096];

  while (true) {
    int n = mbedtls_ssl_read(&ssl_, buf, sizeof(buf));
    if (n > 0) {
      if (listener_) listener_->OnDataReceived(buf, static_cast<size_t>(n));
      continue;
    }

    if (n == MBEDTLS_ERR_SSL_WANT_READ) break;

    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) {
      if (listener_) listener_->OnDisconnected();
      break;
    }

    if (state_ == TLSState::kConnected) {
      char err_buf[128];
      mbedtls_strerror(n, err_buf, sizeof(err_buf));
      Fail(err_buf);
    }
    break;
  }
}

bool MbedTlsTransport::Send(const void* data, size_t len) {
  if (state_ != TLSState::kConnected) return false;

  const unsigned char* ptr = static_cast<const unsigned char*>(data);
  size_t remaining = len;

  while (remaining > 0) {
    int ret = mbedtls_ssl_write(&ssl_, ptr, remaining);
    if (ret > 0) {
      ptr += ret;
      remaining -= static_cast<size_t>(ret);
      continue;
    }

    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;  // Retry
    }

    Fail("mbedtls_ssl_write failed");
    return false;
  }

  return true;
}

void MbedTlsTransport::OnDisconnected() {
  if (state_ != TLSState::kError && listener_) {
    listener_->OnDisconnected();
  }
  Close();
}

void MbedTlsTransport::OnError(const std::string& error) { Fail(error); }

void MbedTlsTransport::Fail(const std::string& reason) {
  state_ = TLSState::kError;
  if (listener_) listener_->OnError(reason);
}

void MbedTlsTransport::Close() {
  if (ssl_inited_) {
    mbedtls_ssl_close_notify(&ssl_);
    mbedtls_ssl_free(&ssl_);
    ssl_inited_ = false;
  }
  if (tcp_) {
    tcp_->Disconnect();
  }
  state_ = TLSState::kIdle;
}

}  // namespace xtils

#endif  // USE_MBEDTLS
