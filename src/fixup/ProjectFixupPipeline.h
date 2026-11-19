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
    std::string targetUnityVersion = "Match Source";
    std::string meshFormat = "fbx";      // "fbx" | "glb" | "obj" | "native"
    std::string audioFormat = "wav";     // "wav" | "mp3" | "m4a" | "ogg" | "native"
    std::string imageFormat = "png";     // "png" | "tga" | "dds"
    std::string scriptMode = "hybrid";   // "decompiled" | "hybrid" | "stubs"

    bool useInternalAssetRipper = true;
    std::filesystem::path internalRipperExePath;

    bool runMeshDecombiner = true;
    bool runPrefabReconstructor = true;
    bool runBuildErrorFixer = true;
};

struct FixupPipelineReport {
    std::filesystem::path outputProjectDir;
    std::string backendUsed; // "Coconut Internal AssetRipper Core (1.3.14 Fork)" or "Coconut Native Exporter"
    std::string resolvedUnityVersion;
    exporter::UnityExportReport exportReport;
    MeshDecombineResult meshDecombine;
    PrefabReconstructResult prefabReconstruct;
    BuildFixReport buildFix;
    std::string markdownReport;
};

class ProjectFixupPipeline {
public:
    // Full one-click Decompile -> Export Unity Project (Internal Forked AssetRipper or Native C++) -> Fixup Pipeline
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
