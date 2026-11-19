#include "ui/WorkbenchApp.h"
#include "ui/SyntaxHighlighter.h"
#include "unity/SampleGameGenerator.h"
#include "unity/ClassIDs.h"
#include "export/AssetRipperClient.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace coconut::ui {

namespace {

std::string FmtFloat(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

float ParseFloatSafe(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

std::string LowerCopy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

class WorkbenchApp::DomClickRouter final : public Rml::EventListener {
public:
    explicit DomClickRouter(WorkbenchApp* app) : app_(app) {}

    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* el = event.GetTargetElement();
        while (el) {
            if (el->HasAttribute("data-action")) {
                std::string action = el->GetAttribute<Rml::String>("data-action", "");
                std::string arg = el->GetAttribute<Rml::String>("data-arg", "");
                app_->HandleDomAction(action, arg);
                event.StopPropagation();
                return;
            }
            el = el->GetParentNode();
        }
    }

private:
    WorkbenchApp* app_ = nullptr;
};

WorkbenchApp::WorkbenchApp() = default;

WorkbenchApp::~WorkbenchApp() {
    Shutdown();
}

Result<void> WorkbenchApp::Initialize(
    RmlBackend& backend,
    const std::filesystem::path& resourceDir,
    const std::filesystem::path& initialGamePath,
    const std::string& initialCenterTab) {
    backend_ = &backend;
    activeCenterTab_ = initialCenterTab.empty() ? "scene" : initialCenterTab;

    // Detect AssetRipper locally
    if (auto arBin = exporter::AssetRipperClient::DetectAssetRipperBinary()) {
        assetRipperPath_ = arBin->string();
        ripperDetectStatus_ = "AssetRipper Detected (" + arBin->filename().string() + ")";
    } else if (exporter::AssetRipperClient::PingServer("127.0.0.1", 52890)) {
        ripperDetectStatus_ = "AssetRipper HTTP API Active (:52890)";
    } else {
        ripperDetectStatus_ = "Native Exporter Ready (Connect AssetRipper on :52890 anytime)";
    }

    auto glRes = sceneRenderer_.Initialize(960, 560);
    if (!glRes) {
        COCO_LOG_ERROR("SceneGL", glRes.Error());
    } else {
        backend_->SetExternalGLTexture("scene_viewport", sceneRenderer_.ColorTextureId(),
                                       sceneRenderer_.Width(), sceneRenderer_.Height());
    }

    BindDataModel();

    auto docPath = resourceDir / "ui" / "main_window.rml";
    document_ = backend_->Context()->LoadDocument(docPath.string());
    if (!document_) {
        return Result<void>::Err("Failed to load RmlUi document: " + docPath.string());
    }
    AttachDomRouters();
    document_->Show();

    COCO_LOG_INFO("UI", std::string("Coconut workbench initialized on ") + std::string(platform::HostPlatformName()));

    if (!initialGamePath.empty() && std::filesystem::exists(initialGamePath)) {
        LoadGameFromPath(initialGamePath);
    } else {
        LoadDemoMonoGame();
    }

    return Result<void>::Ok();
}

void WorkbenchApp::BindDataModel() {
    Rml::DataModelConstructor ctor = backend_->Context()->CreateDataModel("workbench");
    if (!ctor) return;

    ctor.Bind("status_message", &statusMessage_);
    ctor.Bind("project_title", &projectTitle_);
    ctor.Bind("unity_version", &unityVersion_);
    ctor.Bind("backend_name", &backendName_);
    ctor.Bind("platform_name", &platformName_);
    ctor.Bind("active_center_tab", &activeCenterTab_);
    ctor.Bind("active_left_tab", &activeLeftTab_);
    ctor.Bind("active_bottom_tab", &activeBottomTab_);
    ctor.Bind("console_text", &consoleText_);
    ctor.Bind("gizmo_mode", &gizmoModeLabel_);
    ctor.Bind("shading_mode", &shadingModeLabel_);
    ctor.Bind("ripper_status", &ripperDetectStatus_);
    ctor.Bind("target_unity_version", &targetUnityVersion_);
    ctor.Bind("assetripper_path", &assetRipperPath_);
    ctor.Bind("opt_prefer_ripper", &optPreferAssetRipper_);
    ctor.Bind("opt_decombine", &optDecombineMeshes_);
    ctor.Bind("opt_prefabs", &optReconstructPrefabs_);
    ctor.Bind("opt_fix_build", &optFixBuildErrors_);
    ctor.Bind("insp_name", &inspName_);
    ctor.Bind("insp_px", &inspPosX_); ctor.Bind("insp_py", &inspPosY_); ctor.Bind("insp_pz", &inspPosZ_);
    ctor.Bind("insp_rx", &inspRotX_); ctor.Bind("insp_ry", &inspRotY_); ctor.Bind("insp_rz", &inspRotZ_);
    ctor.Bind("insp_sx", &inspSclX_); ctor.Bind("insp_sy", &inspSclY_); ctor.Bind("insp_sz", &inspSclZ_);
    ctor.Bind("rename_input", &renameInput_);
    ctor.Bind("hex_offset_input", &hexOffsetInput_);
    ctor.Bind("hex_byte_input", &hexByteInput_);
    ctor.Bind("fps", &fps_);

    ctor.BindEventCallback("set_center_tab", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) SetCenterTab(args[0].Get<Rml::String>());
    });
    ctor.BindEventCallback("set_left_tab", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            activeLeftTab_ = args[0].Get<Rml::String>();
            h.DirtyVariable("active_left_tab");
        }
    });
    ctor.BindEventCallback("set_bottom_tab", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            activeBottomTab_ = args[0].Get<Rml::String>();
            h.DirtyVariable("active_bottom_tab");
            if (activeBottomTab_ == "console") RefreshConsole();
        }
    });
    ctor.BindEventCallback("open_folder", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (auto dir = platform::OpenFolderDialog("Open Unity Game Build Folder")) {
            LoadGameFromPath(*dir);
        }
    });
    ctor.BindEventCallback("open_file", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        auto f = platform::OpenFileDialog("Open Unity APK, AssetBundle, or SerializedFile", {
            {"Unity Files (*.apk;*.bundle;*.assets)", "*.apk;*.aab;*.bundle;*.unity3d;*.assets"},
            {"All Files (*.*)", "*.*"}
        });
        if (f) LoadGameFromPath(*f);
    });
    ctor.BindEventCallback("load_demo_mono", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        LoadDemoMonoGame();
    });
    ctor.BindEventCallback("load_demo_il2cpp", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        LoadDemoIl2CppApk();
    });
    ctor.BindEventCallback("undo", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (history_.Undo()) {
            statusMessage_ = "Undid last edit";
            modelHandle_.DirtyVariable("status_message");
            RefreshInspectorDom();
            RefreshHierarchyDom();
        }
    });
    ctor.BindEventCallback("redo", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (history_.Redo()) {
            statusMessage_ = "Redid edit";
            modelHandle_.DirtyVariable("status_message");
            RefreshInspectorDom();
            RefreshHierarchyDom();
        }
    });
    ctor.BindEventCallback("save_all", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (!project_) return;
        sceneDoc_.SyncBackToSerializedFiles(*project_);
        auto res = assetManager_.SaveModifiedFiles();
        if (res) {
            history_.MarkSaved();
            statusMessage_ = "Saved all modified Unity SerializedFiles & annotations";
        } else {
            statusMessage_ = "Save error: " + res.Error();
        }
        modelHandle_.DirtyVariable("status_message");
        RefreshConsole();
    });
    ctor.BindEventCallback("run_export_fixup", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        RunExportAndFixupPipeline();
    });
    ctor.BindEventCallback("set_gizmo", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty()) return;
        std::string m = args[0].Get<Rml::String>();
        if (m == "translate") { gizmoMode_ = scene::GizmoMode::Translate; gizmoModeLabel_ = "Translate"; }
        else if (m == "rotate") { gizmoMode_ = scene::GizmoMode::Rotate; gizmoModeLabel_ = "Rotate"; }
        else if (m == "scale") { gizmoMode_ = scene::GizmoMode::Scale; gizmoModeLabel_ = "Scale"; }
        h.DirtyVariable("gizmo_mode");
    });
    ctor.BindEventCallback("set_shading", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty()) return;
        std::string m = args[0].Get<Rml::String>();
        if (m == "shaded") { shadingMode_ = scene::ShadingMode::Shaded; shadingModeLabel_ = "Shaded"; }
        else if (m == "wireframe") { shadingMode_ = scene::ShadingMode::Wireframe; shadingModeLabel_ = "Wireframe"; }
        else if (m == "normals") { shadingMode_ = scene::ShadingMode::Normals; shadingModeLabel_ = "Normals"; }
        h.DirtyVariable("shading_mode");
    });
    ctor.BindEventCallback("toggle_grid", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        showGrid_ = !showGrid_;
    });
    ctor.BindEventCallback("focus_selected", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedNodeIdx_ >= 0 && static_cast<size_t>(selectedNodeIdx_) < sceneDoc_.Nodes().size()) {
            glm::vec3 pos = glm::vec3(sceneDoc_.WorldMatrix(static_cast<size_t>(selectedNodeIdx_))[3]);
            camera_.Focus(pos, 2.2f);
        } else {
            camera_.target = {0.0f, 1.2f, 0.0f};
            camera_.yawDeg = -38.0f; camera_.pitchDeg = 24.0f; camera_.distance = 12.5f;
        }
    });
    ctor.BindEventCallback("create_go", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        size_t idx = sceneDoc_.CreateGameObject("NewCube_" + std::to_string(sceneDoc_.Nodes().size()), -1, 303, 402);
        SelectSceneNode(static_cast<int>(idx));
        RefreshHierarchyDom();
    });
    ctor.BindEventCallback("duplicate_go", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedNodeIdx_ >= 0) {
            size_t idx = sceneDoc_.DuplicateGameObject(static_cast<size_t>(selectedNodeIdx_));
            SelectSceneNode(static_cast<int>(idx));
            RefreshHierarchyDom();
        }
    });
    ctor.BindEventCallback("delete_go", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedNodeIdx_ >= 0 && sceneDoc_.DeleteGameObject(static_cast<size_t>(selectedNodeIdx_))) {
            selectedNodeIdx_ = -1;
            RefreshHierarchyDom();
            RefreshInspectorDom();
        }
    });
    ctor.BindEventCallback("apply_inspector_transform", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedNodeIdx_ < 0 || static_cast<size_t>(selectedNodeIdx_) >= sceneDoc_.Nodes().size()) return;
        size_t idx = static_cast<size_t>(selectedNodeIdx_);
        const auto& n = sceneDoc_.Nodes()[idx];
        glm::vec3 p{ParseFloatSafe(inspPosX_, n.localPosition.x), ParseFloatSafe(inspPosY_, n.localPosition.y), ParseFloatSafe(inspPosZ_, n.localPosition.z)};
        glm::vec3 r{ParseFloatSafe(inspRotX_, n.localEulerDeg.x), ParseFloatSafe(inspRotY_, n.localEulerDeg.y), ParseFloatSafe(inspRotZ_, n.localEulerDeg.z)};
        glm::vec3 s{ParseFloatSafe(inspSclX_, n.localScale.x), ParseFloatSafe(inspSclY_, n.localScale.y), ParseFloatSafe(inspSclZ_, n.localScale.z)};
        if (!inspName_.empty() && inspName_ != n.name) {
            sceneDoc_.SetName(idx, inspName_, &history_);
        }
        sceneDoc_.SetTransform(idx, p, r, s, &history_);
        statusMessage_ = "Updated Transform on " + sceneDoc_.Nodes()[idx].name;
        modelHandle_.DirtyVariable("status_message");
        RefreshHierarchyDom();
        RefreshInspectorDom();
    });
    ctor.BindEventCallback("rename_symbol", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedSymbolIdx_ < 0 || static_cast<size_t>(selectedSymbolIdx_) >= decompiler_.Symbols().size()) return;
        if (renameInput_.empty()) return;
        const auto& sym = decompiler_.Symbols()[static_cast<size_t>(selectedSymbolIdx_)];
        // Rename the first method of this type or the symbol itself in .coco annotation DB
        if (!sym.isIl2Cpp && sym.assemblyIndex < decompiler_.MonoAssemblies().size()) {
            const auto& t = decompiler_.MonoAssemblies()[sym.assemblyIndex].types[sym.typeIndex];
            if (!t.methods.empty()) {
                projectDb_.SetSymbolRename(t.FullName() + "::" + t.methods[0].name, renameInput_);
                statusMessage_ = "Renamed " + t.methods[0].name + " -> " + renameInput_ + " (stored in .coco DB)";
                modelHandle_.DirtyVariable("status_message");
                RefreshDecompilerDom();
            }
        }
    });
    ctor.BindEventCallback("export_dump_cs", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        auto out = platform::GetAppDataDir() / "exports" / "dump.cs";
        if (decompiler_.ExportDumpCS(out, &projectDb_)) {
            statusMessage_ = "Exported dump.cs -> " + out.string();
            modelHandle_.DirtyVariable("status_message");
            RefreshConsole();
        }
    });
    ctor.BindEventCallback("export_ghidra_json", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        auto out = platform::GetAppDataDir() / "exports" / "script.json";
        if (decompiler_.ExportGhidraScriptJson(out)) {
            statusMessage_ = "Exported Ghidra script.json -> " + out.string();
            modelHandle_.DirtyVariable("status_message");
            RefreshConsole();
        }
    });
    ctor.BindEventCallback("patch_hex_byte", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (selectedAssetIdx_ < 0) return;
        try {
            size_t off = std::stoul(hexOffsetInput_, nullptr, 16);
            u8 val = static_cast<u8>(std::stoul(hexByteInput_, nullptr, 16) & 0xFFu);
            auto res = assetManager_.PatchRawByte(static_cast<size_t>(selectedAssetIdx_), off, val);
            if (res) {
                statusMessage_ = "Patched byte at 0x" + hexOffsetInput_ + " = 0x" + hexByteInput_;
                RefreshHexEditorDom();
                RefreshAssetsBrowserDom();
            } else {
                statusMessage_ = "Hex patch failed: " + res.Error();
            }
            modelHandle_.DirtyVariable("status_message");
        } catch (...) {
            statusMessage_ = "Invalid hex offset or byte value";
            modelHandle_.DirtyVariable("status_message");
        }
    });
    ctor.BindEventCallback("set_unity_ver_preset", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            targetUnityVersion_ = args[0].Get<Rml::String>();
            h.DirtyVariable("target_unity_version");
        }
    });
    ctor.BindEventCallback("toggle_export_opt", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty()) return;
        std::string k = args[0].Get<Rml::String>();
        if (k == "ripper") { optPreferAssetRipper_ = !optPreferAssetRipper_; h.DirtyVariable("opt_prefer_ripper"); }
        else if (k == "decombine") { optDecombineMeshes_ = !optDecombineMeshes_; h.DirtyVariable("opt_decombine"); }
        else if (k == "prefabs") { optReconstructPrefabs_ = !optReconstructPrefabs_; h.DirtyVariable("opt_prefabs"); }
        else if (k == "buildfix") { optFixBuildErrors_ = !optFixBuildErrors_; h.DirtyVariable("opt_fix_build"); }
    });
    ctor.BindEventCallback("filter_assets", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            assetCategoryFilter_ = args[0].Get<int>();
            RefreshAssetsBrowserDom();
        }
    });

    modelHandle_ = ctor.GetModelHandle();
}

