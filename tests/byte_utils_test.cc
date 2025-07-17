#include "xtils/utils/byte_reader.h"
#include "xtils/utils/byte_writer.h"
#include "xtils/utils/endianness.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace xtils;

void TestBasicReadWrite() {
    std::cout << "Testing basic read/write operations..." << std::endl;
    
    // Test buffer
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    // Test little-endian writer
    ByteWriter writer_le(buffer, sizeof(buffer), true);
    
    assert(writer_le.WriteUInt8(0x12));
    assert(writer_le.WriteUInt16(0x3456));
    assert(writer_le.WriteUInt32(0x789ABCDE));
    assert(writer_le.WriteUInt64(0x123456789ABCDEF0ULL));
    assert(writer_le.WriteFloat(3.14159f));
    assert(writer_le.WriteDouble(2.71828));
    assert(writer_le.WriteString("Hello"));
    assert(writer_le.WriteNullTerminatedString("World"));
    
    // Test little-endian reader
    ByteReader reader_le(buffer, sizeof(buffer), true);
    
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float f;
    double d;
    std::string str;
    
    assert(reader_le.ReadUInt8(u8) && u8 == 0x12);
    assert(reader_le.ReadUInt16(u16) && u16 == 0x3456);
    assert(reader_le.ReadUInt32(u32) && u32 == 0x789ABCDE);
    assert(reader_le.ReadUInt64(u64) && u64 == 0x123456789ABCDEF0ULL);
    assert(reader_le.ReadFloat(f) && f == 3.14159f);
    assert(reader_le.ReadDouble(d) && d == 2.71828);
    assert(reader_le.ReadString(str, 5) && str == "Hello");
    assert(reader_le.ReadNullTerminatedString(str) && str == "World");
    
    std::cout << "Little-endian tests passed!" << std::endl;
}

void TestBigEndian() {
    std::cout << "Testing big-endian operations..." << std::endl;
    
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    // Test big-endian writer
    ByteWriter writer_be(buffer, sizeof(buffer), false);
    
    assert(writer_be.WriteUInt16(0x1234));
    assert(writer_be.WriteUInt32(0x12345678));
    
    // Test big-endian reader
    ByteReader reader_be(buffer, sizeof(buffer), false);
    
    uint16_t u16;
    uint32_t u32;
    
    assert(reader_be.ReadUInt16(u16) && u16 == 0x1234);
    assert(reader_be.ReadUInt32(u32) && u32 == 0x12345678);
    
    // Verify byte order in buffer (big-endian should store MSB first)
    if (IsSystemLittleEndian()) {
        // On little-endian system, big-endian data should be byte-swapped
        assert(buffer[0] == 0x12 && buffer[1] == 0x34);  // uint16 in big-endian
        assert(buffer[2] == 0x12 && buffer[3] == 0x34 && 
               buffer[4] == 0x56 && buffer[5] == 0x78);  // uint32 in big-endian
    } else {
        // On big-endian system, big-endian data should be stored as-is
        assert(buffer[0] == 0x34 && buffer[1] == 0x12);  // uint16 in native order
        assert(buffer[2] == 0x78 && buffer[3] == 0x56 && 
               buffer[4] == 0x34 && buffer[5] == 0x12);  // uint32 in native order
    }
    
    std::cout << "Big-endian tests passed!" << std::endl;
}

void TestBoundaryConditions() {
    std::cout << "Testing boundary conditions..." << std::endl;
    
    uint8_t buffer[8];
    memset(buffer, 0, sizeof(buffer));
    
    ByteWriter writer(buffer, sizeof(buffer), true);
    ByteReader reader(buffer, sizeof(buffer), true);
    
    // Fill buffer completely
    assert(writer.WriteUInt64(0x123456789ABCDEF0ULL));
    assert(writer.Position() == 8);
    assert(writer.Remaining() == 0);
    
    // Try to write beyond buffer (should fail)
    assert(!writer.WriteUInt8(0xFF));
    assert(writer.Position() == 8);
    
    // Read back
    uint64_t u64;
    assert(reader.ReadUInt64(u64) && u64 == 0x123456789ABCDEF0ULL);
    assert(reader.Position() == 8);
    assert(reader.Remaining() == 0);
    
    // Try to read beyond buffer (should fail)
    uint8_t u8;
    assert(!reader.ReadUInt8(u8));
    
    std::cout << "Boundary condition tests passed!" << std::endl;
}

