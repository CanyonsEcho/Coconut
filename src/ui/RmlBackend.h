#pragma once

#include "core/Types.h"
#include <SDL3/SDL.h>
#include <RmlUi/Core.h>
#include <filesystem>
#include <functional>
#include <string>

namespace coconut::ui {

class RmlBackend {
public:
    RmlBackend();
    ~RmlBackend();

    Result<void> Initialize(const std::string& title, int width, int height, const std::filesystem::path& resourceDir);
    void Shutdown();

    bool PollEvents(const std::function<bool(const SDL_Event&)>& customHandler = nullptr);
    void BeginFrame();
    void RenderUI();
    void EndFrame();

    // Capture the live rendered OpenGL/GLES framebuffer to a PNG file (for verification / screenshots).
    Result<void> CaptureScreenshot(const std::filesystem::path& pngPath);

    Rml::Context* Context() const { return context_; }
    SDL_Window* Window() const { return window_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    float DpiScale() const { return dpiScale_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    Rml::Context* context_ = nullptr;
    int width_ = 1600;
    int height_ = 960;
    float dpiScale_ = 1.0f;
    bool initialized_ = false;
};

} // namespace coconut::ui
