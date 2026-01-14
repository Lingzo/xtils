#include "xtils/net/transport/plain_tcp_transport.h"

#include "xtils/net/http_common.h"
#include "xtils/net/transport/transport.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

PlainTcpTransport::PlainTcpTransport(TaskRunner* runner,
                                     TransportEventListener* listener)
    : Transport(listener), runner_(runner) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
}

PlainTcpTransport::~PlainTcpTransport() { Close(); }

bool PlainTcpTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  (void)ctx;  // Unused for plain TCP
  if (!tcp_) {
    if (listener_) listener_->OnError("TcpClient not initialized");
    return false;
  }

  return tcp_->ConnectToHost(url.host, url.port);
}

bool PlainTcpTransport::Send(const void* data, size_t len) {
  if (!tcp_ || !tcp_->IsConnected()) return false;

  return tcp_->Send(data, len);
}

void PlainTcpTransport::Close() {
  if (tcp_) {
    tcp_->Disconnect();
  }
}

void PlainTcpTransport::OnConnected(bool success) {
  if (listener_) listener_->OnConnected(success);
}

void PlainTcpTransport::OnDataReceived(const void* data, size_t len) {
  if (listener_) listener_->OnDataReceived(data, len);
}

void PlainTcpTransport::OnDisconnected() {
  if (listener_) listener_->OnDisconnected();
}

void PlainTcpTransport::OnError(const std::string& error) {
  if (listener_) listener_->OnError(error);
}

}  // namespace xtils