void WorkbenchApp::AttachDomRouters() {
    clickRouter_ = std::make_unique<DomClickRouter>(this);
    if (document_) {
        document_->AddEventListener(Rml::EventId::Click, clickRouter_.get());
    }
}

void WorkbenchApp::SetCenterTab(const std::string& tab) {
    activeCenterTab_ = tab;
    if (modelHandle_) modelHandle_.DirtyVariable("active_center_tab");
    if (tab == "decompiler") RefreshDecompilerDom();
    else if (tab == "asset") RefreshAssetPreviewDom();
    else if (tab == "hex") RefreshHexEditorDom();
    else if (tab == "export") RefreshExportPanelDom();
}

void WorkbenchApp::LoadDemoMonoGame() {
    auto demoDir = platform::GetAppDataDir() / "demo_mono_game";
    auto gen = unity::SampleGameGenerator::GenerateMonoDemoGame(demoDir);
    if (!gen) {
        statusMessage_ = "Demo generation failed: " + gen.Error();
        if (modelHandle_) modelHandle_.DirtyVariable("status_message");
        return;
    }
    LoadGameFromPath(demoDir);
}

void WorkbenchApp::LoadDemoIl2CppApk() {
    auto demoApk = platform::GetAppDataDir() / "demo_il2cpp_android.apk";
    auto gen = unity::SampleGameGenerator::GenerateIl2CppDemoApk(demoApk);
    if (!gen) {
        statusMessage_ = "IL2CPP demo generation failed: " + gen.Error();
        if (modelHandle_) modelHandle_.DirtyVariable("status_message");
        return;
    }
    LoadGameFromPath(demoApk);
}

