#include "unity/SerializedFile.h"
#include "unity/ClassIDs.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cstring>

namespace coconut::unity {

bool SerializedFile::LooksLikeSerializedFile(ByteSpan data) {
    if (data.size() < 24) return false;
    BinaryReader r(data, Endian::Big);
    u32 metaSize = r.ReadU32();
    u32 fileSize = r.ReadU32();
    u32 version = r.ReadU32();
    u32 dataOffset = r.ReadU32();
    if (version < 9 || version > 30) return false;
    if (version >= 22) {
        if (data.size() < 48) return false;
        r.Skip(4); // endianness + reserved
        u32 meta22 = r.ReadU32();
        u64 fileSize22 = r.ReadU64();
        u64 dataOff22 = r.ReadU64();
        return meta22 > 0 && fileSize22 >= dataOff22 && fileSize22 <= data.size() + 64 && dataOff22 <= data.size();
    }
    return metaSize > 0 && fileSize >= dataOffset && fileSize <= data.size() + 64 && dataOffset <= data.size();
}

Result<SerializedFile> SerializedFile::LoadFromFile(const std::filesystem::path& path) {
    auto bytes = platform::ReadFileBytes(path);
    if (!bytes) return Result<SerializedFile>::Err(bytes.Error());
    auto res = Parse(bytes.Value(), path.filename().string());
    if (res) {
        res.Value().diskPath = path;
    }
    return res;
}

Result<SerializedFile> SerializedFile::Parse(ByteSpan data, std::string sourceName) {
    if (data.size() < 20) {
        return Result<SerializedFile>::Err("SerializedFile too small");
    }

    BinaryReader r(data, Endian::Big);
    SerializedFile sf;
    sf.name = std::move(sourceName);

    sf.header.metadataSize = r.ReadU32();
    sf.header.fileSize = r.ReadU32();
    sf.header.version = r.ReadU32();
    sf.header.dataOffset = r.ReadU32();

    if (sf.header.version < 9 || sf.header.version > 30) {
        return Result<SerializedFile>::Err("Unsupported SerializedFile format version: " +
                                           std::to_string(sf.header.version));
    }

    if (sf.header.version >= 9) {
        sf.header.bigEndian = r.ReadU8() != 0;
        r.Skip(3); // reserved
    }

    if (sf.header.version >= 22) {
        sf.header.metadataSize = r.ReadU32();
        sf.header.fileSize = r.ReadU64();
        sf.header.dataOffset = r.ReadU64();
        r.Skip(8); // unknown / reserved int64
    }

    if (r.HasError() || sf.header.dataOffset > data.size()) {
        return Result<SerializedFile>::Err("Invalid SerializedFile header offsets");
    }

    r.SetEndian(sf.header.bigEndian ? Endian::Big : Endian::Little);

    if (sf.header.version >= 7) {
        sf.unityVersionString = r.ReadCString();
        sf.unityVersion = UnityVersion::Parse(sf.unityVersionString);
    }
    if (sf.header.version >= 8) {
        sf.targetPlatform = static_cast<TargetPlatform>(r.ReadI32());
    }
    if (sf.header.version >= 13) {
        sf.enableTypeTree = r.ReadBool();
    }

    auto readSerializedType = [&](bool isRefType) -> Result<SerializedType> {
        SerializedType st;
        st.classId = r.ReadI32();
        if (sf.header.version >= 16) {
            st.isStrippedType = r.ReadBool();
        }
        if (sf.header.version >= 17) {
            st.scriptTypeIndex = r.ReadI16();
        }
        if (sf.header.version >= 13) {
            bool hasScriptId = (isRefType && st.scriptTypeIndex >= 0) ||
                               (sf.header.version < 16 && st.classId < 0) ||
                               (sf.header.version >= 16 && st.classId == 114);
            if (hasScriptId) {
                auto bytes = r.ReadBytes(16);
                if (bytes.size() == 16) std::memcpy(st.scriptId.data(), bytes.data(), 16);
            }
            auto hashBytes = r.ReadBytes(16);
            if (hashBytes.size() == 16) std::memcpy(st.oldTypeHash.data(), hashBytes.data(), 16);
        }
        if (sf.enableTypeTree) {
            auto treeRes = TypeTreeSerializer::ReadTypeTreeBlob(r, sf.header.version);
            if (!treeRes) return Result<SerializedType>::Err(treeRes.Error());
            st.typeTree = std::move(treeRes.Value());

            if (sf.header.version >= 21) {
                if (isRefType) {
                    st.className = r.ReadCString();
                    st.nameSpace = r.ReadCString();
                    st.assemblyName = r.ReadCString();
                } else {
                    i32 depCount = r.ReadI32();
                    if (depCount > 0 && depCount < 100000) {
                        st.typeDependencies.reserve(static_cast<size_t>(depCount));
                        for (i32 d = 0; d < depCount; ++d) {
                            st.typeDependencies.push_back(r.ReadI32());
                        }
                    }
                }
            }
        }
        return Result<SerializedType>::Ok(std::move(st));
    };

    i32 typeCount = r.ReadI32();
    if (typeCount < 0 || typeCount > 100000) {
        return Result<SerializedFile>::Err("Invalid SerializedFile type count");
    }
    sf.types.reserve(static_cast<size_t>(typeCount));
    for (i32 i = 0; i < typeCount; ++i) {
        auto stRes = readSerializedType(false);
        if (!stRes) return Result<SerializedFile>::Err("Failed reading type " + std::to_string(i) + ": " + stRes.Error());
        sf.types.push_back(std::move(stRes.Value()));
    }

    bool bigIdEnabled = false;
    if (sf.header.version >= 7 && sf.header.version < 14) {
        bigIdEnabled = r.ReadI32() != 0;
    }

    i32 objectCount = r.ReadI32();
    if (objectCount < 0 || objectCount > 5000000) {
        return Result<SerializedFile>::Err("Invalid SerializedFile object count");
    }
    sf.objects.reserve(static_cast<size_t>(objectCount));
    for (i32 i = 0; i < objectCount; ++i) {
        ObjectInfo obj;
        if (bigIdEnabled) {
            obj.pathId = r.ReadI64();
        } else if (sf.header.version < 14) {
            obj.pathId = r.ReadI32();
        } else {
            r.Align(4);
            obj.pathId = r.ReadI64();
        }

        if (sf.header.version >= 22) {
            obj.byteStart = r.ReadU64();
        } else {
            obj.byteStart = r.ReadU32();
        }
        obj.byteSize = r.ReadU32();
        obj.typeId = r.ReadI32();

        if (sf.header.version < 16) {
            obj.classId = r.ReadU16();
            obj.isDestroyed = r.ReadU16();
        } else {
            if (obj.typeId >= 0 && static_cast<size_t>(obj.typeId) < sf.types.size()) {
                obj.classId = sf.types[static_cast<size_t>(obj.typeId)].classId;
            }
        }
        if (sf.header.version >= 11 && sf.header.version < 17) {
            obj.scriptTypeIndex = r.ReadI16();
        }
        if (sf.header.version == 15 || sf.header.version == 16) {
            obj.stripped = r.ReadU8();
        }

        u64 absStart = sf.header.dataOffset + obj.byteStart;
        if (absStart + obj.byteSize <= data.size()) {
            obj.rawData.assign(
                data.begin() + static_cast<std::ptrdiff_t>(absStart),
                data.begin() + static_cast<std::ptrdiff_t>(absStart + obj.byteSize));
        }
        sf.objects.push_back(std::move(obj));
    }

    if (sf.header.version >= 11 && !r.HasError() && r.Remaining() >= 4) {
        i32 scriptCount = r.ReadI32();
        if (scriptCount >= 0 && scriptCount < 100000) {
            for (i32 i = 0; i < scriptCount; ++i) {
                i32 fileIdx = r.ReadI32();
                i64 id = 0;
                if (sf.header.version < 14) {
                    id = r.ReadI32();
                } else {
                    r.Align(4);
                    id = r.ReadI64();
                }
                sf.scriptTypes.emplace_back(fileIdx, id);
            }
        }
    }

    if (!r.HasError() && r.Remaining() >= 4) {
        i32 extCount = r.ReadI32();
        if (extCount >= 0 && extCount < 100000) {
            for (i32 i = 0; i < extCount; ++i) {
                FileIdentifier fi;
                if (sf.header.version >= 6) {
                    fi.tempEmpty = r.ReadCString();
                }
                if (sf.header.version >= 5) {
                    auto g = r.ReadBytes(16);
                    if (g.size() == 16) std::memcpy(fi.guid.data(), g.data(), 16);
                    fi.type = r.ReadI32();
                }
                fi.pathName = r.ReadCString();
                sf.externals.push_back(std::move(fi));
            }
        }
    }

    if (sf.header.version >= 20 && !r.HasError() && r.Remaining() >= 4) {
        i32 refCount = r.ReadI32();
        if (refCount >= 0 && refCount < 100000) {
            for (i32 i = 0; i < refCount; ++i) {
                auto rt = readSerializedType(true);
                if (!rt) break;
                sf.refTypes.push_back(std::move(rt.Value()));
            }
        }
    }

    if (sf.header.version >= 5 && !r.HasError() && r.Remaining() > 0) {
        sf.userInformation = r.ReadCString();
    }

    // Pre-cache object names
    for (auto& obj : sf.objects) {
        obj.cachedName = sf.ResolveObjectName(obj);
    }

    COCO_LOG_INFO("SerializedFile", "Parsed '" + sf.name + "' (v" + std::to_string(sf.header.version) +
                                    ", Unity " + sf.unityVersionString + ", " +
                                    std::to_string(sf.objects.size()) + " objects)");
    return Result<SerializedFile>::Ok(std::move(sf));
}

Result<ByteBuffer> SerializedFile::Serialize() const {
    const u32 ver = header.version >= 22 ? header.version : 22;
    const Endian dataEndian = header.bigEndian ? Endian::Big : Endian::Little;

    // 1. Build concatenated aligned object data section and compute new byteStart offsets
    BinaryWriter dataW(dataEndian);
    std::vector<u64> objOffsets;
    objOffsets.reserve(objects.size());
    for (const auto& obj : objects) {
        dataW.Align(8);
        objOffsets.push_back(dataW.Size());
        dataW.WriteBytes(obj.rawData);
    }
    const ByteBuffer& objectBlob = dataW.Buffer();

    // 2. Build metadata section
    BinaryWriter metaW(dataEndian);
    metaW.WriteCString(unityVersionString);
    metaW.WriteI32(static_cast<i32>(targetPlatform));
    metaW.WriteBool(enableTypeTree);

    auto writeSerializedType = [&](const SerializedType& st, bool isRefType) {
        metaW.WriteI32(st.classId);
        metaW.WriteBool(st.isStrippedType);
        metaW.WriteI16(st.scriptTypeIndex);
        bool hasScriptId = (isRefType && st.scriptTypeIndex >= 0) || st.classId == 114;
        if (hasScriptId) {
            metaW.WriteBytes(st.scriptId);
        }
        metaW.WriteBytes(st.oldTypeHash);
        if (enableTypeTree) {
            TypeTreeSerializer::WriteTypeTreeBlob(metaW, st.typeTree, ver);
            if (isRefType) {
                metaW.WriteCString(st.className);
                metaW.WriteCString(st.nameSpace);
                metaW.WriteCString(st.assemblyName);
            } else {
                metaW.WriteI32(static_cast<i32>(st.typeDependencies.size()));
                for (i32 dep : st.typeDependencies) metaW.WriteI32(dep);
            }
        }
    };

    metaW.WriteI32(static_cast<i32>(types.size()));
    for (const auto& st : types) {
        writeSerializedType(st, false);
    }

    metaW.WriteI32(static_cast<i32>(objects.size()));
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& obj = objects[i];
        metaW.Align(4);
        metaW.WriteI64(obj.pathId);
        metaW.WriteU64(objOffsets[i]);
        metaW.WriteU32(static_cast<u32>(obj.rawData.size()));
        metaW.WriteI32(obj.typeId);
    }

