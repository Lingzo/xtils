#pragma once
#include <memory>

#include "../tcp_client.h"
#include "transport.h"

namespace xtils {

class TaskRunner;

class PlainTcpTransport final : public Transport,
                                public TcpClientEventListener {
 public:
  explicit PlainTcpTransport(TaskRunner* runner,
                             TransportEventListener* listener);
  ~PlainTcpTransport() override;

  // Transport
  bool Connect(const HttpUrl& url, TlsContextPtr ptr = nullptr) override;
  bool Send(const void* data, size_t len) override;
  void Close() override;

 private:
  // TcpClientEventListener
  void OnConnected(bool success) override;
  void OnDataReceived(const void* data, size_t len) override;
  void OnDisconnected() override;
  void OnError(const std::string& error) override;

 private:
  TaskRunner* runner_;
  std::unique_ptr<TcpClient> tcp_;
};

}  // namespace xtils
