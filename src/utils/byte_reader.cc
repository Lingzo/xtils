#include "xtils/utils/byte_reader.h"

#include <algorithm>
#include <cstring>

#include "xtils/utils/endianness.h"

namespace xtils {

ByteReader::ByteReader(const void* buffer, size_t size, bool is_little_endian)
    : buffer_(static_cast<const uint8_t*>(buffer)),
      size_(size),
      position_(0),
      is_little_endian_(is_little_endian) {}

bool ByteReader::Seek(size_t position) {
  if (position > size_) {
    return false;
  }
  position_ = position;
  return true;
}

bool ByteReader::ReadUInt8(uint8_t& value) {
  if (!CanRead(sizeof(uint8_t))) {
    return false;
  }
  value = buffer_[position_];
  position_ += sizeof(uint8_t);
  return true;
}

bool ByteReader::ReadInt8(int8_t& value) {
  if (!CanRead(sizeof(int8_t))) {
    return false;
  }
  value = static_cast<int8_t>(buffer_[position_]);
  position_ += sizeof(int8_t);
  return true;
}

bool ByteReader::ReadUInt16(uint16_t& value) { return ReadValue(value); }

bool ByteReader::ReadInt16(int16_t& value) { return ReadValue(value); }

bool ByteReader::ReadUInt32(uint32_t& value) { return ReadValue(value); }

bool ByteReader::ReadInt32(int32_t& value) { return ReadValue(value); }

bool ByteReader::ReadUInt64(uint64_t& value) { return ReadValue(value); }

bool ByteReader::ReadInt64(int64_t& value) { return ReadValue(value); }

bool ByteReader::ReadFloat(float& value) { return ReadValue(value); }

bool ByteReader::ReadDouble(double& value) { return ReadValue(value); }

bool ByteReader::ReadBytes(void* dest, size_t count) {
  if (!CanRead(count)) {
    return false;
  }
  std::memcpy(dest, buffer_ + position_, count);
  position_ += count;
  return true;
}

bool ByteReader::ReadString(std::string& str, size_t length) {
  if (!CanRead(length)) {
    return false;
  }
  str.assign(reinterpret_cast<const char*>(buffer_ + position_), length);
  position_ += length;
  return true;
}

bool ByteReader::ReadNullTerminatedString(std::string& str, size_t max_length) {
  str.clear();
  size_t remaining = std::min(max_length, Remaining());

  for (size_t i = 0; i < remaining; ++i) {
    if (buffer_[position_ + i] == 0) {
      str.assign(reinterpret_cast<const char*>(buffer_ + position_), i);
      position_ += i + 1;  // Include the null terminator
      return true;
    }
  }

  // No null terminator found within max_length
  return false;
}

template <typename T>
bool ByteReader::ReadValue(T& value) {
  if (!CanRead(sizeof(T))) {
    return false;
  }

  std::memcpy(&value, buffer_ + position_, sizeof(T));
  position_ += sizeof(T);

// Apply endianness conversion if needed (compile-time check)
#if __BYTE_ORDER == __LITTLE_ENDIAN
  if (!is_little_endian_) {
    value = xtils::SwapBytes(value);
  }
#else
  if (is_little_endian_) {
    value = xtils::SwapBytes(value);
  }
#endif

  return true;
}

// Explicit template instantiations
template bool ByteReader::ReadValue<uint16_t>(uint16_t& value);
template bool ByteReader::ReadValue<int16_t>(int16_t& value);
template bool ByteReader::ReadValue<uint32_t>(uint32_t& value);
template bool ByteReader::ReadValue<int32_t>(int32_t& value);
template bool ByteReader::ReadValue<uint64_t>(uint64_t& value);
template bool ByteReader::ReadValue<int64_t>(int64_t& value);
template bool ByteReader::ReadValue<float>(float& value);
template bool ByteReader::ReadValue<double>(double& value);

}  // namespace xtils
