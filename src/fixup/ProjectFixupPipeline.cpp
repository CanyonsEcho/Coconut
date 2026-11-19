#include "fixup/ProjectFixupPipeline.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <sstream>

namespace coconut::fixup {

namespace {

std::string BuildMarkdownReport(const FixupPipelineReport& r, const FixupPipelineOptions& opts) {
    std::ostringstream md;
    md << "# Coconut Unity Project Recovery & Fixup Report\n\n";
    md << "- **Output Directory**: `" << r.outputProjectDir.string() << "`\n";
    md << "- **Target Unity Version**: `" << opts.targetUnityVersion << "`\n";
    md << "- **Export Backend**: " << r.backendUsed << "\n\n";

    md << "## 1. Export Summary\n";
    md << "- Scenes exported: **" << r.exportReport.scenesExported << "**\n";
    md << "- Scripts decompiled: **" << r.exportReport.scriptsExported << "**\n";
    md << "- Meshes exported: **" << r.exportReport.meshesExported << "**\n";
    md << "- Textures exported: **" << r.exportReport.texturesExported << "**\n";
    md << "- Materials exported: **" << r.exportReport.materialsExported << "**\n";
    md << "- Audio clips exported: **" << r.exportReport.audioExported << "**\n";
    md << "- `.meta` files written: **" << r.exportReport.metaFilesGenerated << "**\n\n";

    md << "## 2. Static-Batch Mesh Decombiner\n";
    md << "- Combined meshes detected: **" << r.meshDecombine.combinedMeshesFound << "**\n";
    md << "- Standalone local-space meshes recovered: **" << r.meshDecombine.meshesSplit << "**\n";
    md << "- Scene `MeshFilter` references rewired: **" << r.meshDecombine.sceneReferencesRewired << "**\n";
    for (const auto& e : r.meshDecombine.entries) {
        md << "  - `" << e.gameObjectName << "` -> `" << e.outputMeshPath.filename().string()
           << "` (" << e.vertexCount << " verts, " << e.triangleCount << " tris)\n";
    }
    md << "\n";

    md << "## 3. Prefab Reconstructor\n";
    md << "- Prefabs reconstructed: **" << r.prefabReconstruct.prefabsCreated << "**\n";
    md << "- Scene `PrefabInstance` links emitted: **" << r.prefabReconstruct.sceneInstancesLinked << "**\n";
    for (const auto& p : r.prefabReconstruct.prefabs) {
        md << "  - `" << p.prefabName << ".prefab` (" << p.instanceCount << " instances, GUID `" << p.guid << "`)\n";
    }
    md << "\n";

    md << "## 4. Automated Build Error Fixer\n";
    md << "- C# scripts scanned: **" << r.buildFix.scriptsScanned << "**\n";
    md << "- C# scripts patched: **" << r.buildFix.scriptsFixed << "**\n";
    md << "- Compiler-generated stubs stripped: **" << r.buildFix.compilerGeneratedStripped << "**\n";
    md << "- Unity API version migrations: **" << r.buildFix.unityApiMigrations << "**\n";
    md << "- Invalid identifiers sanitized: **" << r.buildFix.invalidIdentifiersFixed << "**\n";
    md << "- Broken shaders repaired: **" << r.buildFix.shadersRepaired << "**\n";
    md << "- Missing `.meta` files generated: **" << r.buildFix.missingMetasCreated << "**\n";
    md << "- `Assets/csc.rsp` (`-unsafe`) generated: **" << (r.buildFix.cscUnsafeGenerated ? "Yes" : "No") << "**\n";
    if (!r.buildFix.packagesAdded.empty()) {
        md << "- UPM packages added to `Packages/manifest.json`:\n";
        for (const auto& pkg : r.buildFix.packagesAdded) {
            md << "  - `" << pkg << "`\n";
        }
    }
    return md.str();
}

} // namespace

Result<FixupPipelineReport> ProjectFixupPipeline::ExportAndFixup(
    unity::LoadedGameProject& project,
    assets::AssetManager& assets,
    const decompiler::DecompilerEngine& decompiler,
    const scene::SceneDocument& sceneDoc,
    const ProjectDatabase* db,
    const std::filesystem::path& outputDir,
    const FixupPipelineOptions& options,
    std::function<void(const std::string&)> onLog) {
    FixupPipelineReport report;
    report.outputProjectDir = outputDir;

    auto log = [&](const std::string& msg) {
        COCO_LOG_INFO("Pipeline", msg);
        if (onLog) onLog(msg);
    };

    bool exportedViaRipper = false;
    if (options.preferAssetRipper && !project.rootPath.empty()) {
        exporter::AssetRipperConfig arCfg;
        arCfg.executablePath = options.assetRipperExePath;
        arCfg.port = options.assetRipperPort;
        arCfg.targetUnityVersion = options.targetUnityVersion;
        auto arRes = exporter::AssetRipperClient::ExportViaAssetRipper(project.rootPath, outputDir, arCfg, onLog);
        if (arRes) {
            exportedViaRipper = true;
            report.backendUsed = "AssetRipper";
            log("AssetRipper export succeeded — proceeding to automated Coconut fixup passes.");
        } else {
            log("AssetRipper not running locally — using Coconut Native Unity Project Exporter.");
        }
    }

    if (!exportedViaRipper) {
        report.backendUsed = "Coconut Native Exporter";
        exporter::UnityExportOptions expOpts;
        expOpts.targetUnityVersion = options.targetUnityVersion;
        auto expRes = exporter::UnityProjectExporter::ExportProject(
            project, assets, decompiler, sceneDoc, db, outputDir, expOpts, onLog);
        if (!expRes) {
            return Result<FixupPipelineReport>::Err("Unity project export failed: " + expRes.Error());
        }
        report.exportReport = std::move(expRes.Value());
    }

    auto fixRes = FixupExistingProject(outputDir, options, &sceneDoc, &assets, onLog);
    if (!fixRes) return Result<FixupPipelineReport>::Err(fixRes.Error());

    report.meshDecombine = std::move(fixRes.Value().meshDecombine);
    report.prefabReconstruct = std::move(fixRes.Value().prefabReconstruct);
    report.buildFix = std::move(fixRes.Value().buildFix);
    report.markdownReport = BuildMarkdownReport(report, options);
    platform::WriteFileText(outputDir / "COCONUT_FIXUP_REPORT.md", report.markdownReport);
    log("Pipeline complete! Report written to COCONUT_FIXUP_REPORT.md");
    return Result<FixupPipelineReport>::Ok(std::move(report));
}

Result<FixupPipelineReport> ProjectFixupPipeline::FixupExistingProject(
    const std::filesystem::path& unityProjectDir,
    const FixupPipelineOptions& options,
    const scene::SceneDocument* liveScene,
    assets::AssetManager* liveAssets,
    std::function<void(const std::string&)> onLog) {
    FixupPipelineReport report;
    report.outputProjectDir = unityProjectDir;
    report.backendUsed = "Existing Unity Project";

    auto log = [&](const std::string& msg) {
        COCO_LOG_INFO("Fixup", msg);
        if (onLog) onLog(msg);
    };

    if (options.runMeshDecombiner) {
        log("Running Pass 1/3: Static-Batch Mesh Decombiner...");
        auto mdRes = MeshDecombiner::RunOnProject(unityProjectDir, liveScene, liveAssets);
        if (mdRes) report.meshDecombine = std::move(mdRes.Value());
    }
    if (options.runPrefabReconstructor) {
        log("Running Pass 2/3: Prefab Hierarchy Reconstructor...");
        auto prRes = PrefabReconstructor::RunOnProject(unityProjectDir, liveScene);
        if (prRes) report.prefabReconstruct = std::move(prRes.Value());
    }
    if (options.runBuildErrorFixer) {
        log("Running Pass 3/3: Automated Build Error Fixer (Target Unity " + options.targetUnityVersion + ")...");
        auto bfRes = BuildErrorFixer::RunOnProject(unityProjectDir, options.targetUnityVersion);
        if (bfRes) report.buildFix = std::move(bfRes.Value());
    }

    report.markdownReport = BuildMarkdownReport(report, options);
    platform::WriteFileText(unityProjectDir / "COCONUT_FIXUP_REPORT.md", report.markdownReport);
    return Result<FixupPipelineReport>::Ok(std::move(report));
}

} // namespace coconut::fixup