    metaW.WriteI32(static_cast<i32>(scriptTypes.size()));
    for (const auto& [fileIdx, id] : scriptTypes) {
        metaW.WriteI32(fileIdx);
        metaW.Align(4);
        metaW.WriteI64(id);
    }

    metaW.WriteI32(static_cast<i32>(externals.size()));
    for (const auto& ext : externals) {
        metaW.WriteCString(ext.tempEmpty);
        metaW.WriteBytes(ext.guid);
        metaW.WriteI32(ext.type);
        metaW.WriteCString(ext.pathName);
    }

    metaW.WriteI32(static_cast<i32>(refTypes.size()));
    for (const auto& rt : refTypes) {
        writeSerializedType(rt, true);
    }
    metaW.WriteCString(userInformation);

    const ByteBuffer& metaBlob = metaW.Buffer();

    // 3. Assemble v22 Big-Endian outer header + metadata + aligned data blob
    constexpr u64 kHeaderSizeV22 = 48;
    u64 unalignedDataOffset = kHeaderSizeV22 + metaBlob.size();
    u64 dataOffset = (unalignedDataOffset + 15) & ~static_cast<u64>(15);
    if (dataOffset < 4096) dataOffset = std::max<u64>(dataOffset, kHeaderSizeV22 + metaBlob.size());
    u64 totalSize = dataOffset + objectBlob.size();

