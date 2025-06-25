#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "xtils/system/paged_memory.h"
#include "xtils/system/unix_socket.h"
#include "xtils/tasks/task_runner.h"
#include "xtils/utils/string_view.h"

namespace xtils {
class HttpServerConnection;

// Represents an HTTP header.
struct Header {
  // The name of the header (e.g., "Content-Type").
  StringView name;
  // The value of the header (e.g., "text/html").
  StringView value;
};

// Represents an HTTP request.
struct HttpRequest {
  explicit HttpRequest(HttpServerConnection* c) : conn(c) {}

  // Gets the value of a header with the given name, if it exists.
  std::optional<StringView> GetHeader(StringView name) const;

  // A pointer to the HttpServerConnection that this request belongs to.
  HttpServerConnection* conn;

  // These StringViews point to memory in the rxbuf owned by |conn|. They are
  // valid only within the OnHttpRequest() call.
  // The HTTP method (e.g., "GET", "POST").
  StringView method;
  // The URI of the request.
  StringView uri;
  // The origin of the request, if provided in the headers.
  StringView origin;
  // The body of the request.
  StringView body;
  std::list<Header> params;

  static constexpr uint32_t kMaxHeaders = 32;
  std::array<Header, kMaxHeaders> headers{};
  bool is_websocket_handshake = false;

 private:
  friend class HttpServer;
  size_t num_headers = 0;
};

// Represents a WebSocket message.
struct WebsocketMessage {
  explicit WebsocketMessage(HttpServerConnection* c) : conn(c) {}

  // A pointer to the HttpServerConnection that this message belongs to.
  HttpServerConnection* conn;

  // Note: message boundaries are not respected in case of fragmentation.
  // This websocket implementation preserves only the byte stream, but not the
  // atomicity of inbound messages (like SOCK_STREAM, unlike SOCK_DGRAM).
  // Holds onto the connection's |rxbuf|. This is valid only within the scope
  // of the OnWebsocketMessage() callback.
  // The data of the WebSocket message.
  StringView data;

  // If false, the payload contains binary data. If true, it's supposed to
  // contain text. Note that there is no guarantee this will be the case. This
  // merely reflects the opcode that the client sets on each message.
  bool is_text = false;
};

class HttpServerConnection {
 public:
  static constexpr size_t kOmitContentLength = static_cast<size_t>(-1);

  explicit HttpServerConnection(std::unique_ptr<UnixSocket>);
  ~HttpServerConnection();

  void Close();

  // All the above in one shot.
  void SendResponse(const char* http_code,
                    const std::list<Header>& headers = {},
                    StringView content = {}, bool force_close = false);
  void SendResponseAndClose(const char* http_code,
                            const std::list<Header>& headers = {},
                            StringView content = {}) {
    SendResponse(http_code, headers, content, true);
  }

  // The metods below are only valid for websocket connections.

  // Upgrade an existing connection to a websocket. This can be called only in
  // the context of OnHttpRequest(req) if req.is_websocket_handshake == true.
  // If the origin is not in the |allowed_origins_|, the request will fail with
  // a 403 error (this is because there is no browser-side CORS support for
  // websockets).
  void UpgradeToWebsocket(const HttpRequest&);
  void SendWebsocketMessageText(const void* data, size_t len);
  void SendWebsocketMessage(const void* data, size_t len);
  void SendWebsocketMessage(StringView sv) {
    SendWebsocketMessage(sv.data(), sv.size());
  }
  void SendWebsocketFrame(uint8_t opcode, const void* payload,
                          size_t payload_len);

  bool is_websocket() const { return is_websocket_; }

 private:
  void SendResponseHeaders(const char* http_code,
                           const std::list<Header>& headers = {},
                           size_t content_length = 0);

  // Works also for websockets.
  void SendResponseBody(const void* content, size_t content_length);

 private:
  friend class HttpServer;

  size_t rxbuf_avail() { return rxbuf.size() - rxbuf_used; }

  std::unique_ptr<UnixSocket> sock;
  PagedMemory rxbuf;
  size_t rxbuf_used = 0;
  bool is_websocket_ = false;
  bool headers_sent_ = false;
  size_t content_len_headers_ = 0;
  size_t content_len_actual_ = 0;

  // If the origin is in the server's |allowed_origins_| this contains the
  // origin itself. This is used to handle CORS headers.
  std::string origin_allowed_;

  // By default treat connections as keep-alive unless the client says
  // explicitly 'Connection: close'. This improves TraceProcessor's python API.
  // This is consistent with that nginx does.
  bool keepalive_ = true;
};

class HttpRequestHandler {
 public:
  virtual ~HttpRequestHandler() = default;
  virtual void OnHttpRequest(const HttpRequest&) = 0;
  virtual void OnWebsocketMessage(const WebsocketMessage&) {};
  virtual void OnHttpConnectionClosed(HttpServerConnection*) {};
};

class HttpServer : public UnixSocket::EventListener {
 public:
  HttpServer(TaskRunner*, HttpRequestHandler*);
  ~HttpServer() override;
  void Start(const std::string& ip, int port);
  void Stop();
  void AddAllowedOrigin(const std::string&);

 private:
  size_t ParseOneHttpRequest(HttpServerConnection*);
  size_t ParseOneWebsocketFrame(HttpServerConnection*);
  void HandleCorsPreflightRequest(const HttpRequest&);
  bool IsOriginAllowed(StringView);

  // UnixSocket::EventListener implementation.
  void OnNewIncomingConnection(UnixSocket*,
                               std::unique_ptr<UnixSocket>) override;
  void OnConnect(UnixSocket* self, bool connected) override;
  void OnDisconnect(UnixSocket* self) override;
  void OnDataAvailable(UnixSocket* self) override;

  TaskRunner* const task_runner_;
  HttpRequestHandler* req_handler_;
  std::unique_ptr<UnixSocket> sock4_;
  std::list<HttpServerConnection> clients_;
  std::list<std::string> allowed_origins_;
  bool origin_error_logged_ = false;
  bool is_stopping_ = false;
};

}  // namespace xtils
