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
    md << "- **Target Unity Version**: `" << (r.resolvedUnityVersion.empty() ? opts.targetUnityVersion : r.resolvedUnityVersion) << "`\n";
    md << "- **Export Backend**: " << r.backendUsed << "\n";
    md << "- **Formats**: Mesh=`" << opts.meshFormat << "`, Audio=`" << opts.audioFormat
       << "`, Image=`" << opts.imageFormat << "`, Scripts=`" << opts.scriptMode << "`\n\n";

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
    md << "- Scene `MeshFilter` references rewired: **" << r.meshDecombine.sceneReferencesRewired << "**\n\n";

    md << "## 3. Prefab Reconstructor\n";
    md << "- Prefabs reconstructed: **" << r.prefabReconstruct.prefabsCreated << "**\n";
    md << "- Scene `PrefabInstance` links emitted: **" << r.prefabReconstruct.sceneInstancesLinked << "**\n\n";

    md << "## 4. Automated Build Error Fixer\n";
    md << "- C# scripts scanned: **" << r.buildFix.scriptsScanned << "**\n";
    md << "- C# scripts patched: **" << r.buildFix.scriptsFixed << "**\n";
    md << "- Unity API version migrations: **" << r.buildFix.unityApiMigrations << "**\n";
    md << "- Broken shaders repaired: **" << r.buildFix.shadersRepaired << "**\n";
    md << "- Missing `.meta` files generated: **" << r.buildFix.missingMetasCreated << "**\n";
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
    std::string resolvedVer = (options.targetUnityVersion.empty() || options.targetUnityVersion == "Match Source")
                                  ? (project.unityVersion.IsValid() ? project.unityVersion.ToString() : "2022.3.15f1")
                                  : options.targetUnityVersion;
    report.resolvedUnityVersion = resolvedVer;

    auto log = [&](const std::string& msg) {
        COCO_LOG_INFO("Pipeline", msg);
        if (onLog) onLog(msg);
    };

    bool exportedViaInternalRipper = false;
    std::filesystem::path fixupTargetDir = outputDir;

    if (options.useInternalAssetRipper && !project.rootPath.empty()) {
        exporter::AssetRipperConfig arCfg;
        arCfg.executablePath     = options.internalRipperExePath;
        arCfg.targetUnityVersion = options.targetUnityVersion;
        arCfg.meshExportFormat   = options.meshFormat;
        arCfg.audioExportFormat  = options.audioFormat;
        arCfg.imageExportFormat  = options.imageFormat;
        arCfg.scriptExportMode   = options.scriptMode;
        arCfg.decombineMeshes    = options.runMeshDecombiner;
        arCfg.reconstructPrefabs = options.runPrefabReconstructor;
        arCfg.fixBuildErrors     = options.runBuildErrorFixer;

        auto arRes = exporter::AssetRipperClient::ExportViaInternalRipper(project.rootPath, outputDir, arCfg, onLog);
        if (arRes) {
            const auto& r = arRes.Value();
            exportedViaInternalRipper = true;
            report.backendUsed = "Coconut Internal AssetRipper Core (1.3.14 Fork)";
            fixupTargetDir = r.exportedProjectDir.empty() ? outputDir : r.exportedProjectDir;
            report.outputProjectDir = fixupTargetDir;
            report.exportReport.projectDir       = fixupTargetDir;
            report.exportReport.unityVersion     = r.targetUnityVersion.empty() ? resolvedVer : r.targetUnityVersion;
            report.exportReport.scenesExported   = r.scenesExported;
            report.exportReport.scriptsExported  = r.scriptsExported;
            report.exportReport.meshesExported   = r.meshesExported;
            report.exportReport.audioExported    = r.audioExported;
            report.exportReport.texturesExported = r.texturesExported;
            report.meshDecombine.meshesSplit     = r.meshesDecombined;
            report.prefabReconstruct.prefabsCreated = r.prefabsReconstructed;
            report.buildFix.scriptsFixed         = r.buildErrorsFixed;
            report.buildFix.shadersRepaired      = r.shadersFixed;
            log("Internal AssetRipper fork export succeeded — running supplementary C++ verification pass.");
        } else {
            log("Internal AssetRipper fork unavailable or skipped (" + arRes.Error() +
                ") — using Coconut Native C++ Exporter.");
        }
    }

    if (!exportedViaInternalRipper) {
        report.backendUsed = "Coconut Native C++ Exporter";
        exporter::UnityExportOptions expOpts;
        expOpts.targetUnityVersion = resolvedVer;
        expOpts.meshFormat  = options.meshFormat;
        expOpts.audioFormat = options.audioFormat;
        expOpts.imageFormat = options.imageFormat;

        auto expRes = exporter::UnityProjectExporter::ExportProject(
            project, assets, decompiler, sceneDoc, db, outputDir, expOpts, onLog);
        if (!expRes) {
            return Result<FixupPipelineReport>::Err("Unity project export failed: " + expRes.Error());
        }
        report.exportReport = std::move(expRes.Value());
        fixupTargetDir = outputDir;
    }

    FixupPipelineOptions fixOpts = options;
    fixOpts.targetUnityVersion = resolvedVer;
    auto fixRes = FixupExistingProject(fixupTargetDir, fixOpts, &sceneDoc, &assets, onLog);
    if (fixRes) {
        report.meshDecombine.combinedMeshesFound     = std::max(report.meshDecombine.combinedMeshesFound, fixRes.Value().meshDecombine.combinedMeshesFound);
        report.meshDecombine.meshesSplit             += fixRes.Value().meshDecombine.meshesSplit;
        report.meshDecombine.sceneReferencesRewired  += fixRes.Value().meshDecombine.sceneReferencesRewired;
        report.meshDecombine.entries                  = std::move(fixRes.Value().meshDecombine.entries);

        report.prefabReconstruct.prefabsCreated       = std::max(report.prefabReconstruct.prefabsCreated, fixRes.Value().prefabReconstruct.prefabsCreated);
        report.prefabReconstruct.sceneInstancesLinked += fixRes.Value().prefabReconstruct.sceneInstancesLinked;
        report.prefabReconstruct.prefabs              = std::move(fixRes.Value().prefabReconstruct.prefabs);

        report.buildFix.scriptsScanned            += fixRes.Value().buildFix.scriptsScanned;
        report.buildFix.scriptsFixed              += fixRes.Value().buildFix.scriptsFixed;
        report.buildFix.compilerGeneratedStripped += fixRes.Value().buildFix.compilerGeneratedStripped;
        report.buildFix.unityApiMigrations        += fixRes.Value().buildFix.unityApiMigrations;
        report.buildFix.invalidIdentifiersFixed   += fixRes.Value().buildFix.invalidIdentifiersFixed;
        report.buildFix.shadersRepaired           += fixRes.Value().buildFix.shadersRepaired;
        report.buildFix.missingMetasCreated       += fixRes.Value().buildFix.missingMetasCreated;
        report.buildFix.cscUnsafeGenerated        |= fixRes.Value().buildFix.cscUnsafeGenerated;
        report.buildFix.packagesAdded              = std::move(fixRes.Value().buildFix.packagesAdded);
    }

    report.markdownReport = BuildMarkdownReport(report, options);
    platform::WriteFileText(fixupTargetDir / "COCONUT_FIXUP_REPORT.md", report.markdownReport);
    log("Export & Fixup complete! Output: " + fixupTargetDir.string());
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
    report.resolvedUnityVersion = options.targetUnityVersion;

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
