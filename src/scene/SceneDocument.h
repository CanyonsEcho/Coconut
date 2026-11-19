#pragma once

#include "core/Types.h"
#include "core/CommandHistory.h"
#include "unity/SerializedFile.h"
#include "assets/AssetManager.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::scene {

struct ComponentRef {
    i32 classId = 0;
    std::string className;
    i64 pathId = 0;
    size_t fileIndex = 0;
    std::string summary;
};

struct SceneNode {
    u64 id = 0; // Stable runtime node ID (matches GameObject PathID when loaded from SerializedFile)
    size_t fileIndex = 0;
    i64 gameObjectPathId = 0;
    i64 transformPathId = 0;
    i64 meshPathId = 0;
    i64 materialPathId = 0;
    u16 staticBatchFirstSubMesh = 0;
    u16 staticBatchSubMeshCount = 0;

    std::string name;
    bool active = true;
    u32 layer = 0;
    u16 tag = 0;

    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localEulerDeg{0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};

    i64 parentIndex = -1; // index into SceneDocument::nodes_
    std::vector<size_t> children;
    std::vector<ComponentRef> components;

    bool expanded = true;
    bool dirty = false;

    glm::mat4 LocalMatrix() const;
};

class SceneDocument {
public:
    void Clear();

    // Build editable scene graph from a LoadedGameProject + AssetManager
    Result<void> LoadFromProject(unity::LoadedGameProject& project, assets::AssetManager& assets);

    const std::string& SceneName() const { return sceneName_; }
    const std::vector<SceneNode>& Nodes() const { return nodes_; }
    std::vector<SceneNode>& Nodes() { return nodes_; }
    const std::vector<size_t>& RootIndices() const { return rootIndices_; }

    glm::mat4 WorldMatrix(size_t nodeIndex) const;
    int FindNodeById(u64 id) const;
    int FindNodeByName(std::string_view name) const;

    // Live Scene Editing Operations (with Undo/Redo integration)
    void SetTransform(size_t nodeIndex, glm::vec3 pos, glm::vec3 eulerDeg, glm::vec3 scale, CommandHistory* history = nullptr);
    void SetName(size_t nodeIndex, const std::string& newName, CommandHistory* history = nullptr);
    void SetActive(size_t nodeIndex, bool active, CommandHistory* history = nullptr);

    size_t CreateGameObject(const std::string& name, i64 parentIndex = -1, i64 meshPathId = 0, i64 matPathId = 0);
    size_t DuplicateGameObject(size_t nodeIndex);
    bool DeleteGameObject(size_t nodeIndex);
    bool ReparentGameObject(size_t nodeIndex, i64 newParentIndex);

    // Sync edited Transforms / GameObject names/active flags back into the Unity SerializedFile binary objects!
    Result<size_t> SyncBackToSerializedFiles(unity::LoadedGameProject& project);

    // Export the entire live edited scene as a clean Unity YAML (.unity) scene file!
    Result<void> ExportUnitySceneYaml(
        const std::filesystem::path& outPath,
        const std::unordered_map<i64, std::string>& meshGuidMap = {},
        const std::unordered_map<i64, std::string>& materialGuidMap = {}) const;

private:
    void RebuildRoots();

    std::string sceneName_ = "UntitledScene";
    std::vector<SceneNode> nodes_;
    std::vector<size_t> rootIndices_;
    u64 nextSyntheticId_ = 900000;
};

} // namespace coconut::scene
