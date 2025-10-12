#include "xtils/net/websocket_client.h"

#include <cstring>
#include <sstream>

#include "xtils/net/http_common.h"
#include "xtils/net/websocket_common.h"

namespace xtils {

namespace {

constexpr size_t kDefaultMaxMessageSize = 16 * 1024 * 1024;  // 16MB

}  // namespace

WebSocketClient::WebSocketClient(TaskRunner* task_runner,
                                 WebSocketClientEventListener* listener)
    : task_runner_(task_runner),
      listener_(listener),
      tcp_client_(std::make_unique<TcpClient>(task_runner, this)),
      state_(State::kDisconnected),
      frame_bytes_needed_(0),
      frame_parsing_header_(true),
      fragmented_opcode_(WebSocketOpcode::kContinuation),
      receiving_fragmented_(false),
      max_message_size_(kDefaultMaxMessageSize),
      ping_interval_ms_(30000),  // 30 seconds
      auto_reconnect_(false),
      reconnect_delay_ms_(5000),
      close_sent_(false),
      close_code_(0),
      handshake_completed_(false) {}

WebSocketClient::~WebSocketClient() {
  StopPingTimer();
  if (IsConnected() || IsConnecting()) {
    Disconnect();
  }
}

bool WebSocketClient::Connect(const std::string& url) {
  return Connect(url, HttpHeaders{});
}

bool WebSocketClient::Connect(const std::string& url,
                              const HttpHeaders& headers) {
  return Connect(url, headers, std::vector<std::string>{});
}

bool WebSocketClient::Connect(const std::string& url,
                              const HttpHeaders& headers,
                              const std::vector<std::string>& protocols) {
  if (state_ != State::kDisconnected && state_ != State::kClosed &&
      state_ != State::kError) {
    return false;
  }

  HttpUrl parsed_url(url);
  if (!parsed_url.IsValid()) {
    HandleError("Invalid WebSocket URL: " + url);
    return false;
  }

  // Convert ws/wss to http/https
  if (parsed_url.scheme == "ws") {
    parsed_url.scheme = "http";
  } else if (parsed_url.scheme == "wss") {
    parsed_url.scheme = "https";
  } else if (parsed_url.scheme != "http" && parsed_url.scheme != "https") {
    HandleError("Unsupported WebSocket scheme: " + parsed_url.scheme);
    return false;
  }

  websocket_url_ = url;
  connect_headers_ = headers;
  requested_protocols_ = protocols;
  selected_protocol_.clear();
  handshake_completed_ = false;

  // Generate WebSocket key
  websocket_key_ = WebSocketUtils::GenerateWebSocketKey();

  // Build HTTP upgrade request
  std::stringstream request_stream;
  request_stream << "GET " << parsed_url.path;
  if (!parsed_url.query.empty()) {
    request_stream << "?" << parsed_url.query;
  }
  request_stream << " HTTP/1.1\r\n";
  request_stream << "Host: " << parsed_url.host;
  if (parsed_url.port != parsed_url.GetDefaultPort()) {
    request_stream << ":" << parsed_url.port;
  }
  request_stream << "\r\n";

  // Add WebSocket upgrade headers
  request_stream << "Upgrade: websocket\r\n";
  request_stream << "Connection: Upgrade\r\n";
  request_stream << "Sec-WebSocket-Key: " << websocket_key_ << "\r\n";
  request_stream << "Sec-WebSocket-Version: 13\r\n";

  if (!protocols.empty()) {
    request_stream << "Sec-WebSocket-Protocol: ";
    for (size_t i = 0; i < protocols.size(); ++i) {
      if (i > 0) request_stream << ", ";
      request_stream << protocols[i];
    }
    request_stream << "\r\n";
  }

  // Add custom headers
  for (const auto& header : headers) {
    request_stream << header.name << ": " << header.value << "\r\n";
  }

  request_stream << "\r\n";
  handshake_request_ = request_stream.str();

  SetState(State::kConnecting);

  // Connect to server
  if (!tcp_client_->ConnectToHost(parsed_url.host, parsed_url.port)) {
    HandleError("Failed to initiate TCP connection");
    return false;
  }

  return true;
}

bool WebSocketClient::SendText(const std::string& text) {
  return SendWebSocketFrame(WebSocketOpcode::kText, text.data(), text.size());
}

bool WebSocketClient::SendBinary(const void* data, size_t len) {
  return SendWebSocketFrame(WebSocketOpcode::kBinary, data, len);
}

bool WebSocketClient::SendBinary(const std::string& data) {
  return SendBinary(data.data(), data.size());
}

bool WebSocketClient::SendMessage(const WebSocketMessage& message) {
  if (message.is_text) {
    return SendText(message.data);
  } else {
    return SendBinary(message.data);
  }
}

bool WebSocketClient::SendPing(const std::string& data) {
  return SendWebSocketFrame(WebSocketOpcode::kPing, data.data(), data.size());
}

bool WebSocketClient::SendPong(const std::string& data) {
  return SendWebSocketFrame(WebSocketOpcode::kPong, data.data(), data.size());
}

bool WebSocketClient::SendClose(uint16_t code, const std::string& reason) {
  if (close_sent_ || state_ != State::kConnected) {
    return false;
  }

  close_sent_ = true;
  close_code_ = code;
  close_reason_ = reason;

  // Build close payload
  std::vector<uint8_t> payload;
  if (code != 0) {
    uint16_t code_be = HostToBE16(code);
    payload.resize(2 + reason.size());
    memcpy(payload.data(), &code_be, 2);
    if (!reason.empty()) {
      memcpy(payload.data() + 2, reason.data(), reason.size());
    }
  }

  SetState(State::kClosing);
  return SendWebSocketFrame(WebSocketOpcode::kClose, payload.data(),
                            payload.size());
}

void WebSocketClient::Disconnect() {
  if (state_ == State::kDisconnected) {
    return;
  }

  StopPingTimer();

  if (state_ == State::kConnected && !close_sent_) {
    SendClose();
  }

  if (tcp_client_) {
    tcp_client_->Disconnect();
  }

  SetState(State::kDisconnected);
}

void WebSocketClient::Close(uint16_t code, const std::string& reason) {
  if (!SendClose(code, reason)) {
    Disconnect();
  }
}

void WebSocketClient::SetAutoReconnect(bool enable, uint32_t delay_ms) {
  auto_reconnect_ = enable;
  reconnect_delay_ms_ = delay_ms > 0 ? delay_ms : reconnect_delay_ms_;
}

// HttpClientEventListener implementation (not used in this implementation)
void WebSocketClient::OnHttpResponse(HttpClient* client,
                                     const HttpResponse& response) {
  // Not used - we handle handshake manually
}

void WebSocketClient::OnHttpError(HttpClient* client,
                                  const std::string& error) {
  // Not used - we handle handshake manually
}

// TcpClientEventListener implementation
void WebSocketClient::OnConnected(TcpClient* client, bool success) {
  if (!success) {
    HandleError("TCP connection failed");
    return;
  }

  if (state_ != State::kConnecting) {
    return;
  }

  SetState(State::kHandshaking);

  // Send HTTP upgrade request
  if (!tcp_client_->SendString(handshake_request_)) {
    HandleError("Failed to send WebSocket handshake request");
    return;
  }
}

void WebSocketClient::OnDataReceived(TcpClient* client, const void* data,
                                     size_t len) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  receive_buffer_.insert(receive_buffer_.end(), bytes, bytes + len);