void WorkbenchApp::LoadGameFromPath(const std::filesystem::path& path) {
    auto loadRes = unity::GameLoader::Load(path);
    if (!loadRes) {
        statusMessage_ = "Failed to load: " + loadRes.Error();
        COCO_LOG_ERROR("Workbench", statusMessage_);
        if (modelHandle_) modelHandle_.DirtyVariable("status_message");
        RefreshConsole();
        return;
    }

    project_ = std::move(loadRes.Value());
    history_.Clear();
    projectDb_.Clear();
    sceneRenderer_.ClearMeshCache();

    assetManager_.IndexProject(*project_);
    decompiler_.AnalyzeProject(*project_, &projectDb_);
    sceneDoc_.LoadFromProject(*project_, assetManager_);

    projectTitle_ = project_->gameName.empty() ? "UnityBuild" : project_->gameName;
    unityVersion_ = project_->unityVersion.IsValid() ? project_->unityVersion.ToString() : "2022.3.15f1";
    targetUnityVersion_ = unityVersion_;
    backendName_ = std::string(ToString(project_->backend));
    platformName_ = std::string(ToString(project_->platform));

    // Pre-select interesting defaults so every tab is immediately populated!
    selectedNodeIdx_ = sceneDoc_.FindNodeByName("WatchTower");
    if (selectedNodeIdx_ < 0 && !sceneDoc_.Nodes().empty()) selectedNodeIdx_ = 0;
    selectedSymbolIdx_ = decompiler_.Symbols().empty() ? -1 : 0;
    selectedAssetIdx_ = assetManager_.FindEntryByPathIdAnyFile(500, static_cast<i32>(unity::ClassID::Texture2D));
    if (selectedAssetIdx_ < 0 && !assetManager_.Entries().empty()) selectedAssetIdx_ = 0;

    camera_.target = {0.0f, 1.3f, 0.0f};
    camera_.yawDeg = -36.0f;
    camera_.pitchDeg = 25.0f;
    camera_.distance = 11.8f;

    statusMessage_ = "Loaded " + projectTitle_ + " (" + std::to_string(sceneDoc_.Nodes().size()) + " GameObjects, " +
                     std::to_string(assetManager_.Entries().size()) + " assets, " +
                     std::to_string(decompiler_.Symbols().size()) + " script classes)";

    if (modelHandle_) {
        modelHandle_.DirtyVariable("project_title");
        modelHandle_.DirtyVariable("unity_version");
        modelHandle_.DirtyVariable("target_unity_version");
        modelHandle_.DirtyVariable("backend_name");
        modelHandle_.DirtyVariable("platform_name");
        modelHandle_.DirtyVariable("status_message");
    }

    RefreshAllViews();
}

