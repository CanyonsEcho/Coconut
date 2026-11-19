#include "core/BinaryWriter.h"
#include <bit>

namespace coconut {

BinaryWriter::BinaryWriter(Endian endian) : endian_(endian) {}

void BinaryWriter::WriteU8(u8 v) {
    buffer_.push_back(v);
}

void BinaryWriter::WriteI8(i8 v) {
    WriteU8(static_cast<u8>(v));
}

void BinaryWriter::WriteBool(bool v) {
    WriteU8(v ? 1 : 0);
}

void BinaryWriter::WriteU16(u16 v) {
    if (endian_ == Endian::Little) {
        buffer_.push_back(static_cast<u8>(v & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 8) & 0xFF));
    } else {
        buffer_.push_back(static_cast<u8>((v >> 8) & 0xFF));
        buffer_.push_back(static_cast<u8>(v & 0xFF));
    }
}

void BinaryWriter::WriteI16(i16 v) {
    WriteU16(static_cast<u16>(v));
}

void BinaryWriter::WriteU32(u32 v) {
    if (endian_ == Endian::Little) {
        buffer_.push_back(static_cast<u8>(v & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 8) & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 16) & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 24) & 0xFF));
    } else {
        buffer_.push_back(static_cast<u8>((v >> 24) & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 16) & 0xFF));
        buffer_.push_back(static_cast<u8>((v >> 8) & 0xFF));
        buffer_.push_back(static_cast<u8>(v & 0xFF));
    }
}

void BinaryWriter::WriteI32(i32 v) {
    WriteU32(static_cast<u32>(v));
}

void BinaryWriter::WriteU64(u64 v) {
    if (endian_ == Endian::Little) {
        for (int i = 0; i < 8; ++i) {
            buffer_.push_back(static_cast<u8>((v >> (i * 8)) & 0xFF));
        }
    } else {
        for (int i = 7; i >= 0; --i) {
            buffer_.push_back(static_cast<u8>((v >> (i * 8)) & 0xFF));
        }
    }
}

void BinaryWriter::WriteI64(i64 v) {
    WriteU64(static_cast<u64>(v));
}

void BinaryWriter::WriteF32(f32 v) {
    WriteU32(std::bit_cast<u32>(v));
}

void BinaryWriter::WriteF64(f64 v) {
    WriteU64(std::bit_cast<u64>(v));
}

void BinaryWriter::WriteFixedString(std::string_view s) {
    buffer_.insert(buffer_.end(), s.begin(), s.end());
}

void BinaryWriter::WriteCString(std::string_view s) {
    WriteFixedString(s);
    buffer_.push_back(0);
}

void BinaryWriter::WriteAlignedString(std::string_view s) {
    WriteI32(static_cast<i32>(s.size()));
    WriteFixedString(s);
    Align(4);
}

void BinaryWriter::WriteBytes(ByteSpan bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void BinaryWriter::WriteZeros(size_t count) {
    buffer_.insert(buffer_.end(), count, 0);
}

void BinaryWriter::Align(size_t alignment) {
    if (alignment <= 1) return;
    size_t mod = buffer_.size() % alignment;
    if (mod != 0) {
        WriteZeros(alignment - mod);
    }
}

void BinaryWriter::PatchU32(size_t offset, u32 v) {
    if (offset + 4 > buffer_.size()) return;
    if (endian_ == Endian::Little) {
        buffer_[offset + 0] = static_cast<u8>(v & 0xFF);
        buffer_[offset + 1] = static_cast<u8>((v >> 8) & 0xFF);
        buffer_[offset + 2] = static_cast<u8>((v >> 16) & 0xFF);
        buffer_[offset + 3] = static_cast<u8>((v >> 24) & 0xFF);
    } else {
        buffer_[offset + 0] = static_cast<u8>((v >> 24) & 0xFF);
        buffer_[offset + 1] = static_cast<u8>((v >> 16) & 0xFF);
        buffer_[offset + 2] = static_cast<u8>((v >> 8) & 0xFF);
        buffer_[offset + 3] = static_cast<u8>(v & 0xFF);
    }
}

void BinaryWriter::PatchI32(size_t offset, i32 v) {
    PatchU32(offset, static_cast<u32>(v));
}

void BinaryWriter::PatchU64(size_t offset, u64 v) {
    if (offset + 8 > buffer_.size()) return;
    if (endian_ == Endian::Little) {
        for (int i = 0; i < 8; ++i) {
            buffer_[offset + i] = static_cast<u8>((v >> (i * 8)) & 0xFF);
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            buffer_[offset + i] = static_cast<u8>((v >> ((7 - i) * 8)) & 0xFF);
        }
    }
}

void BinaryWriter::PatchI64(size_t offset, i64 v) {
    PatchU64(offset, static_cast<u64>(v));
}

} // namespace coconut
