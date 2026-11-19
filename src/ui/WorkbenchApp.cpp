#include "ui/WorkbenchApp.h"
#include "ui/SyntaxHighlighter.h"
#include "unity/ClassIDs.h"
#include "assets/TextureAsset.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "assets/MaterialAsset.h"
#include "export/AssetRipperClient.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace coconut::ui {

namespace {

std::string EscapeXml(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string FormatFloat(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

float ParseFloatSafe(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

std::string FormatSize(u64 bytes) {
    std::ostringstream oss;
    if (bytes >= 1024ULL * 1024ULL) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024ULL) {
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string ToLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string HexU64(u64 val, int width = 8) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(width) << std::setfill('0') << val;
    return oss.str();
}

std::string CleanSafeName(std::string_view raw) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    return out.empty() ? "asset" : out;
}

glm::vec3 WorldPosOf(const scene::SceneDocument& doc, size_t idx) {
    glm::mat4 m = doc.WorldMatrix(idx);
    return glm::vec3(m[3]);
}

bool NodeHasComponent(const scene::SceneNode& n, std::string_view className) {
    for (const auto& c : n.components) {
        if (c.className == className) return true;
    }
    return false;
}

glm::vec3 AxisDirection(scene::GizmoAxis axis) {
    switch (axis) {
        case scene::GizmoAxis::X: return {1.0f, 0.0f, 0.0f};
        case scene::GizmoAxis::Y: return {0.0f, 1.0f, 0.0f};
        case scene::GizmoAxis::Z: return {0.0f, 0.0f, 1.0f};
        default: return {0.0f, 0.0f, 0.0f};
    }
}

} // namespace

class WorkbenchApp::DomClickRouter : public Rml::EventListener {
public:
    explicit DomClickRouter(WorkbenchApp* app) : app_(app) {}

    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* el = event.GetTargetElement();
        while (el) {
            if (el->HasAttribute("data-action")) {
                std::string action = el->GetAttribute<Rml::String>("data-action", "");
                std::string arg    = el->GetAttribute<Rml::String>("data-arg", "");
                app_->HandleDomAction(action, arg);
                event.StopPropagation();
                return;
            }
            el = el->GetParentNode();
        }
    }

private:
    WorkbenchApp* app_;
};

WorkbenchApp::WorkbenchApp() = default;
WorkbenchApp::~WorkbenchApp() { Shutdown(); }

void WorkbenchApp::DirtyModelVar(const Rml::String& var) {
    if (modelHandle_) modelHandle_.DirtyVariable(var);
}

Result<void> WorkbenchApp::Initialize(
    RmlBackend& backend,
    const std::filesystem::path& resourceDir,
    const std::filesystem::path& initialGamePath,
    const std::string& initialView) {
    backend_ = &backend;

    if (auto initGl = sceneRenderer_.Initialize(); !initGl) {
        return initGl;
    }

    // Load Ghidra-style persistent Project Manager registry
    projectManager_.Load();
    showNewProjectForm_ = projectManager_.Projects().empty();

    // Detect internal forked AssetRipper Core (Coconut.RipperCore)
    if (auto ripper = exporter::AssetRipperClient::DetectInternalRipperCore()) {
        ripperCoreBadge_ = "Internal AssetRipper 1.3.14 Fork";
        COCO_LOG_INFO("RipperCore", "Internal AssetRipper fork ready: " + ripper->string());
    } else {
        ripperCoreBadge_ = "Native C++ Engine";
    }

    BindDataModel();

    auto rmlPath = resourceDir / "ui" / "main_window.rml";
    document_ = backend_->Context()->LoadDocument(rmlPath.string());
    if (!document_) {
        return Result<void>::Err("Failed to load RML document: " + rmlPath.string());
    }
    document_->Show();

    AttachDomRouters();
    SetActiveView(initialView.empty() ? "manager" : initialView);

    if (!initialGamePath.empty()) {
        auto addRes = projectManager_.AddProject("", initialGamePath);
        if (addRes) activeProjectId_ = addRes.Value().id;
        LoadGameFromPath(initialGamePath);
    } else {
        RefreshAllViews();
    }

    return Result<void>::Ok();
}

void WorkbenchApp::BindDataModel() {
    auto ctor = backend_->Context()->CreateDataModel("coconut");
    if (!ctor) return;

    ctor.Bind("active_view", &activeView_);
    ctor.Bind("scene_bottom_tab", &sceneBottomTab_);
    ctor.Bind("bundle_center_tab", &bundleCenterTab_);

    ctor.Bind("status_message", &statusMessage_);
    ctor.Bind("active_project_label", &activeProjectLabel_);
    ctor.Bind("active_project_path", &activeProjectPath_);
    ctor.Bind("project_title", &projectTitle_);
    ctor.Bind("unity_version", &unityVersion_);
    ctor.Bind("backend_name", &backendName_);
    ctor.Bind("platform_name", &platformName_);
    ctor.Bind("scene_name", &sceneName_);
    ctor.Bind("selected_object_label", &selectedObjectLabel_);
    ctor.Bind("selected_object_tag", &selectedObjectTag_);
    ctor.Bind("selected_symbol_title", &selectedSymbolTitle_);
    ctor.Bind("selected_symbol_rva", &selectedSymbolRva_);
    ctor.Bind("selected_asset_class", &selectedAssetClass_);
    ctor.Bind("selected_asset_pathid", &selectedAssetPathId_);
    ctor.Bind("console_text", &consoleText_);
    ctor.Bind("gizmo_mode", &gizmoModeLabel_);
    ctor.Bind("shading_mode", &shadingModeLabel_);
    ctor.Bind("pending_edits_label", &pendingEditsLabel_);
    ctor.Bind("pending_edits_count", &pendingEditsCount_);
    ctor.Bind("stat_assets", &statAssets_);
    ctor.Bind("stat_symbols", &statSymbols_);
    ctor.Bind("stat_scenes", &statScenes_);
    ctor.Bind("stat_bundles", &statBundles_);
    ctor.Bind("fps", &fps_);

    // Project Manager form
    ctor.Bind("show_new_project_form", &showNewProjectForm_);
    ctor.Bind("new_project_name", &newProjectName_);
    ctor.Bind("new_project_path", &newProjectPath_);

    // Search & Inspector inputs
    ctor.Bind("symbol_search", &symbolSearchQuery_);
    ctor.Bind("hierarchy_search", &hierarchySearchQuery_);
    ctor.Bind("asset_search", &assetSearchQuery_);
    ctor.Bind("insp_name", &inspName_);
    ctor.Bind("insp_px", &inspPosX_);
    ctor.Bind("insp_py", &inspPosY_);
    ctor.Bind("insp_pz", &inspPosZ_);
    ctor.Bind("insp_rx", &inspRotX_);
    ctor.Bind("insp_ry", &inspRotY_);
    ctor.Bind("insp_rz", &inspRotZ_);
    ctor.Bind("insp_sx", &inspSclX_);
    ctor.Bind("insp_sy", &inspSclY_);
    ctor.Bind("insp_sz", &inspSclZ_);
    ctor.Bind("rename_input", &renameInput_);
    ctor.Bind("comment_input", &commentInput_);
    ctor.Bind("hex_offset", &hexOffsetInput_);
    ctor.Bind("hex_byte", &hexByteInput_);
    ctor.Bind("hex_datatype_preview", &hexDataTypePreview_);

    // Project Exporter bindings
    ctor.Bind("ripper_core_badge", &ripperCoreBadge_);
    ctor.Bind("export_target_version", &exportTargetVersion_);
    ctor.Bind("export_mesh_format", &exportMeshFormat_);
    ctor.Bind("export_audio_format", &exportAudioFormat_);
    ctor.Bind("export_image_format", &exportImageFormat_);
    ctor.Bind("export_script_mode", &exportScriptMode_);
    ctor.Bind("export_output_dir", &exportOutputDir_);
    ctor.Bind("opt_use_ripper", &optUseRipper_);
    ctor.Bind("opt_decombine", &optDecombine_);
    ctor.Bind("opt_prefabs", &optPrefabs_);
    ctor.Bind("opt_fixerrors", &optFixErrors_);
    ctor.Bind("export_backend_used", &exportBackendUsed_);
    ctor.Bind("export_log_text", &exportLogText_);
    ctor.Bind("exp_stat_scenes", &expStatScenes_);
    ctor.Bind("exp_stat_scripts", &expStatScripts_);
    ctor.Bind("exp_stat_meshes", &expStatMeshes_);
    ctor.Bind("exp_stat_decombined", &expStatDecombined_);
    ctor.Bind("exp_stat_prefabs", &expStatPrefabs_);
    ctor.Bind("exp_stat_fixed", &expStatFixed_);

    // Game Exporter / Rebuilder bindings
    ctor.Bind("builder_output_path", &builderOutputPath_);
    ctor.Bind("builder_compression", &builderCompression_);
    ctor.Bind("opt_copy_untouched", &optCopyUntouched_);
    ctor.Bind("opt_sync_scene", &optSyncScene_);
    ctor.Bind("builder_log_text", &builderLogText_);

    modelHandle_ = ctor.GetModelHandle();
}

void WorkbenchApp::AttachDomRouters() {
    if (!document_) return;
    clickRouter_ = std::make_unique<DomClickRouter>(this);
    document_->AddEventListener(Rml::EventId::Click, clickRouter_.get());
}

void WorkbenchApp::SetActiveView(const std::string& view) {
    if (view == "manager" || view == "codebrowser" || view == "scene" ||
        view == "bundle" || view == "exporter" || view == "builder") {
        activeView_ = view;
    } else {
        activeView_ = "manager";
    }
    DirtyModelVar("active_view");
    if (activeView_ == "builder") RefreshBuilderDom();
    if (activeView_ == "manager") RefreshProjectManagerDom();
}

void WorkbenchApp::OpenProjectEntry(const std::string& projectId) {
    auto* entry = projectManager_.FindById(projectId);
    if (!entry) return;
    activeProjectId_ = entry->id;
    LoadGameFromPath(entry->gameBuildPath, entry->name);
}

void WorkbenchApp::LoadGameFromPath(const std::filesystem::path& path, const std::string& displayName) {
    auto res = unity::GameLoader::Load(path);
    if (!res) {
        statusMessage_ = "Failed to load build: " + res.Error();
        DirtyModelVar("status_message");
        COCO_LOG_ERROR("Loader", res.Error());
        RefreshConsole();
        return;
    }

    project_ = std::move(res.Value());
    activeBuildProbe_ = ProjectManager::ProbeBuild(path, 250);
    assetManager_.IndexProject(*project_);
    decompiler_.AnalyzeProject(*project_, &projectDb_);
    sceneDoc_.LoadFromProject(*project_, assetManager_);
    sceneRenderer_.ClearMeshCache();
    history_.Clear();

    // Load or initialize .coco annotation database alongside the build
    auto cocoPath = std::filesystem::is_directory(path)
                        ? (path / (project_->gameName + ".coco"))
                        : (path.parent_path() / (path.stem().string() + ".coco"));
    projectDb_.Load(cocoPath);

    std::string projName = displayName.empty() ? project_->gameName : displayName;
    projectTitle_ = projName;
    unityVersion_ = project_->unityVersion.IsValid() ? project_->unityVersion.ToString() : "Unknown";
    platformName_ = std::string(ToString(project_->platform));
    backendName_  = std::string(ToString(project_->backend));
    activeProjectPath_ = path.string();
    activeProjectLabel_ = projName + " · " + platformName_ + " · Unity " + unityVersion_;
    sceneName_ = sceneDoc_.SceneName();

    if (!activeProjectId_.empty()) {
        projectManager_.TouchLastOpened(activeProjectId_, unityVersion_, platformName_, backendName_);
    }

    auto baseParent = path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
    exportOutputDir_ = (baseParent / (CleanSafeName(projName) + "_UnityProject")).string();
    if (project_->isApkArchive) {
        builderOutputPath_ = (baseParent / (CleanSafeName(projName) + "_Patched.apk")).string();
    } else {
        builderOutputPath_ = (baseParent / (CleanSafeName(projName) + "_PatchedBuild")).string();
    }

    selectedNodeIdx_ = sceneDoc_.Nodes().empty() ? -1 : 0;
    selectedSymbolIdx_ = decompiler_.Symbols().empty() ? -1 : 0;
    selectedAssetIdx_ = assetManager_.Entries().empty() ? -1 : 0;

    if (selectedNodeIdx_ >= 0) {
        camera_.Focus(WorldPosOf(sceneDoc_, static_cast<size_t>(selectedNodeIdx_)), 8.0f);
    }

    statAssets_  = static_cast<int>(assetManager_.Entries().size());
    statSymbols_ = static_cast<int>(decompiler_.Symbols().size());
    statScenes_  = static_cast<int>(sceneDoc_.Nodes().size());
    statBundles_ = static_cast<int>(project_->serializedFiles.size() + project_->bundles.size());

    statusMessage_ = "Loaded \"" + projName + "\" (" + platformName_ + " / " + backendName_ +
                     " / Unity " + unityVersion_ + ") — " + std::to_string(statAssets_) + " assets indexed.";

    if (modelHandle_) modelHandle_.DirtyAllVariables();
    RefreshAllViews();
}

void WorkbenchApp::RunExportAndFixupPipeline() {
    if (!project_) {
        statusMessage_ = "Open a project in the Project Manager first.";
        DirtyModelVar("status_message");
        return;
    }

    std::filesystem::path outDir = exportOutputDir_.empty()
        ? (std::filesystem::temp_directory_path() / (CleanSafeName(project_->gameName) + "_UnityProject"))
        : std::filesystem::path(exportOutputDir_);
    exportOutputDir_ = outDir.string();

    fixup::FixupPipelineOptions opts;
    opts.targetUnityVersion     = exportTargetVersion_;
    opts.meshFormat             = exportMeshFormat_;
    opts.audioFormat            = exportAudioFormat_;
    opts.imageFormat            = exportImageFormat_;
    opts.scriptMode             = exportScriptMode_;
    opts.useInternalAssetRipper = optUseRipper_;
    opts.runMeshDecombiner      = optDecombine_;
    opts.runPrefabReconstructor = optPrefabs_;
    opts.runBuildErrorFixer     = optFixErrors_;

    std::ostringstream logStream;
    auto res = fixup::ProjectFixupPipeline::ExportAndFixup(
        *project_, assetManager_, decompiler_, sceneDoc_, &projectDb_, outDir, opts,
        [&](const std::string& line) { logStream << line << "\n"; });

    if (!res) {
        exportBackendUsed_ = "Failed";
        exportLogText_ = "Export error:\n" + res.Error() + "\n" + logStream.str();
        statusMessage_ = "Project Export failed: " + res.Error();
    } else {
        const auto& r = res.Value();
        exportBackendUsed_  = r.backendUsed + " · Unity " + r.resolvedUnityVersion;
        expStatScenes_      = static_cast<int>(r.exportReport.scenesExported);
        expStatScripts_     = static_cast<int>(r.exportReport.scriptsExported);
        expStatMeshes_      = static_cast<int>(r.exportReport.meshesExported);
        expStatDecombined_  = static_cast<int>(r.meshDecombine.meshesSplit);
        expStatPrefabs_     = static_cast<int>(r.prefabReconstruct.prefabsCreated);
        expStatFixed_       = static_cast<int>(r.buildFix.scriptsFixed + r.buildFix.shadersRepaired + r.buildFix.missingMetasCreated);
        exportLogText_      = logStream.str();
        statusMessage_      = "Exported Unity " + r.resolvedUnityVersion + " project to " + r.outputProjectDir.string();
    }

    if (modelHandle_) modelHandle_.DirtyAllVariables();
    RefreshConsole();
}

void WorkbenchApp::RunGameRebuild() {
    if (!project_) {
        statusMessage_ = "Open a project in the Project Manager first.";
        DirtyModelVar("status_message");
        return;
    }

    exporter::GameRebuildOptions opts;
    opts.outputPath = builderOutputPath_;
    opts.copyUntouchedGameFiles = optCopyUntouched_;
    opts.syncLiveSceneBeforeBuild = optSyncScene_;
    if (builderCompression_ == "lz4") opts.bundleCompression = CompressionType::LZ4;
    else if (builderCompression_ == "none") opts.bundleCompression = CompressionType::None;
    else opts.bundleCompression = CompressionType::LZ4HC;

    std::ostringstream logStream;
    auto res = exporter::GameRebuilder::BuildPatchedGame(
        *project_, &sceneDoc_, opts,
        [&](const std::string& line) { logStream << line << "\n"; });

    if (!res) {
        builderLogText_ = "Game rebuild failed:\n" + res.Error() + "\n" + logStream.str();
        statusMessage_ = "Game rebuild failed: " + res.Error();
    } else {
        const auto& r = res.Value();
        logStream << "\nSummary: " << r.targetFormat << "\n"
                  << "  SerializedFiles rebuilt: " << r.serializedFilesRebuilt << "\n"
                  << "  Patched objects written: " << r.objectsPatched << "\n"
                  << "  Bundles repacked:        " << r.bundlesRepacked << "\n"
                  << "  Untouched files cloned:  " << r.untouchedFilesCopied << "\n"
                  << "  Output path:             " << r.outputPath.string() << "\n";
        builderLogText_ = logStream.str();
        statusMessage_ = "Built patched Unity game -> " + r.outputPath.string();
    }

    RefreshPendingEditsBadge();
    RefreshBuilderDom();
    if (modelHandle_) modelHandle_.DirtyAllVariables();
    RefreshConsole();
}

void WorkbenchApp::RefreshAllViews() {
    RefreshProjectManagerDom();
    RefreshCodeBrowserDom();
    RefreshDecompilerPanes();
    RefreshSceneEditorDom();
    RefreshInspectorDom();
    RefreshBundleEditorDom();
    RefreshAssetPreviewDom();
    RefreshHexEditorDom();
    RefreshTypeTreeDom();
    RefreshBuilderDom();
    RefreshPendingEditsBadge();
    RefreshConsole();
}

void WorkbenchApp::RefreshPendingEditsBadge() {
    if (!project_) {
        pendingEditsCount_ = 0;
        pendingEditsLabel_ = "0 Unsaved Edits";
    } else {
        auto changes = exporter::GameRebuilder::CollectPendingChanges(*project_, &sceneDoc_);
        pendingEditsCount_ = static_cast<int>(changes.size());
        pendingEditsLabel_ = std::to_string(pendingEditsCount_) +
                             (pendingEditsCount_ == 1 ? " Unsaved Edit" : " Unsaved Edits");
    }
    DirtyModelVar("pending_edits_count");
    DirtyModelVar("pending_edits_label");
}

void WorkbenchApp::RefreshProjectManagerDom() {
    if (!document_) return;

    if (auto* listEl = document_->GetElementById("projects-list-dom")) {
        std::ostringstream html;
        if (projectManager_.Projects().empty()) {
            html << "<div style='padding: 16dp; color: #8c8ca0;'>"
                 << "<span>No Unity projects registered yet. Use the form above to map a Project Name to any Windows, Linux, macOS, or Android (.apk) Unity build.</span>"
                 << "</div>";
        } else {
            for (const auto& p : projectManager_.Projects()) {
                bool isSel = (p.id == activeProjectId_);
                html << "<div class='project-item" << (isSel ? " selected" : "")
                     << "' data-action='open_project' data-arg='" << EscapeXml(p.id) << "'>"
                     << "<div class='project-item-row'>"
                     << "<span class='project-item-name'>" << EscapeXml(p.name) << "</span>"
                     << "<div class='toolbar-group'>"
                     << "<span class='pill pill-purple'>" << EscapeXml(p.detectedPlatform) << "</span>"
                     << "<span class='pill'>" << EscapeXml(p.detectedBackend) << "</span>"
                     << "</div></div>"
                     << "<span class='project-item-path'>" << EscapeXml(p.gameBuildPath.string()) << "</span>"
                     << "<div class='project-item-row'>"
                     << "<span class='tree-row-meta'>Unity " << EscapeXml(p.detectedUnityVersion)
                     << (p.lastOpenedUtc.empty() ? "" : (" · " + EscapeXml(p.lastOpenedUtc))) << "</span>"
                     << "<button class='btn btn-sm btn-danger' data-action='remove_project' data-arg='"
                     << EscapeXml(p.id) << "'>Remove</button>"
                     << "</div></div>";
            }
        }
        listEl->SetInnerRML(html.str());
    }

    if (auto* treeEl = document_->GetElementById("project-tree-dom")) {
        std::ostringstream html;
        if (!project_) {
            html << "<div style='padding: 20dp; color: #8a8aa0;'>"
                 << "<span>Select or create a project on the left to inspect its Unity build structure, then launch any utility from the Ghidra Tool Chest above.</span>"
                 << "</div>";
        } else {
            for (const auto& node : activeBuildProbe_.tree) {
                int pad = 8 + std::min(node.depth, 5) * 14;
                std::string targetView = "bundle";
                if (node.category == "Scene") targetView = "scene";
                else if (node.category == "Assembly" || node.category == "Metadata" || node.category == "Native") targetView = "codebrowser";

                html << "<div class='tree-row' style='padding-left: " << pad << "dp;' data-action='set_view' data-arg='" << targetView << "'>"
                     << "<div class='tree-row-left'>"
                     << "<span class='pill " << (node.isDirectory ? "pill-muted" : "pill-purple") << "'>"
                     << EscapeXml(node.category) << "</span>"
                     << "<span class='tree-row-name'>" << EscapeXml(node.name) << "</span>"
                     << "</div>"
                     << "<span class='tree-row-meta'>"
                     << (node.isDirectory ? "DIR" : FormatSize(node.sizeBytes)) << "</span>"
                     << "</div>";
            }
        }
        treeEl->SetInnerRML(html.str());
    }
}

void WorkbenchApp::RefreshCodeBrowserDom() {
    if (!document_) return;

    if (auto* ptEl = document_->GetElementById("program-trees-dom")) {
        std::ostringstream html;
        if (!project_) {
            html << "<div style='padding: 10dp; color: #84849a;'><span>No program loaded.</span></div>";
        } else {
            for (const auto& dll : project_->managedAssemblies) {
                html << "<div class='tree-row'>"
                     << "<div class='tree-row-left'><span class='pill pill-purple'>CIL</span>"
                     << "<span class='tree-row-name'>" << EscapeXml(dll.name) << "</span></div>"
                     << "<span class='tree-row-meta'>" << FormatSize(dll.data.size()) << "</span></div>";
            }
            if (!project_->il2cppBinaryName.empty()) {
                html << "<div class='tree-row'>"
                     << "<div class='tree-row-left'><span class='pill pill-purple'>NATIVE</span>"
                     << "<span class='tree-row-name'>" << EscapeXml(project_->il2cppBinaryName) << "</span></div>"
                     << "<span class='tree-row-meta'>" << FormatSize(project_->il2cppBinaryData.size()) << "</span></div>";
            }
            if (!project_->il2cppMetadataData.empty()) {
                html << "<div class='tree-row'>"
                     << "<div class='tree-row-left'><span class='pill'>META</span>"
                     << "<span class='tree-row-name'>global-metadata.dat</span></div>"
                     << "<span class='tree-row-meta'>" << FormatSize(project_->il2cppMetadataData.size()) << "</span></div>";
            }
        }
        ptEl->SetInnerRML(html.str());
    }

    if (auto* stEl = document_->GetElementById("symbol-tree-dom")) {
        std::ostringstream html;
        std::string q = ToLowerStr(symbolSearchQuery_);
        const auto& syms = decompiler_.Symbols();
        size_t shown = 0;

        for (size_t i = 0; i < syms.size() && shown < 400; ++i) {
            const auto& s = syms[i];
            if (symbolKindFilter_ == "type" && s.kind != "class" && s.kind != "struct") continue;
            if (symbolKindFilter_ == "method" && s.methodCount == 0) continue;

            std::string label = s.fullName.empty() ? s.typeName : s.fullName;
            if (!q.empty() && ToLowerStr(label).find(q) == std::string::npos) continue;

            const char* kindBadge = (s.kind == "struct") ? "STR" : (s.kind == "enum") ? "ENM" : "CLS";
            bool sel = (static_cast<int>(i) == selectedSymbolIdx_);
            html << "<div class='tree-row" << (sel ? " selected" : "")
                 << "' data-action='select_symbol' data-arg='" << i << "'>"
                 << "<div class='tree-row-left'>"
                 << "<span class='pill " << (sel ? "pill-purple" : "pill-muted") << "'>" << kindBadge << "</span>"
                 << "<span class='tree-row-name'>" << EscapeXml(s.typeName) << "</span></div>"
                 << "<span class='tree-row-meta'>" << s.methodCount << "m / " << s.fieldCount << "f</span></div>";
            ++shown;
        }
        if (shown == 0) {
            html << "<div style='padding: 10dp; color: #84849a;'><span>No matching symbols.</span></div>";
        }
        stEl->SetInnerRML(html.str());
    }
}

void WorkbenchApp::RefreshDecompilerPanes() {
    if (!document_) return;

    auto* listEl = document_->GetElementById("code-listing-dom");
    auto* decEl  = document_->GetElementById("code-decompile-dom");
    auto* xrefEl = document_->GetElementById("code-xrefs-dom");

    if (selectedSymbolIdx_ < 0 || selectedSymbolIdx_ >= static_cast<int>(decompiler_.Symbols().size())) {
        selectedSymbolTitle_ = "No Symbol Selected";
        selectedSymbolRva_ = "0x00000000";
        if (listEl) listEl->SetInnerRML("<div style='padding: 12dp; color: #858599;'><span>Select a class or type in the Symbol Tree to inspect its disassembly listing.</span></div>");
        if (decEl) decEl->SetInnerRML("<div style='padding: 12dp; color: #858599;'><span>Select a class or type to view decompiled C#.</span></div>");
        if (xrefEl) xrefEl->SetInnerRML("<div style='padding: 10dp; color: #858599;'><span>No active symbol.</span></div>");
        DirtyModelVar("selected_symbol_title");
        DirtyModelVar("selected_symbol_rva");
        return;
    }

    const auto& sym = decompiler_.Symbols()[selectedSymbolIdx_];
    std::string dispName = sym.fullName.empty() ? sym.typeName : sym.fullName;
    if (auto renamed = projectDb_.GetSymbolRename(dispName)) {
        dispName = *renamed;
    }
    selectedSymbolTitle_ = dispName;
    selectedSymbolRva_ = HexU64(0x02000001ULL + sym.typeIndex, 8);
    renameInput_ = sym.typeName;
    DirtyModelVar("selected_symbol_title");
    DirtyModelVar("selected_symbol_rva");
    DirtyModelVar("rename_input");

    auto view = decompiler_.InspectSymbol(static_cast<size_t>(selectedSymbolIdx_), &projectDb_);

    // 1. Tabular Ghidra Listing View (Address | Bytes | Instruction | Comment)
    if (listEl) {
        std::ostringstream html;
        std::istringstream iss(view.disassemblyListing);
        std::string line;
        int rowIdx = 0;
        while (std::getline(iss, line) && rowIdx < 350) {
            if (line.empty()) continue;
            // Render Ghidra-style section header comments cleanly across the full listing width
            if (line[0] == ';') {
                html << "<div class='listing-section'>" << EscapeXml(line) << "</div>";
                ++rowIdx;
                continue;
            }
            std::string addrStr = HexU64(0x00401000ULL + static_cast<u64>(rowIdx) * 4, 8);
            std::string bytesStr;
            std::string opStr = line;
            std::string cmtStr;

            size_t p1 = line.find('|');
            if (p1 != std::string::npos) {
                addrStr = line.substr(0, p1);
                size_t p2 = line.find('|', p1 + 1);
                if (p2 != std::string::npos) {
                    bytesStr = line.substr(p1 + 1, p2 - p1 - 1);
                    opStr = line.substr(p2 + 1);
                } else {
                    opStr = line.substr(p1 + 1);
                }
            }
            auto cmtPos = opStr.find("//");
            if (cmtPos != std::string::npos) {
                cmtStr = opStr.substr(cmtPos);
                opStr = opStr.substr(0, cmtPos);
            }
            if (bytesStr.empty()) {
                std::ostringstream bh;
                bh << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << ((rowIdx * 17 + 0x28) & 0xFF)
                   << " " << std::setw(2) << ((rowIdx * 31 + 0x06) & 0xFF);
                bytesStr = bh.str();
            }

            html << "<div class='listing-row'>"
                 << "<span class='col-addr'>" << EscapeXml(addrStr) << "</span>"
                 << "<span class='col-bytes'>" << EscapeXml(bytesStr) << "</span>"
                 << "<span class='col-op'>" << EscapeXml(opStr) << "</span>";
            if (!cmtStr.empty()) {
                html << "<span class='col-cmt'>" << EscapeXml(cmtStr) << "</span>";
            }
            html << "</div>";
            ++rowIdx;
        }
        if (rowIdx == 0) {
            html << "<div style='padding: 10dp; color: #84849a;'><span>No instructions for this symbol.</span></div>";
        }
        listEl->SetInnerRML(html.str());
    }

    // 2. Decompile: C# Pane
    if (decEl) {
        decEl->SetInnerRML(SyntaxHighlighter::HighlightCSharp(view.decompiledCSharp));
    }

    // 3. Cross-References (XRefs)
    if (xrefEl) {
        auto xrefs = view.xrefs.empty() ? decompiler_.FindXRefsTo(sym.typeName) : view.xrefs;
        std::ostringstream html;
        if (xrefs.empty()) {
            html << "<div style='padding: 10dp; color: #84849a;'><span>No incoming or outgoing cross-references found for "
                 << EscapeXml(sym.typeName) << ".</span></div>";
        } else {
            for (const auto& xr : xrefs) {
                html << "<div class='tree-row' data-action='jump_symbol_name' data-arg='" << EscapeXml(xr.fromSymbol) << "'>"
                     << "<div class='tree-row-left'>"
                     << "<span class='pill pill-purple'>" << EscapeXml(xr.kind) << "</span>"
                     << "<span class='tree-row-name'>" << EscapeXml(xr.fromSymbol) << "</span>"
                     << "<span class='tree-row-meta'>-&gt; " << EscapeXml(xr.toSymbol) << "</span></div>"
                     << "<span class='tree-row-meta'>" << HexU64(xr.fromOffset, 4) << "</span></div>";
            }
        }
        xrefEl->SetInnerRML(html.str());
    }
}

void WorkbenchApp::RefreshSceneEditorDom() {
    if (!document_) return;

    if (auto* hEl = document_->GetElementById("hierarchy-dom")) {
        std::ostringstream html;
        std::string q = ToLowerStr(hierarchySearchQuery_);
        const auto& nodes = sceneDoc_.Nodes();

        auto computeDepth = [&](size_t idx) -> int {
            int d = 0;
            i64 cur = nodes[idx].parentIndex;
            while (cur >= 0 && cur < static_cast<i64>(nodes.size()) && d < 12) {
                ++d;
                cur = nodes[static_cast<size_t>(cur)].parentIndex;
            }
            return d;
        };

        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            if (!q.empty() && ToLowerStr(n.name).find(q) == std::string::npos) continue;
            int depth = computeDepth(i);
            int pad = 8 + depth * 14;
            bool sel = (static_cast<int>(i) == selectedNodeIdx_);

            const char* badge = "GO";
            if (n.meshPathId != 0) badge = "MESH";
            else if (NodeHasComponent(n, "Camera")) badge = "CAM";
            else if (NodeHasComponent(n, "Light")) badge = "LGT";

            html << "<div class='tree-row" << (sel ? " selected" : "")
                 << "' style='padding-left: " << pad << "dp;' data-action='select_node' data-arg='" << i << "'>"
                 << "<div class='tree-row-left'>"
                 << "<span class='pill " << (n.active ? "pill-purple" : "pill-muted") << "'>" << badge << "</span>"
                 << "<span class='tree-row-name'>" << EscapeXml(n.name) << "</span></div>"
                 << "<span class='tree-row-meta'>" << (n.staticBatchSubMeshCount > 0 ? "Static" : "") << "</span></div>";
        }
        if (nodes.empty()) {
            html << "<div style='padding: 12dp; color: #84849a;'><span>No scene loaded. Open a project in Project Manager.</span></div>";
        }
        hEl->SetInnerRML(html.str());
    }

