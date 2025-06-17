#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "paged_memory.h"
#include "string_utils.h"
#include "task_runner.h"
#include "unix_socket.h"

namespace base {
namespace mime {
// clang-format off
inline constexpr const char* TEXT_PLAIN = "text/plain";
inline constexpr const char* TEXT_HTML = "text/html";
inline constexpr const char* TEXT_CSS = "text/css";
inline constexpr const char* APPLICATION_JSON = "application/json";
inline constexpr const char* APPLICATION_XML = "application/xml";
inline constexpr const char* APPLICATION_JS = "application/javascript";
inline constexpr const char* APPLICATION_WWW_FORM = "application/x-www-form-urlencoded";
inline constexpr const char* MULTIPART_FORM = "multipart/form-data";
inline constexpr const char* APPLICATION_OCTET_STREAM = "application/octet-stream";
inline constexpr const char* IMAGE_PNG = "image/png";
inline constexpr const char* IMAGE_JPEG = "image/jpeg";
inline constexpr const char* IMAGE_SVG = "image/svg+xml";
inline constexpr const char* VIDEO_MP4 = "video/mp4";
inline constexpr const char* AUDIO_MP3 = "audio/mpeg";
// clang-format on
}  // namespace mime
class HttpServerConnection;

struct Header {
  StringView name;
  StringView value;
};

struct HttpRequest {
  explicit HttpRequest(HttpServerConnection* c) : conn(c) {}

  std::optional<StringView> GetHeader(StringView name) const;

  HttpServerConnection* conn;

  // These StringViews point to memory in the rxbuf owned by |conn|. They are
  // valid only within the OnHttpRequest() call.
  StringView method;
  StringView uri;
  StringView origin;
  StringView body;
  bool is_websocket_handshake = false;

 private:
  friend class HttpServer;

  static constexpr uint32_t kMaxHeaders = 32;
  std::array<Header, kMaxHeaders> headers{};
  size_t num_headers = 0;
};

struct WebsocketMessage {
  explicit WebsocketMessage(HttpServerConnection* c) : conn(c) {}

  HttpServerConnection* conn;

  // Note: message boundaries are not respected in case of fragmentation.
  // This websocket implementation preserves only the byte stream, but not the
  // atomicity of inbound messages (like SOCK_STREAM, unlike SOCK_DGRAM).
  // Holds onto the connection's |rxbuf|. This is valid only within the scope
  // of the OnWebsocketMessage() callback.
  StringView data;

  // If false the payload contains binary data. If true it's supposed to contain
  // text. Note that there is no guarantee this will be the case. This merely
  // reflect the opcode that the client sets on each message.
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
  void Start(int port);
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
};

}  // namespace base
