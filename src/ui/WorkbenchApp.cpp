#include "ui/WorkbenchApp.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <sstream>

namespace coconut::ui {

WorkbenchApp::~WorkbenchApp() {
    Shutdown();
}

Result<void> WorkbenchApp::Initialize(RmlBackend& backend, const std::filesystem::path& resourceDir) {
    backend_ = &backend;
    BindDataModel();

    auto docPath = resourceDir / "ui" / "main_window.rml";
    document_ = backend_->Context()->LoadDocument(docPath.string());
    if (!document_) {
        return Result<void>::Err("Failed to load RmlUi document: " + docPath.string());
    }
    document_->Show();

    COCO_LOG_INFO("UI", std::string("Coconut workbench initialized on ") + std::string(platform::HostPlatformName()));
    RefreshConsole();
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
    ctor.Bind("fps", &fps_);

    ctor.BindEventCallback("set_center_tab", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            activeCenterTab_ = args[0].Get<Rml::String>();
            h.DirtyVariable("active_center_tab");
        }
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
        }
    });

    modelHandle_ = ctor.GetModelHandle();
}

void WorkbenchApp::RefreshConsole() {
    auto logs = Logger::Instance().Snapshot(120);
    std::ostringstream oss;
    for (const auto& e : logs) {
        oss << '[' << e.timestamp << "] [" << Logger::LevelName(e.level) << "] [" << e.category << "] "
            << e.message << '\n';
    }
    consoleText_ = oss.str();
    if (modelHandle_) {
        modelHandle_.DirtyVariable("console_text");
    }
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
}

void WorkbenchApp::RenderSceneViewport() {
    // 3D OpenGL/GLES scene viewport renderer hooks in Milestone 4
}

bool WorkbenchApp::HandleSdlEvent(const SDL_Event&) {
    return false;
}

void WorkbenchApp::Shutdown() {
    if (document_) {
        document_->Close();
        document_ = nullptr;
    }
}

} // namespace coconut::ui
