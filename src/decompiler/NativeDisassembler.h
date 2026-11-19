#pragma once

#include "core/Types.h"
#include <string>
#include <vector>

namespace coconut::decompiler {

enum class BinaryArch {
    Unknown,
    X86_64,
    ARM64,
    ARM32
};

struct BinarySection {
    std::string name;
    u64 virtualAddress = 0;
    u64 fileOffset = 0;
    u64 size = 0;
    bool executable = false;
};

struct NativeInstruction {
    u64 address = 0;
    ByteBuffer rawBytes;
    std::string mnemonic;
    std::string operands;
};

struct NativeBinaryInfo {
    std::string format; // "PE64", "ELF64", "Mach-O 64"
    BinaryArch arch = BinaryArch::Unknown;
    std::vector<BinarySection> sections;
    ByteBuffer rawImage;

    const BinarySection* FindTextSection() const;
    bool RvaToOffset(u64 rva, size_t& outOffset) const;
};

class NativeDisassembler {
public:
    static std::string_view ArchName(BinaryArch arch);
    static Result<NativeBinaryInfo> ParseBinary(ByteSpan data);

    static std::vector<NativeInstruction> DisassembleARM64(ByteSpan code, u64 baseAddr, size_t maxInstructions = 128);
    static std::vector<NativeInstruction> DisassembleX86_64(ByteSpan code, u64 baseAddr, size_t maxInstructions = 128);

    static std::string FormatListing(const std::vector<NativeInstruction>& insns, std::string_view headerComment = {});
};

} // namespace coconut::decompiler
