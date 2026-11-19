#include "ui/RmlBackend.h"
#include "ui/WorkbenchApp.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <SDL3/SDL_main.h>
#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::filesystem::path screenshotPath;
    std::filesystem::path initialGamePath;
    std::string initialTab = "scene";
    bool runExportOnBoot = false;
    bool smokeExit = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--capture-screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
            smokeExit = true;
        } else if (arg == "--tab" && i + 1 < argc) {
            initialTab = argv[++i];
        } else if (arg == "--open" && i + 1 < argc) {
            initialGamePath = argv[++i];
        } else if (arg == "--run-export") {
            runExportOnBoot = true;
        } else if (arg == "--smoke-test") {
            smokeExit = true;
        }
    }

    auto resDir = coconut::platform::GetResourceDir();
    COCO_LOG_INFO("Boot", "Coconut " COCONUT_VERSION " starting (resources: " + resDir.string() + ")");

    coconut::ui::RmlBackend backend;
    auto initRes = backend.Initialize("Coconut — Unity Mono/IL2CPP Decompiler & Editor", 1560, 920, resDir);
    if (!initRes) {
        COCO_LOG_ERROR("Boot", initRes.Error());
        return 1;
    }

    coconut::ui::WorkbenchApp app;
    auto appRes = app.Initialize(backend, resDir, initialGamePath, initialTab);
    if (!appRes) {
        COCO_LOG_ERROR("Boot", appRes.Error());
        return 1;
    }
    if (runExportOnBoot) {
        app.RunExportAndFixupPipeline();
    }

    auto prevTime = std::chrono::steady_clock::now();
    int framesRendered = 0;

    while (backend.PollEvents([&](const SDL_Event& ev) { return app.HandleSdlEvent(ev); })) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prevTime).count();
        prevTime = now;

        app.Update(dt);

        backend.BeginFrame();
        app.RenderSceneViewport();
        backend.RenderUI();

        ++framesRendered;
        if (!screenshotPath.empty() && framesRendered == 4) {
            auto cap = backend.CaptureScreenshot(screenshotPath);
            if (!cap) {
                COCO_LOG_ERROR("Boot", cap.Error());
            } else {
                COCO_LOG_INFO("Boot", "Captured screenshot to " + screenshotPath.string());
            }
        }

        backend.EndFrame();

        if (smokeExit && framesRendered >= 5) {
            break;
        }
    }

    app.Shutdown();
    backend.Shutdown();
    return 0;
}
