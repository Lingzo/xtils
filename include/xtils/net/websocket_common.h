#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace xtils {

// WebSocket opcodes as defined in RFC 6455
enum class WebSocketOpcode : uint8_t {
  kContinuation = 0x0,
  kText = 0x1,
  kBinary = 0x2,
  kClose = 0x8,
  kPing = 0x9,
  kPong = 0xA
};

// WebSocket close codes as defined in RFC 6455
namespace WebSocketCloseCode {
constexpr uint16_t kNormalClosure = 1000;
constexpr uint16_t kGoingAway = 1001;
constexpr uint16_t kProtocolError = 1002;
constexpr uint16_t kUnsupportedData = 1003;
constexpr uint16_t kNoStatusRcvd = 1005;
constexpr uint16_t kAbnormalClosure = 1006;
constexpr uint16_t kInvalidFramePayload = 1007;
constexpr uint16_t kPolicyViolation = 1008;
constexpr uint16_t kMessageTooBig = 1009;
constexpr uint16_t kMandatoryExt = 1010;
constexpr uint16_t kInternalError = 1011;
}  // namespace WebSocketCloseCode

// WebSocket frame structure
struct WebSocketFrame {
  bool fin = true;
  WebSocketOpcode opcode = WebSocketOpcode::kText;
  bool masked = false;
  uint8_t mask[4] = {0};
  std::vector<uint8_t> payload;

  WebSocketFrame() = default;
  WebSocketFrame(WebSocketOpcode op, const void* data, size_t len,
                 bool final = true)
      : fin(final), opcode(op) {
    if (data && len > 0) {
      payload.resize(len);
      std::memcpy(payload.data(), data, len);
    }
  }
};

// WebSocket message (can span multiple frames)
struct WebSocketMessage {
  std::string data;
  bool is_text = false;  // false = binary, true = text

  WebSocketMessage() = default;
  WebSocketMessage(const std::string& msg, bool text = true)
      : data(msg), is_text(text) {}
  WebSocketMessage(const void* buffer, size_t len, bool text = false)
      : data(static_cast<const char*>(buffer), len), is_text(text) {}
};

// WebSocket GUID as defined in RFC 6455
constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Maximum frame size (1MB by default)
constexpr size_t kMaxFrameSize = 1024 * 1024;

// Byte order conversion helpers
inline uint16_t HostToBE16(uint16_t x) { return __builtin_bswap16(x); }
inline uint32_t HostToBE32(uint32_t x) { return __builtin_bswap32(x); }
inline uint64_t HostToBE64(uint64_t x) { return __builtin_bswap64(x); }

inline uint16_t BE16ToHost(uint16_t x) { return __builtin_bswap16(x); }
inline uint32_t BE32ToHost(uint32_t x) { return __builtin_bswap32(x); }
inline uint64_t BE64ToHost(uint64_t x) { return __builtin_bswap64(x); }

// WebSocket utilities namespace
namespace WebSocketUtils {
// Generate WebSocket accept key from client key
std::string ComputeWebSocketAccept(const std::string& client_key);

// Generate random WebSocket key for client handshake
std::string GenerateWebSocketKey();

// Build WebSocket frame binary data
std::vector<uint8_t> BuildFrame(WebSocketOpcode opcode, const void* payload,
                                size_t payload_len, bool fin = true,
                                bool mask = false,
                                const uint8_t* mask_key = nullptr);

// Parse WebSocket frame from buffer
// Returns the number of bytes consumed, or 0 if more data is needed
size_t ParseFrame(const uint8_t* data, size_t data_len, WebSocketFrame& frame);

// Apply/remove XOR mask to payload data
void ApplyMask(uint8_t* data, size_t len, const uint8_t* mask);

// Validate WebSocket opcode
bool IsValidOpcode(uint8_t opcode);

// Check if opcode is a control frame
bool IsControlFrame(WebSocketOpcode opcode);

// Check if opcode is a data frame
bool IsDataFrame(WebSocketOpcode opcode);

// Convert opcode to string for logging
const char* OpcodeToString(WebSocketOpcode opcode);

// Validate close code
bool IsValidCloseCode(uint16_t code);

// Get default close reason for code
const char* GetCloseReasonString(uint16_t code);

}  // namespace WebSocketUtils

}  // namespace xtils