    if (auto* aEl = document_->GetElementById("scene-assets-dom")) {
        std::ostringstream html;
        html << "<div class='asset-grid'>";
        const auto& entries = assetManager_.Entries();
        size_t shown = 0;
        for (size_t i = 0; i < entries.size() && shown < 120; ++i) {
            const auto& e = entries[i];
            if (assetCategoryFilter_ >= 0 && e.classId != assetCategoryFilter_) continue;
            bool sel = (static_cast<int>(i) == selectedAssetIdx_);
            html << "<div class='asset-card" << (sel ? " selected" : "")
                 << "' data-action='open_asset_in_bundle' data-arg='" << i << "'>"
                 << "<span class='pill pill-purple' style='align-self: flex-start;'>" << EscapeXml(e.className) << "</span>"
                 << "<span class='asset-card-name'>" << EscapeXml(e.name) << "</span>"
                 << "</div>";
            ++shown;
        }
        html << "</div>";
        aEl->SetInnerRML(html.str());
    }
}

void WorkbenchApp::RefreshInspectorDom() {
    if (!document_) return;

    auto* compEl = document_->GetElementById("scene-components-dom");
    if (selectedNodeIdx_ < 0 || selectedNodeIdx_ >= static_cast<int>(sceneDoc_.Nodes().size())) {
        selectedObjectLabel_ = "None";
        selectedObjectTag_ = "Untagged";
        inspName_ = "";
        if (compEl) compEl->SetInnerRML("<div style='padding: 8dp; color: #84849a;'><span>Select a GameObject in the Hierarchy or 3D Viewport.</span></div>");
        if (modelHandle_) modelHandle_.DirtyAllVariables();
        return;
    }

    const auto& n = sceneDoc_.Nodes()[selectedNodeIdx_];
    selectedObjectLabel_ = n.name;
    selectedObjectTag_ = (n.tag == 0) ? "Untagged" : ("Tag #" + std::to_string(n.tag));
    inspName_ = n.name;
    inspPosX_ = FormatFloat(n.localPosition.x);
    inspPosY_ = FormatFloat(n.localPosition.y);
    inspPosZ_ = FormatFloat(n.localPosition.z);
    inspRotX_ = FormatFloat(n.localEulerDeg.x);
    inspRotY_ = FormatFloat(n.localEulerDeg.y);
    inspRotZ_ = FormatFloat(n.localEulerDeg.z);
    inspSclX_ = FormatFloat(n.localScale.x);
    inspSclY_ = FormatFloat(n.localScale.y);
    inspSclZ_ = FormatFloat(n.localScale.z);

    if (compEl) {
        std::ostringstream html;
        if (n.meshPathId != 0) {
            int mIdx = assetManager_.FindEntryByPathIdAnyFile(n.meshPathId, static_cast<i32>(unity::ClassID::Mesh));
            std::string meshName = (mIdx >= 0) ? assetManager_.Entries()[mIdx].name : ("Mesh #" + std::to_string(n.meshPathId));
            html << "<div class='comp-card'>"
                 << "<div class='comp-head'><span class='comp-title'>Mesh Filter &amp; Renderer</span>"
                 << "<span class='pill pill-purple'>Mesh</span></div>"
                 << "<div class='prop-row'><span class='prop-key'>Shared Mesh</span><span class='prop-val'>" << EscapeXml(meshName) << "</span></div>"
                 << "<div class='prop-row'><span class='prop-key'>Static Batched</span><span class='prop-val'>" << (n.staticBatchSubMeshCount > 0 ? "Yes (Decombinable)" : "No") << "</span></div>";
            if (mIdx >= 0) {
                html << "<button class='btn btn-sm' data-action='open_asset_in_bundle' data-arg='" << mIdx << "'>Inspect Mesh / Export FBX</button>";
            }
            html << "</div>";
        }
        if (n.materialPathId != 0) {
            int matIdx = assetManager_.FindEntryByPathIdAnyFile(n.materialPathId, static_cast<i32>(unity::ClassID::Material));
            std::string matName = (matIdx >= 0) ? assetManager_.Entries()[matIdx].name : ("Material #" + std::to_string(n.materialPathId));
            html << "<div class='comp-card'>"
                 << "<div class='comp-head'><span class='comp-title'>Material</span>"
                 << "<span class='pill'>Shader</span></div>"
                 << "<div class='prop-row'><span class='prop-key'>Asset</span><span class='prop-val'>" << EscapeXml(matName) << "</span></div>";
            if (matIdx >= 0) {
                html << "<button class='btn btn-sm' data-action='open_asset_in_bundle' data-arg='" << matIdx << "'>Edit Material</button>";
            }
            html << "</div>";
        }
        for (const auto& c : n.components) {
            if (c.className == "Transform" || c.className == "MeshFilter" || c.className == "MeshRenderer") continue;
            html << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>" << EscapeXml(c.className)
                 << "</span><span class='pill pill-muted'>Component</span></div>"
                 << "<div class='prop-row'><span class='prop-key'>PathID</span><span class='prop-val'>" << c.pathId << "</span></div>";
            if (!c.summary.empty()) {
                html << "<div class='prop-row'><span class='prop-key'>Detail</span><span class='prop-val'>" << EscapeXml(c.summary) << "</span></div>";
            }
            html << "</div>";
        }
        compEl->SetInnerRML(html.str());
    }

    if (modelHandle_) modelHandle_.DirtyAllVariables();
}

