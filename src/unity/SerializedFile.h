#pragma once

#include "core/Types.h"
#include "unity/TypeTree.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::unity {

struct SerializedType {
    i32 classId = 0;
    bool isStrippedType = false;
    i16 scriptTypeIndex = -1;
    std::array<u8, 16> scriptId{};
    std::array<u8, 16> oldTypeHash{};
    TypeTree typeTree;
    std::vector<i32> typeDependencies;
    std::string className;
    std::string nameSpace;
    std::string assemblyName;
};

struct ObjectInfo {
    i64 pathId = 0;
    u64 byteStart = 0; // relative to dataOffset
    u32 byteSize = 0;
    i32 typeId = 0;    // index into types_ (v16+) or classId (<16)
    i32 classId = 0;
    u16 isDestroyed = 0;
    i16 scriptTypeIndex = -1;
    u8  stripped = 0;

    // Live raw payload buffer (editable in Hex Editor or via TypeValue re-serialization!)
    ByteBuffer rawData;
    bool dirty = false;
    std::string cachedName;
};

struct FileIdentifier {
    std::string tempEmpty;
    std::array<u8, 16> guid{};
    i32 type = 0;
    std::string pathName;
};

struct SerializedFileHeader {
    u32 metadataSize = 0;
    u64 fileSize = 0;
    u32 version = 22; // Unity 2020-2023+ default v22
    u64 dataOffset = 0;
    bool bigEndian = false;
};

// Full Unity SerializedFile (v9 - v23) reader AND live binary re-serializer!
class SerializedFile {
public:
    static bool LooksLikeSerializedFile(ByteSpan data);

    static Result<SerializedFile> Parse(ByteSpan data, std::string sourceName = {});
    static Result<SerializedFile> LoadFromFile(const std::filesystem::path& path);

    // Re-serialize the entire SerializedFile (including any patched rawData or TypeValue edits)
    // back into a valid Unity binary file!
    Result<ByteBuffer> Serialize() const;

    ObjectInfo* FindObject(i64 pathId);
    const ObjectInfo* FindObject(i64 pathId) const;
    const SerializedType* GetTypeForObject(const ObjectInfo& obj) const;

    // Parse an object's payload via its TypeTree into an editable TypeValue DOM.
    Result<TypeValue> ReadObjectValue(const ObjectInfo& obj) const;

    // Re-serialize an edited TypeValue DOM back into obj.rawData and mark obj.dirty = true.
    Result<void> WriteObjectValue(ObjectInfo& obj, const TypeValue& val);

    // Extract best human-readable object name (m_Name or fallback ClassID#PathID).
    std::string ResolveObjectName(const ObjectInfo& obj) const;

    std::string name;
    std::filesystem::path diskPath;
    SerializedFileHeader header;
    std::string unityVersionString = "2022.3.15f1";
    UnityVersion unityVersion;
    TargetPlatform targetPlatform = TargetPlatform::StandaloneWindows64;
    bool enableTypeTree = true;

    std::vector<SerializedType> types;
    std::vector<ObjectInfo> objects;
    std::vector<std::pair<i32, i64>> scriptTypes;
    std::vector<FileIdentifier> externals;
    std::vector<SerializedType> refTypes;
    std::string userInformation;
};

} // namespace coconut::unity
