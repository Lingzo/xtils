#pragma once
#include <stddef.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace xtils {

constexpr size_t kSHA1Length = 20;
using SHA1Digest = std::array<uint8_t, kSHA1Length>;

SHA1Digest SHA1Hash(const std::string& str);
SHA1Digest SHA1Hash(const void* data, size_t size);

}  // namespace xtils
