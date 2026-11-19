#pragma once

#include "ui/RmlBackend.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include "unity/GameLoader.h"
#include "assets/AssetManager.h"
#include "decompiler/DecompilerEngine.h"
#include "scene/SceneDocument.h"
#include "scene/SceneRenderer.h"
#include "fixup/ProjectFixupPipeline.h"

#include <RmlUi/Core.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace coconut::ui {

class WorkbenchApp {
public:
    WorkbenchApp();
    ~WorkbenchApp();

    Result<void> Initialize(RmlBackend& backend, const std::filesystem::path& resourceDir,
                            const std::filesystem::path& initialGamePath = {},
                            const std::string& initialCenterTab = "scene");
    void Update(float dtSeconds);
    void RenderSceneViewport();
    bool HandleSdlEvent(const SDL_Event& ev);
    void Shutdown();

    // High-level actions callable from UI or CLI
    void LoadDemoMonoGame();
    void LoadDemoIl2CppApk();
    void LoadGameFromPath(const std::filesystem::path& path);
    void RunExportAndFixupPipeline();
    void SetCenterTab(const std::string& tab);

private:
    class DomClickRouter;

    void BindDataModel();
    void AttachDomRouters();
    void RefreshAllViews();
    void RefreshHierarchyDom();
    void RefreshSymbolsDom();
    void RefreshDecompilerDom();
    void RefreshInspectorDom();
    void RefreshAssetPreviewDom();
    void RefreshHexEditorDom();
    void RefreshAssetsBrowserDom();
    void RefreshXRefsDom();
    void RefreshConsole();
    void RefreshExportPanelDom();

    void SelectSceneNode(int nodeIndex);
    void SelectSymbol(int symbolIndex);
    void SelectAsset(int entryIndex);
    void HandleDomAction(const std::string& action, const std::string& arg);

    bool IsPointInsideViewport(float x, float y, float& normX, float& normY) const;
    void UpdateViewportRect();

    RmlBackend* backend_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<DomClickRouter> clickRouter_;

    // Core Engine State
    std::optional<unity::LoadedGameProject> project_;
    assets::AssetManager assetManager_;
    decompiler::DecompilerEngine decompiler_;
    scene::SceneDocument sceneDoc_;
    scene::SceneRenderer sceneRenderer_;
    scene::OrbitCamera camera_;
    CommandHistory history_;
    ProjectDatabase projectDb_;

    // Selection & Tool State
    int selectedNodeIdx_ = -1;
    int selectedSymbolIdx_ = -1;
    int selectedAssetIdx_ = -1;
    int assetCategoryFilter_ = -1; // -1 = All
    std::string assetSearchQuery_;
    std::string hierarchySearchQuery_;
    std::string symbolSearchQuery_;
    std::string xrefSearchQuery_;

    scene::GizmoMode gizmoMode_ = scene::GizmoMode::Translate;
    scene::GizmoAxis activeGizmoAxis_ = scene::GizmoAxis::None;
    scene::ShadingMode shadingMode_ = scene::ShadingMode::Shaded;
    bool showGrid_ = true;

    // Viewport mouse interaction state
    float vpX_ = 0, vpY_ = 0, vpW_ = 800, vpH_ = 500;
    bool orbiting_ = false;
    bool panning_ = false;
    bool draggingGizmo_ = false;
    float lastMouseX_ = 0, lastMouseY_ = 0;
    float gizmoDragStartProj_ = 0.0f;
    glm::vec3 gizmoDragStartPos_{0.0f};
    glm::vec3 gizmoDragStartRot_{0.0f};
    glm::vec3 gizmoDragStartScale_{1.0f};
    int previewTextureGen_ = 1;

    // Export / Fixup UI state
    Rml::String targetUnityVersion_ = "2022.3.15f1";
    Rml::String assetRipperPath_;
    bool optPreferAssetRipper_ = true;
    bool optDecombineMeshes_ = true;
    bool optReconstructPrefabs_ = true;
    bool optFixBuildErrors_ = true;
    std::string lastFixupReportMd_;
    std::string lastExportStatsSummary_ = "Configure your target Unity version and fixup passes, then click Export & Fix Up.";

    // Inspector & Tool input bindings
    Rml::String inspName_;
    Rml::String inspPosX_ = "0.00", inspPosY_ = "0.00", inspPosZ_ = "0.00";
    Rml::String inspRotX_ = "0.00", inspRotY_ = "0.00", inspRotZ_ = "0.00";
    Rml::String inspSclX_ = "1.00", inspSclY_ = "1.00", inspSclZ_ = "1.00";
    Rml::String renameInput_;
    Rml::String hexOffsetInput_ = "0000";
    Rml::String hexByteInput_ = "00";

    // Bound UI Header/Tab/Status State
    Rml::String statusMessage_ = "Ready";
    Rml::String projectTitle_ = "No Project";
    Rml::String unityVersion_ = "—";
    Rml::String backendName_ = "—";
    Rml::String platformName_ = "—";
    Rml::String activeCenterTab_ = "scene";
    Rml::String activeLeftTab_ = "hierarchy";
    Rml::String activeBottomTab_ = "assets";
    Rml::String consoleText_;
    Rml::String gizmoModeLabel_ = "Translate";
    Rml::String shadingModeLabel_ = "Shaded";
    Rml::String ripperDetectStatus_ = "Auto (Native Fallback Ready)";
    int fps_ = 60;
    float fpsTimer_ = 0.0f;
    int frameCount_ = 0;
};

} // namespace coconut::ui
