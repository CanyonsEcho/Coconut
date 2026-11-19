#pragma once

#include "core/Types.h"
#include "scene/SceneDocument.h"
#include "scene/GizmoMath.h"
#include "assets/AssetManager.h"

#include <unordered_map>

namespace coconut::scene {

enum class ShadingMode {
    Shaded,
    Wireframe,
    Normals
};

// Hardware-accelerated OpenGL 3.3 Core / OpenGL ES 3.0 3D Scene Viewport Renderer
// Renders into an offscreen FBO texture displayed seamlessly inside RmlUi!
class SceneRenderer {
public:
    SceneRenderer() = default;
    ~SceneRenderer();

    Result<void> Initialize(int width = 1024, int height = 640);
    void Resize(int width, int height);
    void Shutdown();
    void ClearMeshCache();

    void Render(
        const SceneDocument& scene,
        assets::AssetManager& assets,
        const OrbitCamera& camera,
        int selectedNodeIndex,
        GizmoMode gizmoMode,
        GizmoAxis activeAxis,
        ShadingMode shadingMode,
        bool showGrid);

    // Ray-pick the closest GameObject in the scene
    int PickObject(
        const SceneDocument& scene,
        assets::AssetManager& assets,
        const Ray& ray) const;

    u32 ColorTextureId() const { return colorTex_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    struct GpuMesh {
        u32 vao = 0;
        u32 vbo = 0;
        u32 ebo = 0;
        u32 indexCount = 0;
        glm::vec3 boundsMin{-0.5f};
        glm::vec3 boundsMax{0.5f};
    };

    bool CreateFramebuffer(int w, int h);
    void DestroyFramebuffer();
    bool CompileShaders();
    void BuildGridAndGizmoMeshes();
    const GpuMesh* GetOrUploadMesh(i64 meshPathId, assets::AssetManager& assets);

    u32 fbo_ = 0;
    u32 colorTex_ = 0;
    u32 depthRbo_ = 0;
    int width_ = 0;
    int height_ = 0;

    u32 meshProgram_ = 0;
    u32 lineProgram_ = 0;

    u32 gridVao_ = 0;
    u32 gridVbo_ = 0;
    u32 gridVertCount_ = 0;

    u32 gizmoVao_ = 0;
    u32 gizmoVbo_ = 0;

    GpuMesh fallbackCube_;
    std::unordered_map<i64, GpuMesh> meshCache_;
    bool initialized_ = false;
};

} // namespace coconut::scene
