#pragma once

#include "core/Types.h"

namespace coconut {

class BinaryWriter {
public:
    explicit BinaryWriter(Endian endian = Endian::Little);

    void SetEndian(Endian e) { endian_ = e; }
    Endian GetEndian() const { return endian_; }

    size_t Size() const { return buffer_.size(); }
    const ByteBuffer& Buffer() const { return buffer_; }
    ByteBuffer TakeBuffer() { return std::move(buffer_); }
    void Reserve(size_t bytes) { buffer_.reserve(bytes); }

    void WriteU8(u8 v);
    void WriteI8(i8 v);
    void WriteBool(bool v);
    void WriteU16(u16 v);
    void WriteI16(i16 v);
    void WriteU32(u32 v);
    void WriteI32(i32 v);
    void WriteU64(u64 v);
    void WriteI64(i64 v);
    void WriteF32(f32 v);
    void WriteF64(f64 v);

    void WriteFixedString(std::string_view s);
    void WriteCString(std::string_view s);
    void WriteAlignedString(std::string_view s);
    void WriteBytes(ByteSpan bytes);
    void WriteZeros(size_t count);
    void Align(size_t alignment);

    void PatchU32(size_t offset, u32 v);
    void PatchI32(size_t offset, i32 v);
    void PatchU64(size_t offset, u64 v);
    void PatchI64(size_t offset, i64 v);

private:
    ByteBuffer buffer_;
    Endian endian_ = Endian::Little;
};

} // namespace coconut
