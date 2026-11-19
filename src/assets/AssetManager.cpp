#include "assets/AssetManager.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cctype>

namespace coconut::assets {

namespace {

std::string ToLowerStr(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

void AssetManager::Clear() {
    project_ = nullptr;
    entries_.clear();
    textureCache_.clear();
    meshCache_.clear();
    audioCache_.clear();
    materialCache_.clear();
}

void AssetManager::IndexProject(unity::LoadedGameProject& project) {
    Clear();
    project_ = &project;

    for (size_t fi = 0; fi < project.serializedFiles.size(); ++fi) {
        const auto& sf = project.serializedFiles[fi];
        for (size_t oi = 0; oi < sf.objects.size(); ++oi) {
            const auto& obj = sf.objects[oi];
            AssetEntry e;
            e.fileIndex = fi;
            e.objectIndex = oi;
            e.fileName = sf.name;
            e.pathId = obj.pathId;
            e.classId = obj.classId;
            e.className = std::string(unity::ClassIDName(obj.classId));
            e.category = unity::ClassifyClassID(obj.classId);
            e.name = obj.cachedName.empty() ? (e.className + "_" + std::to_string(obj.pathId)) : obj.cachedName;
            e.byteSize = static_cast<u32>(obj.rawData.size());
            e.dirty = obj.dirty;
            entries_.push_back(std::move(e));
        }
    }

    COCO_LOG_INFO("Assets", "Indexed " + std::to_string(entries_.size()) + " assets across " +
                            std::to_string(project.serializedFiles.size()) + " serialized files");
}

std::vector<size_t> AssetManager::FilterEntries(
    unity::AssetCategory category,
    bool useCategoryFilter,
    std::string_view searchQuery) const {
    std::vector<size_t> result;
    std::string needle = ToLowerStr(searchQuery);
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (useCategoryFilter && e.category != category) continue;
        if (!needle.empty()) {
            std::string hay = ToLowerStr(e.name + " " + e.className + " " + e.fileName);
            if (hay.find(needle) == std::string::npos) continue;
        }
        result.push_back(i);
    }
    return result;
}

unity::SerializedFile* AssetManager::GetFile(size_t fileIndex) {
    if (!project_ || fileIndex >= project_->serializedFiles.size()) return nullptr;
    return &project_->serializedFiles[fileIndex];
}

const unity::SerializedFile* AssetManager::GetFile(size_t fileIndex) const {
    if (!project_ || fileIndex >= project_->serializedFiles.size()) return nullptr;
    return &project_->serializedFiles[fileIndex];
}

unity::ObjectInfo* AssetManager::GetObject(size_t entryIndex) {
    if (!project_ || entryIndex >= entries_.size()) return nullptr;
    const auto& e = entries_[entryIndex];
    return &project_->serializedFiles[e.fileIndex].objects[e.objectIndex];
}

const unity::ObjectInfo* AssetManager::GetObject(size_t entryIndex) const {
    if (!project_ || entryIndex >= entries_.size()) return nullptr;
    const auto& e = entries_[entryIndex];
    return &project_->serializedFiles[e.fileIndex].objects[e.objectIndex];
}

int AssetManager::FindEntryByPathId(size_t fileIndex, i64 pathId) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].fileIndex == fileIndex && entries_[i].pathId == pathId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int AssetManager::FindEntryByPathIdAnyFile(i64 pathId, i32 expectedClassId) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].pathId == pathId && (expectedClassId < 0 || entries_[i].classId == expectedClassId)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Result<Texture2DData*> AssetManager::GetTexture(size_t entryIndex) {
    auto it = textureCache_.find(entryIndex);
    if (it != textureCache_.end()) return Result<Texture2DData*>::Ok(&it->second);
    if (!project_ || entryIndex >= entries_.size()) return Result<Texture2DData*>::Err("Invalid entry index");

    const auto& e = entries_[entryIndex];
    const auto& sf = project_->serializedFiles[e.fileIndex];
    const auto& obj = sf.objects[e.objectIndex];
    auto res = TextureCodec::ParseFromObject(sf, obj, project_);
    if (!res) return Result<Texture2DData*>::Err(res.Error());
    auto [inserted, _] = textureCache_.emplace(entryIndex, std::move(res.Value()));
    return Result<Texture2DData*>::Ok(&inserted->second);
}

Result<MeshData*> AssetManager::GetMesh(size_t entryIndex) {
    auto it = meshCache_.find(entryIndex);
    if (it != meshCache_.end()) return Result<MeshData*>::Ok(&it->second);
    if (!project_ || entryIndex >= entries_.size()) return Result<MeshData*>::Err("Invalid entry index");

    const auto& e = entries_[entryIndex];
    const auto& sf = project_->serializedFiles[e.fileIndex];
    const auto& obj = sf.objects[e.objectIndex];
    auto res = MeshCodec::ParseFromObject(sf, obj, project_);
    if (!res) return Result<MeshData*>::Err(res.Error());
    auto [inserted, _] = meshCache_.emplace(entryIndex, std::move(res.Value()));
    return Result<MeshData*>::Ok(&inserted->second);
}

