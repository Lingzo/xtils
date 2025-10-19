#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace xtils {

class ByteWriter {
 public:
  ByteWriter(void* buffer, size_t size, bool is_little_endian);
  ByteWriter(const ByteWriter&) = delete;
  ByteWriter& operator=(const ByteWriter&) = delete;

  // Position management
  size_t Position() const { return position_; }
  size_t Size() const { return size_; }
  size_t Remaining() const { return size_ - position_; }
  bool HasRemaining() const { return position_ < size_; }
  bool Seek(size_t position);
  void Reset() { position_ = 0; }

  // Write methods
  bool WriteUInt8(uint8_t value);
  bool WriteInt8(int8_t value);
  bool WriteUInt16(uint16_t value);
  bool WriteInt16(int16_t value);
  bool WriteUInt32(uint32_t value);
  bool WriteInt32(int32_t value);
  bool WriteUInt64(uint64_t value);
  bool WriteInt64(int64_t value);
  bool WriteFloat(float value);
  bool WriteDouble(double value);

  // Write raw bytes
  bool WriteBytes(const void* src, size_t count);
  bool WriteString(const std::string& str);
  bool WriteNullTerminatedString(const std::string& str);

  // Check if there's enough space to write
  bool CanWrite(size_t bytes) const { return position_ + bytes <= size_; }

 private:
  uint8_t* buffer_;
  size_t size_;
  size_t position_;
  const bool is_little_endian_;

  template <typename T>
  bool WriteValue(T value);
};

}  // namespace xtils
