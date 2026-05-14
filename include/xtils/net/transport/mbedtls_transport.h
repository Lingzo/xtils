#pragma once
#ifdef USE_MBEDTLS

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <atomic>
#include <memory>

#include "xtils/net/tcp_client.h"
#include "xtils/net/transport/transport.h"

namespace xtils {

class TaskRunner;

class MbedTlsContext final : public TlsContext {
 public:
  static std::shared_ptr<MbedTlsContext> Create(const TlsCertConfig& cfg);

  ~MbedTlsContext() override;

  TlsEngine engine() const override { return TlsEngine::kMbedTLS; }

  mbedtls_ssl_config* config() { return &conf_; }
  mbedtls_ctr_drbg_context* ctr_drbg() { return &ctr_drbg_; }

 private:
  MbedTlsContext();

  mbedtls_ssl_config conf_;
  mbedtls_x509_crt ca_cert_;
  mbedtls_x509_crt client_cert_;
  mbedtls_pk_context client_key_;
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context ctr_drbg_;
  bool has_ca_{false};
  bool has_client_cert_{false};
};

class MbedTlsTransport final : public Transport,
                                public TcpClientEventListener {
 public:
  explicit MbedTlsTransport(TaskRunner* runner,
                             TransportEventListener* listener);
  ~MbedTlsTransport() override;

  bool Connect(const HttpUrl& url, TlsContextPtr ctx) override;
  bool Send(const void* data, size_t len) override;
  void Close() override;

 private:
  // TcpClientEventListener
  void OnConnected(bool success) override;
  void OnDataReceived(const void* data, size_t len) override;
  void OnDisconnected() override;
  void OnError(const std::string& error) override;
  bool OnReadable() override;

  void ContinueHandshake();
  void DrainRead();
  void Fail(const std::string& reason);

  static int MbedSend(void* ctx, const unsigned char* buf, size_t len);
  static int MbedRecv(void* ctx, unsigned char* buf, size_t len);

 private:
  enum class TLSState { kIdle, kHandshaking, kConnected, kError };

  TaskRunner* runner_;
  std::unique_ptr<TcpClient> tcp_;

  TlsContextPtr ctx_;
  mbedtls_ssl_context ssl_;
  bool ssl_inited_{false};
  std::atomic<TLSState> state_;

  std::string host_;
};

}  // namespace xtils

#endif  // USE_MBEDTLS
