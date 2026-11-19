#pragma once

#include "core/Types.h"
#include "scene/SceneDocument.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::fixup {

struct ReconstructedPrefab {
    std::string prefabName;
    std::filesystem::path prefabPath;
    std::string guid;
    size_t instanceCount = 0;
    std::vector<std::string> instanceNames;
};

struct PrefabReconstructResult {
    size_t prefabsCreated = 0;
    size_t sceneInstancesLinked = 0;
    std::vector<ReconstructedPrefab> prefabs;
    std::vector<std::string> logs;
};

// Detects structurally repeated GameObject subtrees across Unity scenes,
// synthesizes clean .prefab YAML assets + .meta files in Assets/Prefabs/Reconstructed/,
// and rewrites scene instances into Unity PrefabInstance (!u!1001) blocks with property overrides.
class PrefabReconstructor {
public:
    static Result<PrefabReconstructResult> RunOnProject(
        const std::filesystem::path& unityProjectDir,
        const scene::SceneDocument* liveScene = nullptr);
};

} // namespace coconut::fixup
