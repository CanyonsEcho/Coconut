#pragma once

#include "core/Types.h"
#include "scene/SceneDocument.h"
#include "assets/AssetManager.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::fixup {

struct DecombinedMeshEntry {
    std::string gameObjectName;
    std::string sourceMeshName;
    std::filesystem::path outputMeshPath;
    std::string newGuid;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
};

struct MeshDecombineResult {
    size_t combinedMeshesFound = 0;
    size_t meshesSplit = 0;
    size_t sceneReferencesRewired = 0;
    std::vector<DecombinedMeshEntry> entries;
    std::vector<std::string> logs;
};

class MeshDecombiner {
public:
    // Splits a single combined MeshData into geometric connected-component islands (BFS over shared vertices)
    // or explicit submeshes when present.
    static std::vector<assets::MeshData> SplitMeshIslands(const assets::MeshData& combined);

    // Runs static-batch mesh decombining on an exported Unity project directory,
    // writing local-space standalone meshes to Assets/Meshes/Decombined/ and rewriting .unity references.
    static Result<MeshDecombineResult> RunOnProject(
        const std::filesystem::path& unityProjectDir,
        const scene::SceneDocument* liveScene = nullptr,
        assets::AssetManager* liveAssets = nullptr);
};

} // namespace coconut::fixup
