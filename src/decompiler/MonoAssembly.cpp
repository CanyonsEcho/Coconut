#include "decompiler/MonoAssembly.h"
#include "core/BinaryReader.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace coconut::decompiler {

namespace {

struct PeSection {
    std::string name;
    u32 virtualSize = 0;
    u32 virtualAddress = 0;
    u32 rawSize = 0;
    u32 rawPointer = 0;
};

bool RvaToFileOffset(u32 rva, const std::vector<PeSection>& sections, size_t fileSize, size_t& outOff) {
    for (const auto& s : sections) {
        u32 span = std::max(s.virtualSize, s.rawSize);
        if (rva >= s.virtualAddress && rva < s.virtualAddress + span) {
            size_t off = s.rawPointer + (rva - s.virtualAddress);
            if (off < fileSize) {
                outOff = off;
                return true;
            }
        }
    }
    return false;
}

u32 ReadCompressedUInt(ByteSpan blob, size_t& pos) {
    if (pos >= blob.size()) return 0;
    u8 b0 = blob[pos++];
    if ((b0 & 0x80u) == 0) {
        return b0;
    }
    if ((b0 & 0xC0u) == 0x80u && pos < blob.size()) {
        u8 b1 = blob[pos++];
        return ((b0 & 0x3Fu) << 8) | b1;
    }
    if ((b0 & 0xE0u) == 0xC0u && pos + 2 < blob.size()) {
        u8 b1 = blob[pos++];
        u8 b2 = blob[pos++];
        u8 b3 = blob[pos++];
        return ((b0 & 0x1Fu) << 24) | (static_cast<u32>(b1) << 16) | (static_cast<u32>(b2) << 8) | b3;
    }
    return 0;
}

std::string ReadHeapString(ByteSpan heap, u32 offset) {
    if (offset >= heap.size()) return {};
    size_t len = 0;
    while (offset + len < heap.size() && heap[offset + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(heap.data() + offset), len);
}

ByteSpan ReadHeapBlob(ByteSpan heap, u32 offset) {
    if (offset >= heap.size()) return {};
    size_t pos = offset;
    u32 len = ReadCompressedUInt(heap, pos);
    if (pos + len > heap.size()) return {};
    return heap.subspan(pos, len);
}

std::string DecodeElementType(
    ByteSpan sig,
    size_t& pos,
    const std::vector<MonoTypeRefInfo>& typeRefs,
    const std::vector<std::string>& typeDefNames) {
    if (pos >= sig.size()) return "object";
    u8 elem = sig[pos++];
    switch (elem) {
        case 0x01: return "void";
        case 0x02: return "bool";
        case 0x03: return "char";
        case 0x04: return "sbyte";
        case 0x05: return "byte";
        case 0x06: return "short";
        case 0x07: return "ushort";
        case 0x08: return "int";
        case 0x09: return "uint";
        case 0x0A: return "long";
        case 0x0B: return "ulong";
        case 0x0C: return "float";
        case 0x0D: return "double";
        case 0x0E: return "string";
        case 0x0F: return DecodeElementType(sig, pos, typeRefs, typeDefNames) + "*";
        case 0x10: return "ref " + DecodeElementType(sig, pos, typeRefs, typeDefNames);
        case 0x11: // VALUETYPE
        case 0x12: // CLASS
        {
            u32 coded = ReadCompressedUInt(sig, pos);
            u32 tag = coded & 3u;
            u32 rid = coded >> 2;
            if (tag == 0 && rid >= 1 && rid <= typeDefNames.size()) return typeDefNames[rid - 1];
            if (tag == 1 && rid >= 1 && rid <= typeRefs.size()) return typeRefs[rid - 1].name;
            return "Type_" + std::to_string(rid);
        }
        case 0x13: { u32 idx = ReadCompressedUInt(sig, pos); return "T" + std::to_string(idx); }
        case 0x15: { // GENERICINST
            std::string base = DecodeElementType(sig, pos, typeRefs, typeDefNames);
            u32 argCount = ReadCompressedUInt(sig, pos);
            std::string out = base + "<";
            for (u32 i = 0; i < argCount; ++i) {
                if (i > 0) out += ", ";
                out += DecodeElementType(sig, pos, typeRefs, typeDefNames);
            }
            out += ">";
            return out;
        }
        case 0x18: return "IntPtr";
        case 0x19: return "UIntPtr";
        case 0x1C: return "object";
        case 0x1D: return DecodeElementType(sig, pos, typeRefs, typeDefNames) + "[]";
        case 0x1E: { u32 idx = ReadCompressedUInt(sig, pos); return "TM" + std::to_string(idx); }
        case 0x1F:
        case 0x20: {
            ReadCompressedUInt(sig, pos);
            return DecodeElementType(sig, pos, typeRefs, typeDefNames);
        }
        default: return "object";
    }
}

std::string VisibilityFromFieldFlags(u16 flags) {
    switch (flags & 0x07u) {
        case 1: return "private";
        case 3: return "internal";
        case 4: return "protected";
        case 5: return "protected internal";
        case 6: return "public";
        default: return "private";
    }
}

std::string VisibilityFromMethodFlags(u16 flags) {
    switch (flags & 0x07u) {
        case 1: return "private";
        case 3: return "internal";
        case 4: return "protected";
        case 5: return "protected internal";
        case 6: return "public";
        default: return "private";
    }
}

} // namespace

std::string MonoAssembly::ResolveToken(u32 token) const {
    u32 table = (token >> 24) & 0xFFu;
    u32 rid = token & 0x00FFFFFFu;
    if (rid == 0) return "null";

    if (table == 0x70) {
        return "\"" + ResolveUserString(token) + "\"";
    }
    if (table == 0x01 && rid <= typeRefs.size()) {
        return typeRefs[rid - 1].FullName();
    }
    if (table == 0x02 && rid <= types.size()) {
        return types[rid - 1].FullName();
    }
    if (table == 0x0A && rid <= memberRefs.size()) {
        const auto& mr = memberRefs[rid - 1];
        return mr.ownerName.empty() ? mr.name : (mr.ownerName + "." + mr.name);
    }
    if (table == 0x04) {
        for (const auto& t : types) {
            for (const auto& f : t.fields) {
                if (f.token == token) return t.name + "." + f.name;
            }
        }
    }
    if (table == 0x06) {
        for (const auto& t : types) {
            for (const auto& m : t.methods) {
                if (m.token == token) return t.name + "." + m.name;
            }
        }
    }

    std::ostringstream oss;
    oss << "token_0x" << std::hex << token;
    return oss.str();
}

std::string MonoAssembly::ResolveUserString(u32 token) const {
    for (const auto& [tok, val] : userStrings) {
        if (tok == token) return val;
    }
    return {};
}

Result<MonoAssembly> MonoAssembly::Parse(ByteSpan peBytes, std::string fileName) {
    if (peBytes.size() < 0x80 || peBytes[0] != 'M' || peBytes[1] != 'Z') {
        return Result<MonoAssembly>::Err("Not a valid PE/MZ assembly");
    }

    BinaryReader r(peBytes, Endian::Little);
    r.Seek(0x3C);
    u32 peOffset = r.ReadU32();
    if (peOffset + 24 > peBytes.size()) {
        return Result<MonoAssembly>::Err("Invalid PE header offset");
    }

    r.Seek(peOffset);
    if (r.ReadU32() != 0x00004550u) { // "PE\0\0"
        return Result<MonoAssembly>::Err("Missing PE signature");
    }

    r.Skip(2); // Machine
    u16 numSections = r.ReadU16();
    r.Skip(12);
    u16 optHeaderSize = r.ReadU16();
    r.Skip(2); // Characteristics

    size_t optStart = r.Position();
    u16 optMagic = r.ReadU16();
    bool isPe64 = (optMagic == 0x020B);
    size_t dataDirOffset = optStart + (isPe64 ? 112 : 96);

    r.Seek(dataDirOffset + 14 * 8); // Directory #14 = CLR Runtime Header
    u32 cliRva = r.ReadU32();
    u32 cliSize = r.ReadU32();
    if (cliRva == 0 || cliSize < 48) {
        return Result<MonoAssembly>::Err("PE binary does not contain a .NET CLI/COR20 header");
    }

    // Parse Section Headers
    r.Seek(optStart + optHeaderSize);
    std::vector<PeSection> sections;
    sections.reserve(numSections);
    for (u16 i = 0; i < numSections; ++i) {
        PeSection s;
        std::string rawName = r.ReadFixedString(8);
        size_t nul = rawName.find('\0');
        s.name = (nul == std::string::npos) ? rawName : rawName.substr(0, nul);
        s.virtualSize = r.ReadU32();
        s.virtualAddress = r.ReadU32();
        s.rawSize = r.ReadU32();
        s.rawPointer = r.ReadU32();
        r.Skip(16);
        sections.push_back(std::move(s));
    }

    size_t cliOff = 0;
    if (!RvaToFileOffset(cliRva, sections, peBytes.size(), cliOff)) {
        return Result<MonoAssembly>::Err("Cannot map CLI header RVA to file offset");
    }

    r.Seek(cliOff);
    r.Skip(8); // cb, major, minor
    u32 metaRva = r.ReadU32();
    u32 metaSize = r.ReadU32();

    size_t metaOff = 0;
    if (!RvaToFileOffset(metaRva, sections, peBytes.size(), metaOff) || metaOff + metaSize > peBytes.size()) {
        return Result<MonoAssembly>::Err("Cannot map CLI Metadata Root RVA");
    }

    BinaryReader mr(peBytes.subspan(metaOff, metaSize), Endian::Little);
    if (mr.ReadU32() != 0x424A5342u) { // "BSJB"
        return Result<MonoAssembly>::Err("Invalid CLI metadata signature (expected BSJB)");
    }

    MonoAssembly asmOut;
    asmOut.fileName = std::move(fileName);

    mr.Skip(8); // major, minor, reserved
    u32 verLen = mr.ReadU32();
    std::string rawVer = mr.ReadFixedString(verLen);
    size_t z = rawVer.find('\0');
    asmOut.runtimeVersion = (z == std::string::npos) ? rawVer : rawVer.substr(0, z);
    mr.Skip(2); // flags
    u16 streamCount = mr.ReadU16();

    ByteSpan tildeSpan, stringsSpan, usSpan, blobSpan;
    for (u16 i = 0; i < streamCount; ++i) {
        u32 sOff = mr.ReadU32();
        u32 sSize = mr.ReadU32();
        std::string sName = mr.ReadCString(64);
        mr.Align(4);
        if (sOff + sSize <= metaSize) {
            ByteSpan sp = peBytes.subspan(metaOff + sOff, sSize);
            if (sName == "#~" || sName == "#-") tildeSpan = sp;
            else if (sName == "#Strings") stringsSpan = sp;
            else if (sName == "#US") usSpan = sp;
            else if (sName == "#Blob") blobSpan = sp;
        }
    }

    if (tildeSpan.empty()) {
        return Result<MonoAssembly>::Err("CLI metadata missing #~ tables stream");
    }

    // Parse #US user string heap
    {
        size_t pos = 1;
        while (pos < usSpan.size()) {
            u32 tok = 0x70000000u | static_cast<u32>(pos);
            u32 len = ReadCompressedUInt(usSpan, pos);
            if (len == 0 || pos + len > usSpan.size()) break;
            std::string ascii;
            size_t charBytes = len > 0 ? len - 1 : 0;
            for (size_t i = 0; i + 1 < charBytes; i += 2) {
                u16 ch = static_cast<u16>(usSpan[pos + i] | (usSpan[pos + i + 1] << 8));
                ascii.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
            }
            asmOut.userStrings.emplace_back(tok, std::move(ascii));
            pos += len;
        }
    }

    // Parse #~ tables header
    BinaryReader tr(tildeSpan, Endian::Little);
    tr.Skip(4); // Reserved
    tr.Skip(2); // Major, Minor
    u8 heapSizes = tr.ReadU8();
    tr.Skip(1); // Reserved
    u64 validMask = tr.ReadU64();
    tr.Skip(8); // Sorted

    bool str4  = (heapSizes & 0x01u) != 0;
    bool guid4 = (heapSizes & 0x02u) != 0;
    bool blob4 = (heapSizes & 0x04u) != 0;

    std::array<u32, 64> rowCounts{};
    for (int t = 0; t < 64; ++t) {
        if ((validMask & (1ULL << t)) != 0) {
            rowCounts[static_cast<size_t>(t)] = tr.ReadU32();
        }
    }

    auto readStrIdx  = [&]() -> u32 { return str4  ? tr.ReadU32() : tr.ReadU16(); };
    auto readGuidIdx = [&]() -> u32 { return guid4 ? tr.ReadU32() : tr.ReadU16(); };
    auto readBlobIdx = [&]() -> u32 { return blob4 ? tr.ReadU32() : tr.ReadU16(); };
    auto readTableIdx = [&](int tableId) -> u32 {
        return rowCounts[static_cast<size_t>(tableId)] >= 65536 ? tr.ReadU32() : tr.ReadU16();
    };
    auto codedSize4 = [&](int tagBits, std::initializer_list<int> tables) -> bool {
        u32 limit = 1u << (16 - tagBits);
        for (int tid : tables) {
            if (rowCounts[static_cast<size_t>(tid)] >= limit) return true;
        }
        return false;
    };
    auto readCoded = [&](int tagBits, std::initializer_list<int> tables) -> u32 {
        return codedSize4(tagBits, tables) ? tr.ReadU32() : tr.ReadU16();
    };

    struct RawTypeDef {
        u32 flags; u32 nameIdx; u32 nsIdx; u32 extendsCoded; u32 fieldList; u32 methodList;
    };
    struct RawField {
        u16 flags; u32 nameIdx; u32 sigIdx;
    };
    struct RawMethod {
        u32 rva; u16 implFlags; u16 flags; u32 nameIdx; u32 sigIdx; u32 paramList;
    };
    struct RawParam {
        u16 flags; u16 seq; u32 nameIdx;
    };
    struct RawMemberRef {
        u32 classCoded; u32 nameIdx; u32 sigIdx;
    };

    std::vector<RawTypeDef> rawTypes;
    std::vector<RawField> rawFields;
    std::vector<RawMethod> rawMethods;
    std::vector<RawParam> rawParams;
    std::vector<RawMemberRef> rawMemberRefs;

    // Walk ECMA-335 tables in order 0x00 .. 0x2C
    for (int t = 0; t < 45; ++t) {
        u32 count = rowCounts[static_cast<size_t>(t)];
        if (count == 0) continue;

        for (u32 i = 0; i < count; ++i) {
            switch (t) {
                case 0x00: // Module
                    tr.ReadU16();
                    readStrIdx();
                    readGuidIdx(); readGuidIdx(); readGuidIdx();
                    break;
                case 0x01: { // TypeRef
                    readCoded(2, {0x00, 0x1A, 0x23, 0x01});
                    u32 n = readStrIdx();
                    u32 ns = readStrIdx();
                    asmOut.typeRefs.push_back({0x01000000u | (i + 1), ReadHeapString(stringsSpan, ns), ReadHeapString(stringsSpan, n)});
                    break;
                }
                case 0x02: { // TypeDef
                    RawTypeDef td{};
                    td.flags = tr.ReadU32();
                    td.nameIdx = readStrIdx();
                    td.nsIdx = readStrIdx();
                    td.extendsCoded = readCoded(2, {0x02, 0x01, 0x1B});
                    td.fieldList = readTableIdx(0x04);
                    td.methodList = readTableIdx(0x06);
                    rawTypes.push_back(td);
                    break;
                }
                case 0x04: { // Field
                    RawField f{};
                    f.flags = tr.ReadU16();
                    f.nameIdx = readStrIdx();
                    f.sigIdx = readBlobIdx();
                    rawFields.push_back(f);
                    break;
                }
                case 0x06: { // MethodDef
                    RawMethod m{};
                    m.rva = tr.ReadU32();
                    m.implFlags = tr.ReadU16();
                    m.flags = tr.ReadU16();
                    m.nameIdx = readStrIdx();
                    m.sigIdx = readBlobIdx();
                    m.paramList = readTableIdx(0x08);
                    rawMethods.push_back(m);
                    break;
                }
                case 0x08: { // Param
                    RawParam p{};
                    p.flags = tr.ReadU16();
                    p.seq = tr.ReadU16();
                    p.nameIdx = readStrIdx();
                    rawParams.push_back(p);
                    break;
                }
                case 0x09: // InterfaceImpl
                    readTableIdx(0x02);
                    readCoded(2, {0x02, 0x01, 0x1B});
                    break;
                case 0x0A: { // MemberRef
                    RawMemberRef mref{};
                    mref.classCoded = readCoded(3, {0x02, 0x01, 0x1A, 0x06, 0x1B});
                    mref.nameIdx = readStrIdx();
                    mref.sigIdx = readBlobIdx();
                    rawMemberRefs.push_back(mref);
                    break;
                }
                case 0x0B: // Constant
                    tr.ReadU16();
                    readCoded(2, {0x04, 0x08, 0x17});
                    readBlobIdx();
                    break;
                case 0x0C: // CustomAttribute
                    readCoded(5, {0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C});
                    readCoded(3, {0x06, 0x0A});
                    readBlobIdx();
                    break;
                case 0x0D: // FieldMarshal
                    readCoded(1, {0x04, 0x08}); readBlobIdx(); break;
                case 0x0E: // DeclSecurity
                    tr.ReadU16(); readCoded(2, {0x02, 0x06, 0x20}); readBlobIdx(); break;
                case 0x0F: // ClassLayout
                    tr.ReadU16(); tr.ReadU32(); readTableIdx(0x02); break;
                case 0x10: // FieldLayout
                    tr.ReadU32(); readTableIdx(0x04); break;
                case 0x11: // StandAloneSig
                    readBlobIdx(); break;
                case 0x12: // EventMap
                    readTableIdx(0x02); readTableIdx(0x14); break;
                case 0x14: // Event
                    tr.ReadU16(); readStrIdx(); readCoded(2, {0x02, 0x01, 0x1B}); break;
                case 0x15: // PropertyMap
                    readTableIdx(0x02); readTableIdx(0x17); break;
                case 0x17: // Property
                    tr.ReadU16(); readStrIdx(); readBlobIdx(); break;
                case 0x18: // MethodSemantics
                    tr.ReadU16(); readTableIdx(0x06); readCoded(1, {0x14, 0x17}); break;
                case 0x19: // MethodImpl
                    readTableIdx(0x02); readCoded(1, {0x06, 0x0A}); readCoded(1, {0x06, 0x0A}); break;
                case 0x1A: // ModuleRef
                    readStrIdx(); break;
                case 0x1B: // TypeSpec
                    readBlobIdx(); break;
                case 0x1C: // ImplMap
                    tr.ReadU16(); readCoded(1, {0x04, 0x06}); readStrIdx(); readTableIdx(0x1A); break;
                case 0x1D: // FieldRVA
                    tr.ReadU32(); readTableIdx(0x04); break;
                case 0x20: { // Assembly
                    tr.ReadU32(); tr.Skip(8); tr.ReadU32();
                    readBlobIdx();
                    u32 nameIdx = readStrIdx();
                    readStrIdx();
                    asmOut.assemblyName = ReadHeapString(stringsSpan, nameIdx);
                    break;
                }
                case 0x23: { // AssemblyRef
                    tr.Skip(8); tr.ReadU32();
                    readBlobIdx();
                    u32 nameIdx = readStrIdx();
                    readStrIdx();
                    readBlobIdx();
                    asmOut.assemblyRefs.push_back(ReadHeapString(stringsSpan, nameIdx));
                    break;
                }
                case 0x26: // File
                    tr.ReadU32(); readStrIdx(); readBlobIdx(); break;
                case 0x27: // ExportedType
                    tr.ReadU32(); tr.ReadU32(); readStrIdx(); readStrIdx();
                    readCoded(2, {0x26, 0x23, 0x27}); break;
                case 0x28: // ManifestResource
                    tr.ReadU32(); tr.ReadU32(); readStrIdx();
                    readCoded(2, {0x26, 0x23, 0x27}); break;
                case 0x29: // NestedClass
                    readTableIdx(0x02); readTableIdx(0x02); break;
                case 0x2A: // GenericParam
                    tr.ReadU16(); tr.ReadU16(); readCoded(1, {0x02, 0x06}); readStrIdx(); break;
                case 0x2B: // MethodSpec
                    readCoded(1, {0x06, 0x0A}); readBlobIdx(); break;
                case 0x2C: // GenericParamConstraint
                    readTableIdx(0x2A); readCoded(2, {0x02, 0x01, 0x1B}); break;
                default:
                    break;
            }
        }
    }

    // Collect TypeDef names first for cross-references
    std::vector<std::string> typeDefNames;
    typeDefNames.reserve(rawTypes.size());
    for (const auto& rt : rawTypes) {
        typeDefNames.push_back(ReadHeapString(stringsSpan, rt.nameIdx));
    }

    // Resolve MemberRefs
    for (size_t i = 0; i < rawMemberRefs.size(); ++i) {
        const auto& rm = rawMemberRefs[i];
        MonoMemberRefInfo info;
        info.token = 0x0A000000u | static_cast<u32>(i + 1);
        info.name = ReadHeapString(stringsSpan, rm.nameIdx);
        u32 tag = rm.classCoded & 7u;
        u32 rid = rm.classCoded >> 3;
        if (tag == 0 && rid >= 1 && rid <= typeDefNames.size()) info.ownerName = typeDefNames[rid - 1];
        else if (tag == 1 && rid >= 1 && rid <= asmOut.typeRefs.size()) info.ownerName = asmOut.typeRefs[rid - 1].name;
        asmOut.memberRefs.push_back(std::move(info));
    }

    // Build high-level MonoTypeInfo list with fields, methods, signatures, and extracted CIL method bodies!
    asmOut.types.reserve(rawTypes.size());
    for (size_t ti = 0; ti < rawTypes.size(); ++ti) {
        const auto& rt = rawTypes[ti];
        MonoTypeInfo type;
        type.token = 0x02000000u | static_cast<u32>(ti + 1);
        type.flags = rt.flags;
        type.name = ReadHeapString(stringsSpan, rt.nameIdx);
        type.nameSpace = ReadHeapString(stringsSpan, rt.nsIdx);
        type.isAbstract = (rt.flags & 0x0080u) != 0;
        type.isSealed   = (rt.flags & 0x0100u) != 0;

        u32 extTag = rt.extendsCoded & 3u;
        u32 extRid = rt.extendsCoded >> 2;
        if (extRid > 0) {
            if (extTag == 0 && extRid <= typeDefNames.size()) type.baseType = typeDefNames[extRid - 1];
            else if (extTag == 1 && extRid <= asmOut.typeRefs.size()) type.baseType = asmOut.typeRefs[extRid - 1].FullName();
        }
        if ((rt.flags & 0x0020u) != 0) type.kind = "interface";
        else if (type.baseType == "System.Enum") type.kind = "enum";
        else if (type.baseType == "System.ValueType") type.kind = "struct";
        else type.kind = "class";

        u32 fStart = rt.fieldList >= 1 ? rt.fieldList - 1 : static_cast<u32>(rawFields.size());
        u32 fEnd = (ti + 1 < rawTypes.size() && rawTypes[ti + 1].fieldList >= 1)
                       ? std::min<u32>(rawTypes[ti + 1].fieldList - 1, static_cast<u32>(rawFields.size()))
                       : static_cast<u32>(rawFields.size());
        for (u32 fi = fStart; fi < fEnd && fi < rawFields.size(); ++fi) {
            const auto& rf = rawFields[fi];
            MonoFieldInfo field;
            field.token = 0x04000000u | (fi + 1);
            field.flags = rf.flags;
            field.name = ReadHeapString(stringsSpan, rf.nameIdx);
            field.visibility = VisibilityFromFieldFlags(rf.flags);
            field.isStatic = (rf.flags & 0x0010u) != 0;
            field.isReadOnly = (rf.flags & 0x0020u) != 0;

            ByteSpan sig = ReadHeapBlob(blobSpan, rf.sigIdx);
            size_t spos = 0;
            if (!sig.empty() && sig[0] == 0x06) {
                spos = 1;
                field.typeName = DecodeElementType(sig, spos, asmOut.typeRefs, typeDefNames);
            } else {
                field.typeName = "object";
            }
            type.fields.push_back(std::move(field));
        }

        u32 mStart = rt.methodList >= 1 ? rt.methodList - 1 : static_cast<u32>(rawMethods.size());
        u32 mEnd = (ti + 1 < rawTypes.size() && rawTypes[ti + 1].methodList >= 1)
                       ? std::min<u32>(rawTypes[ti + 1].methodList - 1, static_cast<u32>(rawMethods.size()))
                       : static_cast<u32>(rawMethods.size());
        for (u32 mi = mStart; mi < mEnd && mi < rawMethods.size(); ++mi) {
            const auto& rm = rawMethods[mi];
            MonoMethodInfo meth;
            meth.token = 0x06000000u | (mi + 1);
            meth.rva = rm.rva;
            meth.implFlags = rm.implFlags;
            meth.flags = rm.flags;
            meth.name = ReadHeapString(stringsSpan, rm.nameIdx);
            meth.visibility = VisibilityFromMethodFlags(rm.flags);
            meth.isStatic = (rm.flags & 0x0010u) != 0;
            meth.isVirtual = (rm.flags & 0x0040u) != 0;
            meth.isAbstract = (rm.flags & 0x0400u) != 0;

            // Decode method signature (#Blob)
            ByteSpan sig = ReadHeapBlob(blobSpan, rm.sigIdx);
            std::vector<std::string> paramTypes;
            if (sig.size() >= 3) {
                size_t spos = 0;
                u8 callConv = sig[spos++];
                if (callConv & 0x10u) { ReadCompressedUInt(sig, spos); } // generic param count
                u32 paramCount = ReadCompressedUInt(sig, spos);
                meth.returnType = DecodeElementType(sig, spos, asmOut.typeRefs, typeDefNames);
                for (u32 p = 0; p < paramCount; ++p) {
                    paramTypes.push_back(DecodeElementType(sig, spos, asmOut.typeRefs, typeDefNames));
                }
            }

            // Attach Param names
            u32 pStart = rm.paramList >= 1 ? rm.paramList - 1 : static_cast<u32>(rawParams.size());
            u32 pEnd = (mi + 1 < rawMethods.size() && rawMethods[mi + 1].paramList >= 1)
                           ? std::min<u32>(rawMethods[mi + 1].paramList - 1, static_cast<u32>(rawParams.size()))
                           : static_cast<u32>(rawParams.size());
            for (size_t pi = 0; pi < paramTypes.size(); ++pi) {
                MonoParamInfo mp;
                mp.sequence = static_cast<u16>(pi + 1);
                mp.typeName = paramTypes[pi];
                mp.name = "arg" + std::to_string(pi);
                for (u32 rp = pStart; rp < pEnd && rp < rawParams.size(); ++rp) {
                    if (rawParams[rp].seq == pi + 1) {
                        std::string pn = ReadHeapString(stringsSpan, rawParams[rp].nameIdx);
                        if (!pn.empty()) mp.name = pn;
                        mp.flags = rawParams[rp].flags;
                        break;
                    }
                }
                meth.params.push_back(std::move(mp));
            }

            // Extract CIL Method Body from RVA (Tiny or Fat header!)
            size_t bodyOff = 0;
            if (rm.rva != 0 && RvaToFileOffset(rm.rva, sections, peBytes.size(), bodyOff)) {
                u8 hdr0 = peBytes[bodyOff];
                u8 format = hdr0 & 0x03u;
                if (format == 0x02) { // Tiny header
                    u32 codeSize = hdr0 >> 2;
                    if (bodyOff + 1 + codeSize <= peBytes.size()) {
                        meth.ilCode.assign(
                            peBytes.begin() + static_cast<std::ptrdiff_t>(bodyOff + 1),
                            peBytes.begin() + static_cast<std::ptrdiff_t>(bodyOff + 1 + codeSize));
                    }
                } else if (format == 0x03 && bodyOff + 12 <= peBytes.size()) { // Fat header
                    BinaryReader br(peBytes.subspan(bodyOff, 12), Endian::Little);
                    u16 flagsAndSize = br.ReadU16();
                    u32 hdrBytes = ((flagsAndSize >> 12) & 0x0Fu) * 4;
                    meth.maxStack = br.ReadU16();
                    u32 codeSize = br.ReadU32();
                    if (hdrBytes >= 12 && bodyOff + hdrBytes + codeSize <= peBytes.size()) {
                        meth.ilCode.assign(
                            peBytes.begin() + static_cast<std::ptrdiff_t>(bodyOff + hdrBytes),
                            peBytes.begin() + static_cast<std::ptrdiff_t>(bodyOff + hdrBytes + codeSize));
                    }
                }
            }

            type.methods.push_back(std::move(meth));
        }

        asmOut.types.push_back(std::move(type));
    }

    COCO_LOG_INFO("Mono", "Parsed managed assembly '" + asmOut.fileName + "' (" +
                          std::to_string(asmOut.types.size()) + " types, " +
                          std::to_string(asmOut.userStrings.size()) + " user strings)");
    return Result<MonoAssembly>::Ok(std::move(asmOut));
}

} // namespace coconut::decompiler
