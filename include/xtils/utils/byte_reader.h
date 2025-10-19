#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace xtils {

class ByteReader {
 public:
  ByteReader(const void* buffer, size_t size, bool is_little_endian);
  ByteReader(const ByteReader&) = delete;
  ByteReader& operator=(const ByteReader&) = delete;

  // Position management
  size_t Position() const { return position_; }
  size_t Size() const { return size_; }
  size_t Remaining() const { return size_ - position_; }
  bool HasRemaining() const { return position_ < size_; }
  bool Seek(size_t position);
  void Reset() { position_ = 0; }

  // Read methods - return true on success, value via reference
  bool ReadUInt8(uint8_t& value);
  bool ReadInt8(int8_t& value);
  bool ReadUInt16(uint16_t& value);
  bool ReadInt16(int16_t& value);
  bool ReadUInt32(uint32_t& value);
  bool ReadInt32(int32_t& value);
  bool ReadUInt64(uint64_t& value);
  bool ReadInt64(int64_t& value);
  bool ReadFloat(float& value);
  bool ReadDouble(double& value);

  // Read raw bytes
  bool ReadBytes(void* dest, size_t count);
  bool ReadString(std::string& str, size_t length);
  bool ReadNullTerminatedString(std::string& str, size_t max_length = SIZE_MAX);

  // Check if there's enough data to read
  bool CanRead(size_t bytes) const { return position_ + bytes <= size_; }

 private:
  const uint8_t* buffer_;
  size_t size_;
  size_t position_;
  const bool is_little_endian_;

  template <typename T>
  bool ReadValue(T& value);
};

}  // namespace xtils
