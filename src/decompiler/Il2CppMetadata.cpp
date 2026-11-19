#include "decompiler/Il2CppMetadata.h"
#include "core/BinaryReader.h"
#include "core/Logger.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace coconut::decompiler {

namespace {

std::string ReadNullTermString(ByteSpan strTable, i32 offset) {
    if (offset < 0 || static_cast<size_t>(offset) >= strTable.size()) return {};
    size_t len = 0;
    while (static_cast<size_t>(offset) + len < strTable.size() &&
           strTable[static_cast<size_t>(offset) + len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(strTable.data() + offset), len);
}

} // namespace

Result<Il2CppMetadata> Il2CppMetadata::Parse(ByteSpan data) {
    if (data.size() < 200) {
        return Result<Il2CppMetadata>::Err("global-metadata.dat too small");
    }

    BinaryReader r(data, Endian::Little);
    u32 sanity = r.ReadU32();
    if (sanity != 0xFAB11BAFu) {
        return Result<Il2CppMetadata>::Err("Invalid IL2CPP global-metadata.dat magic (expected 0xFAB11BAF)");
    }

    Il2CppMetadata meta;
    meta.version = r.ReadI32();
    if (meta.version < 16 || meta.version > 35) {
        return Result<Il2CppMetadata>::Err("Unsupported IL2CPP metadata version: " + std::to_string(meta.version));
    }

    auto readSpanAt = [&](size_t headerOff) -> ByteSpan {
        if (headerOff + 8 > data.size()) return {};
        BinaryReader hr(data.subspan(headerOff, 8), Endian::Little);
        u32 off = hr.ReadU32();
        u32 sz  = hr.ReadU32();
        if (off == 0 || sz == 0 || static_cast<size_t>(off) + sz > data.size()) return {};
        return data.subspan(off, sz);
    };

    // Standard IL2CPP header offsets (v24-v31)
    ByteSpan litInfoSpan = readSpanAt(8);
    ByteSpan litDataSpan = readSpanAt(16);
    ByteSpan stringSpan  = readSpanAt(24);
    ByteSpan methodsSpan = readSpanAt(48);
    ByteSpan fieldsSpan  = readSpanAt(112);
    ByteSpan typesSpan   = readSpanAt(160);
    ByteSpan imagesSpan  = readSpanAt(168);

    // 1. Parse String Literals
    if (!litInfoSpan.empty() && !litDataSpan.empty()) {
        BinaryReader lr(litInfoSpan, Endian::Little);
        size_t count = litInfoSpan.size() / 8;
        meta.stringLiterals.reserve(std::min<size_t>(count, 100000));
        for (size_t i = 0; i < count; ++i) {
            u32 len = lr.ReadU32();
            i32 idx = lr.ReadI32();
            if (idx >= 0 && static_cast<size_t>(idx) + len <= litDataSpan.size()) {
                meta.stringLiterals.emplace_back(
                    reinterpret_cast<const char*>(litDataSpan.data() + idx), len);
            }
        }
    }

    // 2. Parse Raw Fields (12 bytes each: nameIndex, typeIndex, token)
    std::vector<Il2CppFieldInfo> allFields;
    if (!fieldsSpan.empty()) {
        BinaryReader fr(fieldsSpan, Endian::Little);
        size_t fCount = fieldsSpan.size() / 12;
        allFields.reserve(fCount);
        for (size_t i = 0; i < fCount; ++i) {
            Il2CppFieldInfo f;
            f.name = ReadNullTermString(stringSpan, fr.ReadI32());
            f.typeIndex = fr.ReadI32();
            f.token = fr.ReadU32();
            allFields.push_back(std::move(f));
        }
    }

    // 3. Parse Raw Methods (32 bytes each in v24-v31)
    std::vector<Il2CppMethodInfo> allMethods;
    if (!methodsSpan.empty()) {
        BinaryReader mr(methodsSpan, Endian::Little);
        size_t mCount = methodsSpan.size() / 32;
        allMethods.reserve(mCount);
        for (size_t i = 0; i < mCount; ++i) {
            Il2CppMethodInfo m;
            m.name = ReadNullTermString(stringSpan, mr.ReadI32());
            m.declaringType = mr.ReadI32();
            mr.ReadI32(); // returnType
            mr.ReadI32(); // parameterStart
            mr.ReadI32(); // genericContainerIndex
            m.token = mr.ReadU32();
            m.flags = mr.ReadU16();
            mr.ReadU16(); // iflags
            mr.ReadU16(); // slot
            m.paramCount = mr.ReadU16();
            allMethods.push_back(std::move(m));
        }
    }

    // 4. Parse TypeDefinitions (88 bytes each in v27-v31)
    if (!typesSpan.empty()) {
        size_t stride = 88;
        if (typesSpan.size() % 88 != 0 && typesSpan.size() % 92 == 0) stride = 92;
        size_t tCount = typesSpan.size() / stride;
        meta.types.reserve(tCount);
        for (size_t i = 0; i < tCount; ++i) {
            BinaryReader tr(typesSpan.subspan(i * stride, stride), Endian::Little);
            Il2CppTypeInfo td;
            td.name = ReadNullTermString(stringSpan, tr.ReadI32());
            td.nameSpace = ReadNullTermString(stringSpan, tr.ReadI32());
            tr.Skip(20); // byval, declaring, parent, element, genericContainer
            td.flags = tr.ReadU32();
            i32 fieldStart = tr.ReadI32();
            i32 methodStart = tr.ReadI32();
            tr.Skip(24); // event, prop, nested, iface, vtable, ifaceOffsets
            u16 methodCount = tr.ReadU16();
            tr.ReadU16(); // property_count
            u16 fieldCount = tr.ReadU16();
            tr.Skip(10); // event/nested/vtable/iface counts
            tr.ReadU32(); // bitfield
            td.token = tr.ReadU32();

            if (fieldStart >= 0) {
                for (u16 fi = 0; fi < fieldCount && static_cast<size_t>(fieldStart + fi) < allFields.size(); ++fi) {
                    td.fields.push_back(allFields[static_cast<size_t>(fieldStart + fi)]);
                }
            }
            if (methodStart >= 0) {
                for (u16 mi = 0; mi < methodCount && static_cast<size_t>(methodStart + mi) < allMethods.size(); ++mi) {
                    td.methods.push_back(allMethods[static_cast<size_t>(methodStart + mi)]);
                }
            }
            meta.types.push_back(std::move(td));
        }
    }

    // 5. Parse ImageDefinitions (40 bytes each in v24+)
    if (!imagesSpan.empty()) {
        size_t imgCount = imagesSpan.size() / 40;
        for (size_t i = 0; i < imgCount; ++i) {
            BinaryReader ir(imagesSpan.subspan(i * 40, 40), Endian::Little);
            Il2CppImageInfo img;
            img.name = ReadNullTermString(stringSpan, ir.ReadI32());
            ir.ReadI32(); // assemblyIndex
            img.typeStart = ir.ReadI32();
            img.typeCount = ir.ReadU32();
            meta.images.push_back(std::move(img));
        }
    }

    COCO_LOG_INFO("IL2CPP", "Parsed global-metadata.dat v" + std::to_string(meta.version) + " (" +
                            std::to_string(meta.images.size()) + " images, " +
                            std::to_string(meta.types.size()) + " types, " +
                            std::to_string(meta.stringLiterals.size()) + " string literals)");
    return Result<Il2CppMetadata>::Ok(std::move(meta));
}

std::string Il2CppMetadata::GenerateTypeStubCS(const Il2CppTypeInfo& type) const {
    std::ostringstream oss;
    oss << "// Reconstructed by Coconut IL2CPP Analyzer (Metadata v" << version << ")\n";
    oss << "using System;\nusing UnityEngine;\n\n";
    bool hasNs = !type.nameSpace.empty();
    if (hasNs) oss << "namespace " << type.nameSpace << "\n{\n";
    std::string ind = hasNs ? "    " : "";
    oss << ind << "// Token: 0x" << std::hex << std::uppercase << type.token << std::dec << "\n";
    oss << ind << "public class " << type.name << "\n" << ind << "{\n";
    for (const auto& f : type.fields) {
        oss << ind << "    public object " << f.name << "; // Token: 0x"
            << std::hex << std::uppercase << f.token << std::dec << "\n";
    }
    if (!type.fields.empty() && !type.methods.empty()) oss << '\n';
    for (const auto& m : type.methods) {
        oss << ind << "    // RVA: 0x" << std::hex << std::uppercase << m.nativeRva
            << " | Token: 0x" << m.token << std::dec << "\n";
        oss << ind << "    public void " << m.name << "(";
        for (u16 p = 0; p < m.paramCount; ++p) {
            if (p > 0) oss << ", ";
            oss << "object arg" << p;
        }
        oss << ")\n" << ind << "    {\n"
            << ind << "        // Native IL2CPP implementation (inspect Disassembly Listing)\n"
            << ind << "    }\n\n";
    }
    oss << ind << "}\n";
    if (hasNs) oss << "}\n";
    return oss.str();
}

std::string Il2CppMetadata::GenerateDumpCS() const {
    std::ostringstream oss;
    oss << "// Coconut IL2CPP dump.cs — Metadata v" << version << "\n";
    oss << "// Images: " << images.size() << " | Types: " << types.size()
        << " | String Literals: " << stringLiterals.size() << "\n\n";
    for (const auto& t : types) {
        oss << GenerateTypeStubCS(t) << "\n";
    }
    return oss.str();
}

} // namespace coconut::decompiler
