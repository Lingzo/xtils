#include "xtils/utils/byte_writer.h"

#include <cstring>

#include "xtils/utils/endianness.h"

namespace xtils {

ByteWriter::ByteWriter(void* buffer, size_t size, bool is_little_endian)
    : buffer_(static_cast<uint8_t*>(buffer)),
      size_(size),
      position_(0),
      is_little_endian_(is_little_endian) {}

bool ByteWriter::Seek(size_t position) {
  if (position > size_) {
    return false;
  }
  position_ = position;
  return true;
}

bool ByteWriter::WriteUInt8(uint8_t value) {
  if (!CanWrite(sizeof(uint8_t))) {
    return false;
  }
  buffer_[position_] = value;
  position_ += sizeof(uint8_t);
  return true;
}

bool ByteWriter::WriteInt8(int8_t value) {
  if (!CanWrite(sizeof(int8_t))) {
    return false;
  }
  buffer_[position_] = static_cast<uint8_t>(value);
  position_ += sizeof(int8_t);
  return true;
}

bool ByteWriter::WriteUInt16(uint16_t value) { return WriteValue(value); }

bool ByteWriter::WriteInt16(int16_t value) { return WriteValue(value); }

bool ByteWriter::WriteUInt32(uint32_t value) { return WriteValue(value); }

bool ByteWriter::WriteInt32(int32_t value) { return WriteValue(value); }

bool ByteWriter::WriteUInt64(uint64_t value) { return WriteValue(value); }

bool ByteWriter::WriteInt64(int64_t value) { return WriteValue(value); }

bool ByteWriter::WriteFloat(float value) { return WriteValue(value); }

bool ByteWriter::WriteDouble(double value) { return WriteValue(value); }

bool ByteWriter::WriteBytes(const void* src, size_t count) {
  if (!CanWrite(count)) {
    return false;
  }
  std::memcpy(buffer_ + position_, src, count);
  position_ += count;
  return true;
}

bool ByteWriter::WriteString(const std::string& str) {
  if (!CanWrite(str.length())) {
    return false;
  }
  std::memcpy(buffer_ + position_, str.data(), str.length());
  position_ += str.length();
  return true;
}

bool ByteWriter::WriteNullTerminatedString(const std::string& str) {
  if (!CanWrite(str.length() + 1)) {
    return false;
  }
  std::memcpy(buffer_ + position_, str.data(), str.length());
  position_ += str.length();
  buffer_[position_] = 0;
  position_ += 1;
  return true;
}

template <typename T>
bool ByteWriter::WriteValue(T value) {
  if (!CanWrite(sizeof(T))) {
    return false;
  }

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

  std::memcpy(buffer_ + position_, &value, sizeof(T));
  position_ += sizeof(T);
  return true;
}

// Explicit template instantiations
template bool ByteWriter::WriteValue<uint16_t>(uint16_t value);
template bool ByteWriter::WriteValue<int16_t>(int16_t value);
template bool ByteWriter::WriteValue<uint32_t>(uint32_t value);
template bool ByteWriter::WriteValue<int32_t>(int32_t value);
template bool ByteWriter::WriteValue<uint64_t>(uint64_t value);
template bool ByteWriter::WriteValue<int64_t>(int64_t value);
template bool ByteWriter::WriteValue<float>(float value);
template bool ByteWriter::WriteValue<double>(double value);

}  // namespace xtils