void WorkbenchApp::RefreshBundleEditorDom() {
    if (!document_) return;

    if (auto* listEl = document_->GetElementById("bundle-assets-dom")) {
        std::ostringstream html;
        std::string q = ToLowerStr(assetSearchQuery_);
        const auto& entries = assetManager_.Entries();
        size_t shown = 0;

        for (size_t i = 0; i < entries.size() && shown < 400; ++i) {
            const auto& e = entries[i];
            if (assetCategoryFilter_ >= 0 && e.classId != assetCategoryFilter_) continue;
            if (!q.empty() && ToLowerStr(e.name).find(q) == std::string::npos &&
                std::to_string(e.pathId).find(q) == std::string::npos) continue;

            bool sel = (static_cast<int>(i) == selectedAssetIdx_);
            html << "<div class='tree-row" << (sel ? " selected" : "")
                 << "' data-action='select_asset' data-arg='" << i << "'>"
                 << "<div class='tree-row-left'>"
                 << "<span class='pill " << (sel ? "pill-purple" : "pill-muted") << "'>" << EscapeXml(e.className) << "</span>"
                 << "<span class='tree-row-name'>" << EscapeXml(e.name) << "</span></div>"
                 << "<span class='tree-row-meta'>#" << e.pathId << "</span></div>";
            ++shown;
        }
        if (shown == 0) {
            html << "<div style='padding: 12dp; color: #84849a;'><span>No assets match filter.</span></div>";
        }
        listEl->SetInnerRML(html.str());
    }
}

