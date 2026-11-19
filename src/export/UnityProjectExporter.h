#pragma once

#include "core/Types.h"
#include "core/ProjectDatabase.h"
#include "unity/GameLoader.h"
#include "assets/AssetManager.h"
#include "decompiler/DecompilerEngine.h"
#include "scene/SceneDocument.h"

#include <filesystem>
#include <functional>
#include <string>

namespace coconut::exporter {

struct UnityExportOptions {
    std::string targetUnityVersion = "2022.3.15f1"; // Any Unity version: 5.6 .. 2022.3 LTS .. 6000.x
    bool exportScenes = true;
    bool exportScripts = true;
    bool exportMeshes = true;
    bool exportTextures = true;
    bool exportMaterials = true;
    bool exportAudio = true;
    bool exportTextAssets = true;
    bool generateMetaFiles = true;
    bool generateProjectSettings = true;
};

struct UnityExportReport {
    std::filesystem::path projectDir;
    std::string unityVersion;
    size_t scenesExported = 0;
    size_t scriptsExported = 0;
    size_t meshesExported = 0;
    size_t texturesExported = 0;
    size_t materialsExported = 0;
    size_t audioExported = 0;
    size_t textAssetsExported = 0;
    size_t metaFilesGenerated = 0;
};

// Native C++ full Unity Project Exporter supporting any target Unity version (5.x through Unity 6 / 6000.x).
class UnityProjectExporter {
public:
    static std::string DeterministicGuid(std::string_view key);

    static Result<UnityExportReport> ExportProject(
        unity::LoadedGameProject& project,
        assets::AssetManager& assets,
        const decompiler::DecompilerEngine& decompiler,
        const scene::SceneDocument& sceneDoc,
        const ProjectDatabase* db,
        const std::filesystem::path& outputDir,
        const UnityExportOptions& options = {},
        std::function<void(const std::string&)> onLog = nullptr);

    static Result<void> WriteMetaFile(const std::filesystem::path& assetPath, const std::string& guid, std::string_view importerKind);
};

} // namespace coconut::exporter
