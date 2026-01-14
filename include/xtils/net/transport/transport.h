#pragma once
#include <memory>
#include <string>

namespace xtils {

class HttpUrl;

enum class TlsEngine {
  kOpenSSL,
  kMbedTLS,
};

struct TlsCertConfig {
  std::string cert_file;
  std::string key_file;
  std::string ca_file;
  bool verify_peer = true;

  bool operator<(const TlsCertConfig& o) const {
    return std::tie(cert_file, key_file, ca_file) <
           std::tie(o.cert_file, o.key_file, o.ca_file);
  }

  static TlsCertConfig Default() {
    TlsCertConfig cfg;
    cfg.ca_file = "/etc/ssl/certs/ca-certificates.crt";
    cfg.verify_peer = true;
    return cfg;
  }
  static TlsCertConfig Insecure() {
    TlsCertConfig cfg;
    cfg.verify_peer = false;
    return cfg;
  }
};

class TlsContext {
 public:
  virtual ~TlsContext() = default;

  virtual TlsEngine engine() const = 0;
};

using TlsContextPtr = std::shared_ptr<TlsContext>;

class TransportEventListener {
 public:
  virtual ~TransportEventListener() = default;

  virtual void OnConnected(bool success) = 0;
  virtual void OnDataReceived(const void* data, size_t len) = 0;
  virtual void OnDisconnected() = 0;
  virtual void OnError(const std::string& error) = 0;
};

class Transport {
 public:
  explicit Transport(TransportEventListener* listener) : listener_(listener) {}
  virtual ~Transport() = default;

  virtual bool Connect(const HttpUrl& url, TlsContextPtr ctx) = 0;
  virtual bool Send(const void* data, size_t len) = 0;
  virtual void Close() = 0;

 protected:
  TransportEventListener* listener_{nullptr};
};

}  // namespace xtils
