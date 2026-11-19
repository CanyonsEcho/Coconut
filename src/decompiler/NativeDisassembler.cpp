#include "decompiler/NativeDisassembler.h"
#include "core/BinaryReader.h"
#include "core/Logger.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace coconut::decompiler {

namespace {

std::string RegX(u32 r, bool spIf31 = false) {
    if (r == 31) return spIf31 ? "sp" : "xzr";
    if (r == 29) return "fp";
    if (r == 30) return "lr";
    return "x" + std::to_string(r);
}

std::string RegW(u32 r, bool spIf31 = false) {
    if (r == 31) return spIf31 ? "wsp" : "wzr";
    return "w" + std::to_string(r);
}

std::string HexImm(i64 v) {
    std::ostringstream oss;
    if (v < 0) {
        oss << "-0x" << std::hex << (-v);
    } else {
        oss << "0x" << std::hex << v;
    }
    return oss.str();
}

std::string RegX64(u8 reg, bool rexB = false, bool is64 = true) {
    static const char* kR64[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15"};
    static const char* kR32[16] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi","r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"};
    int idx = (reg & 7) + (rexB ? 8 : 0);
    return is64 ? kR64[idx] : kR32[idx];
}

} // namespace

const BinarySection* NativeBinaryInfo::FindTextSection() const {
    for (const auto& s : sections) {
        if (s.name == ".text" || s.name == "__text" || s.executable) return &s;
    }
    return sections.empty() ? nullptr : &sections.front();
}

bool NativeBinaryInfo::RvaToOffset(u64 rva, size_t& outOffset) const {
    for (const auto& s : sections) {
        if (rva >= s.virtualAddress && rva < s.virtualAddress + s.size) {
            outOffset = static_cast<size_t>(s.fileOffset + (rva - s.virtualAddress));
            return outOffset < rawImage.size();
        }
    }
    return false;
}

std::string_view NativeDisassembler::ArchName(BinaryArch arch) {
    switch (arch) {
        case BinaryArch::X86_64: return "x86-64";
        case BinaryArch::ARM64:  return "ARM64 (AArch64)";
        case BinaryArch::ARM32:  return "ARMv7";
        default:                 return "Unknown";
    }
}

Result<NativeBinaryInfo> NativeDisassembler::ParseBinary(ByteSpan data) {
    if (data.size() < 64) {
        return Result<NativeBinaryInfo>::Err("Native binary too small");
    }

    NativeBinaryInfo info;
    info.rawImage.assign(data.begin(), data.end());

    // 1. ELF64 (Linux / Android libil2cpp.so)
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        bool is64 = (data[4] == 2);
        info.format = is64 ? "ELF64" : "ELF32";
        BinaryReader r(data, Endian::Little);
        r.Seek(18);
        u16 machine = r.ReadU16();
        if (machine == 183) info.arch = BinaryArch::ARM64;
        else if (machine == 62) info.arch = BinaryArch::X86_64;
        else if (machine == 40) info.arch = BinaryArch::ARM32;

        if (is64) {
            r.Seek(40);
            u64 shoff = r.ReadU64();
            r.Seek(58);
            u16 shentsize = r.ReadU16();
            u16 shnum = r.ReadU16();
            u16 shstrndx = r.ReadU16();

            u64 shstrOff = 0, shstrSize = 0;
            if (shoff > 0 && shstrndx < shnum && shoff + static_cast<u64>(shstrndx + 1) * shentsize <= data.size()) {
                r.Seek(static_cast<size_t>(shoff + static_cast<u64>(shstrndx) * shentsize + 24));
                shstrOff = r.ReadU64();
                shstrSize = r.ReadU64();
            }

            for (u16 i = 0; i < shnum; ++i) {
                size_t off = static_cast<size_t>(shoff + static_cast<u64>(i) * shentsize);
                if (off + 64 > data.size()) break;
                r.Seek(off);
                u32 nameIdx = r.ReadU32();
                u32 shType = r.ReadU32();
                u64 flags = r.ReadU64();
                u64 addr = r.ReadU64();
                u64 fileOff = r.ReadU64();
                u64 sz = r.ReadU64();
                if (shType == 0 || sz == 0) continue;

                BinarySection sec;
                if (shstrOff + nameIdx < data.size() && nameIdx < shstrSize) {
                    const char* p = reinterpret_cast<const char*>(data.data() + shstrOff + nameIdx);
                    size_t maxL = data.size() - static_cast<size_t>(shstrOff + nameIdx);
                    size_t l = 0;
                    while (l < maxL && p[l] != '\0') ++l;
                    sec.name.assign(p, l);
                }
                sec.virtualAddress = addr;
                sec.fileOffset = fileOff;
                sec.size = sz;
                sec.executable = (flags & 0x4ULL) != 0;
                info.sections.push_back(std::move(sec));
            }
        }
        return Result<NativeBinaryInfo>::Ok(std::move(info));
    }

