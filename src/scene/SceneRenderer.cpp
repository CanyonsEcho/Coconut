#include "scene/SceneRenderer.h"
#include "assets/MeshAsset.h"
#include "unity/ClassIDs.h"
#include "core/Logger.h"

#if defined(COCONUT_USE_GLES3)
#include <GLES3/gl3.h>
#elif defined(RMLUI_GL3_CUSTOM_LOADER)
#include RMLUI_GL3_CUSTOM_LOADER
#else
#include <RmlUi_Include_GL3.h>
#endif

#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <vector>

namespace coconut::scene {

namespace {

#if defined(COCONUT_USE_GLES3)
const char* kMeshVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kMeshFragSrc = R"(#version 300 es
precision highp float;
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec4 uColor;
uniform vec3 uCamPos;
uniform int uMode;
uniform int uSelected;
out vec4 FragColor;
void main() {
    vec3 N = normalize(vNormal);
    if (uMode == 2) {
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    vec3 L = normalize(vec3(0.55, 0.85, 0.45));
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float hemi = N.y * 0.5 + 0.5;
    vec3 amb = mix(vec3(0.11, 0.11, 0.14), vec3(0.24, 0.23, 0.29), hemi);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.35;
    vec3 col = uColor.rgb * (amb + diff * 0.82) + vec3(spec);
    if (uSelected == 1) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 2.2);
        col = mix(col, vec3(0.66, 0.52, 0.98), 0.25 + rim * 0.65);
    }
    FragColor = vec4(col, 1.0);
}
)";

const char* kLineVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uVP;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

const char* kLineFragSrc = R"(#version 300 es
precision highp float;
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";
#else
const char* kMeshVertSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kMeshFragSrc = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec4 uColor;
uniform vec3 uCamPos;
uniform int uMode;
uniform int uSelected;
out vec4 FragColor;
void main() {
    vec3 N = normalize(vNormal);
    if (uMode == 2) {
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    vec3 L = normalize(vec3(0.55, 0.85, 0.45));
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float hemi = N.y * 0.5 + 0.5;
    vec3 amb = mix(vec3(0.11, 0.11, 0.14), vec3(0.24, 0.23, 0.29), hemi);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.35;
    vec3 col = uColor.rgb * (amb + diff * 0.82) + vec3(spec);
    if (uSelected == 1) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 2.2);
        col = mix(col, vec3(0.66, 0.52, 0.98), 0.28 + rim * 0.65);
    }
    FragColor = vec4(col, 1.0);
}
)";

const char* kLineVertSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uVP;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

const char* kLineFragSrc = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";
#endif