    BinaryWriter out(Endian::Big);
    out.WriteU32(0); // legacy metadataSize (0 in v22)
    out.WriteU32(0); // legacy fileSize
    out.WriteU32(ver);
    out.WriteU32(0); // legacy dataOffset
    out.WriteU8(header.bigEndian ? 1 : 0);
    out.WriteZeros(3);
    out.WriteU32(static_cast<u32>(metaBlob.size()));
    out.WriteU64(totalSize);
    out.WriteU64(dataOffset);
    out.WriteU64(0); // reserved
    out.WriteBytes(metaBlob);
    if (out.Size() < dataOffset) {
        out.WriteZeros(static_cast<size_t>(dataOffset - out.Size()));
    }
    out.WriteBytes(objectBlob);

    return Result<ByteBuffer>::Ok(out.TakeBuffer());
}

ObjectInfo* SerializedFile::FindObject(i64 pathId) {
    for (auto& o : objects) {
        if (o.pathId == pathId) return &o;
    }
    return nullptr;
}

const ObjectInfo* SerializedFile::FindObject(i64 pathId) const {
    for (const auto& o : objects) {
        if (o.pathId == pathId) return &o;
    }
    return nullptr;
}

const SerializedType* SerializedFile::GetTypeForObject(const ObjectInfo& obj) const {
    if (obj.typeId >= 0 && static_cast<size_t>(obj.typeId) < types.size()) {
        return &types[static_cast<size_t>(obj.typeId)];
    }
    for (const auto& t : types) {
        if (t.classId == obj.classId) return &t;
    }
    return nullptr;
}

