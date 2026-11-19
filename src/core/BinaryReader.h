#pragma once

#include "core/Types.h"
#include <cstring>

namespace coconut {

class BinaryReader {
public:
    explicit BinaryReader(ByteSpan data, Endian endian = Endian::Little);
    explicit BinaryReader(const ByteBuffer& data, Endian endian = Endian::Little);

    void SetEndian(Endian e) { endian_ = e; }
    Endian GetEndian() const { return endian_; }

    size_t Size() const { return data_.size(); }
    size_t Position() const { return pos_; }
    size_t Remaining() const { return pos_ <= data_.size() ? data_.size() - pos_ : 0; }
    bool IsEof() const { return pos_ >= data_.size(); }
    bool HasError() const { return error_; }
    const u8* Data() const { return data_.data(); }

    bool Seek(size_t offset);
    bool Skip(size_t count);
    void Align(size_t alignment);

    u8  ReadU8();
    i8  ReadI8();
    u16 ReadU16();
    i16 ReadI16();
    u32 ReadU32();
    i32 ReadI32();
    u64 ReadU64();
    i64 ReadI64();
    f32 ReadF32();
    f64 ReadF64();
    bool ReadBool();

    std::string ReadFixedString(size_t length);
    std::string ReadCString(size_t maxLen = 65536);
    std::string ReadAlignedString(); // Unity length-prefixed + 4-byte aligned string
    ByteBuffer  ReadBytes(size_t count);
    ByteSpan    ReadSpan(size_t count);

    BinaryReader Slice(size_t offset, size_t count) const;

private:
    bool Ensure(size_t count);

    ByteSpan data_;
    size_t pos_ = 0;
    Endian endian_ = Endian::Little;
    bool error_ = false;
};

} // namespace coconut
