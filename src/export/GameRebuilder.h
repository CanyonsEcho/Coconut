#pragma once

#include "core/Types.h"
#include "core/Compression.h"
#include "unity/GameLoader.h"
#include "scene/SceneDocument.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace coconut::exporter {

struct PendingGameChange {
    std::string fileName;
    std::string objectName;
    std::string className;
    i64 pathId = 0;
    size_t byteSize = 0;
    std::string changeKind; // "Scene/Transform", "Asset/Hex Patch", "Bundle Repack"
};

struct GameRebuildOptions {
    std::filesystem::path outputPath; // Destination directory (desktop) or .apk file (Android)
    CompressionType bundleCompression = CompressionType::LZ4HC;
    bool copyUntouchedGameFiles = true;
    bool syncLiveSceneBeforeBuild = true;
    bool writePatchManifest = true;
};

struct GameRebuildReport {
    std::filesystem::path outputPath;
    std::string targetFormat; // "Windows/Linux/macOS Patched Build" or "Android Patched APK"
    size_t serializedFilesRebuilt = 0;
    size_t objectsPatched = 0;
    size_t bundlesRepacked = 0;
    size_t untouchedFilesCopied = 0;
    u64 totalOutputBytes = 0;
    bool requiresApkSigning = false;
    std::vector<std::string> rebuiltFiles;
    std::vector<std::string> logs;
};

// Builds/exports the modified Unity game directly from inside Coconut with every
// scene edit, transform change, material/texture edit, asset replacement, and hex patch applied!
class GameRebuilder {
public:
    static std::vector<PendingGameChange> CollectPendingChanges(
        const unity::LoadedGameProject& project,
        const scene::SceneDocument* sceneDoc = nullptr);

    static Result<GameRebuildReport> BuildPatchedGame(
        unity::LoadedGameProject& project,
        scene::SceneDocument* sceneDoc,
        const GameRebuildOptions& options,
        std::function<void(const std::string&)> onLog = nullptr);
};

} // namespace coconut::exporter
