#include "core/LzmaDecoder.h"
#include <array>
#include <cstring>

namespace coconut {

namespace {

constexpr u32 kNumStates = 12;
constexpr u32 kNumPosBitsMax = 4;
constexpr u32 kNumPosStatesMax = 1u << kNumPosBitsMax;
constexpr u32 kNumLenToPosStates = 4;
constexpr u32 kMatchMinLen = 2;
constexpr u32 kNumAlignBits = 4;
constexpr u32 kStartPosModelIndex = 4;
constexpr u32 kEndPosModelIndex = 14;
constexpr u32 kNumFullDistances = 1u << (kEndPosModelIndex >> 1);
constexpr u32 kNumBitModelTotalBits = 11;
constexpr u32 kBitModelTotal = 1u << kNumBitModelTotalBits;
constexpr u32 kNumMoveBits = 5;
constexpr u32 kTopValue = 1u << 24;

struct RangeDecoder {
    const u8* ptr = nullptr;
    const u8* end = nullptr;
    u32 range = 0xFFFFFFFFu;
    u32 code = 0;
    bool corrupted = false;

    bool Init(ByteSpan data) {
        if (data.size() < 5) return false;
        ptr = data.data();
        end = data.data() + data.size();
        if (*ptr++ != 0) return false; // Leading 0 byte in LZMA stream
        code = 0;
        for (int i = 0; i < 4; ++i) {
            code = (code << 8) | (*ptr++);
        }
        range = 0xFFFFFFFFu;
        return true;
    }

    void Normalize() {
        if (range < kTopValue) {
            range <<= 8;
            if (ptr < end) {
                code = (code << 8) | (*ptr++);
            } else {
                code <<= 8;
            }
        }
    }

    u32 DecodeBit(u16& prob) {
        Normalize();
        u32 bound = (range >> kNumBitModelTotalBits) * prob;
        if (code < bound) {
            range = bound;
            prob = static_cast<u16>(prob + ((kBitModelTotal - prob) >> kNumMoveBits));
            return 0;
        } else {
            range -= bound;
            code -= bound;
            prob = static_cast<u16>(prob - (prob >> kNumMoveBits));
            return 1;
        }
    }

    u32 DecodeDirectBits(int numBits) {
        u32 res = 0;
        for (int i = numBits; i > 0; --i) {
            Normalize();
            range >>= 1;
            u32 t = (code - range) >> 31;
            code -= range & (t - 1);
            res = (res << 1) | (1u - t);
        }
        return res;
    }
};

struct BitTreeDecoder {
    int numBits = 0;
    std::vector<u16> probs;

    void Init(int bits) {
        numBits = bits;
        probs.assign(static_cast<size_t>(1) << bits, kBitModelTotal / 2);
    }

    u32 Decode(RangeDecoder& rd) {
        u32 m = 1;
        for (int i = 0; i < numBits; ++i) {
            m = (m << 1) + rd.DecodeBit(probs[m]);
        }
        return m - (1u << numBits);
    }

    u32 ReverseDecode(RangeDecoder& rd) {
        u32 m = 1;
        u32 symbol = 0;
        for (int i = 0; i < numBits; ++i) {
            u32 bit = rd.DecodeBit(probs[m]);
            m = (m << 1) + bit;
            symbol |= (bit << i);
        }
        return symbol;
    }
};

u32 ReverseDecodeArray(u16* probs, int numBits, RangeDecoder& rd) {
    u32 m = 1;
    u32 symbol = 0;
    for (int i = 0; i < numBits; ++i) {
        u32 bit = rd.DecodeBit(probs[m]);
        m = (m << 1) + bit;
        symbol |= (bit << i);
    }
    return symbol;
}

struct LenDecoder {
    u16 choice = kBitModelTotal / 2;
    u16 choice2 = kBitModelTotal / 2;
    std::array<BitTreeDecoder, kNumPosStatesMax> lowCoder;
    std::array<BitTreeDecoder, kNumPosStatesMax> midCoder;
    BitTreeDecoder highCoder;

    void Init(u32 numPosStates) {
        choice = kBitModelTotal / 2;
        choice2 = kBitModelTotal / 2;
        for (u32 i = 0; i < numPosStates; ++i) {
            lowCoder[i].Init(3);
            midCoder[i].Init(3);
        }
        highCoder.Init(8);
    }

