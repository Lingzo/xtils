#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "xtils/net/http_client.h"
#include "xtils/net/http_common.h"
#include "xtils/net/tcp_client.h"
#include "xtils/net/websocket_common.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

// Use shared WebSocket types from websocket_common.h

class WebSocketClient;

class WebSocketClientEventListener {
 public:
  virtual ~WebSocketClientEventListener() = default;

  // Called when WebSocket connection is successfully established
  virtual void OnWebSocketConnected(WebSocketClient* client) = 0;

  // Called when a WebSocket message is received
  virtual void OnWebSocketMessage(WebSocketClient* client,
                                  const WebSocketMessage& message) = 0;

  // Called when WebSocket connection is closed
  virtual void OnWebSocketClosed(WebSocketClient* client, uint16_t code,
                                 const std::string& reason) = 0;

  // Called when WebSocket encounters an error
  virtual void OnWebSocketError(WebSocketClient* client,
                                const std::string& error) = 0;

  // Called when a ping is received (optional override)
  virtual void OnWebSocketPing(WebSocketClient* client,
                               const std::string& data) {}

  // Called when a pong is received (optional override)
  virtual void OnWebSocketPong(WebSocketClient* client,
                               const std::string& data) {}
};

class WebSocketClient : public HttpClientEventListener,
                        public TcpClientEventListener {
 public:
  enum class State {
    kDisconnected = 0,
    kConnecting,
    kHandshaking,
    kConnected,
    kClosing,
    kClosed,
    kError
  };

  explicit WebSocketClient(TaskRunner* task_runner,
                           WebSocketClientEventListener* listener = nullptr);
  ~WebSocketClient() override;

  // Connect to WebSocket server
  bool Connect(const std::string& url);
  bool Connect(const std::string& url, const HttpHeaders& headers);
  bool Connect(const std::string& url, const HttpHeaders& headers,
               const std::vector<std::string>& protocols);

  // Send WebSocket messages
  bool SendText(const std::string& text);
  bool SendBinary(const void* data, size_t len);
  bool SendBinary(const std::string& data);
  bool SendMessage(const WebSocketMessage& message);

  // Send control frames
  bool SendPing(const std::string& data = "");
  bool SendPong(const std::string& data = "");
  bool SendClose(uint16_t code = WebSocketCloseCode::kNormalClosure,
                 const std::string& reason = "");

  // Connection management
  void Disconnect();
  void Close(uint16_t code = WebSocketCloseCode::kNormalClosure,
             const std::string& reason = "");

  // State queries
  State GetState() const { return state_; }
  bool IsConnected() const { return state_ == State::kConnected; }
  bool IsConnecting() const {
    return state_ == State::kConnecting || state_ == State::kHandshaking;
  }

  // Configuration
  void SetMaxMessageSize(size_t max_size) { max_message_size_ = max_size; }
  void SetPingInterval(uint32_t interval_ms) {
    ping_interval_ms_ = interval_ms;
  }
  void SetAutoReconnect(bool enable, uint32_t delay_ms = 5000);

  // Get connection info
  std::string GetUrl() const { return websocket_url_; }
  std::string GetProtocol() const { return selected_protocol_; }

 private:
  // HttpClientEventListener implementation
  void OnHttpResponse(HttpClient* client,
                      const HttpResponse& response) override;
  void OnHttpError(HttpClient* client, const std::string& error) override;

  // TcpClientEventListener implementation
  void OnConnected(TcpClient* client, bool success) override;
  void OnDataReceived(TcpClient* client, const void* data, size_t len) override;
  void OnDisconnected(TcpClient* client) override;
  void OnError(TcpClient* client, const std::string& error) override;

  // WebSocket protocol handling
  void ProcessHandshakeResponse();
  void ProcessWebSocketFrames();
  bool ValidateHandshakeResponse(const std::string& response);

  // Frame processing
  bool ParseWebSocketFrame();
  void HandleWebSocketFrame(const WebSocketFrame& frame);

  // Frame construction and sending
  bool SendWebSocketFrame(WebSocketOpcode opcode, const void* payload,
                          size_t payload_len, bool fin = true);

  // Utility methods
  void SetState(State new_state);
  void HandleError(const std::string& error);
  void StartPingTimer();
  void StopPingTimer();
  void ScheduleReconnect();

  // Connection state
  TaskRunner* task_runner_;
  WebSocketClientEventListener* listener_;
  std::unique_ptr<TcpClient> tcp_client_;
  State state_;

  // WebSocket connection info
  std::string websocket_url_;
  std::string websocket_key_;
  std::string handshake_request_;
  bool handshake_completed_;
  HttpHeaders connect_headers_;
  std::vector<std::string> requested_protocols_;
  std::string selected_protocol_;

  // Frame parsing state
  std::vector<uint8_t> receive_buffer_;
  std::vector<uint8_t> frame_buffer_;  // Buffer for incomplete frames
  size_t frame_bytes_needed_;
  bool frame_parsing_header_;

  // Message fragmentation support
  std::vector<uint8_t> fragmented_message_;
  WebSocketOpcode fragmented_opcode_;
  bool receiving_fragmented_;

  // Configuration
  size_t max_message_size_;
  uint32_t ping_interval_ms_;
  bool auto_reconnect_;
  uint32_t reconnect_delay_ms_;

  // Connection management
  bool close_sent_;
  uint16_t close_code_;
  std::string close_reason_;

  // Auto-ping support
  std::atomic_int ping_timer_id_;
};

}  // namespace xtils
