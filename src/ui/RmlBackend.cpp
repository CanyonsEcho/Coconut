#include "ui/RmlBackend.h"
#include "core/Logger.h"

#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <RmlUi/Debugger.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(COCONUT_USE_GLES3)
#include <GLES3/gl3.h>
#elif defined(RMLUI_GL3_CUSTOM_LOADER)
#include RMLUI_GL3_CUSTOM_LOADER
#else
#include <RmlUi_Include_GL3.h>
#endif

#include <memory>
#include <vector>

namespace coconut::ui {

namespace {

class CoconutFileInterface : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        FILE* fp = fopen(path.c_str(), "rb");
        return reinterpret_cast<Rml::FileHandle>(fp);
    }
    void Close(Rml::FileHandle file) override {
        if (file) fclose(reinterpret_cast<FILE*>(file));
    }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return file ? fread(buffer, 1, size, reinterpret_cast<FILE*>(file)) : 0;
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return file && fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override {
        return file ? static_cast<size_t>(ftell(reinterpret_cast<FILE*>(file))) : 0;
    }
};

class CoconutRenderInterface : public RenderInterface_GL3 {
public:
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
        int w = 0, h = 0, ch = 0;
        stbi_uc* pixels = stbi_load(source.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            return RenderInterface_GL3::LoadTexture(texture_dimensions, source);
        }
        texture_dimensions.x = w;
        texture_dimensions.y = h;
        // Premultiply alpha for RmlUi GL3 renderer
        size_t total = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        for (size_t i = 0; i < total; i += 4) {
            float a = pixels[i + 3] / 255.0f;
            pixels[i + 0] = static_cast<stbi_uc>(pixels[i + 0] * a);
            pixels[i + 1] = static_cast<stbi_uc>(pixels[i + 1] * a);
            pixels[i + 2] = static_cast<stbi_uc>(pixels[i + 2] * a);
        }
        Rml::TextureHandle handle = GenerateTexture({pixels, total}, texture_dimensions);
        stbi_image_free(pixels);
        return handle;
    }
};

} // namespace

struct RmlBackend::Impl {
    std::unique_ptr<SystemInterface_SDL> systemInterface;
    std::unique_ptr<CoconutRenderInterface> renderInterface;
    std::unique_ptr<CoconutFileInterface> fileInterface;
};

RmlBackend::RmlBackend() : impl_(new Impl()) {}

RmlBackend::~RmlBackend() {
    Shutdown();
    delete impl_;
}

Result<void> RmlBackend::Initialize(
    const std::string& title,
    int width,
    int height,
    const std::filesystem::path& resourceDir) {
    width_ = width;
    height_ = height;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return Result<void>::Err(std::string("SDL_Init failed: ") + SDL_GetError());
    }

#if defined(COCONUT_USE_GLES3)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(title.c_str(), width_, height_, flags);
    if (!window_) {
        return Result<void>::Err(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        return Result<void>::Err(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);

    Rml::String glErr;
    if (!RmlGL3::Initialize(&glErr)) {
        return Result<void>::Err("RmlGL3::Initialize failed: " + glErr);
    }

    impl_->systemInterface = std::make_unique<SystemInterface_SDL>(window_);
    impl_->renderInterface = std::make_unique<CoconutRenderInterface>();
    impl_->fileInterface = std::make_unique<CoconutFileInterface>();

    Rml::SetSystemInterface(impl_->systemInterface.get());
    Rml::SetRenderInterface(impl_->renderInterface.get());
    Rml::SetFileInterface(impl_->fileInterface.get());

    if (!Rml::Initialise()) {
        return Result<void>::Err("Rml::Initialise failed");
    }

    // Load bundled OFL fonts (IBM Plex Sans + JetBrains Mono)
    auto fontDir = resourceDir / "fonts";
    const char* fonts[] = {
        "IBMPlexSans-Regular.ttf",
        "IBMPlexSans-Medium.ttf",
        "IBMPlexSans-SemiBold.ttf",
        "IBMPlexSans-Bold.ttf",
        "JetBrainsMono-Regular.ttf",
        "JetBrainsMono-Bold.ttf"
    };
    for (const char* f : fonts) {
        auto fp = fontDir / f;
        if (std::filesystem::exists(fp)) {
            Rml::LoadFontFace(fp.string());
        } else {
            COCO_LOG_WARN("UI", "Font not found: " + fp.string());
        }
    }

    int pixW = width_, pixH = height_;
    SDL_GetWindowSizeInPixels(window_, &pixW, &pixH);
    width_ = pixW;
    height_ = pixH;
    dpiScale_ = SDL_GetWindowDisplayScale(window_);
    if (dpiScale_ <= 0.0f) dpiScale_ = 1.0f;

    impl_->renderInterface->SetViewport(width_, height_);
    context_ = Rml::CreateContext("coconut_main", Rml::Vector2i(width_, height_));
    if (!context_) {
        return Result<void>::Err("Rml::CreateContext failed");
    }
    context_->SetDensityIndependentPixelRatio(dpiScale_);
    Rml::Debugger::Initialise(context_);

    initialized_ = true;
    return Result<void>::Ok();
}

bool RmlBackend::PollEvents(const std::function<bool(const SDL_Event&)>& customHandler) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F8) {
            Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
        }
        if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            width_ = ev.window.data1;
            height_ = ev.window.data2;
            impl_->renderInterface->SetViewport(width_, height_);
            if (context_) {
                context_->SetDimensions(Rml::Vector2i(width_, height_));
            }
        }
        if (customHandler && customHandler(ev)) {
            continue;
        }
        if (context_) {
            RmlSDL::InputEventHandler(context_, window_, ev);
        }
    }
    return true;
}

void RmlBackend::BeginFrame() {
    if (!initialized_) return;
    glViewport(0, 0, width_, height_);
    glClearColor(0.075f, 0.086f, 0.102f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void RmlBackend::RenderUI() {
    if (!initialized_ || !context_) return;
    context_->Update();
    impl_->renderInterface->BeginFrame();
    context_->Render();
    impl_->renderInterface->EndFrame();
}

void RmlBackend::EndFrame() {
    if (!initialized_) return;
    SDL_GL_SwapWindow(window_);
}

Result<void> RmlBackend::CaptureScreenshot(const std::filesystem::path& pngPath) {
    if (!initialized_ || width_ <= 0 || height_ <= 0) {
        return Result<void>::Err("Renderer not initialized");
    }
    std::vector<u8> pixels(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically from OpenGL bottom-left origin to top-left PNG
    std::vector<u8> flipped(pixels.size());
    size_t rowBytes = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        std::memcpy(
            flipped.data() + static_cast<size_t>(y) * rowBytes,
            pixels.data() + static_cast<size_t>(height_ - 1 - y) * rowBytes,
            rowBytes);
    }

    if (pngPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(pngPath.parent_path(), ec);
    }
    int ok = stbi_write_png(pngPath.string().c_str(), width_, height_, 4, flipped.data(), static_cast<int>(rowBytes));
    if (!ok) {
        return Result<void>::Err("stbi_write_png failed for: " + pngPath.string());
    }
    return Result<void>::Ok();
}

void RmlBackend::Shutdown() {
    if (!initialized_) return;
    Rml::Shutdown();
    RmlGL3::Shutdown();
    if (glContext_) {
        SDL_GL_DestroyContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
    initialized_ = false;
}

} // namespace coconut::ui
