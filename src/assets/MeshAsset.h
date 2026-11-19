#pragma once

#include "core/Types.h"
#include "unity/SerializedFile.h"
#include "unity/GameLoader.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::assets {

struct SubMeshInfo {
    u32 firstByte = 0;
    u32 indexCount = 0;
    i32 topology = 0; // 0 = Triangles
    u32 firstVertex = 0;
    u32 vertexCount = 0;
};

struct MeshData {
    std::string name;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> colors;
    std::vector<u32> indices;
    std::vector<SubMeshInfo> subMeshes;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool IsValid() const { return !positions.empty() && !indices.empty(); }
    void RecalculateBounds();
    void RecalculateNormals();
    void Transform(const glm::mat4& matrix);
};

class MeshCodec {
public:
    static Result<MeshData> ParseFromObject(
        const unity::SerializedFile& sf,
        const unity::ObjectInfo& obj,
        const unity::LoadedGameProject* project = nullptr);

    // Export MeshData to standard Wavefront .OBJ or glTF 2.0 (.gltf embedded JSON)
    static Result<void> ExportOBJ(const MeshData& mesh, const std::filesystem::path& objPath);
    static Result<void> ExportGLTF(const MeshData& mesh, const std::filesystem::path& gltfPath);

    // Procedural primitive generators (also used for demo game & gizmos)
    static MeshData CreateBox(const std::string& name, glm::vec3 size = {1.0f, 1.0f, 1.0f});
    static MeshData CreateSphere(const std::string& name, float radius = 0.5f, int stacks = 16, int slices = 24);
    static MeshData CreateCylinder(const std::string& name, float radius = 0.5f, float height = 1.5f, int slices = 20);
    static MeshData CreatePlane(const std::string& name, float width = 12.0f, float depth = 12.0f);
    static MeshData CombineMeshes(const std::string& name, const std::vector<std::pair<MeshData, glm::mat4>>& parts);
};

} // namespace coconut::assets