void TestPositionManagement() {
    std::cout << "Testing position management..." << std::endl;
    
    uint8_t buffer[16];
    memset(buffer, 0, sizeof(buffer));
    
    ByteWriter writer(buffer, sizeof(buffer), true);
    ByteReader reader(buffer, sizeof(buffer), true);
    
    // Write some data
    assert(writer.WriteUInt32(0x12345678));
    assert(writer.WriteUInt32(0x9ABCDEF0));
    assert(writer.Position() == 8);
    
    // Seek to beginning
    writer.Reset();
    assert(writer.Position() == 0);
    
    // Overwrite first value
    assert(writer.WriteUInt32(0xDEADBEEF));
    
    // Read and verify
    uint32_t u32_1, u32_2;
    assert(reader.ReadUInt32(u32_1) && u32_1 == 0xDEADBEEF);
    assert(reader.ReadUInt32(u32_2) && u32_2 == 0x9ABCDEF0);
    
    // Test seeking
    assert(reader.Seek(4));
    assert(reader.Position() == 4);
    assert(reader.ReadUInt32(u32_2) && u32_2 == 0x9ABCDEF0);
    
    // Test invalid seek
    assert(!reader.Seek(100));
    
    std::cout << "Position management tests passed!" << std::endl;
}

void TestSignedTypes() {
    std::cout << "Testing signed integer types..." << std::endl;
    
    uint8_t buffer[16];
    memset(buffer, 0, sizeof(buffer));
    
    ByteWriter writer(buffer, sizeof(buffer), true);
    ByteReader reader(buffer, sizeof(buffer), true);
    
    // Test negative values
    assert(writer.WriteInt8(-128));
    assert(writer.WriteInt16(-32768));
    assert(writer.WriteInt32(-2147483648));
    assert(writer.WriteInt64(static_cast<int64_t>(-9223372036854775808ULL)));
    
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    
    assert(reader.ReadInt8(i8) && i8 == -128);
    assert(reader.ReadInt16(i16) && i16 == -32768);
    assert(reader.ReadInt32(i32) && i32 == -2147483648);
    assert(reader.ReadInt64(i64) && i64 == static_cast<int64_t>(-9223372036854775808ULL));
    
    std::cout << "Signed integer tests passed!" << std::endl;
}

void TestEndiannessConversion() {
    std::cout << "Testing endianness conversion..." << std::endl;
    
    uint8_t buffer[8];
    memset(buffer, 0, sizeof(buffer));
    
    // Write in little-endian
    ByteWriter writer_le(buffer, sizeof(buffer), true);
    assert(writer_le.WriteUInt32(0x12345678));
    
    // Read as little-endian - should get original value
    ByteReader reader_le(buffer, sizeof(buffer), true);
    uint32_t value_le;
    assert(reader_le.ReadUInt32(value_le));
    assert(value_le == 0x12345678);
    
    // Read as big-endian - should get byte-swapped value
    ByteReader reader_be(buffer, sizeof(buffer), false);
    uint32_t value_be;
    assert(reader_be.ReadUInt32(value_be));
    assert(value_be == 0x78563412);  // Byte-swapped
    
    // Test the reverse: write big-endian, read little-endian
    memset(buffer, 0, sizeof(buffer));
    ByteWriter writer_be(buffer, sizeof(buffer), false);
    assert(writer_be.WriteUInt32(0x12345678));
    
    ByteReader reader_le2(buffer, sizeof(buffer), true);
    uint32_t value_le2;
    assert(reader_le2.ReadUInt32(value_le2));
    assert(value_le2 == 0x78563412);  // Byte-swapped
    
    std::cout << "Endianness conversion tests passed!" << std::endl;
}

int main() {
    std::cout << "Running ByteReader and ByteWriter tests..." << std::endl;
    std::cout << "System is " << (IsSystemLittleEndian() ? "little-endian" : "big-endian") << std::endl;
    
    TestBasicReadWrite();
    TestBigEndian();
    TestBoundaryConditions();
    TestPositionManagement();
    TestSignedTypes();
    TestEndiannessConversion();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}