Result<TypeValue> SerializedFile::ReadObjectValue(const ObjectInfo& obj) const {
    const auto* st = GetTypeForObject(obj);
    if (!st || st->typeTree.Empty()) {
        return Result<TypeValue>::Err("No TypeTree available for object " + std::to_string(obj.pathId));
    }
    BinaryReader r(obj.rawData, header.bigEndian ? Endian::Big : Endian::Little);
    return TypeTreeSerializer::ReadObject(r, st->typeTree);
}

Result<void> SerializedFile::WriteObjectValue(ObjectInfo& obj, const TypeValue& val) {
    const auto* st = GetTypeForObject(obj);
    if (!st || st->typeTree.Empty()) {
        return Result<void>::Err("No TypeTree available for object " + std::to_string(obj.pathId));
    }
    auto bytes = TypeTreeSerializer::WriteObject(val, st->typeTree, header.bigEndian ? Endian::Big : Endian::Little);
    if (!bytes) return Result<void>::Err(bytes.Error());
    obj.rawData = std::move(bytes.Value());
    obj.byteSize = static_cast<u32>(obj.rawData.size());
    obj.dirty = true;
    obj.cachedName = ResolveObjectName(obj);
    return Result<void>::Ok();
}

std::string SerializedFile::ResolveObjectName(const ObjectInfo& obj) const {
    auto valRes = ReadObjectValue(obj);
    if (valRes) {
        if (const auto* nameField = valRes.Value().FindField("m_Name")) {
            std::string n = nameField->AsString();
            if (!n.empty()) return n;
        }
    } else if (obj.rawData.size() >= 8) {
        // Fallback: many Unity NamedObjects start with an aligned string m_Name
        BinaryReader r(obj.rawData, header.bigEndian ? Endian::Big : Endian::Little);
        i32 len = r.ReadI32();
        if (len > 0 && len < 128 && static_cast<size_t>(len + 4) <= obj.rawData.size()) {
            std::string cand = r.ReadFixedString(static_cast<size_t>(len));
            bool printable = true;
            for (char c : cand) {
                if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) {
                    printable = false;
                    break;
                }
            }
            if (printable) return cand;
        }
    }
    return std::string(ClassIDName(obj.classId)) + "_" + std::to_string(obj.pathId);
}

} // namespace coconut::unity