  if (!handshake_completed_) {
    ProcessHandshakeResponse();
  } else {
    ProcessWebSocketFrames();
  }
}

void WebSocketClient::OnDisconnected(TcpClient* client) {
  StopPingTimer();

  if (state_ != State::kClosed) {
    SetState(State::kDisconnected);

    if (listener_) {
      listener_->OnWebSocketClosed(this, WebSocketCloseCode::kAbnormalClosure,
                                   "Connection lost");
    }

    if (auto_reconnect_) {
      ScheduleReconnect();
    }
  }
}

void WebSocketClient::OnError(TcpClient* client, const std::string& error) {
  HandleError("TCP error: " + error);
}

void WebSocketClient::ProcessHandshakeResponse() {
  // Look for end of HTTP response headers
  std::string buffer_str(receive_buffer_.begin(), receive_buffer_.end());
  size_t header_end = buffer_str.find("\r\n\r\n");

  if (header_end == std::string::npos) {
    return;  // Need more data
  }

  std::string response = buffer_str.substr(0, header_end);

  // Remove processed data from buffer
  receive_buffer_.erase(receive_buffer_.begin(),
                        receive_buffer_.begin() + header_end + 4);

  if (!ValidateHandshakeResponse(response)) {
    HandleError("WebSocket handshake validation failed");
    return;
  }

  handshake_completed_ = true;
  SetState(State::kConnected);
  ping_timer_id_.store(1);  // Initialize ping timer ID
  StartPingTimer();

  if (listener_) {
    listener_->OnWebSocketConnected(this);
  }

  // Process any remaining data as WebSocket frames
  if (!receive_buffer_.empty()) {
    ProcessWebSocketFrames();
  }
}