    // 2. PE / PE64 (Windows GameAssembly.dll)
    if (data[0] == 'M' && data[1] == 'Z') {
        BinaryReader r(data, Endian::Little);
        r.Seek(0x3C);
        u32 peOff = r.ReadU32();
        if (peOff + 24 > data.size()) return Result<NativeBinaryInfo>::Err("Invalid PE header");
        r.Seek(peOff);
        if (r.ReadU32() != 0x00004550u) return Result<NativeBinaryInfo>::Err("Missing PE signature");
        u16 machine = r.ReadU16();
        u16 numSec = r.ReadU16();
        r.Skip(12);
        u16 optSize = r.ReadU16();
        r.Skip(2);
        info.format = (machine == 0x8664 || machine == 0xAA64) ? "PE64" : "PE32";
        if (machine == 0x8664) info.arch = BinaryArch::X86_64;
        else if (machine == 0xAA64) info.arch = BinaryArch::ARM64;

        r.Skip(optSize);
        for (u16 i = 0; i < numSec; ++i) {
            if (r.Remaining() < 40) break;
            BinarySection sec;
            std::string raw = r.ReadFixedString(8);
            size_t nul = raw.find('\0');
            sec.name = (nul == std::string::npos) ? raw : raw.substr(0, nul);
            u32 vsz = r.ReadU32();
            sec.virtualAddress = r.ReadU32();
            u32 rsz = r.ReadU32();
            sec.fileOffset = r.ReadU32();
            sec.size = std::max(vsz, rsz);
            r.Skip(12);
            u32 chars = r.ReadU32();
            sec.executable = (chars & 0x20000000u) != 0;
            info.sections.push_back(std::move(sec));
        }
        return Result<NativeBinaryInfo>::Ok(std::move(info));
    }