Result<AudioClipData*> AssetManager::GetAudio(size_t entryIndex) {
    auto it = audioCache_.find(entryIndex);
    if (it != audioCache_.end()) return Result<AudioClipData*>::Ok(&it->second);
    if (!project_ || entryIndex >= entries_.size()) return Result<AudioClipData*>::Err("Invalid entry index");

    const auto& e = entries_[entryIndex];
    const auto& sf = project_->serializedFiles[e.fileIndex];
    const auto& obj = sf.objects[e.objectIndex];
    auto res = AudioCodec::ParseFromObject(sf, obj, project_);
    if (!res) return Result<AudioClipData*>::Err(res.Error());
    auto [inserted, _] = audioCache_.emplace(entryIndex, std::move(res.Value()));
    return Result<AudioClipData*>::Ok(&inserted->second);
}

Result<MaterialData*> AssetManager::GetMaterial(size_t entryIndex) {
    auto it = materialCache_.find(entryIndex);
    if (it != materialCache_.end()) return Result<MaterialData*>::Ok(&it->second);
    if (!project_ || entryIndex >= entries_.size()) return Result<MaterialData*>::Err("Invalid entry index");

    const auto& e = entries_[entryIndex];
    const auto& sf = project_->serializedFiles[e.fileIndex];
    const auto& obj = sf.objects[e.objectIndex];
    auto res = MaterialCodec::ParseFromObject(sf, obj);
    if (!res) return Result<MaterialData*>::Err(res.Error());
    auto [inserted, _] = materialCache_.emplace(entryIndex, std::move(res.Value()));
    return Result<MaterialData*>::Ok(&inserted->second);
}

Result<std::string> AssetManager::GetTextAssetContent(size_t entryIndex) {
    if (!project_ || entryIndex >= entries_.size()) return Result<std::string>::Err("Invalid entry index");
    const auto& e = entries_[entryIndex];
    const auto& sf = project_->serializedFiles[e.fileIndex];
    const auto& obj = sf.objects[e.objectIndex];
    auto valRes = sf.ReadObjectValue(obj);
    if (valRes) {
        if (const auto* sc = valRes.Value().FindField("m_Script")) {
            return Result<std::string>::Ok(sc->AsString());
        }
        return Result<std::string>::Ok(valRes.Value().ToYaml());
    }
    return Result<std::string>::Ok(std::string(reinterpret_cast<const char*>(obj.rawData.data()), obj.rawData.size()));
}

Result<void> AssetManager::PatchRawByte(size_t entryIndex, size_t offset, u8 newByte) {
    auto* obj = GetObject(entryIndex);
    if (!obj) return Result<void>::Err("Invalid asset entry");
    if (offset >= obj->rawData.size()) return Result<void>::Err("Byte offset out of range");
    obj->rawData[offset] = newByte;
    obj->dirty = true;
    entries_[entryIndex].dirty = true;
    textureCache_.erase(entryIndex);
    meshCache_.erase(entryIndex);
    audioCache_.erase(entryIndex);
    materialCache_.erase(entryIndex);
    return Result<void>::Ok();
}

Result<void> AssetManager::ApplyTextureEdits(size_t entryIndex) {
    auto it = textureCache_.find(entryIndex);
    if (it == textureCache_.end()) return Result<void>::Err("Texture not loaded in cache");
    auto* sf = GetFile(entries_[entryIndex].fileIndex);
    auto* obj = GetObject(entryIndex);
    if (!sf || !obj) return Result<void>::Err("Invalid asset reference");
    auto res = TextureCodec::WriteBackToObject(*sf, *obj, it->second);
    if (res) {
        entries_[entryIndex].dirty = true;
        entries_[entryIndex].byteSize = static_cast<u32>(obj->rawData.size());
    }
    return res;
}

Result<void> AssetManager::ApplyMaterialEdits(size_t entryIndex) {
    auto it = materialCache_.find(entryIndex);
    if (it == materialCache_.end()) return Result<void>::Err("Material not loaded in cache");
    auto* sf = GetFile(entries_[entryIndex].fileIndex);
    auto* obj = GetObject(entryIndex);
    if (!sf || !obj) return Result<void>::Err("Invalid asset reference");
    auto res = MaterialCodec::WriteBackToObject(*sf, *obj, it->second);
    if (res) {
        entries_[entryIndex].dirty = true;
        entries_[entryIndex].byteSize = static_cast<u32>(obj->rawData.size());
    }
    return res;
}

bool AssetManager::HasDirtyAssets() const {
    for (const auto& e : entries_) {
        if (e.dirty) return true;
    }
    return false;
}

Result<void> AssetManager::SaveModifiedFiles(const std::filesystem::path& outputDir) {
    if (!project_) return Result<void>::Err("No project loaded");
    size_t savedCount = 0;
    for (auto& sf : project_->serializedFiles) {
        bool anyDirty = false;
        for (const auto& o : sf.objects) {
            if (o.dirty) { anyDirty = true; break; }
        }
        if (!anyDirty) continue;

        auto bytesRes = sf.Serialize();
        if (!bytesRes) return Result<void>::Err("Failed to serialize " + sf.name + ": " + bytesRes.Error());

        std::filesystem::path dst = outputDir.empty()
            ? (sf.diskPath.empty() ? (project_->dataPath / sf.name) : sf.diskPath)
            : (outputDir / sf.name);
        auto writeRes = platform::WriteFileBytes(dst, bytesRes.Value());
        if (!writeRes) return writeRes;

        for (auto& o : sf.objects) o.dirty = false;
        ++savedCount;
        COCO_LOG_INFO("Assets", "Saved modified SerializedFile -> " + dst.string());
    }
    for (auto& e : entries_) e.dirty = false;
    if (savedCount == 0) {
        COCO_LOG_INFO("Assets", "No modified SerializedFiles to save.");
    }
    return Result<void>::Ok();
}

} // namespace coconut::assets
