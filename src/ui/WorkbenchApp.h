#pragma once

#include "ui/RmlBackend.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include <RmlUi/Core.h>
#include <filesystem>
#include <string>

namespace coconut::ui {

class WorkbenchApp {
public:
    WorkbenchApp() = default;
    ~WorkbenchApp();

    Result<void> Initialize(RmlBackend& backend, const std::filesystem::path& resourceDir);
    void Update(float dtSeconds);
    void RenderSceneViewport();
    bool HandleSdlEvent(const SDL_Event& ev);
    void Shutdown();

private:
    void BindDataModel();
    void RefreshConsole();

    RmlBackend* backend_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;

    // Bound UI state
    Rml::String statusMessage_ = "Ready — Open a Unity build or load the built-in sample project";
    Rml::String projectTitle_ = "No Project Loaded";
    Rml::String unityVersion_ = "—";
    Rml::String backendName_ = "—";
    Rml::String platformName_ = "—";
    Rml::String activeCenterTab_ = "scene";
    Rml::String activeLeftTab_ = "hierarchy";
    Rml::String activeBottomTab_ = "assets";
    Rml::String consoleText_;
    int fps_ = 60;
    float fpsTimer_ = 0.0f;
    int frameCount_ = 0;

    CommandHistory history_;
    ProjectDatabase projectDb_;
};

} // namespace coconut::ui