void WorkbenchApp::RunExportAndFixupPipeline() {
    if (!project_) return;
    auto outDir = platform::GetAppDataDir() / "exports" / (project_->gameName + "_UnityProject");

    fixup::FixupPipelineOptions opts;
    opts.targetUnityVersion = targetUnityVersion_.empty() ? "2022.3.15f1" : targetUnityVersion_;
    opts.preferAssetRipper = optPreferAssetRipper_;
    opts.assetRipperExePath = assetRipperPath_;
    opts.runMeshDecombiner = optDecombineMeshes_;
    opts.runPrefabReconstructor = optReconstructPrefabs_;
    opts.runBuildErrorFixer = optFixBuildErrors_;

    auto res = fixup::ProjectFixupPipeline::ExportAndFixup(
        *project_, assetManager_, decompiler_, sceneDoc_, &projectDb_, outDir, opts);

    if (res) {
        const auto& r = res.Value();
        lastFixupReportMd_ = r.markdownReport;
        std::ostringstream sum;
        sum << "Exported via " << r.backendUsed << " -> " << outDir.string()
            << " | Decombined: " << r.meshDecombine.meshesSplit
            << " | Prefabs: " << r.prefabReconstruct.prefabsCreated
            << " | Scripts Fixed: " << r.buildFix.scriptsFixed;
        lastExportStatsSummary_ = sum.str();
        statusMessage_ = "Unity Project Export & Fixup Complete -> " + outDir.string();
    } else {
        lastExportStatsSummary_ = "Export failed: " + res.Error();
        statusMessage_ = lastExportStatsSummary_;
    }

    if (modelHandle_) modelHandle_.DirtyVariable("status_message");
    RefreshExportPanelDom();
    RefreshConsole();
}

void WorkbenchApp::RefreshAllViews() {
    if (selectedNodeIdx_ >= 0) SelectSceneNode(selectedNodeIdx_);
    RefreshHierarchyDom();
    RefreshSymbolsDom();
    RefreshDecompilerDom();
    RefreshInspectorDom();
    RefreshAssetPreviewDom();
    RefreshHexEditorDom();
    RefreshAssetsBrowserDom();
    RefreshXRefsDom();
    RefreshExportPanelDom();
    RefreshConsole();
}

void WorkbenchApp::RefreshHierarchyDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("hierarchy-tree");
    if (!el) return;

    std::ostringstream html;
    std::function<void(size_t, int)> emitNode = [&](size_t idx, int depth) {
        const auto& n = sceneDoc_.Nodes()[idx];
        std::string cls = (static_cast<int>(idx) == selectedNodeIdx_) ? "tree-row selected" : "tree-row";
        if (!n.active) cls += " muted";
        int padLeft = 10 + depth * 16;
        html << "<div class=\"" << cls << "\" style=\"padding-left: " << padLeft << "dp;\" "
             << "data-action=\"select_node\" data-arg=\"" << idx << "\">"
             << "<span class=\"node-dot\"></span>"
             << "<span class=\"tree-label\">" << SyntaxHighlighter::EscapeRml(n.name) << "</span>";
        if (n.staticBatchSubMeshCount > 0) {
            html << "<span class=\"mini-tag\">Batched</span>";
        }
        html << "</div>";
        for (size_t childIdx : n.children) {
            emitNode(childIdx, depth + 1);
        }
    };

    for (size_t rootIdx : sceneDoc_.RootIndices()) {
        emitNode(rootIdx, 0);
    }
    if (sceneDoc_.Nodes().empty()) {
        html << "<div class=\"empty-state\">No scene GameObjects loaded.</div>";
    }
    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshSymbolsDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("symbol-list");
    if (!el) return;

    std::ostringstream html;
    const auto& syms = decompiler_.Symbols();
    for (size_t i = 0; i < syms.size(); ++i) {
        const auto& s = syms[i];
        std::string cls = (static_cast<int>(i) == selectedSymbolIdx_) ? "tree-row selected" : "tree-row";
        html << "<div class=\"" << cls << "\" data-action=\"select_symbol\" data-arg=\"" << i << "\">"
             << "<span class=\"sym-kind\">" << (s.isIl2Cpp ? "IL2" : "CLS") << "</span>"
             << "<span class=\"tree-label\">" << SyntaxHighlighter::EscapeRml(s.fullName) << "</span>"
             << "<span class=\"mini-tag\">" << s.methodCount << "m</span>"
             << "</div>";
    }
    if (syms.empty()) {
        html << "<div class=\"empty-state\">No managed or IL2CPP symbols indexed.</div>";
    }
    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshDecompilerDom() {
    if (!document_) return;
    auto* disEl = document_->GetElementById("disasm-view");
    auto* csEl  = document_->GetElementById("csharp-view");
    if (!disEl || !csEl) return;

    if (selectedSymbolIdx_ < 0 || static_cast<size_t>(selectedSymbolIdx_) >= decompiler_.Symbols().size()) {
        disEl->SetInnerRML("<div class=\"empty-state\">Select a script class in the Symbols tree to inspect its IL/Native disassembly listing.</div>");
        csEl->SetInnerRML("<div class=\"empty-state\">Decompiled C# source will appear here.</div>");
        return;
    }

    auto view = decompiler_.InspectSymbol(static_cast<size_t>(selectedSymbolIdx_), &projectDb_);
    disEl->SetInnerRML("<div class=\"code-block\">" + SyntaxHighlighter::HighlightDisassembly(view.disassemblyListing) + "</div>");
    csEl->SetInnerRML("<div class=\"code-block\">" + SyntaxHighlighter::HighlightCSharp(view.decompiledCSharp) + "</div>");
}