    // 3. Mach-O 64 (macOS GameAssembly.dylib)
    u32 magic = static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
                (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
    if (magic == 0xFEEDFACFu) {
        info.format = "Mach-O 64";
        BinaryReader r(data, Endian::Little);
        r.Skip(4);
        u32 cpuType = r.ReadU32();
        if (cpuType == 0x0100000Cu) info.arch = BinaryArch::ARM64;
        else if (cpuType == 0x01000007u) info.arch = BinaryArch::X86_64;
        r.Skip(8);
        u32 ncmds = r.ReadU32();
        r.Skip(12);
        for (u32 c = 0; c < ncmds && !r.HasError(); ++c) {
            size_t cmdStart = r.Position();
            u32 cmd = r.ReadU32();
            u32 cmdSize = r.ReadU32();
            if (cmd == 0x19 /*LC_SEGMENT_64*/ && cmdSize >= 72) {
                r.Skip(56);
                u32 nsects = r.ReadU32();
                r.Skip(4);
                for (u32 s = 0; s < nsects; ++s) {
                    std::string sname = r.ReadFixedString(16);
                    size_t nul = sname.find('\0');
                    if (nul != std::string::npos) sname.resize(nul);
                    r.Skip(16); // segname
                    u64 addr = r.ReadU64();
                    u64 sz = r.ReadU64();
                    u32 off = r.ReadU32();
                    r.Skip(28);
                    info.sections.push_back({sname, addr, off, sz, sname == "__text"});
                }
            }
            if (cmdSize == 0) break;
            r.Seek(cmdStart + cmdSize);
        }
        return Result<NativeBinaryInfo>::Ok(std::move(info));
    }

    return Result<NativeBinaryInfo>::Err("Unrecognized native binary format (expected ELF, PE, or Mach-O)");
}

std::vector<NativeInstruction> NativeDisassembler::DisassembleARM64(ByteSpan code, u64 baseAddr, size_t maxInstructions) {
    std::vector<NativeInstruction> out;
    size_t count = std::min(code.size() / 4, maxInstructions);
    out.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        size_t off = i * 4;
        u32 w = static_cast<u32>(code[off]) |
                (static_cast<u32>(code[off + 1]) << 8) |
                (static_cast<u32>(code[off + 2]) << 16) |
                (static_cast<u32>(code[off + 3]) << 24);

        NativeInstruction ins;
        ins.address = baseAddr + off;
        ins.rawBytes.assign(code.begin() + static_cast<std::ptrdiff_t>(off),
                            code.begin() + static_cast<std::ptrdiff_t>(off + 4));

        if (w == 0xD65F03C0u) {
            ins.mnemonic = "ret";
        } else if (w == 0xD503201Fu) {
            ins.mnemonic = "nop";
        } else if ((w & 0xFC000000u) == 0x94000000u) { // bl
            i32 imm26 = static_cast<i32>(w & 0x03FFFFFFu);
            if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF;
            ins.mnemonic = "bl";
            ins.operands = HexImm(static_cast<i64>(ins.address) + static_cast<i64>(imm26) * 4);
        } else if ((w & 0xFC000000u) == 0x14000000u) { // b
            i32 imm26 = static_cast<i32>(w & 0x03FFFFFFu);
            if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF;
            ins.mnemonic = "b";
            ins.operands = HexImm(static_cast<i64>(ins.address) + static_cast<i64>(imm26) * 4);
        } else if ((w & 0xFFE00000u) == 0xA9800000u || (w & 0xFFE00000u) == 0xA9A00000u) { // stp pre-index
            u32 rt = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 rt2 = (w >> 10) & 31u;
            i32 imm7 = static_cast<i32>((w >> 15) & 0x7Fu);
            if (imm7 & 0x40) imm7 |= ~0x7F;
            ins.mnemonic = "stp";
            ins.operands = RegX(rt) + ", " + RegX(rt2) + ", [" + RegX(rn, true) + ", #" + std::to_string(imm7 * 8) + "]!";
        } else if ((w & 0xFFC00000u) == 0xA8C00000u) { // ldp post-index
            u32 rt = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 rt2 = (w >> 10) & 31u;
            i32 imm7 = static_cast<i32>((w >> 15) & 0x7Fu);
            if (imm7 & 0x40) imm7 |= ~0x7F;
            ins.mnemonic = "ldp";
            ins.operands = RegX(rt) + ", " + RegX(rt2) + ", [" + RegX(rn, true) + "], #" + std::to_string(imm7 * 8);
        } else if ((w & 0x7F800000u) == 0x11000000u) { // add imm (incl mov fp, sp)
            bool is64 = (w >> 31) != 0;
            u32 rd = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 imm12 = (w >> 10) & 0xFFFu;
            if (imm12 == 0 && (rd == 31 || rn == 31)) {
                ins.mnemonic = "mov";
                ins.operands = (is64 ? RegX(rd, true) : RegW(rd, true)) + ", " + (is64 ? RegX(rn, true) : RegW(rn, true));
            } else {
                ins.mnemonic = "add";
                ins.operands = (is64 ? RegX(rd, true) : RegW(rd, true)) + ", " +
                               (is64 ? RegX(rn, true) : RegW(rn, true)) + ", #" + HexImm(imm12);
            }
        } else if ((w & 0x7F800000u) == 0x52800000u) { // movz
            bool is64 = (w >> 31) != 0;
            u32 rd = w & 31u;
            u32 imm16 = (w >> 5) & 0xFFFFu;
            ins.mnemonic = "mov";
            ins.operands = (is64 ? RegX(rd) : RegW(rd)) + ", #" + std::to_string(imm16);
        } else if ((w & 0xBFC00000u) == 0xB9400000u) { // ldr (unsigned offset)
            bool is64 = (w >> 30) == 3;
            u32 rt = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 imm = ((w >> 10) & 0xFFFu) * (is64 ? 8u : 4u);
            ins.mnemonic = "ldr";
            ins.operands = (is64 ? RegX(rt) : RegW(rt)) + ", [" + RegX(rn, true) + ", #" + HexImm(imm) + "]";
        } else if ((w & 0xBFC00000u) == 0xB9000000u) { // str (unsigned offset)
            bool is64 = (w >> 30) == 3;
            u32 rt = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 imm = ((w >> 10) & 0xFFFu) * (is64 ? 8u : 4u);
            ins.mnemonic = "str";
            ins.operands = (is64 ? RegX(rt) : RegW(rt)) + ", [" + RegX(rn, true) + ", #" + HexImm(imm) + "]";
        } else if ((w & 0x7FE0FC00u) == 0x4B000000u || (w & 0x7FE0FC00u) == 0x0B000000u) { // sub/add reg
            bool is64 = (w >> 31) != 0;
            bool isSub = (w & 0x40000000u) != 0;
            u32 rd = w & 31u;
            u32 rn = (w >> 5) & 31u;
            u32 rm = (w >> 16) & 31u;
            ins.mnemonic = isSub ? "sub" : "add";
            ins.operands = (is64 ? RegX(rd) : RegW(rd)) + ", " +
                           (is64 ? RegX(rn) : RegW(rn)) + ", " + (is64 ? RegX(rm) : RegW(rm));
        } else {
            ins.mnemonic = ".word";
            ins.operands = HexImm(w);
        }

        out.push_back(std::move(ins));
    }
    return out;
}

