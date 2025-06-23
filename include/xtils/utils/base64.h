#pragma once

#include <optional>
#include <string>

#include "xtils/utils/string_view.h"

namespace xtils {

// Returns the length of the destination string (included '=' padding).
// Does NOT include the size of the string null terminator.
inline size_t Base64EncSize(size_t src_size) { return (src_size + 2) / 3 * 4; }

// Returns the upper bound on the length of the destination buffer.
// The actual decoded length might be <= the number returned here.
inline size_t Base64DecSize(size_t src_size) { return (src_size + 3) / 4 * 3; }

// Does NOT null-terminate |dst|.
ssize_t Base64Encode(const void* src, size_t src_size, char* dst,
                     size_t dst_size);

std::string Base64Encode(const void* src, size_t src_size);

inline std::string Base64Encode(StringView sv) {
  return Base64Encode(sv.data(), sv.size());
}

// Returns -1 in case of failure.
ssize_t Base64Decode(const char* src, size_t src_size, uint8_t* dst,
                     size_t dst_size);

std::optional<std::string> Base64Decode(const char* src, size_t src_size);

inline std::optional<std::string> Base64Decode(StringView sv) {
  return Base64Decode(sv.data(), sv.size());
}

}  // namespace xtils
