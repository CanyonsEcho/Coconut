#pragma once

#include "core/Types.h"
#include "unity/ClassIDs.h"
#include "unity/GameLoader.h"
#include "assets/TextureAsset.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "assets/MaterialAsset.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace coconut::assets {

struct AssetEntry {
    size_t fileIndex = 0;
    size_t objectIndex = 0;
    std::string fileName;
    i64 pathId = 0;
    i32 classId = 0;
    std::string className;
    unity::AssetCategory category = unity::AssetCategory::Other;
    std::string name;
    u32 byteSize = 0;
    bool dirty = false;
};

class AssetManager {
public:
    void Clear();
    void IndexProject(unity::LoadedGameProject& project);

    const std::vector<AssetEntry>& Entries() const { return entries_; }
    std::vector<size_t> FilterEntries(unity::AssetCategory category, bool useCategoryFilter, std::string_view searchQuery) const;

    unity::SerializedFile* GetFile(size_t fileIndex);
    const unity::SerializedFile* GetFile(size_t fileIndex) const;
    unity::ObjectInfo* GetObject(size_t entryIndex);
    const unity::ObjectInfo* GetObject(size_t entryIndex) const;
    int FindEntryByPathId(size_t fileIndex, i64 pathId) const;
    int FindEntryByPathIdAnyFile(i64 pathId, i32 expectedClassId = -1) const;

    // Decoded asset accessors (cached per entry index)
    Result<Texture2DData*> GetTexture(size_t entryIndex);
    Result<MeshData*> GetMesh(size_t entryIndex);
    Result<AudioClipData*> GetAudio(size_t entryIndex);
    Result<MaterialData*> GetMaterial(size_t entryIndex);
    Result<std::string> GetTextAssetContent(size_t entryIndex);

    // Live editing helpers
    Result<void> PatchRawByte(size_t entryIndex, size_t offset, u8 newByte);
    Result<void> ApplyTextureEdits(size_t entryIndex);
    Result<void> ApplyMaterialEdits(size_t entryIndex);
    Result<void> SaveModifiedFiles(const std::filesystem::path& outputDir = {});

    bool HasDirtyAssets() const;

private:
    unity::LoadedGameProject* project_ = nullptr;
    std::vector<AssetEntry> entries_;
    std::unordered_map<size_t, Texture2DData> textureCache_;
    std::unordered_map<size_t, MeshData> meshCache_;
    std::unordered_map<size_t, AudioClipData> audioCache_;
    std::unordered_map<size_t, MaterialData> materialCache_;
};

} // namespace coconut::assets