void WebSocketClient::ProcessWebSocketFrames() {
  while (!receive_buffer_.empty()) {
    if (!ParseWebSocketFrame()) {
      break;  // Need more data
    }
  }
}

bool WebSocketClient::ValidateHandshakeResponse(const std::string& response) {
  std::istringstream stream(response);
  std::string line;

  // Parse status line
  if (!std::getline(stream, line)) {
    return false;
  }

  std::istringstream status_stream(line);
  std::string http_version;
  int status_code;
  status_stream >> http_version >> status_code;

  if (status_code != 101) {
    return false;
  }

  // Parse headers
  std::map<std::string, std::string> headers;
  while (std::getline(stream, line) && !line.empty() && line != "\r") {
    if (line.back() == '\r') {
      line.pop_back();
    }

    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string name = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      // Trim whitespace
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);

      // Convert header name to lowercase for comparison
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      headers[name] = value;
    }
  }

  // Validate required headers
  auto upgrade_it = headers.find("upgrade");
  auto connection_it = headers.find("connection");
  auto accept_it = headers.find("sec-websocket-accept");

  if (upgrade_it == headers.end() || connection_it == headers.end() ||
      accept_it == headers.end()) {
    return false;
  }

  // Check header values (case-insensitive)
  std::string upgrade = upgrade_it->second;
  std::string connection = connection_it->second;
  std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);
  std::transform(connection.begin(), connection.end(), connection.begin(),
                 ::tolower);

  if (upgrade != "websocket") {
    return false;
  }

  if (connection.find("upgrade") == std::string::npos) {
    return false;
  }

  // Validate Sec-WebSocket-Accept
  std::string expected_accept =
      WebSocketUtils::ComputeWebSocketAccept(websocket_key_);
  if (accept_it->second != expected_accept) {
    return false;
  }

  // Check for selected protocol
  auto protocol_it = headers.find("sec-websocket-protocol");
  if (protocol_it != headers.end()) {
    std::string protocol = protocol_it->second;
    bool found = false;
    for (const auto& requested : requested_protocols_) {
      if (requested == protocol) {
        selected_protocol_ = protocol;
        found = true;
        break;
      }
    }
    if (!found) {
      return false;  // Server selected unsupported protocol
    }
  }

  return true;
}

bool WebSocketClient::ParseWebSocketFrame() {
  WebSocketFrame frame;
  size_t consumed = WebSocketUtils::ParseFrame(receive_buffer_.data(),
                                               receive_buffer_.size(), frame);

  if (consumed == 0) {
    return false;  // Need more data
  }

  // Server frames should not be masked
  if (frame.masked) {
    HandleError("Received masked frame from server");
    return false;
  }

  if (frame.payload.size() > max_message_size_) {
    HandleError("Frame payload too large");
    return false;
  }

  // Remove consumed data from buffer
  receive_buffer_.erase(receive_buffer_.begin(),
                        receive_buffer_.begin() + consumed);

  // Process the frame
  HandleWebSocketFrame(frame);
  return true;
}