void WorkbenchApp::RefreshInspectorDom() {
    if (!document_) return;
    auto* compsEl = document_->GetElementById("inspector-components");
    if (!compsEl) return;

    if (selectedNodeIdx_ < 0 || static_cast<size_t>(selectedNodeIdx_) >= sceneDoc_.Nodes().size()) {
        compsEl->SetInnerRML("<div class=\"empty-state\">Select a GameObject in the 3D Scene or Hierarchy to inspect components.</div>");
        return;
    }

    const auto& n = sceneDoc_.Nodes()[static_cast<size_t>(selectedNodeIdx_)];
    std::ostringstream html;
    for (const auto& c : n.components) {
        html << "<div class=\"insp-card\">"
             << "<div class=\"insp-card-title\">" << SyntaxHighlighter::EscapeRml(c.className)
             << "<span class=\"mini-tag\">#" << c.pathId << "</span></div>";
        if (!c.summary.empty()) {
            html << "<div class=\"insp-card-sub\">" << SyntaxHighlighter::EscapeRml(c.summary) << "</div>";
        }
        html << "</div>";
    }

    // Show live serialized Unity TypeTree YAML for this GameObject
    if (project_ && n.fileIndex < project_->serializedFiles.size() && n.gameObjectPathId != 0) {
        const auto& sf = project_->serializedFiles[n.fileIndex];
        if (const auto* goObj = sf.FindObject(n.gameObjectPathId)) {
            auto valRes = sf.ReadObjectValue(*goObj);
            if (valRes) {
                html << "<div class=\"insp-card\">"
                     << "<div class=\"insp-card-title\">Serialized TypeTree</div>"
                     << "<div class=\"code-block small-code\">"
                     << SyntaxHighlighter::HighlightYaml(valRes.Value().ToYaml())
                     << "</div></div>";
            }
        }
    }
    compsEl->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshAssetPreviewDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("asset-preview-panel");
    if (!el) return;

    if (selectedAssetIdx_ < 0 || static_cast<size_t>(selectedAssetIdx_) >= assetManager_.Entries().size()) {
        el->SetInnerRML("<div class=\"empty-state\">Select any Texture2D, Mesh, Material, AudioClip, or TextAsset in the Project Assets browser.</div>");
        return;
    }

    const auto& e = assetManager_.Entries()[static_cast<size_t>(selectedAssetIdx_)];
    std::ostringstream html;
    html << "<div class=\"preview-header\">"
         << "<div class=\"preview-title\">" << SyntaxHighlighter::EscapeRml(e.name) << "</div>"
         << "<div class=\"badge badge-accent\">" << SyntaxHighlighter::EscapeRml(e.className) << "</div>"
         << "<div class=\"badge\">PathID #" << e.pathId << "</div>"
         << "<div class=\"badge\">" << e.byteSize << " bytes</div>"
         << "</div>";

    if (e.classId == static_cast<i32>(unity::ClassID::Texture2D)) {
        auto texRes = assetManager_.GetTexture(static_cast<size_t>(selectedAssetIdx_));
        if (texRes && texRes.Value()->IsValid()) {
            auto* tex = texRes.Value();
            std::string texKey = "asset_preview_" + std::to_string(++previewTextureGen_);
            backend_->SetDynamicRGBAImage(texKey, tex->width, tex->height, tex->rgbaPixels);

            html << "<div class=\"preview-toolbar\">"
                 << "<div class=\"badge\">" << tex->width << " x " << tex->height << " ("
                 << assets::TextureCodec::FormatName(tex->format) << ")</div>"
                 << "<button class=\"toolbar-btn\" data-action=\"tex_invert\" data-arg=\"\">Invert RGB</button>"
                 << "<button class=\"toolbar-btn\" data-action=\"tex_gray\" data-arg=\"\">Grayscale</button>"
                 << "<button class=\"toolbar-btn\" data-action=\"tex_purple\" data-arg=\"\">Purple Tint</button>"
                 << "<button class=\"toolbar-btn\" data-action=\"tex_flip\" data-arg=\"\">Flip Y</button>"
                 << "<button class=\"toolbar-btn primary\" data-action=\"tex_export\" data-arg=\"\">Export PNG</button>"
                 << "</div>"
                 << "<div class=\"tex-preview-frame\">"
                 << "<img class=\"tex-preview-img\" src=\"coconut://" << texKey << "\" />"
                 << "</div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::Mesh)) {
        auto mRes = assetManager_.GetMesh(static_cast<size_t>(selectedAssetIdx_));
        if (mRes && mRes.Value()->IsValid()) {
            const auto& m = *mRes.Value();
            html << "<div class=\"preview-toolbar\">"
                 << "<div class=\"badge\">Vertices: " << m.positions.size() << "</div>"
                 << "<div class=\"badge\">Triangles: " << (m.indices.size() / 3) << "</div>"
                 << "<div class=\"badge\">SubMeshes: " << m.subMeshes.size() << "</div>"
                 << "<button class=\"toolbar-btn primary\" data-action=\"mesh_export_obj\" data-arg=\"\">Export .OBJ</button>"
                 << "<button class=\"toolbar-btn\" data-action=\"mesh_export_gltf\" data-arg=\"\">Export .glTF</button>"
                 << "</div>"
                 << "<div class=\"insp-card\"><div class=\"insp-card-title\">Mesh Geometry Diagnostics</div>"
                 << "<div class=\"insp-card-sub\">Bounds Min: (" << FmtFloat(m.boundsMin.x) << ", " << FmtFloat(m.boundsMin.y) << ", " << FmtFloat(m.boundsMin.z) << ")</div>"
                 << "<div class=\"insp-card-sub\">Bounds Max: (" << FmtFloat(m.boundsMax.x) << ", " << FmtFloat(m.boundsMax.y) << ", " << FmtFloat(m.boundsMax.z) << ")</div>"
                 << "</div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::AudioClip)) {
        auto aRes = assetManager_.GetAudio(static_cast<size_t>(selectedAssetIdx_));
        if (aRes) {
            const auto& clip = *aRes.Value();
            html << "<div class=\"preview-toolbar\">"
                 << "<div class=\"badge\">" << clip.frequency << " Hz</div>"
                 << "<div class=\"badge\">" << clip.channels << " ch</div>"
                 << "<div class=\"badge\">" << FmtFloat(clip.lengthSeconds) << " sec</div>"
                 << "<button class=\"toolbar-btn primary\" data-action=\"audio_export\" data-arg=\"\">Export .WAV</button>"
                 << "</div><div class=\"waveform-box\">";
            for (float pk : clip.waveformPeaks) {
                int h = std::max(6, static_cast<int>(pk * 110.0f));
                html << "<div class=\"wave-bar\" style=\"height: " << h << "dp;\"></div>";
            }
            html << "</div>";
        }
    } else if (e.classId == static_cast<i32>(unity::ClassID::Material)) {
        auto mRes = assetManager_.GetMaterial(static_cast<size_t>(selectedAssetIdx_));
        if (mRes) {
            const auto& mat = *mRes.Value();
            auto c = mat.PrimaryColor();
            html << "<div class=\"preview-toolbar\">"
                 << "<div class=\"badge\">Shader: " << SyntaxHighlighter::EscapeRml(mat.shaderName) << "</div>"
                 << "<button class=\"toolbar-btn primary\" data-action=\"mat_set_purple\" data-arg=\"\">Set Purple Albedo</button>"
                 << "<button class=\"toolbar-btn\" data-action=\"mat_set_slate\" data-arg=\"\">Set Slate Albedo</button>"
                 << "</div>"
                 << "<div class=\"insp-card\"><div class=\"insp-card-title\">Material Properties</div>"
                 << "<div class=\"insp-card-sub\">Primary Color: RGBA(" << FmtFloat(c.r) << ", " << FmtFloat(c.g) << ", " << FmtFloat(c.b) << ", " << FmtFloat(c.a) << ")</div>";
            for (const auto& f : mat.floats) {
                html << "<div class=\"insp-card-sub\">" << SyntaxHighlighter::EscapeRml(f.name) << " = " << FmtFloat(f.value) << "</div>";
            }
            html << "</div>";
        }
    } else {
        auto txtRes = assetManager_.GetTextAssetContent(static_cast<size_t>(selectedAssetIdx_));
        if (txtRes) {
            html << "<div class=\"code-block\">" << SyntaxHighlighter::HighlightYaml(txtRes.Value()) << "</div>";
        }
    }

    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshHexEditorDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("hex-view");
    if (!el) return;

    const auto* obj = (selectedAssetIdx_ >= 0) ? assetManager_.GetObject(static_cast<size_t>(selectedAssetIdx_)) : nullptr;
    if (!obj || obj->rawData.empty()) {
        el->SetInnerRML("<div class=\"empty-state\">Select an asset to inspect or live-patch its binary bytes.</div>");
        return;
    }

    std::ostringstream oss;
    size_t maxBytes = std::min<size_t>(obj->rawData.size(), 1024);
    for (size_t row = 0; row < maxBytes; row += 16) {
        oss << "<div class=\"code-line\"><span class=\"syn-addr\">" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << row << "</span>&#160;&#160;";
        std::ostringstream ascii;
        for (size_t col = 0; col < 16; ++col) {
            if (col == 8) oss << "&#160;";
            if (row + col < maxBytes) {
                u8 b = obj->rawData[row + col];
                oss << "<span class=\"syn-op\">" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << static_cast<int>(b) << "</span>&#160;";
                ascii << ((b >= 32 && b < 127) ? static_cast<char>(b) : '.');
            } else {
                oss << "&#160;&#160;&#160;";
                ascii << ' ';
            }
        }
        oss << "&#160;<span class=\"syn-comment\">|" << SyntaxHighlighter::EscapeRml(ascii.str()) << "|</span></div>";
    }
    if (obj->rawData.size() > maxBytes) {
        oss << "<div class=\"code-line\"><span class=\"syn-comment\">... showing first " << std::dec << maxBytes << " of " << obj->rawData.size() << " bytes</span></div>";
    }
    el->SetInnerRML("<div class=\"code-block\">" + oss.str() + "</div>");
}

void WorkbenchApp::RefreshAssetsBrowserDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("assets-list");
    if (!el) return;

    auto indices = assetManager_.FilterEntries(
        static_cast<unity::AssetCategory>(std::max(0, assetCategoryFilter_)),
        assetCategoryFilter_ >= 0,
        assetSearchQuery_);

    std::ostringstream html;
    size_t limit = std::min<size_t>(indices.size(), 120);
    for (size_t k = 0; k < limit; ++k) {
        size_t i = indices[k];
        const auto& e = assetManager_.Entries()[i];
        std::string cls = (static_cast<int>(i) == selectedAssetIdx_) ? "asset-chip selected" : "asset-chip";
        html << "<div class=\"" << cls << "\" data-action=\"select_asset\" data-arg=\"" << i << "\">"
             << "<div class=\"asset-chip-type\">" << SyntaxHighlighter::EscapeRml(e.className)
             << (e.dirty ? " *" : "") << "</div>"
             << "<div class=\"asset-chip-name\">" << SyntaxHighlighter::EscapeRml(e.name) << "</div>"
             << "<div class=\"asset-chip-meta\">#" << e.pathId << " | " << e.byteSize << " B</div>"
             << "</div>";
    }
    if (indices.empty()) {
        html << "<div class=\"empty-state\">No assets match this category.</div>";
    }
    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshXRefsDom() {
    if (!document_) return;
    auto* el = document_->GetElementById("xrefs-list");
    if (!el) return;

    const auto& xrefs = decompiler_.AllXRefs();
    std::ostringstream html;
    size_t limit = std::min<size_t>(xrefs.size(), 100);
    for (size_t i = 0; i < limit; ++i) {
        const auto& x = xrefs[i];
        html << "<div class=\"xref-row\">"
             << "<span class=\"mini-tag\">" << SyntaxHighlighter::EscapeRml(x.kind) << "</span> "
             << "<span class=\"syn-type\">" << SyntaxHighlighter::EscapeRml(x.fromSymbol) << "</span>"
             << " -> <span class=\"syn-string\">" << SyntaxHighlighter::EscapeRml(x.toSymbol) << "</span>"
             << "</div>";
    }
    if (xrefs.empty()) {
        html << "<div class=\"empty-state\">No cross-references extracted yet.</div>";
    }
    el->SetInnerRML(html.str());
}

void WorkbenchApp::RefreshExportPanelDom() {
    if (!document_) return;
    if (auto* sumEl = document_->GetElementById("export-summary-text")) {
        sumEl->SetInnerRML(SyntaxHighlighter::EscapeRml(lastExportStatsSummary_));
    }
    if (auto* repEl = document_->GetElementById("fixup-report-view")) {
        if (lastFixupReportMd_.empty()) {
            repEl->SetInnerRML("<div class=\"empty-state\">Run the Export &amp; Fixup Pipeline to generate a full recovery report (decombined static meshes, reconstructed prefabs, and automated C#/Shader build fixes).</div>");
        } else {
            std::istringstream iss(lastFixupReportMd_);
            std::string line;
            std::ostringstream out;
            out << "<div class=\"code-block\">";
            while (std::getline(iss, line)) {
                out << "<div class=\"code-line\">" << SyntaxHighlighter::EscapeRml(line) << "</div>";
            }
            out << "</div>";
            repEl->SetInnerRML(out.str());
        }
    }
}

void WorkbenchApp::RefreshConsole() {
    auto logs = Logger::Instance().Snapshot(80);
    std::ostringstream oss;
    for (const auto& e : logs) {
        oss << '[' << e.timestamp << "] [" << Logger::LevelName(e.level) << "] [" << e.category << "] "
            << e.message << '\n';
    }
    consoleText_ = oss.str();
    if (modelHandle_) modelHandle_.DirtyVariable("console_text");
}

void WorkbenchApp::SelectSceneNode(int nodeIndex) {
    selectedNodeIdx_ = nodeIndex;
    if (nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < sceneDoc_.Nodes().size()) {
        const auto& n = sceneDoc_.Nodes()[static_cast<size_t>(nodeIndex)];
        inspName_ = n.name;
        inspPosX_ = FmtFloat(n.localPosition.x); inspPosY_ = FmtFloat(n.localPosition.y); inspPosZ_ = FmtFloat(n.localPosition.z);
        inspRotX_ = FmtFloat(n.localEulerDeg.x); inspRotY_ = FmtFloat(n.localEulerDeg.y); inspRotZ_ = FmtFloat(n.localEulerDeg.z);
        inspSclX_ = FmtFloat(n.localScale.x);    inspSclY_ = FmtFloat(n.localScale.y);    inspSclZ_ = FmtFloat(n.localScale.z);
        if (modelHandle_) {
            modelHandle_.DirtyVariable("insp_name");
            modelHandle_.DirtyVariable("insp_px"); modelHandle_.DirtyVariable("insp_py"); modelHandle_.DirtyVariable("insp_pz");
            modelHandle_.DirtyVariable("insp_rx"); modelHandle_.DirtyVariable("insp_ry"); modelHandle_.DirtyVariable("insp_rz");
            modelHandle_.DirtyVariable("insp_sx"); modelHandle_.DirtyVariable("insp_sy"); modelHandle_.DirtyVariable("insp_sz");
        }
    }
    RefreshHierarchyDom();
    RefreshInspectorDom();
}

void WorkbenchApp::SelectSymbol(int symbolIndex) {
    selectedSymbolIdx_ = symbolIndex;
    SetCenterTab("decompiler");
    RefreshSymbolsDom();
    RefreshDecompilerDom();
}

void WorkbenchApp::SelectAsset(int entryIndex) {
    selectedAssetIdx_ = entryIndex;
    if (activeCenterTab_ != "hex") {
        SetCenterTab("asset");
    } else {
        RefreshHexEditorDom();
    }
    RefreshAssetsBrowserDom();
    RefreshAssetPreviewDom();
    RefreshHexEditorDom();
}

void WorkbenchApp::HandleDomAction(const std::string& action, const std::string& arg) {
    if (action == "select_node") {
        SelectSceneNode(std::stoi(arg));
    } else if (action == "select_symbol") {
        SelectSymbol(std::stoi(arg));
    } else if (action == "select_asset") {
        SelectAsset(std::stoi(arg));
    } else if (action == "tex_invert" || action == "tex_gray" || action == "tex_purple" || action == "tex_flip") {
        if (selectedAssetIdx_ >= 0) {
            auto tRes = assetManager_.GetTexture(static_cast<size_t>(selectedAssetIdx_));
            if (tRes) {
                if (action == "tex_invert") assets::TextureCodec::InvertRGB(*tRes.Value());
                else if (action == "tex_gray") assets::TextureCodec::ConvertGrayscale(*tRes.Value());
                else if (action == "tex_purple") assets::TextureCodec::ApplyTint(*tRes.Value(), 0.95f, 0.72f, 1.25f);
                else if (action == "tex_flip") assets::TextureCodec::FlipVertical(*tRes.Value());
                assetManager_.ApplyTextureEdits(static_cast<size_t>(selectedAssetIdx_));
                statusMessage_ = "Applied texture edit to " + tRes.Value()->name;
                modelHandle_.DirtyVariable("status_message");
                RefreshAssetPreviewDom();
                RefreshAssetsBrowserDom();
            }
        }
    } else if (action == "tex_export" && selectedAssetIdx_ >= 0) {
        auto tRes = assetManager_.GetTexture(static_cast<size_t>(selectedAssetIdx_));
        if (tRes) {
            auto out = platform::GetAppDataDir() / "exports" / (tRes.Value()->name + ".png");
            if (assets::TextureCodec::ExportPNG(*tRes.Value(), out)) {
                statusMessage_ = "Exported PNG -> " + out.string();
                modelHandle_.DirtyVariable("status_message");
            }
        }
    } else if ((action == "mesh_export_obj" || action == "mesh_export_gltf") && selectedAssetIdx_ >= 0) {
        auto mRes = assetManager_.GetMesh(static_cast<size_t>(selectedAssetIdx_));
        if (mRes) {
            bool isObj = (action == "mesh_export_obj");
            auto out = platform::GetAppDataDir() / "exports" / (mRes.Value()->name + (isObj ? ".obj" : ".gltf"));
            auto r = isObj ? assets::MeshCodec::ExportOBJ(*mRes.Value(), out)
                           : assets::MeshCodec::ExportGLTF(*mRes.Value(), out);
            if (r) {
                statusMessage_ = "Exported Mesh -> " + out.string();
                modelHandle_.DirtyVariable("status_message");
            }
        }
    } else if (action == "audio_export" && selectedAssetIdx_ >= 0) {
        auto aRes = assetManager_.GetAudio(static_cast<size_t>(selectedAssetIdx_));
        if (aRes) {
            auto out = platform::GetAppDataDir() / "exports" / (aRes.Value()->name + ".wav");
            if (assets::AudioCodec::ExportWAV(*aRes.Value(), out)) {
                statusMessage_ = "Exported WAV -> " + out.string();
                modelHandle_.DirtyVariable("status_message");
            }
        }
    } else if ((action == "mat_set_purple" || action == "mat_set_slate") && selectedAssetIdx_ >= 0) {
        auto mRes = assetManager_.GetMaterial(static_cast<size_t>(selectedAssetIdx_));
        if (mRes) {
            mRes.Value()->SetPrimaryColor(action == "mat_set_purple"
                                              ? glm::vec4(0.56f, 0.36f, 0.96f, 1.0f)
                                              : glm::vec4(0.32f, 0.34f, 0.40f, 1.0f));
            assetManager_.ApplyMaterialEdits(static_cast<size_t>(selectedAssetIdx_));
            statusMessage_ = "Updated Material color on " + mRes.Value()->name;
            modelHandle_.DirtyVariable("status_message");
            RefreshAssetPreviewDom();
            RefreshAssetsBrowserDom();
        }
    }
}

void WorkbenchApp::UpdateViewportRect() {
    if (!document_) return;
    auto* img = document_->GetElementById("scene-viewport-img");
    if (!img) return;
    Rml::Vector2f offset = img->GetAbsoluteOffset(Rml::BoxArea::Content);
    Rml::Vector2f size = img->GetBox().GetSize(Rml::BoxArea::Content);
    float dpi = backend_ ? backend_->DpiScale() : 1.0f;
    vpX_ = offset.x / dpi;
    vpY_ = offset.y / dpi;
    vpW_ = size.x / dpi;
    vpH_ = size.y / dpi;

    int targetW = std::clamp(static_cast<int>(size.x), 256, 2560);
    int targetH = std::clamp(static_cast<int>(size.y), 256, 1600);
    if (std::abs(targetW - sceneRenderer_.Width()) > 16 || std::abs(targetH - sceneRenderer_.Height()) > 16) {
        sceneRenderer_.Resize(targetW, targetH);
        backend_->SetExternalGLTexture("scene_viewport", sceneRenderer_.ColorTextureId(),
                                       sceneRenderer_.Width(), sceneRenderer_.Height());
    }
}

bool WorkbenchApp::IsPointInsideViewport(float x, float y, float& normX, float& normY) const {
    if (activeCenterTab_ != "scene" || vpW_ <= 1.0f || vpH_ <= 1.0f) return false;
    if (x < vpX_ || x > vpX_ + vpW_ || y < vpY_ || y > vpY_ + vpH_) return false;
    normX = (x - vpX_) / vpW_;
    normY = (y - vpY_) / vpH_;
    return true;
}

void WorkbenchApp::Update(float dtSeconds) {
    fpsTimer_ += dtSeconds;
    ++frameCount_;
    if (fpsTimer_ >= 0.5f) {
        fps_ = static_cast<int>(static_cast<float>(frameCount_) / fpsTimer_);
        fpsTimer_ = 0.0f;
        frameCount_ = 0;
        if (modelHandle_) modelHandle_.DirtyVariable("fps");
    }
    UpdateViewportRect();
}

void WorkbenchApp::RenderSceneViewport() {
    if (activeCenterTab_ != "scene") return;
    sceneRenderer_.Render(
        sceneDoc_, assetManager_, camera_,
        selectedNodeIdx_, gizmoMode_, activeGizmoAxis_,
        shadingMode_, showGrid_);
}

bool WorkbenchApp::HandleSdlEvent(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;
        if (ctrl && ev.key.key == SDLK_Z) {
            if (history_.Undo()) { RefreshHierarchyDom(); RefreshInspectorDom(); }
            return true;
        }
        if (ctrl && ev.key.key == SDLK_Y) {
            if (history_.Redo()) { RefreshHierarchyDom(); RefreshInspectorDom(); }
            return true;
        }
    }

    if (activeCenterTab_ != "scene") return false;

    float nx = 0, ny = 0;
    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (IsPointInsideViewport(ev.button.x, ev.button.y, nx, ny)) {
            lastMouseX_ = ev.button.x;
            lastMouseY_ = ev.button.y;
            float aspect = vpW_ / std::max(1.0f, vpH_);

            if (ev.button.button == SDL_BUTTON_RIGHT) {
                orbiting_ = true;
                return true;
            }
            if (ev.button.button == SDL_BUTTON_MIDDLE) {
                panning_ = true;
                return true;
            }
            if (ev.button.button == SDL_BUTTON_LEFT) {
                auto ray = camera_.ScreenPointToRay(nx, ny, aspect);
                // First test active 3D gizmo handle on selected node
                if (selectedNodeIdx_ >= 0 && static_cast<size_t>(selectedNodeIdx_) < sceneDoc_.Nodes().size()) {
                    glm::vec3 origin = glm::vec3(sceneDoc_.WorldMatrix(static_cast<size_t>(selectedNodeIdx_))[3]);
                    float gizmoLen = std::clamp(glm::length(camera_.Position() - origin) * 0.16f, 0.8f, 3.5f);
                    auto axis = scene::GizmoMath::PickAxisHandle(ray, origin, gizmoLen);
                    if (axis != scene::GizmoAxis::None) {
                        activeGizmoAxis_ = axis;
                        draggingGizmo_ = true;
                        glm::vec3 axisDir = axis == scene::GizmoAxis::X ? glm::vec3(1, 0, 0) :
                                            axis == scene::GizmoAxis::Y ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
                        gizmoDragStartProj_ = scene::GizmoMath::ProjectRayOntoAxis(ray, origin, axisDir);
                        const auto& n = sceneDoc_.Nodes()[static_cast<size_t>(selectedNodeIdx_)];
                        gizmoDragStartPos_ = n.localPosition;
                        gizmoDragStartRot_ = n.localEulerDeg;
                        gizmoDragStartScale_ = n.localScale;
                        return true;
                    }
                }
                // Otherwise ray-pick scene GameObject
                int picked = sceneRenderer_.PickObject(sceneDoc_, assetManager_, ray);
                if (picked >= 0) {
                    SelectSceneNode(picked);
                    return true;
                }
            }
        }
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (draggingGizmo_ && selectedNodeIdx_ >= 0) {
            size_t idx = static_cast<size_t>(selectedNodeIdx_);
            const auto& n = sceneDoc_.Nodes()[idx];
            glm::vec3 newPos = n.localPosition, newRot = n.localEulerDeg, newScl = n.localScale;
            glm::vec3 oldPos = gizmoDragStartPos_, oldRot = gizmoDragStartRot_, oldScl = gizmoDragStartScale_;
            // Record final gizmo drag in Undo stack
            u64 targetId = n.id;
            history_.PushExecuted(std::make_unique<LambdaCommand>(
                "Gizmo Edit " + n.name,
                [this, targetId, newPos, newRot, newScl]() {
                    int i = sceneDoc_.FindNodeById(targetId);
                    if (i >= 0) sceneDoc_.SetTransform(static_cast<size_t>(i), newPos, newRot, newScl, nullptr);
                },
                [this, targetId, oldPos, oldRot, oldScl]() {
                    int i = sceneDoc_.FindNodeById(targetId);
                    if (i >= 0) sceneDoc_.SetTransform(static_cast<size_t>(i), oldPos, oldRot, oldScl, nullptr);
                }));
            SelectSceneNode(selectedNodeIdx_);
        }
        orbiting_ = false;
        panning_ = false;
        draggingGizmo_ = false;
        activeGizmoAxis_ = scene::GizmoAxis::None;
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        float dx = ev.motion.x - lastMouseX_;
        float dy = ev.motion.y - lastMouseY_;
        lastMouseX_ = ev.motion.x;
        lastMouseY_ = ev.motion.y;
        if (orbiting_) {
            camera_.Orbit(-dx * 0.35f, dy * 0.35f);
            return true;
        }
        if (panning_) {
            camera_.Pan(dx, dy);
            return true;
        }
        if (draggingGizmo_ && selectedNodeIdx_ >= 0 && IsPointInsideViewport(ev.motion.x, ev.motion.y, nx, ny)) {
            float aspect = vpW_ / std::max(1.0f, vpH_);
            auto ray = camera_.ScreenPointToRay(nx, ny, aspect);
            glm::vec3 origin = glm::vec3(sceneDoc_.WorldMatrix(static_cast<size_t>(selectedNodeIdx_))[3]);
            glm::vec3 axisDir = activeGizmoAxis_ == scene::GizmoAxis::X ? glm::vec3(1, 0, 0) :
                                activeGizmoAxis_ == scene::GizmoAxis::Y ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
            float curProj = scene::GizmoMath::ProjectRayOntoAxis(ray, origin, axisDir);
            float delta = curProj - gizmoDragStartProj_;
            auto& n = sceneDoc_.Nodes()[static_cast<size_t>(selectedNodeIdx_)];
            if (gizmoMode_ == scene::GizmoMode::Translate) {
                n.localPosition = gizmoDragStartPos_ + axisDir * delta;
            } else if (gizmoMode_ == scene::GizmoMode::Rotate) {
                n.localEulerDeg = gizmoDragStartRot_ + axisDir * (delta * 25.0f);
            } else if (gizmoMode_ == scene::GizmoMode::Scale) {
                n.localScale = glm::max(glm::vec3(0.1f), gizmoDragStartScale_ + axisDir * delta);
            }
            n.dirty = true;
            return true;
        }
    } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (IsPointInsideViewport(mx, my, nx, ny)) {
            camera_.Zoom(ev.wheel.y > 0 ? 0.88f : 1.14f);
            return true;
        }
    }

    return false;
}

void WorkbenchApp::Shutdown() {
    sceneRenderer_.Shutdown();
    if (document_) {
        if (clickRouter_) {
            document_->RemoveEventListener(Rml::EventId::Click, clickRouter_.get());
        }
        document_->Close();
        document_ = nullptr;
    }
    clickRouter_.reset();
}

} // namespace coconut::ui
