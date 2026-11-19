#include "core/BinaryReader.h"
#include <bit>
#include <algorithm>

namespace coconut {

BinaryReader::BinaryReader(ByteSpan data, Endian endian)
    : data_(data), pos_(0), endian_(endian) {}

BinaryReader::BinaryReader(const ByteBuffer& data, Endian endian)
    : data_(data.data(), data.size()), pos_(0), endian_(endian) {}

bool BinaryReader::Ensure(size_t count) {
    if (count > Remaining()) {
        error_ = true;
        return false;
    }
    return true;
}

bool BinaryReader::Seek(size_t offset) {
    if (offset > data_.size()) {
        error_ = true;
        return false;
    }
    pos_ = offset;
    return true;
}

bool BinaryReader::Skip(size_t count) {
    if (!Ensure(count)) return false;
    pos_ += count;
    return true;
}

void BinaryReader::Align(size_t alignment) {
    if (alignment <= 1) return;
    size_t mod = pos_ % alignment;
    if (mod != 0) {
        size_t pad = alignment - mod;
        if (pad <= Remaining()) {
            pos_ += pad;
        } else {
            pos_ = data_.size();
        }
    }
}

u8 BinaryReader::ReadU8() {
    if (!Ensure(1)) return 0;
    return data_[pos_++];
}

i8 BinaryReader::ReadI8() {
    return static_cast<i8>(ReadU8());
}

bool BinaryReader::ReadBool() {
    return ReadU8() != 0;
}

u16 BinaryReader::ReadU16() {
    if (!Ensure(2)) return 0;
    u16 b0 = data_[pos_];
    u16 b1 = data_[pos_ + 1];
    pos_ += 2;
    if (endian_ == Endian::Little) {
        return static_cast<u16>(b0 | (b1 << 8));
    }
    return static_cast<u16>((b0 << 8) | b1);
}

i16 BinaryReader::ReadI16() {
    return static_cast<i16>(ReadU16());
}

u32 BinaryReader::ReadU32() {
    if (!Ensure(4)) return 0;
    u32 b0 = data_[pos_];
    u32 b1 = data_[pos_ + 1];
    u32 b2 = data_[pos_ + 2];
    u32 b3 = data_[pos_ + 3];
    pos_ += 4;
    if (endian_ == Endian::Little) {
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

i32 BinaryReader::ReadI32() {
    return static_cast<i32>(ReadU32());
}

u64 BinaryReader::ReadU64() {
    if (!Ensure(8)) return 0;
    u64 v = 0;
    if (endian_ == Endian::Little) {
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<u64>(data_[pos_ + i]) << (i * 8);
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<u64>(data_[pos_ + i]);
        }
    }
    pos_ += 8;
    return v;
}

i64 BinaryReader::ReadI64() {
    return static_cast<i64>(ReadU64());
}

f32 BinaryReader::ReadF32() {
    u32 raw = ReadU32();
    return std::bit_cast<f32>(raw);
}

f64 BinaryReader::ReadF64() {
    u64 raw = ReadU64();
    return std::bit_cast<f64>(raw);
}

std::string BinaryReader::ReadFixedString(size_t length) {
    if (!Ensure(length)) return {};
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), length);
    pos_ += length;
    return s;
}

std::string BinaryReader::ReadCString(size_t maxLen) {
    std::string result;
    size_t limit = std::min(Remaining(), maxLen);
    for (size_t i = 0; i < limit; ++i) {
        u8 c = data_[pos_++];
        if (c == 0) {
            return result;
        }
        result.push_back(static_cast<char>(c));
    }
    return result;
}

std::string BinaryReader::ReadAlignedString() {
    i32 len = ReadI32();
    if (len < 0 || static_cast<size_t>(len) > Remaining()) {
        error_ = true;
        return {};
    }
    std::string s = ReadFixedString(static_cast<size_t>(len));
    Align(4);
    return s;
}

ByteBuffer BinaryReader::ReadBytes(size_t count) {
    if (!Ensure(count)) return {};
    ByteBuffer out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                   data_.begin() + static_cast<std::ptrdiff_t>(pos_ + count));
    pos_ += count;
    return out;
}

ByteSpan BinaryReader::ReadSpan(size_t count) {
    if (!Ensure(count)) return {};
    ByteSpan sp = data_.subspan(pos_, count);
    pos_ += count;
    return sp;
}

BinaryReader BinaryReader::Slice(size_t offset, size_t count) const {
    if (offset >= data_.size()) {
        return BinaryReader(ByteSpan{}, endian_);
    }
    size_t actual = std::min(count, data_.size() - offset);
    return BinaryReader(data_.subspan(offset, actual), endian_);
}

} // namespace coconut
