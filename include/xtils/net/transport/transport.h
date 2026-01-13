#pragma once
#include <functional>
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
};

class TlsContext {
 public:
  virtual ~TlsContext() = default;

  virtual TlsEngine engine() const = 0;
};

using TlsContextPtr = std::shared_ptr<TlsContext>;

class Transport {
 public:
  virtual ~Transport() = default;

  virtual bool Connect(const HttpUrl& url, TlsContextPtr ctx) = 0;
  virtual bool Send(const void* data, size_t len) = 0;
  virtual void Close() = 0;

  void SetOnConnected(std::function<void(bool)> cb) {
    on_connected_ = std::move(cb);
  }
  void SetOnData(std::function<void(const void*, size_t)> cb) {
    on_data_ = std::move(cb);
  }
  void SetOnDisconnected(std::function<void()> cb) {
    on_disconnected_ = std::move(cb);
  }
  void SetOnError(std::function<void(const std::string&)> cb) {
    on_error_ = std::move(cb);
  }

 protected:
  std::function<void(bool)> on_connected_;
  std::function<void(const void*, size_t)> on_data_;
  std::function<void()> on_disconnected_;
  std::function<void(const std::string&)> on_error_;
};

}  // namespace xtils
