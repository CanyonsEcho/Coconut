#pragma once

#include "ui/RmlBackend.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include "core/ProjectManager.h"
#include "unity/GameLoader.h"
#include "assets/AssetManager.h"
#include "decompiler/DecompilerEngine.h"
#include "scene/SceneDocument.h"
#include "scene/SceneRenderer.h"
#include "export/GameRebuilder.h"
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
                            const std::string& initialView = "manager");
    void Update(float dtSeconds);
    void RenderSceneViewport();
    bool HandleSdlEvent(const SDL_Event& ev);
    void Shutdown();

    // High-level actions
    void OpenProjectEntry(const std::string& projectId);
    void LoadGameFromPath(const std::filesystem::path& path, const std::string& displayName = {});
    void RunExportAndFixupPipeline();
    void RunGameRebuild();
    void SetActiveView(const std::string& view);

private:
    class DomClickRouter;

    void BindDataModel();
    void AttachDomRouters();
    void RefreshAllViews();

    // Per-utility DOM refreshers
    void RefreshProjectManagerDom();
    void RefreshCodeBrowserDom();
    void RefreshDecompilerPanes();
    void RefreshSceneEditorDom();
    void RefreshInspectorDom();
    void RefreshBundleEditorDom();
    void RefreshAssetPreviewDom();
    void RefreshHexEditorDom();
    void RefreshTypeTreeDom();
    void RefreshBuilderDom();
    void RefreshPendingEditsBadge();
    void RefreshConsole();

    void SelectSceneNode(int nodeIndex);
    void SelectSymbol(int symbolIndex);
    void SelectAsset(int entryIndex);
    void HandleDomAction(const std::string& action, const std::string& arg);

    bool IsPointInsideViewport(float x, float y, float& normX, float& normY) const;
    void UpdateViewportRect();
    void DirtyModelVar(const Rml::String& var);

    RmlBackend* backend_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<DomClickRouter> clickRouter_;

    // Core Engine State
    ProjectManager projectManager_;
    std::string activeProjectId_;
    BuildProbeInfo activeBuildProbe_;

    std::optional<unity::LoadedGameProject> project_;
    assets::AssetManager assetManager_;
    decompiler::DecompilerEngine decompiler_;
    scene::SceneDocument sceneDoc_;
    scene::SceneRenderer sceneRenderer_;
    scene::OrbitCamera camera_;
    CommandHistory history_;
    ProjectDatabase projectDb_;

    // Selection & Filter State
    int selectedNodeIdx_ = -1;
    int selectedSymbolIdx_ = -1;
    int selectedAssetIdx_ = -1;
    int assetCategoryFilter_ = -1; // -1 = All
    std::string symbolKindFilter_ = "all";
    Rml::String assetSearchQuery_;
    Rml::String hierarchySearchQuery_;
    Rml::String symbolSearchQuery_;

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

    // Project Manager Form State
    bool showNewProjectForm_ = true;
    Rml::String newProjectName_;
    Rml::String newProjectPath_;

    // Project Exporter State
    Rml::String exportTargetVersion_ = "Match Source";
    Rml::String exportMeshFormat_ = "fbx";
    Rml::String exportAudioFormat_ = "wav";
    Rml::String exportImageFormat_ = "png";
    Rml::String exportScriptMode_ = "hybrid";
    Rml::String exportOutputDir_;
    bool optUseRipper_ = true;
    bool optDecombine_ = true;
    bool optPrefabs_ = true;
    bool optFixErrors_ = true;
    Rml::String ripperCoreBadge_ = "Native C++ Fallback";
    Rml::String exportBackendUsed_ = "Ready";
    Rml::String exportLogText_ = "Select your Target Unity Version and asset formats on the left, then click Export & Fix Up Unity Project.";
    int expStatScenes_ = 0;
    int expStatScripts_ = 0;
    int expStatMeshes_ = 0;
    int expStatDecombined_ = 0;
    int expStatPrefabs_ = 0;
    int expStatFixed_ = 0;

    // Game Exporter / Rebuilder State
    Rml::String builderOutputPath_;
    Rml::String builderCompression_ = "lz4hc";
    bool optCopyUntouched_ = true;
    bool optSyncScene_ = true;
    Rml::String builderLogText_ = "Make edits in the Scene Editor or Bundle Editor, then click Build Patched Game to rebuild your modified Unity build.";

    // Inspector & CodeBrowser Input Bindings
    Rml::String inspName_;
    Rml::String inspPosX_ = "0.00", inspPosY_ = "0.00", inspPosZ_ = "0.00";
    Rml::String inspRotX_ = "0.00", inspRotY_ = "0.00", inspRotZ_ = "0.00";
    Rml::String inspSclX_ = "1.00", inspSclY_ = "1.00", inspSclZ_ = "1.00";
    Rml::String renameInput_;
    Rml::String commentInput_;
    Rml::String hexOffsetInput_ = "0000";
    Rml::String hexByteInput_ = "00";
    Rml::String hexDataTypePreview_ = "Select an asset to inspect bytes";

    // Bound Top-Level UI State
    Rml::String activeView_ = "manager";
    Rml::String sceneBottomTab_ = "project";
    Rml::String bundleCenterTab_ = "preview";
    Rml::String statusMessage_ = "Ready — Create or open a Unity game project in the Project Manager.";
    Rml::String activeProjectLabel_ = "No Project Loaded";
    Rml::String activeProjectPath_ = "—";
    Rml::String projectTitle_ = "No Project Open";
    Rml::String unityVersion_ = "—";
    Rml::String backendName_ = "—";
    Rml::String platformName_ = "—";
    Rml::String sceneName_ = "No Scene";
    Rml::String selectedObjectLabel_ = "None";
    Rml::String selectedObjectTag_ = "Untagged";
    Rml::String selectedSymbolTitle_ = "No Symbol Selected";
    Rml::String selectedSymbolRva_ = "0x00000000";
    Rml::String selectedAssetClass_ = "None";
    Rml::String selectedAssetPathId_ = "—";
    Rml::String consoleText_;
    Rml::String gizmoModeLabel_ = "Translate";
    Rml::String shadingModeLabel_ = "Shaded";
    Rml::String pendingEditsLabel_ = "0 Unsaved Edits";
    int pendingEditsCount_ = 0;
    int statAssets_ = 0;
    int statSymbols_ = 0;
    int statScenes_ = 0;
    int statBundles_ = 0;
    int fps_ = 60;
    float fpsTimer_ = 0.0f;
    int frameCount_ = 0;
};

} // namespace coconut::ui