u32 CompileShaderStage(GLenum type, const char* src) {
    u32 sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]{};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        COCO_LOG_ERROR("SceneGL", std::string("Shader compile error: ") + log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

u32 LinkProgram(const char* vs, const char* fs) {
    u32 v = CompileShaderStage(GL_VERTEX_SHADER, vs);
    u32 f = CompileShaderStage(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    u32 p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]{};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        COCO_LOG_ERROR("SceneGL", std::string("Shader link error: ") + log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void UploadMeshData(const assets::MeshData& src, u32& vao, u32& vbo, u32& ebo, u32& indexCount) {
    std::vector<float> verts;
    verts.reserve(src.positions.size() * 6);
    for (size_t i = 0; i < src.positions.size(); ++i) {
        verts.push_back(src.positions[i].x);
        verts.push_back(src.positions[i].y);
        verts.push_back(src.positions[i].z);
        glm::vec3 n = (i < src.normals.size()) ? src.normals[i] : glm::vec3(0, 1, 0);
        verts.push_back(n.x);
        verts.push_back(n.y);
        verts.push_back(n.z);
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(src.indices.size() * sizeof(u32)), src.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    indexCount = static_cast<u32>(src.indices.size());
}

} // namespace

SceneRenderer::~SceneRenderer() {
    Shutdown();
}

Result<void> SceneRenderer::Initialize(int width, int height) {
    if (!CompileShaders()) {
        return Result<void>::Err("Failed to compile OpenGL 3D scene shaders");
    }
    if (!CreateFramebuffer(width, height)) {
        return Result<void>::Err("Failed to create 3D scene viewport framebuffer");
    }
    BuildGridAndGizmoMeshes();

    auto box = assets::MeshCodec::CreateBox("FallbackCube", {0.7f, 0.7f, 0.7f});
    UploadMeshData(box, fallbackCube_.vao, fallbackCube_.vbo, fallbackCube_.ebo, fallbackCube_.indexCount);
    fallbackCube_.boundsMin = box.boundsMin;
    fallbackCube_.boundsMax = box.boundsMax;

    initialized_ = true;
    COCO_LOG_INFO("SceneGL", "3D Scene Viewport initialized (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    return Result<void>::Ok();
}

bool SceneRenderer::CompileShaders() {
    meshProgram_ = LinkProgram(kMeshVertSrc, kMeshFragSrc);
    lineProgram_ = LinkProgram(kLineVertSrc, kLineFragSrc);
    return meshProgram_ != 0 && lineProgram_ != 0;
}

bool SceneRenderer::CreateFramebuffer(int w, int h) {
    DestroyFramebuffer();
    width_ = std::max(64, w);
    height_ = std::max(64, h);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

void SceneRenderer::DestroyFramebuffer() {
    if (depthRbo_) { glDeleteRenderbuffers(1, &depthRbo_); depthRbo_ = 0; }
    if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
    if (fbo_)      { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
}

void SceneRenderer::Resize(int width, int height) {
    if (!initialized_) return;
    if (width == width_ && height == height_) return;
    if (width < 64 || height < 64) return;
    CreateFramebuffer(width, height);
}

void SceneRenderer::BuildGridAndGizmoMeshes() {
    std::vector<float> lines;
    auto pushLine = [&](glm::vec3 a, glm::vec3 b, glm::vec3 col) {
        lines.insert(lines.end(), {a.x, a.y, a.z, col.r, col.g, col.b, b.x, b.y, b.z, col.r, col.g, col.b});
    };

    const int half = 16;
    for (int i = -half; i <= half; ++i) {
        if (i == 0) continue;
        glm::vec3 c = (i % 4 == 0) ? glm::vec3(0.28f, 0.27f, 0.34f) : glm::vec3(0.18f, 0.18f, 0.21f);
        pushLine({static_cast<float>(i), 0.0f, -static_cast<float>(half)},
                 {static_cast<float>(i), 0.0f,  static_cast<float>(half)}, c);
        pushLine({-static_cast<float>(half), 0.0f, static_cast<float>(i)},
                 { static_cast<float>(half), 0.0f, static_cast<float>(i)}, c);
    }
    // World X/Z center axes
    pushLine({-static_cast<float>(half), 0.002f, 0.0f}, {static_cast<float>(half), 0.002f, 0.0f}, {0.72f, 0.28f, 0.34f});
    pushLine({0.0f, 0.002f, -static_cast<float>(half)}, {0.0f, 0.002f, static_cast<float>(half)}, {0.42f, 0.50f, 0.85f});

    gridVertCount_ = static_cast<u32>(lines.size() / 6);
    glGenVertexArrays(1, &gridVao_);
    glGenBuffers(1, &gridVbo_);
    glBindVertexArray(gridVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    glGenVertexArrays(1, &gizmoVao_);
    glGenBuffers(1, &gizmoVbo_);
    glBindVertexArray(gizmoVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
    glBufferData(GL_ARRAY_BUFFER, 64 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
}

void SceneRenderer::ClearMeshCache() {
    for (auto& [id, gm] : meshCache_) {
        if (gm.ebo) glDeleteBuffers(1, &gm.ebo);
        if (gm.vbo) glDeleteBuffers(1, &gm.vbo);
        if (gm.vao) glDeleteVertexArrays(1, &gm.vao);
    }
    meshCache_.clear();
}

const SceneRenderer::GpuMesh* SceneRenderer::GetOrUploadMesh(i64 meshPathId, assets::AssetManager& assets) {
    if (meshPathId == 0) return nullptr;
    auto it = meshCache_.find(meshPathId);
    if (it != meshCache_.end()) return &it->second;

    int entryIdx = assets.FindEntryByPathIdAnyFile(meshPathId, static_cast<i32>(unity::ClassID::Mesh));
    if (entryIdx < 0) return &fallbackCube_;

    auto meshRes = assets.GetMesh(static_cast<size_t>(entryIdx));
    if (!meshRes || !meshRes.Value()->IsValid()) return &fallbackCube_;

    GpuMesh gm;
    UploadMeshData(*meshRes.Value(), gm.vao, gm.vbo, gm.ebo, gm.indexCount);
    gm.boundsMin = meshRes.Value()->boundsMin;
    gm.boundsMax = meshRes.Value()->boundsMax;
    auto [ins, _] = meshCache_.emplace(meshPathId, gm);
    return &ins->second;
}

void SceneRenderer::Render(
    const SceneDocument& scene,
    assets::AssetManager& assets,
    const OrbitCamera& camera,
    int selectedNodeIndex,
    GizmoMode /*gizmoMode*/,
    GizmoAxis activeAxis,
    ShadingMode shadingMode,
    bool showGrid) {
    if (!initialized_ || fbo_ == 0) return;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4]{};
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    // Rich dark-gray viewport backdrop
    glClearColor(0.095f, 0.095f, 0.108f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(width_) / static_cast<float>(std::max(1, height_));
    // Flip Y in projection because RmlUi expects top-left UV origin for GL textures
    glm::mat4 proj = camera.ProjectionMatrix(aspect);
    proj[1][1] *= -1.0f;
    glm::mat4 view = camera.ViewMatrix();
    glm::mat4 vp = proj * view;
    glm::vec3 camPos = camera.Position();

    // 1. Draw Floor Grid
    if (showGrid && gridVao_ != 0) {
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(glGetUniformLocation(lineProgram_, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
        glBindVertexArray(gridVao_);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridVertCount_));
        glBindVertexArray(0);
    }

    // 2. Draw Scene Meshes
    glUseProgram(meshProgram_);
    glUniform3fv(glGetUniformLocation(meshProgram_, "uCamPos"), 1, glm::value_ptr(camPos));
    glUniform1i(glGetUniformLocation(meshProgram_, "uMode"), static_cast<int>(shadingMode));

#if !defined(COCONUT_USE_GLES3)
    if (shadingMode == ShadingMode::Wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
#endif

    const auto& nodes = scene.Nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (!node.active) continue;
        const GpuMesh* gm = GetOrUploadMesh(node.meshPathId, assets);
        if (!gm) continue;

        glm::mat4 model = (node.staticBatchSubMeshCount > 0) ? glm::mat4(1.0f) : scene.WorldMatrix(i);
        glm::mat4 mvp = vp * model;

        glm::vec4 color(0.68f, 0.68f, 0.74f, 1.0f);
        if (node.materialPathId != 0) {
            int matEntry = assets.FindEntryByPathIdAnyFile(node.materialPathId, static_cast<i32>(unity::ClassID::Material));
            if (matEntry >= 0) {
                auto matRes = assets.GetMaterial(static_cast<size_t>(matEntry));
                if (matRes) color = matRes.Value()->PrimaryColor();
            }
        }

        glUniformMatrix4fv(glGetUniformLocation(meshProgram_, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(meshProgram_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform4fv(glGetUniformLocation(meshProgram_, "uColor"), 1, glm::value_ptr(color));
        glUniform1i(glGetUniformLocation(meshProgram_, "uSelected"), static_cast<int>(i) == selectedNodeIndex ? 1 : 0);

        glBindVertexArray(gm->vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gm->indexCount), GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);

#if !defined(COCONUT_USE_GLES3)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    // 3. Draw Interactive 3D Transform Gizmo on selected GameObject
    if (selectedNodeIndex >= 0 && static_cast<size_t>(selectedNodeIndex) < nodes.size()) {
        glm::mat4 world = scene.WorldMatrix(static_cast<size_t>(selectedNodeIndex));
        glm::vec3 origin = glm::vec3(world[3]);
        float gizmoLen = std::clamp(glm::length(camPos - origin) * 0.16f, 0.8f, 3.5f);

        std::vector<float> gVerts;
        auto addSeg = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
            gVerts.insert(gVerts.end(), {a.x, a.y, a.z, c.r, c.g, c.b, b.x, b.y, b.z, c.r, c.g, c.b});
        };
        glm::vec3 colX = activeAxis == GizmoAxis::X ? glm::vec3(1.0f, 0.88f, 0.35f) : glm::vec3(0.95f, 0.30f, 0.36f);
        glm::vec3 colY = activeAxis == GizmoAxis::Y ? glm::vec3(1.0f, 0.88f, 0.35f) : glm::vec3(0.35f, 0.88f, 0.45f);
        glm::vec3 colZ = activeAxis == GizmoAxis::Z ? glm::vec3(1.0f, 0.88f, 0.35f) : glm::vec3(0.65f, 0.46f, 0.98f);

        addSeg(origin, origin + glm::vec3(gizmoLen, 0, 0), colX);
        addSeg(origin, origin + glm::vec3(0, gizmoLen, 0), colY);
        addSeg(origin, origin + glm::vec3(0, 0, gizmoLen), colZ);

        glDisable(GL_DEPTH_TEST);
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(glGetUniformLocation(lineProgram_, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
        glBindVertexArray(gizmoVao_);
        glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(gVerts.size() * sizeof(float)), gVerts.data());
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gVerts.size() / 6));
        glBindVertexArray(0);
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

int SceneRenderer::PickObject(
    const SceneDocument& scene,
    assets::AssetManager& assets,
    const Ray& ray) const {
    float bestT = std::numeric_limits<float>::max();
    int bestIdx = -1;

    const auto& nodes = scene.Nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!n.active || n.meshPathId == 0) continue;

        glm::vec3 bmin(-0.5f), bmax(0.5f);
        int meshEntry = assets.FindEntryByPathIdAnyFile(n.meshPathId, static_cast<i32>(unity::ClassID::Mesh));
        if (meshEntry >= 0) {
            auto mRes = assets.GetMesh(static_cast<size_t>(meshEntry));
            if (mRes && mRes.Value()->IsValid()) {
                bmin = mRes.Value()->boundsMin;
                bmax = mRes.Value()->boundsMax;
            }
        }

        glm::mat4 world = scene.WorldMatrix(i);
        glm::vec3 worldMin(1e9f), worldMax(-1e9f);
        for (int cx = 0; cx < 2; ++cx) {
            for (int cy = 0; cy < 2; ++cy) {
                for (int cz = 0; cz < 2; ++cz) {
                    glm::vec3 corner(cx ? bmax.x : bmin.x, cy ? bmax.y : bmin.y, cz ? bmax.z : bmin.z);
                    glm::vec3 wc = glm::vec3(world * glm::vec4(corner, 1.0f));
                    worldMin = glm::min(worldMin, wc);
                    worldMax = glm::max(worldMax, wc);
                }
            }
        }
        // Slight padding so flat planes can be picked cleanly
        worldMin -= glm::vec3(0.05f);
        worldMax += glm::vec3(0.05f);

        auto hit = GizmoMath::IntersectRayAABB(ray, worldMin, worldMax);
        if (hit && *hit < bestT) {
            bestT = *hit;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

void SceneRenderer::Shutdown() {
    if (!initialized_) return;
    ClearMeshCache();
    if (fallbackCube_.vao) {
        glDeleteBuffers(1, &fallbackCube_.ebo);
        glDeleteBuffers(1, &fallbackCube_.vbo);
        glDeleteVertexArrays(1, &fallbackCube_.vao);
        fallbackCube_ = {};
    }
    if (gridVbo_) { glDeleteBuffers(1, &gridVbo_); gridVbo_ = 0; }
    if (gridVao_) { glDeleteVertexArrays(1, &gridVao_); gridVao_ = 0; }
    if (gizmoVbo_) { glDeleteBuffers(1, &gizmoVbo_); gizmoVbo_ = 0; }
    if (gizmoVao_) { glDeleteVertexArrays(1, &gizmoVao_); gizmoVao_ = 0; }
    if (meshProgram_) { glDeleteProgram(meshProgram_); meshProgram_ = 0; }
    if (lineProgram_) { glDeleteProgram(lineProgram_); lineProgram_ = 0; }
    DestroyFramebuffer();
    initialized_ = false;
}

} // namespace coconut::scene