    u32 Decode(RangeDecoder& rd, u32 posState) {
        if (rd.DecodeBit(choice) == 0) {
            return lowCoder[posState].Decode(rd);
        }
        if (rd.DecodeBit(choice2) == 0) {
            return 8 + midCoder[posState].Decode(rd);
        }
        return 16 + highCoder.Decode(rd);
    }
};

} // namespace

Result<ByteBuffer> LzmaDecoder::Decompress(ByteSpan inputWithProps, size_t expectedUncompressedSize) {
    if (inputWithProps.size() < 10) {
        return Result<ByteBuffer>::Err("LZMA input too small (needs 5-byte properties + stream)");
    }

    u8 d = inputWithProps[0];
    if (d >= 9 * 5 * 5) {
        return Result<ByteBuffer>::Err("Invalid LZMA properties byte");
    }
    u32 lc = d % 9;
    d /= 9;
    u32 lp = d % 5;
    u32 pb = d / 5;

    u32 dictSize = static_cast<u32>(inputWithProps[1]) |
                   (static_cast<u32>(inputWithProps[2]) << 8) |
                   (static_cast<u32>(inputWithProps[3]) << 16) |
                   (static_cast<u32>(inputWithProps[4]) << 24);
    if (dictSize < 4096) dictSize = 4096;

    RangeDecoder rd;
    if (!rd.Init(inputWithProps.subspan(5))) {
        return Result<ByteBuffer>::Err("Failed to initialize LZMA range decoder");
    }

    const u32 numPosStates = 1u << pb;
    const u32 posStateMask = numPosStates - 1;
    const u32 literalPosMask = (1u << lp) - 1;

    std::vector<u16> isMatch(kNumStates << kNumPosBitsMax, kBitModelTotal / 2);
    std::array<u16, kNumStates> isRep{};
    std::array<u16, kNumStates> isRepG0{};
    std::array<u16, kNumStates> isRepG1{};
    std::array<u16, kNumStates> isRepG2{};
    isRep.fill(kBitModelTotal / 2);
    isRepG0.fill(kBitModelTotal / 2);
    isRepG1.fill(kBitModelTotal / 2);
    isRepG2.fill(kBitModelTotal / 2);

    std::vector<u16> isRep0Long(kNumStates << kNumPosBitsMax, kBitModelTotal / 2);

    std::array<BitTreeDecoder, kNumLenToPosStates> posSlotDecoder;
    for (auto& dec : posSlotDecoder) dec.Init(6);

    std::vector<u16> posDecoders(1 + kNumFullDistances - kEndPosModelIndex, kBitModelTotal / 2);
    BitTreeDecoder alignDecoder;
    alignDecoder.Init(kNumAlignBits);

    LenDecoder lenDecoder;
    LenDecoder repLenDecoder;
    lenDecoder.Init(numPosStates);
    repLenDecoder.Init(numPosStates);

    const size_t litCount = static_cast<size_t>(0x300) << (lc + lp);
    std::vector<u16> litProbs(litCount, kBitModelTotal / 2);

    ByteBuffer out;
    out.reserve(expectedUncompressedSize);

    u32 state = 0;
    u32 rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;

    while (out.size() < expectedUncompressedSize) {
        u32 posState = static_cast<u32>(out.size()) & posStateMask;
        u32 matchIdx = (state << kNumPosBitsMax) + posState;

        if (rd.DecodeBit(isMatch[matchIdx]) == 0) {
            // Literal
            u8 prevByte = out.empty() ? 0 : out.back();
            size_t litState = ((out.size() & literalPosMask) << lc) + (prevByte >> (8 - lc));
            u16* probs = &litProbs[litState * 0x300];

            u32 symbol = 1;
            if (state >= 7) {
                if (rep0 >= out.size()) {
                    return Result<ByteBuffer>::Err("LZMA match distance out of range during literal");
                }
                u32 matchByte = out[out.size() - 1 - rep0];
                do {
                    u32 matchBit = (matchByte >> 7) & 1u;
                    matchByte <<= 1;
                    u32 bit = rd.DecodeBit(probs[((1 + matchBit) << 8) + symbol]);
                    symbol = (symbol << 1) | bit;
                    if (matchBit != bit) break;
                } while (symbol < 0x100);
            }
            while (symbol < 0x100) {
                symbol = (symbol << 1) | rd.DecodeBit(probs[symbol]);
            }
            out.push_back(static_cast<u8>(symbol - 0x100));

            if (state < 4) state = 0;
            else if (state < 10) state -= 3;
            else state -= 6;
        } else {
            u32 len = 0;
            if (rd.DecodeBit(isRep[state]) == 1) {
                if (rd.DecodeBit(isRepG0[state]) == 0) {
                    if (rd.DecodeBit(isRep0Long[matchIdx]) == 0) {
                        state = state < 7 ? 9 : 11;
                        if (rep0 >= out.size()) {
                            return Result<ByteBuffer>::Err("LZMA short rep distance out of range");
                        }
                        out.push_back(out[out.size() - 1 - rep0]);
                        continue;
                    }
                } else {
                    u32 dist = 0;
                    if (rd.DecodeBit(isRepG1[state]) == 0) {
                        dist = rep1;
                    } else {
                        if (rd.DecodeBit(isRepG2[state]) == 0) {
                            dist = rep2;
                        } else {
                            dist = rep3;
                            rep3 = rep2;
                        }
                        rep2 = rep1;
                    }
                    rep1 = rep0;
                    rep0 = dist;
                }
                len = repLenDecoder.Decode(rd, posState) + kMatchMinLen;
                state = state < 7 ? 8 : 11;
            } else {
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                len = lenDecoder.Decode(rd, posState) + kMatchMinLen;
                state = state < 7 ? 7 : 10;

                u32 lenToPosState = (len - kMatchMinLen < kNumLenToPosStates)
                                        ? (len - kMatchMinLen)
                                        : (kNumLenToPosStates - 1);
                u32 posSlot = posSlotDecoder[lenToPosState].Decode(rd);
                if (posSlot < kStartPosModelIndex) {
                    rep0 = posSlot;
                } else {
                    u32 numDirectBits = (posSlot >> 1) - 1;
                    rep0 = (2u | (posSlot & 1u)) << numDirectBits;
                    if (posSlot < kEndPosModelIndex) {
                        rep0 += ReverseDecodeArray(
                            &posDecoders[rep0 - posSlot],
                            static_cast<int>(numDirectBits),
                            rd);
                    } else {
                        rep0 += rd.DecodeDirectBits(static_cast<int>(numDirectBits - kNumAlignBits)) << kNumAlignBits;
                        rep0 += alignDecoder.ReverseDecode(rd);
                        if (rep0 == 0xFFFFFFFFu) {
                            break; // End-of-stream marker
                        }
                    }
                }
            }

            if (rep0 >= out.size()) {
                return Result<ByteBuffer>::Err("LZMA match distance exceeds decoded output size");
            }
            for (u32 i = 0; i < len && out.size() < expectedUncompressedSize; ++i) {
                out.push_back(out[out.size() - 1 - rep0]);
            }
        }
    }

    return Result<ByteBuffer>::Ok(std::move(out));
}

} // namespace coconut
