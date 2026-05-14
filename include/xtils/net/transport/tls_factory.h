#pragma once
#include <memory>

#include "xtils/net/transport/transport.h"

#if defined(USE_OPENSSL)
#include "xtils/net/transport/tls_transport.h"
#elif defined(USE_MBEDTLS)
#include "xtils/net/transport/mbedtls_transport.h"
#endif

namespace xtils {

class TaskRunner;

// Factory function to create a TLS context from configuration.
// Returns the appropriate backend based on compile-time selection.
inline TlsContextPtr CreateTlsContext(const TlsCertConfig& cfg) {
#if defined(USE_OPENSSL)
  return OpenSslContext::Create(cfg);
#elif defined(USE_MBEDTLS)
  return MbedTlsContext::Create(cfg);
#else
  (void)cfg;
  return nullptr;
#endif
}

// Factory function to create a TLS transport.
// Returns the appropriate backend based on compile-time selection.
inline std::unique_ptr<Transport> CreateTlsTransport(
    TaskRunner* runner, TransportEventListener* listener) {
#if defined(USE_OPENSSL)
  return std::make_unique<TlsTransport>(runner, listener);
#elif defined(USE_MBEDTLS)
  return std::make_unique<MbedTlsTransport>(runner, listener);
#else
  (void)runner;
  (void)listener;
  return nullptr;
#endif
}

}  // namespace xtils
