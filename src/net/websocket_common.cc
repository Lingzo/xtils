#include "xtils/net/websocket_common.h"

#include <cstring>
#include <random>

#include "xtils/utils/base64.h"
#include "xtils/utils/sha1.h"

namespace xtils {
namespace WebSocketUtils {

std::string ComputeWebSocketAccept(const std::string& client_key) {
  std::string combined = client_key + kWebSocketGuid;
  auto digest = SHA1Hash(combined.data(), combined.size());
  return Base64Encode(digest.data(), digest.size());
}

std::string GenerateWebSocketKey() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> dis(0, 255);

  uint8_t random_bytes[16];
  for (int i = 0; i < 16; ++i) {
    random_bytes[i] = dis(gen);
  }

  return Base64Encode(random_bytes, sizeof(random_bytes));
}

std::vector<uint8_t> BuildFrame(WebSocketOpcode opcode, const void* payload,
                                size_t payload_len, bool fin, bool mask,
                                const uint8_t* mask_key) {
  std::vector<uint8_t> frame;

  // First byte: FIN + RSV + Opcode
  uint8_t byte0 = static_cast<uint8_t>(opcode);
  if (fin) {
    byte0 |= 0x80;
  }
  frame.push_back(byte0);

  // Second byte: MASK + Payload length
  uint8_t byte1 = 0;
  if (mask) {
    byte1 |= 0x80;
  }

  if (payload_len < 126) {
    byte1 |= static_cast<uint8_t>(payload_len);
    frame.push_back(byte1);
  } else if (payload_len < 65536) {
    byte1 |= 126;
    frame.push_back(byte1);
    uint16_t len16 = HostToBE16(static_cast<uint16_t>(payload_len));
    frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&len16),
                 reinterpret_cast<uint8_t*>(&len16) + 2);
  } else {
    byte1 |= 127;
    frame.push_back(byte1);
    uint64_t len64 = HostToBE64(payload_len);
    frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&len64),
                 reinterpret_cast<uint8_t*>(&len64) + 8);
  }

  // Masking key (if masked)
  uint8_t generated_mask[4] = {0};
  if (mask) {
    if (mask_key) {
      memcpy(generated_mask, mask_key, 4);
    } else {
      // Generate random mask
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<uint8_t> dis(0, 255);
      for (int i = 0; i < 4; ++i) {
        generated_mask[i] = dis(gen);
      }
    }
    frame.insert(frame.end(), generated_mask, generated_mask + 4);
  }

  // Payload
  if (payload && payload_len > 0) {
    size_t payload_start = frame.size();
    frame.resize(frame.size() + payload_len);
    memcpy(&frame[payload_start], payload, payload_len);

    if (mask) {
      ApplyMask(&frame[payload_start], payload_len, generated_mask);
    }
  }

  return frame;
}

size_t ParseFrame(const uint8_t* data, size_t data_len, WebSocketFrame& frame) {
  if (data_len < 2) {
    return 0;  // Need at least 2 bytes for basic header
  }

  const uint8_t* ptr = data;
  size_t consumed = 0;

  // Parse first byte
  uint8_t byte0 = *ptr++;
  consumed++;

  frame.fin = (byte0 & 0x80) != 0;
  frame.opcode = static_cast<WebSocketOpcode>(byte0 & 0x0F);

  // Parse second byte
  uint8_t byte1 = *ptr++;
  consumed++;

  frame.masked = (byte1 & 0x80) != 0;
  uint8_t payload_len = byte1 & 0x7F;

  // Parse extended payload length
  uint64_t extended_payload_len = payload_len;
  if (payload_len == 126) {
    if (data_len < consumed + 2) {
      return 0;
    }
    uint16_t len16;
    memcpy(&len16, ptr, 2);
    extended_payload_len = BE16ToHost(len16);
    ptr += 2;
    consumed += 2;
  } else if (payload_len == 127) {
    if (data_len < consumed + 8) {
      return 0;
    }
    uint64_t len64;
    memcpy(&len64, ptr, 8);
    extended_payload_len = BE64ToHost(len64);
    ptr += 8;
    consumed += 8;
  }

  // Check for oversized frames
  if (extended_payload_len > kMaxFrameSize) {
    return 0;  // Frame too large
  }

  // Parse masking key
  if (frame.masked) {
    if (data_len < consumed + 4) {
      return 0;
    }
    memcpy(frame.mask, ptr, 4);
    ptr += 4;
    consumed += 4;
  }

  // Check if we have enough data for the payload
  if (data_len < consumed + extended_payload_len) {
    return 0;
  }

  // Extract payload
  frame.payload.resize(extended_payload_len);
  if (extended_payload_len > 0) {
    memcpy(frame.payload.data(), ptr, extended_payload_len);

    // Unmask payload if needed
    if (frame.masked) {
      ApplyMask(frame.payload.data(), frame.payload.size(), frame.mask);
    }
  }

  consumed += extended_payload_len;
  return consumed;
}

void ApplyMask(uint8_t* data, size_t len, const uint8_t* mask) {
  for (size_t i = 0; i < len; ++i) {
    data[i] ^= mask[i % 4];
  }
}

bool IsValidOpcode(uint8_t opcode) {
  return opcode <= 0x2 || (opcode >= 0x8 && opcode <= 0xA);
}

bool IsControlFrame(WebSocketOpcode opcode) {
  return static_cast<uint8_t>(opcode) >= 0x8;
}

bool IsDataFrame(WebSocketOpcode opcode) {
  return static_cast<uint8_t>(opcode) <= 0x2;
}

const char* OpcodeToString(WebSocketOpcode opcode) {
  switch (opcode) {
    case WebSocketOpcode::kContinuation:
      return "CONTINUATION";
    case WebSocketOpcode::kText:
      return "TEXT";
    case WebSocketOpcode::kBinary:
      return "BINARY";
    case WebSocketOpcode::kClose:
      return "CLOSE";
    case WebSocketOpcode::kPing:
      return "PING";
    case WebSocketOpcode::kPong:
      return "PONG";
    default:
      return "UNKNOWN";
  }
}

bool IsValidCloseCode(uint16_t code) {
  // Valid close codes according to RFC 6455
  return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1011) ||
         (code >= 3000 && code <= 4999);
}

const char* GetCloseReasonString(uint16_t code) {
  switch (code) {
    case WebSocketCloseCode::kNormalClosure:
      return "Normal Closure";
    case WebSocketCloseCode::kGoingAway:
      return "Going Away";
    case WebSocketCloseCode::kProtocolError:
      return "Protocol Error";
    case WebSocketCloseCode::kUnsupportedData:
      return "Unsupported Data";
    case WebSocketCloseCode::kNoStatusRcvd:
      return "No Status Received";
    case WebSocketCloseCode::kAbnormalClosure:
      return "Abnormal Closure";
    case WebSocketCloseCode::kInvalidFramePayload:
      return "Invalid Frame Payload Data";
    case WebSocketCloseCode::kPolicyViolation:
      return "Policy Violation";
    case WebSocketCloseCode::kMessageTooBig:
      return "Message Too Big";
    case WebSocketCloseCode::kMandatoryExt:
      return "Mandatory Extension";
    case WebSocketCloseCode::kInternalError:
      return "Internal Error";
    default:
      return "Unknown";
  }
}

}  // namespace WebSocketUtils
}  // namespace xtils
