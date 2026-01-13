#include "xtils/net/transport/plain_tcp_transport.h"

#include "xtils/net/http_common.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

PlainTcpTransport::PlainTcpTransport(TaskRunner* runner) : runner_(runner) {
  tcp_ = std::make_unique<TcpClient>(runner_, this);
}

PlainTcpTransport::~PlainTcpTransport() { Close(); }

bool PlainTcpTransport::Connect(const HttpUrl& url, TlsContextPtr ctx) {
  (void)ctx;  // Unused for plain TCP
  if (!tcp_) {
    if (on_error_) on_error_("TcpClient not initialized");
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

void PlainTcpTransport::OnConnected(TcpClient*, bool success) {
  if (on_connected_) on_connected_(success);
}

void PlainTcpTransport::OnDataReceived(TcpClient*, const void* data,
                                       size_t len) {
  if (on_data_) on_data_(data, len);
}

void PlainTcpTransport::OnDisconnected(TcpClient*) {
  if (on_disconnected_) on_disconnected_();
}

void PlainTcpTransport::OnError(TcpClient*, const std::string& error) {
  if (on_error_) on_error_(error);
}

}  // namespace xtils