std::vector<NativeInstruction> NativeDisassembler::DisassembleX86_64(ByteSpan code, u64 baseAddr, size_t maxInstructions) {
    std::vector<NativeInstruction> out;
    size_t pos = 0;

    while (pos < code.size() && out.size() < maxInstructions) {
        size_t start = pos;
        bool rexW = false, rexR = false, rexB = false;
        if ((code[pos] & 0xF0u) == 0x40u && pos + 1 < code.size()) {
            u8 rex = code[pos++];
            rexW = (rex & 0x08u) != 0;
            rexR = (rex & 0x04u) != 0;
            rexB = (rex & 0x01u) != 0;
        }
        u8 op = code[pos++];
        NativeInstruction ins;
        ins.address = baseAddr + start;

        if (op == 0x90) {
            ins.mnemonic = "nop";
        } else if (op == 0xC3) {
            ins.mnemonic = "ret";
        } else if (op == 0xCC) {
            ins.mnemonic = "int3";
        } else if (op >= 0x50 && op <= 0x57) {
            ins.mnemonic = "push";
            ins.operands = RegX64(op - 0x50, rexB, true);
        } else if (op >= 0x58 && op <= 0x5F) {
            ins.mnemonic = "pop";
            ins.operands = RegX64(op - 0x58, rexB, true);
        } else if (op == 0x89 && pos < code.size()) {
            u8 modrm = code[pos++];
            u8 reg = (modrm >> 3) & 7;
            u8 rm = modrm & 7;
            ins.mnemonic = "mov";
            ins.operands = RegX64(rm, rexB, rexW) + ", " + RegX64(reg, rexR, rexW);
        } else if (op == 0x8B && pos < code.size()) {
            u8 modrm = code[pos++];
            u8 reg = (modrm >> 3) & 7;
            u8 rm = modrm & 7;
            ins.mnemonic = "mov";
            ins.operands = RegX64(reg, rexR, rexW) + ", " + RegX64(rm, rexB, rexW);
        } else if (op == 0x83 && pos + 1 < code.size()) {
            u8 modrm = code[pos++];
            i8 imm = static_cast<i8>(code[pos++]);
            u8 ext = (modrm >> 3) & 7;
            u8 rm = modrm & 7;
            ins.mnemonic = (ext == 5) ? "sub" : (ext == 0 ? "add" : "alu");
            ins.operands = RegX64(rm, rexB, rexW) + ", " + HexImm(imm);
        } else if (op == 0xE8 && pos + 4 <= code.size()) {
            i32 rel = 0;
            std::memcpy(&rel, code.data() + pos, 4);
            pos += 4;
            ins.mnemonic = "call";
            ins.operands = HexImm(static_cast<i64>(baseAddr + pos) + rel);
        } else if (op == 0xE9 && pos + 4 <= code.size()) {
            i32 rel = 0;
            std::memcpy(&rel, code.data() + pos, 4);
            pos += 4;
            ins.mnemonic = "jmp";
            ins.operands = HexImm(static_cast<i64>(baseAddr + pos) + rel);
        } else if (op >= 0xB8 && op <= 0xBF && pos + 4 <= code.size()) {
            u32 imm = 0;
            std::memcpy(&imm, code.data() + pos, 4);
            pos += 4;
            ins.mnemonic = "mov";
            ins.operands = RegX64(op - 0xB8, rexB, false) + ", " + HexImm(imm);
        } else {
            ins.mnemonic = "db";
            ins.operands = HexImm(op);
        }

        ins.rawBytes.assign(code.begin() + static_cast<std::ptrdiff_t>(start),
                            code.begin() + static_cast<std::ptrdiff_t>(pos));
        out.push_back(std::move(ins));
    }
    return out;
}

std::string NativeDisassembler::FormatListing(const std::vector<NativeInstruction>& insns, std::string_view headerComment) {
    std::ostringstream oss;
    if (!headerComment.empty()) {
        oss << "; " << headerComment << "\n";
    }
    for (const auto& ins : insns) {
        oss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << ins.address << " | ";
        std::ostringstream hex;
        for (size_t i = 0; i < ins.rawBytes.size(); ++i) {
            if (i > 0) hex << ' ';
            hex << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(ins.rawBytes[i]);
        }
        oss << std::left << std::setw(14) << std::setfill(' ') << hex.str() << " | " << ins.mnemonic;
        if (!ins.operands.empty()) oss << " " << ins.operands;
        oss << "\n";
    }
    return oss.str();
}

} // namespace coconut::decompiler