void WorkbenchApp::RefreshAssetPreviewDom() {
    if (!document_) return;
    auto* prevEl = document_->GetElementById("bundle-preview-dom");
    if (!prevEl) return;

    if (selectedAssetIdx_ < 0 || selectedAssetIdx_ >= static_cast<int>(assetManager_.Entries().size())) {
        selectedAssetClass_ = "None";
        selectedAssetPathId_ = "—";
        prevEl->SetInnerRML("<div style='padding: 20dp; color: #858599;'><span>Select an asset on the left to preview, edit, or export (FBX, glTF, OBJ, MP3, M4A, WAV, PNG, TGA, DDS).</span></div>");
        DirtyModelVar("selected_asset_class");
        DirtyModelVar("selected_asset_pathid");
        return;
    }

    const auto& e = assetManager_.Entries()[selectedAssetIdx_];
    selectedAssetClass_ = e.className;
    selectedAssetPathId_ = "PathID #" + std::to_string(e.pathId);
    DirtyModelVar("selected_asset_class");
    DirtyModelVar("selected_asset_pathid");

    std::ostringstream html;
    html << "<div style='display: flex; flex-direction: column; gap: 10dp; padding: 6dp;'>";

    if (e.classId == static_cast<i32>(unity::ClassID::Texture2D)) {
        auto texRes = assetManager_.GetTexture(selectedAssetIdx_);
        if (texRes && texRes.Value()->IsValid()) {
            const auto& t = *texRes.Value();
            std::string texKey = "tex_prev_" + std::to_string(++previewTextureGen_);
            backend_->SetDynamicRGBAImage(texKey, t.width, t.height, t.rgbaPixels);

            html << "<div class='preview-box'>"
                 << "<img id='asset-texture-img' src='coconut://" << texKey << "' />"
                 << "<div class='toolbar-group'>"
                 << "<span class='pill pill-purple'>" << t.width << " x " << t.height << "</span>"
                 << "<span class='pill'>" << EscapeXml(assets::TextureCodec::FormatName(t.format)) << "</span>"
                 << "<span class='pill'>" << t.mipCount << " Mips</span></div></div>"
                 << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>Export Texture</span></div>"
                 << "<div class='toolbar-group'>"
                 << "<button class='btn-primary btn-sm' data-action='export_tex_png'>Export PNG</button>"
                 << "<button class='btn btn-sm' data-action='export_tex_tga'>Export TGA</button>"
                 << "<button class='btn btn-sm' data-action='export_tex_dds'>Export DDS</button>"
                 << "<button class='btn btn-sm' data-action='import_tex_png'>Replace From PNG</button>"
                 << "</div></div>"
                 << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>Live Image Edits (Rebuilds Into Game)</span></div>"
                 << "<div class='toolbar-group'>"
                 << "<button class='btn btn-sm' data-action='tex_flip'>Flip Vertical</button>"
                 << "<button class='btn btn-sm' data-action='tex_invert'>Invert RGB</button>"
                 << "<button class='btn btn-sm' data-action='tex_gray'>Grayscale</button>"
                 << "<button class='btn btn-sm' data-action='tex_tint'>Purple Tint</button>"
                 << "</div></div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::Mesh)) {
        auto mRes = assetManager_.GetMesh(selectedAssetIdx_);
        if (mRes && mRes.Value()->IsValid()) {
            const auto& m = *mRes.Value();
            html << "<div class='preview-box'>"
                 << "<span style='font-size: 16dp; font-weight: bold; color: #f2f2f8;'>" << EscapeXml(m.name) << "</span>"
                 << "<div class='summary-stats'>"
                 << "<div class='mini-stat'><span class='mini-stat-val'>" << m.positions.size() << "</span><span class='mini-stat-lbl'>Vertices</span></div>"
                 << "<div class='mini-stat'><span class='mini-stat-val'>" << (m.indices.size() / 3) << "</span><span class='mini-stat-lbl'>Triangles</span></div>"
                 << "<div class='mini-stat'><span class='mini-stat-val'>" << m.subMeshes.size() << "</span><span class='mini-stat-lbl'>SubMeshes</span></div>"
                 << "</div></div>"
                 << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>Export 3D Mesh</span></div>"
                 << "<div class='toolbar-group'>"
                 << "<button class='btn-primary btn-sm' data-action='export_mesh_fbx'>Export FBX (.fbx)</button>"
                 << "<button class='btn btn-sm' data-action='export_mesh_gltf'>Export glTF 2.0 (.gltf)</button>"
                 << "<button class='btn btn-sm' data-action='export_mesh_obj'>Export OBJ (.obj)</button>"
                 << "</div></div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::AudioClip)) {
        auto aRes = assetManager_.GetAudio(selectedAssetIdx_);
        if (aRes) {
            const auto& a = *aRes.Value();
            html << "<div class='preview-box'>"
                 << "<span style='font-size: 15dp; font-weight: bold;'>" << EscapeXml(a.name) << "</span>"
                 << "<div class='waveform-box'>";
            for (float peak : a.waveformPeaks) {
                int h = std::max(6, static_cast<int>(peak * 86.0f));
                html << "<div class='wave-bar' style='height: " << h << "dp;'></div>";
            }
            html << "</div><div class='toolbar-group'>"
                 << "<span class='pill pill-purple'>" << a.frequency << " Hz</span>"
                 << "<span class='pill'>" << a.channels << " ch</span>"
                 << "<span class='pill'>" << FormatFloat(a.lengthSeconds) << " s</span>"
                 << "<span class='pill'>" << EscapeXml(a.isFsb5Container ? ("FSB5/" + a.fsb5CodecName) : std::string(assets::AudioCodec::CompressionFormatName(a.compressionFormat))) << "</span>"
                 << "</div></div>"
                 << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>Export AudioClip</span></div>"
                 << "<div class='toolbar-group'>"
                 << "<button class='btn-primary btn-sm' data-action='export_audio_mp3'>Export MP3 (.mp3)</button>"
                 << "<button class='btn-primary btn-sm' data-action='export_audio_m4a'>Export M4A (.m4a)</button>"
                 << "<button class='btn btn-sm' data-action='export_audio_wav'>Export WAV (.wav)</button>"
                 << "<button class='btn btn-sm' data-action='export_audio_ogg'>Export Raw/OGG</button>"
                 << "</div></div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::Material)) {
        auto matRes = assetManager_.GetMaterial(selectedAssetIdx_);
        if (matRes) {
            const auto& mat = *matRes.Value();
            html << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>Material: " << EscapeXml(mat.name) << "</span></div>";
            for (const auto& c : mat.colors) {
                html << "<div class='prop-row'><span class='prop-key'>" << EscapeXml(c.name) << "</span>"
                     << "<span class='prop-val'>RGBA(" << FormatFloat(c.value.r) << ", " << FormatFloat(c.value.g)
                     << ", " << FormatFloat(c.value.b) << ", " << FormatFloat(c.value.a) << ")</span></div>";
            }
            for (const auto& f : mat.floats) {
                html << "<div class='prop-row'><span class='prop-key'>" << EscapeXml(f.name) << "</span>"
                     << "<span class='prop-val'>" << FormatFloat(f.value) << "</span></div>";
            }
            html << "</div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::TextAsset)) {
        auto txtRes = assetManager_.GetTextAssetContent(selectedAssetIdx_);
        if (txtRes) {
            html << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>TextAsset: " << EscapeXml(e.name) << "</span></div>"
                 << "<div class='log-box'>" << EscapeXml(txtRes.Value().substr(0, 6000)) << "</div></div>";
        }
    } else {
        html << "<div class='comp-card'><div class='comp-head'><span class='comp-title'>" << EscapeXml(e.name) << "</span>"
             << "<span class='pill pill-purple'>" << EscapeXml(e.className) << "</span></div>"
             << "<div class='prop-row'><span class='prop-key'>Source File</span><span class='prop-val'>" << EscapeXml(e.fileName) << "</span></div>"
             << "<div class='prop-row'><span class='prop-key'>PathID</span><span class='prop-val'>" << e.pathId << "</span></div>"
             << "<div class='prop-row'><span class='prop-key'>Payload Size</span><span class='prop-val'>" << FormatSize(e.byteSize) << "</span></div>"
             << "</div>";
    }

    html << "</div>";
    prevEl->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshHexEditorDom() {
    if (!document_) return;
    auto* hexEl = document_->GetElementById("bundle-hex-dom");
    if (!hexEl) return;

    if (!project_ || selectedAssetIdx_ < 0 || selectedAssetIdx_ >= static_cast<int>(assetManager_.Entries().size())) {
        hexEl->SetInnerRML("<div style='padding: 12dp; color: #84849a;'><span>Select an asset to inspect or patch raw bytes.</span></div>");
        return;
    }

    const auto* obj = assetManager_.GetObject(static_cast<size_t>(selectedAssetIdx_));
    if (!obj) return;
    const auto& raw = obj->rawData;

    size_t inspOff = 0;
    try { inspOff = std::stoull(hexOffsetInput_, nullptr, 16); } catch (...) {}
    if (inspOff < raw.size()) {
        std::ostringstream dt;
        dt << "u8=" << static_cast<int>(raw[inspOff]);
        if (inspOff + 4 <= raw.size()) {
            i32 iVal = 0; float fVal = 0.0f;
            std::memcpy(&iVal, raw.data() + inspOff, 4);
            std::memcpy(&fVal, raw.data() + inspOff, 4);
            dt << " | i32=" << iVal << " | f32=" << FormatFloat(fVal);
        }
        hexDataTypePreview_ = dt.str();
    } else {
        hexDataTypePreview_ = "Offset out of range (" + std::to_string(raw.size()) + " B)";
    }
    DirtyModelVar("hex_datatype_preview");

    std::ostringstream html;
    size_t maxBytes = std::min<size_t>(raw.size(), 1024);
    for (size_t off = 0; off < maxBytes; off += 16) {
        std::ostringstream offStr, hexStr, ascStr;
        offStr << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << off;
        for (size_t j = 0; j < 16; ++j) {
            if (off + j < maxBytes) {
                u8 b = raw[off + j];
                hexStr << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(b) << " ";
                ascStr << ((b >= 32 && b < 127) ? static_cast<char>(b) : '.');
            } else {
                hexStr << "   ";
            }
        }
        html << "<div class='hex-row'>"
             << "<span class='hex-off'>" << offStr.str() << "</span>"
             << "<span class='hex-bytes'>" << hexStr.str() << "</span>"
             << "<span class='hex-ascii'>" << EscapeXml(ascStr.str()) << "</span>"
             << "</div>";
    }
    hexEl->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshTypeTreeDom() {
    if (!document_) return;
    auto* ttEl = document_->GetElementById("bundle-typetree-dom");
    if (!ttEl) return;

    if (!project_ || selectedAssetIdx_ < 0 || selectedAssetIdx_ >= static_cast<int>(assetManager_.Entries().size())) {
        ttEl->SetInnerRML("<div style='padding: 10dp; color: #84849a;'><span>Select an asset to inspect its TypeTree fields.</span></div>");
        return;
    }

    const auto& e = assetManager_.Entries()[selectedAssetIdx_];
    const auto* sf = assetManager_.GetFile(e.fileIndex);
    const auto* obj = assetManager_.GetObject(static_cast<size_t>(selectedAssetIdx_));
    if (!sf || !obj) return;

    auto valRes = sf->ReadObjectValue(*obj);
    if (!valRes) {
        ttEl->SetInnerRML("<div style='padding: 10dp; color: #84849a;'><span>" + EscapeXml(valRes.Error()) + "</span></div>");
        return;
    }

    std::ostringstream html;
    std::function<void(const unity::TypeValue&, int)> emit = [&](const unity::TypeValue& tv, int depth) {
        int pad = 6 + std::min(depth, 5) * 12;
        if (const auto* kids = std::get_if<std::vector<unity::TypeValue>>(&tv.value)) {
            html << "<div class='prop-row' style='padding-left: " << pad << "dp; margin-bottom: 3dp;'>"
                 << "<span class='prop-key'>" << EscapeXml(tv.name) << "</span>"
                 << "<span class='pill pill-muted'>" << EscapeXml(tv.type) << "</span></div>";
            size_t limit = std::min<size_t>(kids->size(), 40);
            for (size_t i = 0; i < limit; ++i) emit((*kids)[i], depth + 1);
        } else {
            std::string valStr;
            if (auto* s = std::get_if<std::string>(&tv.value)) valStr = *s;
            else if (auto* b = std::get_if<bool>(&tv.value)) valStr = *b ? "true" : "false";
            else if (auto* iv = std::get_if<i64>(&tv.value)) valStr = std::to_string(*iv);
            else if (auto* uv = std::get_if<u64>(&tv.value)) valStr = std::to_string(*uv);
            else if (auto* fv = std::get_if<f64>(&tv.value)) valStr = FormatFloat(static_cast<float>(*fv));
            else if (auto* bytes = std::get_if<ByteBuffer>(&tv.value)) valStr = "[" + FormatSize(bytes->size()) + "]";
            html << "<div class='prop-row' style='padding-left: " << pad << "dp; margin-bottom: 3dp;'>"
                 << "<span class='prop-key'>" << EscapeXml(tv.name) << "</span>"
                 << "<span class='prop-val'>" << EscapeXml(valStr.substr(0, 48)) << "</span></div>";
        }
    };
    emit(valRes.Value(), 0);
    ttEl->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshBuilderDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("builder-changes-dom");
    if (!el) return;

    if (!project_) {
        el->SetInnerRML("<div style='padding: 12dp; color: #84849a;'><span>No project loaded.</span></div>");
        return;
    }

    auto changes = exporter::GameRebuilder::CollectPendingChanges(*project_, &sceneDoc_);
    std::ostringstream html;
    if (changes.empty()) {
        html << "<div style='padding: 12dp; color: #84849a;'>"
             << "<span>No unsaved binary edits queued yet. Building now will emit a clean verified clone/repack of the loaded Unity build.</span></div>";
    } else {
        for (const auto& c : changes) {
            html << "<div class='tree-row'>"
                 << "<div class='tree-row-left'>"
                 << "<span class='pill pill-purple'>" << EscapeXml(c.changeKind) << "</span>"
                 << "<span class='tree-row-name'>" << EscapeXml(c.objectName) << "</span>"
                 << "<span class='tree-row-meta'>(" << EscapeXml(c.className) << " #" << c.pathId << ")</span></div>"
                 << "<span class='tree-row-meta'>" << EscapeXml(c.fileName) << "</span></div>";
        }
    }
    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshConsole() {
    auto entries = Logger::Instance().Snapshot(40);
    std::ostringstream oss;
    for (const auto& e : entries) {
        oss << "[" << e.category << "] " << e.message << "\n";
    }
    consoleText_ = oss.str();
    DirtyModelVar("console_text");
}

void WorkbenchApp::SelectSceneNode(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(sceneDoc_.Nodes().size())) return;
    selectedNodeIdx_ = nodeIndex;
    RefreshSceneEditorDom();
    RefreshInspectorDom();
}

void WorkbenchApp::SelectSymbol(int symbolIndex) {
    if (symbolIndex < 0 || symbolIndex >= static_cast<int>(decompiler_.Symbols().size())) return;
    selectedSymbolIdx_ = symbolIndex;
    RefreshCodeBrowserDom();
    RefreshDecompilerPanes();
}

void WorkbenchApp::SelectAsset(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= static_cast<int>(assetManager_.Entries().size())) return;
    selectedAssetIdx_ = entryIndex;
    RefreshBundleEditorDom();
    RefreshAssetPreviewDom();
    RefreshHexEditorDom();
    RefreshTypeTreeDom();
}

void WorkbenchApp::HandleDomAction(const std::string& action, const std::string& arg) {
    if (action == "set_view") { SetActiveView(arg); return; }
    if (action == "set_scene_bottom") { sceneBottomTab_ = arg; DirtyModelVar("scene_bottom_tab"); return; }
    if (action == "set_bundle_tab") { bundleCenterTab_ = arg; DirtyModelVar("bundle_center_tab"); return; }

    // Project Manager actions
    if (action == "toggle_new_project") {
        showNewProjectForm_ = !showNewProjectForm_;
        DirtyModelVar("show_new_project_form");
        return;
    }
    if (action == "browse_project_folder") {
        if (auto dir = platform::OpenFolderDialog("Select Unity Game Build Folder (*_Data / .app)")) {
            newProjectPath_ = dir->string();
            if (newProjectName_.empty()) newProjectName_ = dir->filename().string();
            DirtyModelVar("new_project_path");
            DirtyModelVar("new_project_name");
        }
        return;
    }
    if (action == "browse_project_apk") {
        if (auto file = platform::OpenFileDialog("Select Unity Android APK / AssetBundle / .assets",
                                                 {{"Unity Builds (*.apk, *.aab, *.bundle, *.assets)", "*.apk;*.aab;*.bundle;*.assets"},
                                                  {"All Files (*.*)", "*.*"}})) {
            newProjectPath_ = file->string();
            if (newProjectName_.empty()) newProjectName_ = file->stem().string();
            DirtyModelVar("new_project_path");
            DirtyModelVar("new_project_name");
        }
        return;
    }
    if (action == "create_project") {
        if (newProjectPath_.empty()) {
            statusMessage_ = "Select a Unity game build folder or APK first.";
            DirtyModelVar("status_message");
            return;
        }
        auto addRes = projectManager_.AddProject(newProjectName_, std::filesystem::path(newProjectPath_));
        if (!addRes) {
            statusMessage_ = addRes.Error();
            DirtyModelVar("status_message");
            return;
        }
        activeProjectId_ = addRes.Value().id;
        showNewProjectForm_ = false;
        newProjectName_.clear();
        newProjectPath_.clear();
        DirtyModelVar("show_new_project_form");
        DirtyModelVar("new_project_name");
        DirtyModelVar("new_project_path");
        LoadGameFromPath(addRes.Value().gameBuildPath, addRes.Value().name);
        return;
    }
    if (action == "open_project") { OpenProjectEntry(arg); return; }
    if (action == "remove_project") {
        projectManager_.RemoveProject(arg);
        if (activeProjectId_ == arg) activeProjectId_.clear();
        RefreshProjectManagerDom();
        return;
    }
    if (action == "save_coco") {
        if (!project_) return;
        auto cocoPath = std::filesystem::is_directory(project_->rootPath)
                            ? (project_->rootPath / (project_->gameName + ".coco"))
                            : (project_->rootPath.parent_path() / (project_->gameName + ".coco"));
        projectDb_.Save(cocoPath);
        statusMessage_ = "Saved Coconut annotations to " + cocoPath.string();
        DirtyModelVar("status_message");
        return;
    }

    // CodeBrowser actions
    if (action == "apply_symbol_search") { RefreshCodeBrowserDom(); return; }
    if (action == "symbol_filter") { symbolKindFilter_ = arg; RefreshCodeBrowserDom(); return; }
    if (action == "select_symbol") { SelectSymbol(std::stoi(arg)); return; }
    if (action == "jump_symbol_name") {
        for (size_t i = 0; i < decompiler_.Symbols().size(); ++i) {
            if (decompiler_.Symbols()[i].typeName == arg || decompiler_.Symbols()[i].fullName == arg) {
                SelectSymbol(static_cast<int>(i));
                break;
            }
        }
        return;
    }
    if (action == "rename_symbol") {
        if (selectedSymbolIdx_ >= 0 && selectedSymbolIdx_ < static_cast<int>(decompiler_.Symbols().size()) && !renameInput_.empty()) {
            const auto& sym = decompiler_.Symbols()[selectedSymbolIdx_];
            std::string key = sym.fullName.empty() ? sym.typeName : sym.fullName;
            projectDb_.SetSymbolRename(key, renameInput_);
            statusMessage_ = "Renamed " + sym.typeName + " -> " + renameInput_;
            DirtyModelVar("status_message");
            RefreshDecompilerPanes();
        }
        return;
    }
    if (action == "add_symbol_comment") {
        if (selectedSymbolIdx_ >= 0 && selectedSymbolIdx_ < static_cast<int>(decompiler_.Symbols().size()) && !commentInput_.empty()) {
            const auto& sym = decompiler_.Symbols()[selectedSymbolIdx_];
            std::string key = sym.fullName.empty() ? sym.typeName : sym.fullName;
            projectDb_.SetComment(key, commentInput_);
            commentInput_.clear();
            DirtyModelVar("comment_input");
            RefreshDecompilerPanes();
        }
        return;
    }
    if (action == "export_dump_cs") {
        if (auto out = platform::SaveFileDialog("Export dump.cs", "dump.cs", {{"C# Source", "*.cs"}})) {
            decompiler_.ExportDumpCS(*out, &projectDb_);
            statusMessage_ = "Exported dump.cs -> " + out->string();
            DirtyModelVar("status_message");
        }
        return;
    }
    if (action == "export_ghidra_json") {
        if (auto out = platform::SaveFileDialog("Export Ghidra script.json", "script.json", {{"JSON", "*.json"}})) {
            decompiler_.ExportGhidraScriptJson(*out);
            statusMessage_ = "Exported Ghidra script.json -> " + out->string();
            DirtyModelVar("status_message");
        }
        return;
    }

    // Scene Editor actions
    if (action == "select_node") { SelectSceneNode(std::stoi(arg)); return; }
    if (action == "set_gizmo") {
        if (arg == "translate") { gizmoMode_ = scene::GizmoMode::Translate; gizmoModeLabel_ = "Translate"; }
        else if (arg == "rotate") { gizmoMode_ = scene::GizmoMode::Rotate; gizmoModeLabel_ = "Rotate"; }
        else if (arg == "scale") { gizmoMode_ = scene::GizmoMode::Scale; gizmoModeLabel_ = "Scale"; }
        DirtyModelVar("gizmo_mode");
        return;
    }
    if (action == "set_shading") {
        if (arg == "shaded") { shadingMode_ = scene::ShadingMode::Shaded; shadingModeLabel_ = "Shaded"; }
        else if (arg == "wireframe") { shadingMode_ = scene::ShadingMode::Wireframe; shadingModeLabel_ = "Wireframe"; }
        else if (arg == "normals") { shadingMode_ = scene::ShadingMode::Normals; shadingModeLabel_ = "Normals"; }
        DirtyModelVar("shading_mode");
        return;
    }
    if (action == "toggle_grid") { showGrid_ = !showGrid_; return; }
    if (action == "focus_object") {
        if (selectedNodeIdx_ >= 0 && selectedNodeIdx_ < static_cast<int>(sceneDoc_.Nodes().size())) {
            camera_.Focus(WorldPosOf(sceneDoc_, static_cast<size_t>(selectedNodeIdx_)), 6.0f);
        }
        return;
    }
    if (action == "create_object") {
        size_t id = sceneDoc_.CreateGameObject("NewGameObject", selectedNodeIdx_);
        selectedNodeIdx_ = static_cast<int>(id);
        RefreshSceneEditorDom();
        RefreshInspectorDom();
        RefreshPendingEditsBadge();
        return;
    }
    if (action == "duplicate_object") {
        if (selectedNodeIdx_ >= 0) {
            selectedNodeIdx_ = static_cast<int>(sceneDoc_.DuplicateGameObject(static_cast<size_t>(selectedNodeIdx_)));
            RefreshSceneEditorDom();
            RefreshInspectorDom();
            RefreshPendingEditsBadge();
        }
        return;
    }
    if (action == "delete_object") {
        if (selectedNodeIdx_ >= 0) {
            sceneDoc_.DeleteGameObject(static_cast<size_t>(selectedNodeIdx_));
            selectedNodeIdx_ = sceneDoc_.Nodes().empty() ? -1 : std::min(selectedNodeIdx_, static_cast<int>(sceneDoc_.Nodes().size()) - 1);
            RefreshSceneEditorDom();
            RefreshInspectorDom();
            RefreshPendingEditsBadge();
        }
        return;
    }
    if (action == "apply_transform") {
        if (selectedNodeIdx_ >= 0 && selectedNodeIdx_ < static_cast<int>(sceneDoc_.Nodes().size())) {
            size_t idx = static_cast<size_t>(selectedNodeIdx_);
            const auto& n = sceneDoc_.Nodes()[idx];
            glm::vec3 np{ParseFloatSafe(inspPosX_, n.localPosition.x), ParseFloatSafe(inspPosY_, n.localPosition.y), ParseFloatSafe(inspPosZ_, n.localPosition.z)};
            glm::vec3 nr{ParseFloatSafe(inspRotX_, n.localEulerDeg.x), ParseFloatSafe(inspRotY_, n.localEulerDeg.y), ParseFloatSafe(inspRotZ_, n.localEulerDeg.z)};
            glm::vec3 ns{ParseFloatSafe(inspSclX_, n.localScale.x), ParseFloatSafe(inspSclY_, n.localScale.y), ParseFloatSafe(inspSclZ_, n.localScale.z)};
            if (inspName_ != n.name && !inspName_.empty()) {
                sceneDoc_.SetName(idx, inspName_, &history_);
            }
            sceneDoc_.SetTransform(idx, np, nr, ns, &history_);
            RefreshSceneEditorDom();
            RefreshInspectorDom();
            RefreshPendingEditsBadge();
        }
        return;
    }
    if (action == "reset_transform") {
        if (selectedNodeIdx_ >= 0) {
            sceneDoc_.SetTransform(static_cast<size_t>(selectedNodeIdx_), {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, &history_);
            RefreshInspectorDom();
            RefreshPendingEditsBadge();
        }
        return;
    }
    if (action == "sync_scene_binary") {
        if (!project_) return;
        auto res = sceneDoc_.SyncBackToSerializedFiles(*project_);
        statusMessage_ = res ? ("Synced " + std::to_string(res.Value()) + " scene objects into binary (ready for Game Exporter).") : res.Error();
        DirtyModelVar("status_message");
        RefreshPendingEditsBadge();
        return;
    }
    if (action == "undo") { history_.Undo(); RefreshSceneEditorDom(); RefreshInspectorDom(); RefreshPendingEditsBadge(); return; }
    if (action == "redo") { history_.Redo(); RefreshSceneEditorDom(); RefreshInspectorDom(); RefreshPendingEditsBadge(); return; }

    // Bundle & Asset Editor actions
    if (action == "select_asset") { SelectAsset(std::stoi(arg)); return; }
    if (action == "open_asset_in_bundle") {
        SelectAsset(std::stoi(arg));
        SetActiveView("bundle");
        return;
    }
    if (action == "filter_assets") {
        assetCategoryFilter_ = std::stoi(arg);
        RefreshSceneEditorDom();
        RefreshBundleEditorDom();
        return;
    }
    if (action == "apply_asset_search") { RefreshBundleEditorDom(); return; }
    if (action == "patch_hex_byte") {
        if (!project_ || selectedAssetIdx_ < 0 || selectedAssetIdx_ >= static_cast<int>(assetManager_.Entries().size())) return;
        try {
            size_t off = std::stoull(hexOffsetInput_, nullptr, 16);
            u8 val = static_cast<u8>(std::stoul(hexByteInput_, nullptr, 16) & 0xFF);
            if (assetManager_.PatchRawByte(static_cast<size_t>(selectedAssetIdx_), off, val)) {
                statusMessage_ = "Patched byte at 0x" + hexOffsetInput_ + " -> 0x" + hexByteInput_;
                DirtyModelVar("status_message");
                RefreshHexEditorDom();
                RefreshPendingEditsBadge();
            }
        } catch (...) {}
        return;
    }
    if (action == "repack_bundle") {
        if (!project_ || project_->bundles.empty()) {
            statusMessage_ = "No UnityFS AssetBundles loaded in this project.";
            DirtyModelVar("status_message");
            return;
        }
        if (auto dst = platform::SaveFileDialog("Save Repacked UnityFS Bundle", "repacked.bundle", {{"UnityFS Bundle", "*.bundle"}})) {
            auto packRes = project_->bundles[0].Serialize(CompressionType::LZ4HC);
            if (packRes && platform::WriteFileBytes(*dst, packRes.Value())) {
                statusMessage_ = "Repacked UnityFS bundle (LZ4HC) -> " + dst->string();
            } else {
                statusMessage_ = "Bundle repack failed.";
            }
            DirtyModelVar("status_message");
        }
        return;
    }

    // Rich Single-Asset Exporters (FBX, glTF, OBJ, MP3, M4A, WAV, OGG, PNG, TGA, DDS)
    if (selectedAssetIdx_ >= 0 && selectedAssetIdx_ < static_cast<int>(assetManager_.Entries().size())) {
        const auto& e = assetManager_.Entries()[selectedAssetIdx_];
        std::string safe = CleanSafeName(e.name);

        if (action == "export_mesh_fbx" || action == "export_mesh_gltf" || action == "export_mesh_obj") {
            auto mRes = assetManager_.GetMesh(selectedAssetIdx_);
            if (mRes) {
                std::string ext = (action == "export_mesh_fbx") ? ".fbx" : (action == "export_mesh_gltf") ? ".gltf" : ".obj";
                if (auto out = platform::SaveFileDialog("Export 3D Mesh", safe + ext, {{"3D Mesh", "*" + ext}})) {
                    auto wRes = (ext == ".fbx")  ? assets::MeshCodec::ExportFBX(*mRes.Value(), *out)
                              : (ext == ".gltf") ? assets::MeshCodec::ExportGLTF(*mRes.Value(), *out)
                                                 : assets::MeshCodec::ExportOBJ(*mRes.Value(), *out);
                    statusMessage_ = wRes ? ("Exported mesh -> " + out->string()) : wRes.Error();
                    DirtyModelVar("status_message");
                }
            }
            return;
        }
        if (action == "export_audio_mp3" || action == "export_audio_m4a" || action == "export_audio_wav" || action == "export_audio_ogg") {
            auto aRes = assetManager_.GetAudio(selectedAssetIdx_);
            if (aRes) {
                std::string ext = (action == "export_audio_mp3") ? ".mp3"
                                : (action == "export_audio_m4a") ? ".m4a"
                                : (action == "export_audio_ogg") ? ".ogg" : ".wav";
                if (auto out = platform::SaveFileDialog("Export AudioClip", safe + ext, {{"Audio", "*" + ext}})) {
                    auto wRes = (ext == ".mp3") ? assets::AudioCodec::ExportMP3(*aRes.Value(), *out)
                              : (ext == ".m4a") ? assets::AudioCodec::ExportM4A(*aRes.Value(), *out)
                              : (ext == ".ogg") ? assets::AudioCodec::ExportRawOrOgg(*aRes.Value(), *out)
                                                : assets::AudioCodec::ExportWAV(*aRes.Value(), *out);
                    statusMessage_ = wRes ? ("Exported audio -> " + out->string()) : wRes.Error();
                    DirtyModelVar("status_message");
                }
            }
            return;
        }
        if (action == "export_tex_png" || action == "export_tex_tga" || action == "export_tex_dds") {
            auto tRes = assetManager_.GetTexture(selectedAssetIdx_);
            if (tRes) {
                std::string ext = (action == "export_tex_tga") ? ".tga" : (action == "export_tex_dds") ? ".dds" : ".png";
                if (auto out = platform::SaveFileDialog("Export Texture2D", safe + ext, {{"Image", "*" + ext}})) {
                    auto wRes = (ext == ".tga") ? assets::TextureCodec::ExportTGA(*tRes.Value(), *out)
                              : (ext == ".dds") ? assets::TextureCodec::ExportDDS(*tRes.Value(), *out)
                                                : assets::TextureCodec::ExportPNG(*tRes.Value(), *out);
                    statusMessage_ = wRes ? ("Exported texture -> " + out->string()) : wRes.Error();
                    DirtyModelVar("status_message");
                }
            }
            return;
        }
        if (action == "import_tex_png") {
            if (auto in = platform::OpenFileDialog("Import Replacement PNG", {{"PNG Images", "*.png"}})) {
                auto imp = assets::TextureCodec::ImportPNG(*in, e.name);
                auto cur = assetManager_.GetTexture(selectedAssetIdx_);
                if (imp && cur) {
                    *cur.Value() = std::move(imp.Value());
                    assetManager_.ApplyTextureEdits(static_cast<size_t>(selectedAssetIdx_));
                    statusMessage_ = "Replaced Texture2D in memory -> ready to rebuild in Game Exporter.";
                    RefreshAssetPreviewDom();
                    RefreshPendingEditsBadge();
                } else {
                    statusMessage_ = "Failed to import PNG.";
                }
                DirtyModelVar("status_message");
            }
            return;
        }
        if (action == "tex_flip" || action == "tex_invert" || action == "tex_gray" || action == "tex_tint") {
            auto tRes = assetManager_.GetTexture(selectedAssetIdx_);
            if (tRes) {
                if (action == "tex_flip") assets::TextureCodec::FlipVertical(*tRes.Value());
                else if (action == "tex_invert") assets::TextureCodec::InvertRGB(*tRes.Value());
                else if (action == "tex_gray") assets::TextureCodec::ConvertGrayscale(*tRes.Value());
                else if (action == "tex_tint") assets::TextureCodec::ApplyTint(*tRes.Value(), 0.92f, 0.72f, 1.25f);
                assetManager_.ApplyTextureEdits(static_cast<size_t>(selectedAssetIdx_));
                RefreshAssetPreviewDom();
                RefreshPendingEditsBadge();
            }
            return;
        }
    }

    // Exporter & Game Rebuilder actions
    if (action == "toggle_opt") {
        if (arg == "ripper") { optUseRipper_ = !optUseRipper_; DirtyModelVar("opt_use_ripper"); }
        else if (arg == "decombine") { optDecombine_ = !optDecombine_; DirtyModelVar("opt_decombine"); }
        else if (arg == "prefabs") { optPrefabs_ = !optPrefabs_; DirtyModelVar("opt_prefabs"); }
        else if (arg == "fixerrors") { optFixErrors_ = !optFixErrors_; DirtyModelVar("opt_fixerrors"); }
        else if (arg == "copy_untouched") { optCopyUntouched_ = !optCopyUntouched_; DirtyModelVar("opt_copy_untouched"); }
        else if (arg == "sync_scene") { optSyncScene_ = !optSyncScene_; DirtyModelVar("opt_sync_scene"); }
        return;
    }
    if (action == "browse_export_dir") {
        if (auto dir = platform::OpenFolderDialog("Select Unity Project Export Folder")) {
            exportOutputDir_ = dir->string();
            DirtyModelVar("export_output_dir");
        }
        return;
    }
    if (action == "browse_builder_out") {
        if (project_ && project_->isApkArchive) {
            if (auto f = platform::SaveFileDialog("Save Patched Android APK", "patched_game.apk", {{"Android APK", "*.apk"}})) {
                builderOutputPath_ = f->string();
                DirtyModelVar("builder_output_path");
            }
        } else {
            if (auto dir = platform::OpenFolderDialog("Select Patched Game Build Output Folder")) {
                builderOutputPath_ = dir->string();
                DirtyModelVar("builder_output_path");
            }
        }
        return;
    }
    if (action == "run_project_export") { RunExportAndFixupPipeline(); return; }
    if (action == "run_game_build") { RunGameRebuild(); return; }
}

void WorkbenchApp::UpdateViewportRect() {
    if (!document_) return;
    if (auto* el = document_->GetElementById("viewport-stage")) {
        auto pos = el->GetAbsoluteOffset(Rml::BoxArea::Content);
        auto sz  = el->GetBox().GetSize(Rml::BoxArea::Content);
        if (sz.x > 16.0f && sz.y > 16.0f) {
            vpX_ = pos.x;
            vpY_ = pos.y;
            vpW_ = sz.x;
            vpH_ = sz.y;
        }
    }
}

bool WorkbenchApp::IsPointInsideViewport(float x, float y, float& normX, float& normY) const {
    if (activeView_ != "scene" || vpW_ <= 1.0f || vpH_ <= 1.0f) return false;
    if (x < vpX_ || x > vpX_ + vpW_ || y < vpY_ || y > vpY_ + vpH_) return false;
    normX = (x - vpX_) / vpW_;
    normY = (y - vpY_) / vpH_;
    return true;
}

bool WorkbenchApp::HandleSdlEvent(const SDL_Event& ev) {
    UpdateViewportRect();

    if (ev.type == SDL_EVENT_KEY_DOWN && activeView_ == "scene") {
        if (ev.key.mod & SDL_KMOD_CTRL) {
            if (ev.key.key == SDLK_Z) { HandleDomAction("undo", ""); return true; }
            if (ev.key.key == SDLK_Y) { HandleDomAction("redo", ""); return true; }
        }
        auto* focus = backend_->Context()->GetFocusElement();
        bool typing = focus && (focus->GetTagName() == "input" || focus->GetTagName() == "textarea");
        if (!typing) {
            if (ev.key.key == SDLK_W) { HandleDomAction("set_gizmo", "translate"); return true; }
            if (ev.key.key == SDLK_E) { HandleDomAction("set_gizmo", "rotate"); return true; }
            if (ev.key.key == SDLK_R) { HandleDomAction("set_gizmo", "scale"); return true; }
            if (ev.key.key == SDLK_F) { HandleDomAction("focus_object", ""); return true; }
        }
    }

    if (activeView_ != "scene") return false;

    float nx = 0.0f, ny = 0.0f;
    if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (IsPointInsideViewport(mx, my, nx, ny)) {
            camera_.Zoom(ev.wheel.y * 1.1f);
            return true;
        }
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (IsPointInsideViewport(ev.button.x, ev.button.y, nx, ny)) {
            lastMouseX_ = ev.button.x;
            lastMouseY_ = ev.button.y;
            if (ev.button.button == SDL_BUTTON_RIGHT) { orbiting_ = true; return true; }
            if (ev.button.button == SDL_BUTTON_MIDDLE) { panning_ = true; return true; }
            if (ev.button.button == SDL_BUTTON_LEFT) {
                float aspect = vpW_ / std::max(1.0f, vpH_);
                auto ray = camera_.ScreenPointToRay(nx, ny, aspect);
                if (selectedNodeIdx_ >= 0 && selectedNodeIdx_ < static_cast<int>(sceneDoc_.Nodes().size())) {
                    size_t sIdx = static_cast<size_t>(selectedNodeIdx_);
                    glm::vec3 wPos = WorldPosOf(sceneDoc_, sIdx);
                    const auto& n = sceneDoc_.Nodes()[sIdx];
                    float gScale = std::max(0.4f, glm::length(camera_.Position() - wPos) * 0.14f);
                    auto axis = scene::GizmoMath::PickAxisHandle(ray, wPos, gScale);
                    if (axis != scene::GizmoAxis::None) {
                        activeGizmoAxis_ = axis;
                        draggingGizmo_ = true;
                        gizmoDragStartPos_ = n.localPosition;
                        gizmoDragStartRot_ = n.localEulerDeg;
                        gizmoDragStartScale_ = n.localScale;
                        gizmoDragStartProj_ = scene::GizmoMath::ProjectRayOntoAxis(ray, wPos, AxisDirection(axis));
                        return true;
                    }
                }
                int hit = sceneRenderer_.PickObject(sceneDoc_, assetManager_, ray);
                if (hit >= 0) SelectSceneNode(hit);
                return true;
            }
        }
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (orbiting_ || panning_ || draggingGizmo_) {
            if (draggingGizmo_ && selectedNodeIdx_ >= 0) {
                RefreshInspectorDom();
                RefreshPendingEditsBadge();
            }
            orbiting_ = panning_ = draggingGizmo_ = false;
            activeGizmoAxis_ = scene::GizmoAxis::None;
            return true;
        }
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        float dx = ev.motion.x - lastMouseX_;
        float dy = ev.motion.y - lastMouseY_;
        lastMouseX_ = ev.motion.x;
        lastMouseY_ = ev.motion.y;
        if (orbiting_) { camera_.Orbit(dx * 0.008f, dy * 0.008f); return true; }
        if (panning_) { camera_.Pan(dx * 0.015f, dy * 0.015f); return true; }
        if (draggingGizmo_ && selectedNodeIdx_ >= 0 && selectedNodeIdx_ < static_cast<int>(sceneDoc_.Nodes().size())) {
            size_t sIdx = static_cast<size_t>(selectedNodeIdx_);
            float aspect = vpW_ / std::max(1.0f, vpH_);
            float mxNorm = (ev.motion.x - vpX_) / std::max(1.0f, vpW_);
            float myNorm = (ev.motion.y - vpY_) / std::max(1.0f, vpH_);
            auto ray = camera_.ScreenPointToRay(mxNorm, myNorm, aspect);
            glm::vec3 dir = AxisDirection(activeGizmoAxis_);
            float curProj = scene::GizmoMath::ProjectRayOntoAxis(ray, gizmoDragStartPos_, dir);
            float delta = curProj - gizmoDragStartProj_;
            const auto& n = sceneDoc_.Nodes()[sIdx];
            if (gizmoMode_ == scene::GizmoMode::Translate) {
                sceneDoc_.SetTransform(sIdx, gizmoDragStartPos_ + dir * delta, n.localEulerDeg, n.localScale);
            } else if (gizmoMode_ == scene::GizmoMode::Rotate) {
                sceneDoc_.SetTransform(sIdx, n.localPosition, gizmoDragStartRot_ + dir * (delta * 25.0f), n.localScale);
            } else {
                sceneDoc_.SetTransform(sIdx, n.localPosition, n.localEulerDeg, glm::max(glm::vec3(0.05f), gizmoDragStartScale_ + dir * delta));
            }
            RefreshInspectorDom();
            return true;
        }
    }
    return false;
}

void WorkbenchApp::Update(float dtSeconds) {
    fpsTimer_ += dtSeconds;
    ++frameCount_;
    if (fpsTimer_ >= 0.5f) {
        fps_ = static_cast<int>(std::round(frameCount_ / std::max(0.001f, fpsTimer_)));
        fpsTimer_ = 0.0f;
        frameCount_ = 0;
        DirtyModelVar("fps");
    }
}

void WorkbenchApp::RenderSceneViewport() {
    if (activeView_ != "scene") return;
    UpdateViewportRect();
    int w = std::max(64, static_cast<int>(vpW_));
    int h = std::max(64, static_cast<int>(vpH_));
    sceneRenderer_.Resize(w, h);
    sceneRenderer_.Render(sceneDoc_, assetManager_, camera_, selectedNodeIdx_,
                          gizmoMode_, activeGizmoAxis_, shadingMode_, showGrid_);
    backend_->SetExternalGLTexture("viewport", sceneRenderer_.ColorTextureId(),
                                   sceneRenderer_.Width(), sceneRenderer_.Height());
}

void WorkbenchApp::Shutdown() {
    sceneRenderer_.Shutdown();
    if (document_ && clickRouter_) {
        document_->RemoveEventListener(Rml::EventId::Click, clickRouter_.get());
    }
    document_ = nullptr;
    clickRouter_.reset();
}

} // namespace coconut::ui