void WebSocketClient::HandleWebSocketFrame(const WebSocketFrame& frame) {
  const uint8_t* payload = frame.payload.data();
  size_t payload_len = frame.payload.size();

  switch (frame.opcode) {
    case WebSocketOpcode::kText:
    case WebSocketOpcode::kBinary:
      if (receiving_fragmented_) {
        HandleError(
            "New data frame before previous fragmented message completed");
        return;
      }

      if (frame.fin) {
        // Complete message
        WebSocketMessage message(payload, payload_len,
                                 frame.opcode == WebSocketOpcode::kText);
        if (listener_) {
          listener_->OnWebSocketMessage(this, message);
        }
      } else {
        // Start of fragmented message
        receiving_fragmented_ = true;
        fragmented_opcode_ = frame.opcode;
        fragmented_message_.clear();
        fragmented_message_.insert(fragmented_message_.end(), payload,
                                   payload + payload_len);
      }
      break;

    case WebSocketOpcode::kContinuation:
      if (!receiving_fragmented_) {
        HandleError("Continuation frame without initial fragment");
        return;
      }

      fragmented_message_.insert(fragmented_message_.end(), payload,
                                 payload + payload_len);

      if (frame.fin) {
        // Complete fragmented message
        WebSocketMessage message(fragmented_message_.data(),
                                 fragmented_message_.size(),
                                 fragmented_opcode_ == WebSocketOpcode::kText);
        receiving_fragmented_ = false;
        fragmented_message_.clear();

        if (listener_) {
          listener_->OnWebSocketMessage(this, message);
        }
      }
      break;

    case WebSocketOpcode::kClose: {
      uint16_t code = WebSocketCloseCode::kNoStatusRcvd;
      std::string reason;

      if (payload_len >= 2) {
        memcpy(&code, payload, 2);
        code = BE16ToHost(code);

        if (payload_len > 2) {
          reason = std::string(reinterpret_cast<const char*>(payload + 2),
                               payload_len - 2);
        }
      }

      if (!close_sent_) {
        // Echo the close frame back
        SendClose(code, reason);
      }

      SetState(State::kClosed);
      StopPingTimer();

      if (listener_) {
        listener_->OnWebSocketClosed(this, code, reason);
      }
      break;
    }

    case WebSocketOpcode::kPing:
      // Respond with pong
      SendPong(
          std::string(reinterpret_cast<const char*>(payload), payload_len));

      if (listener_) {
        listener_->OnWebSocketPing(
            this,
            std::string(reinterpret_cast<const char*>(payload), payload_len));
      }
      break;

    case WebSocketOpcode::kPong:
      if (listener_) {
        listener_->OnWebSocketPong(
            this,
            std::string(reinterpret_cast<const char*>(payload), payload_len));
      }
      break;

    default:
      HandleError("Unsupported WebSocket opcode: " +
                  std::to_string(static_cast<int>(frame.opcode)));
      break;
  }
}

bool WebSocketClient::SendWebSocketFrame(WebSocketOpcode opcode,
                                         const void* payload,
                                         size_t payload_len, bool fin) {
  if (state_ != State::kConnected && opcode != WebSocketOpcode::kClose) {
    return false;
  }

  if (!tcp_client_ || !tcp_client_->IsConnected()) {
    return false;
  }

  auto frame_data =
      WebSocketUtils::BuildFrame(opcode, payload, payload_len, fin, true);
  return tcp_client_->Send(frame_data.data(), frame_data.size());
}

void WebSocketClient::SetState(State new_state) {
  if (state_ != new_state) {
    state_ = new_state;
  }
}

void WebSocketClient::HandleError(const std::string& error) {
  SetState(State::kError);
  StopPingTimer();

  if (listener_) {
    listener_->OnWebSocketError(this, error);
  }

  if (auto_reconnect_ && state_ == State::kError) {
    ScheduleReconnect();
  }
}

void WebSocketClient::StartPingTimer() {
  if (ping_interval_ms_ == 0 && ping_timer_id_ >= 0) {
    return;
  }
  task_runner_->PostDelayedTask(
      [this]() {
        ping_timer_id_.fetch_add(1);
        SendPing();
      },
      ping_interval_ms_);
}

void WebSocketClient::StopPingTimer() {
  if (ping_timer_id_ != 0) {
    ping_timer_id_ = 0;
  }
}

void WebSocketClient::ScheduleReconnect() {
  if (!auto_reconnect_) {
    return;
  }
  if (!tcp_client_) {
    return;
  }
  SetState(State::kClosed);
  tcp_client_->Disconnect();
  task_runner_->PostDelayedTask(
      [this]() {
        Connect(websocket_url_, connect_headers_, requested_protocols_);
      },
      reconnect_delay_ms_);
}

}  // namespace xtils
