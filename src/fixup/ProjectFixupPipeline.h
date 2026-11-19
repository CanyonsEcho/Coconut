#pragma once

#include "core/Types.h"
#include "core/ProjectDatabase.h"
#include "unity/GameLoader.h"
#include "assets/AssetManager.h"
#include "decompiler/DecompilerEngine.h"
#include "scene/SceneDocument.h"
#include "export/AssetRipperClient.h"
#include "export/UnityProjectExporter.h"
#include "fixup/MeshDecombiner.h"
#include "fixup/PrefabReconstructor.h"
#include "fixup/BuildErrorFixer.h"

#include <filesystem>
#include <functional>
#include <string>

namespace coconut::fixup {

struct FixupPipelineOptions {
    std::string targetUnityVersion = "2022.3.15f1";
    bool preferAssetRipper = true;
    std::filesystem::path assetRipperExePath;
    u16 assetRipperPort = 52890;

    bool runMeshDecombiner = true;
    bool runPrefabReconstructor = true;
    bool runBuildErrorFixer = true;
};

struct FixupPipelineReport {
    std::filesystem::path outputProjectDir;
    std::string backendUsed; // "AssetRipper" or "Coconut Native Exporter"
    exporter::UnityExportReport exportReport;
    MeshDecombineResult meshDecombine;
    PrefabReconstructResult prefabReconstruct;
    BuildFixReport buildFix;
    std::string markdownReport;
};

class ProjectFixupPipeline {
public:
    // Full one-click Decompile -> Export Unity Project (via AssetRipper or native) -> Fixup Pipeline
    static Result<FixupPipelineReport> ExportAndFixup(
        unity::LoadedGameProject& project,
        assets::AssetManager& assets,
        const decompiler::DecompilerEngine& decompiler,
        const scene::SceneDocument& sceneDoc,
        const ProjectDatabase* db,
        const std::filesystem::path& outputDir,
        const FixupPipelineOptions& options = {},
        std::function<void(const std::string&)> onLog = nullptr);

    // Run only the automated fixup passes on an already-exported Unity project directory
    static Result<FixupPipelineReport> FixupExistingProject(
        const std::filesystem::path& unityProjectDir,
        const FixupPipelineOptions& options = {},
        const scene::SceneDocument* liveScene = nullptr,
        assets::AssetManager* liveAssets = nullptr,
        std::function<void(const std::string&)> onLog = nullptr);
};

} // namespace coconut::fixup
