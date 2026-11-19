#pragma once

#include "core/Types.h"

namespace coconut {

// Self-contained LZMA1 decoder compatible with UnityFS raw LZMA blocks
// (5-byte LZMA properties header [lc/lp/pb + 4-byte LE dictionary size] followed by range-coded stream).
class LzmaDecoder {
public:
    static Result<ByteBuffer> Decompress(ByteSpan inputWithProps, size_t expectedUncompressedSize);
};

} // namespace coconut
