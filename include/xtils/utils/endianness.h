#pragma once

#include <endian.h>

#include <cstdint>

namespace xtils {

// Compile-time endianness detection
constexpr bool IsSystemLittleEndian() {
  return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// Compile-time constant for system endianness
constexpr bool kSystemIsLittleEndian = IsSystemLittleEndian();

// Efficient byte swapping functions using system endian.h
template <typename T>
T SwapBytes(T value) {
  if constexpr (sizeof(T) == 1) {
    return value;
  } else if constexpr (sizeof(T) == 2) {
    uint16_t v = *reinterpret_cast<uint16_t*>(&value);
    v = __bswap_16(v);
    return *reinterpret_cast<T*>(&v);
  } else if constexpr (sizeof(T) == 4) {
    uint32_t v = *reinterpret_cast<uint32_t*>(&value);
    v = __bswap_32(v);
    return *reinterpret_cast<T*>(&v);
  } else if constexpr (sizeof(T) == 8) {
    uint64_t v = *reinterpret_cast<uint64_t*>(&value);
    v = __bswap_64(v);
    return *reinterpret_cast<T*>(&v);
  }
  return value;
}

}  // namespace xtils
