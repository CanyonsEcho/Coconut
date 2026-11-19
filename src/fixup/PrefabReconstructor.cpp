#include "fixup/PrefabReconstructor.h"
#include "export/UnityProjectExporter.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace coconut::fixup {

namespace {

// Normalize instance names like "SupplyCrate_01" or "Enemy (2)" -> "SupplyCrate" / "Enemy"
std::string BasePrefabName(std::string name) {
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (!name.empty() && name.back() == ')') {
        size_t open = name.rfind('(');
        if (open != std::string::npos) name.resize(open);
    }
    while (!name.empty() && (std::isdigit(static_cast<unsigned char>(name.back())) ||
                             name.back() == '_' || name.back() == '-' || name.back() == ' ')) {
        name.pop_back();
    }
    return name.empty() ? "ReconstructedPrefab" : name;
}

std::string ComputeSubtreeSignature(const scene::SceneDocument& doc, size_t idx) {
    const auto& n = doc.Nodes()[idx];
    std::ostringstream sig;
    sig << "M=" << n.meshPathId << ";Mat=" << n.materialPathId << ";C=[";
    std::vector<i32> cids;
    for (const auto& c : n.components) cids.push_back(c.classId);
    std::sort(cids.begin(), cids.end());
    for (i32 id : cids) sig << id << ",";
    sig << "];K={";
    std::vector<std::string> childSigs;
    for (size_t childIdx : n.children) {
        childSigs.push_back(ComputeSubtreeSignature(doc, childIdx));
    }
    std::sort(childSigs.begin(), childSigs.end());
    for (const auto& cs : childSigs) sig << "(" << cs << ")";
    sig << "}";
    return sig.str();
}

void WritePrefabNodeRecursive(
    std::ostringstream& y,
    const scene::SceneDocument& doc,
    size_t idx,
    u64 parentTfId,
    u64& nextFileId,
    bool isRoot) {
    const auto& n = doc.Nodes()[idx];
    u64 goId = nextFileId++;
    u64 tfId = nextFileId++;
    u64 mfId = n.meshPathId != 0 ? nextFileId++ : 0;
    u64 mrId = n.meshPathId != 0 ? nextFileId++ : 0;

    std::vector<u64> childTfIds;
    std::ostringstream childrenYaml;
    for (size_t childIdx : n.children) {
        u64 childTf = nextFileId + 1; // child's tfId follows its goId
        childTfIds.push_back(childTf);
        WritePrefabNodeRecursive(childrenYaml, doc, childIdx, tfId, nextFileId, false);
    }

    y << "--- !u!1 &" << goId << "\n"
      << "GameObject:\n"
      << "  m_Component:\n"
      << "  - component: {fileID: " << tfId << "}\n";
    if (mfId) y << "  - component: {fileID: " << mfId << "}\n";
    if (mrId) y << "  - component: {fileID: " << mrId << "}\n";
    y << "  m_Layer: " << n.layer << "\n"
      << "  m_Name: " << (isRoot ? BasePrefabName(n.name) : n.name) << "\n"
      << "  m_IsActive: " << (n.active ? 1 : 0) << "\n";

    glm::vec3 pos = isRoot ? glm::vec3(0.0f) : n.localPosition;
    glm::quat q = isRoot ? glm::quat(1, 0, 0, 0) : glm::quat(glm::radians(n.localEulerDeg));
    y << "--- !u!4 &" << tfId << "\n"
      << "Transform:\n"
      << "  m_GameObject: {fileID: " << goId << "}\n"
      << "  m_LocalRotation: {x: " << q.x << ", y: " << q.y << ", z: " << q.z << ", w: " << q.w << "}\n"
      << "  m_LocalPosition: {x: " << pos.x << ", y: " << pos.y << ", z: " << pos.z << "}\n"
      << "  m_LocalScale: {x: " << n.localScale.x << ", y: " << n.localScale.y << ", z: " << n.localScale.z << "}\n"
      << "  m_Children:\n";
    for (u64 cid : childTfIds) y << "  - {fileID: " << cid << "}\n";
    y << "  m_Father: {fileID: " << parentTfId << "}\n";

    if (mfId) {
        std::string meshGuid = exporter::UnityProjectExporter::DeterministicGuid("mesh:" + std::to_string(n.meshPathId));
        y << "--- !u!33 &" << mfId << "\n"
          << "MeshFilter:\n"
          << "  m_GameObject: {fileID: " << goId << "}\n"
          << "  m_Mesh: {fileID: 4300000, guid: " << meshGuid << ", type: 2}\n";
    }
    if (mrId) {
        std::string matGuid = exporter::UnityProjectExporter::DeterministicGuid("mat:" + std::to_string(n.materialPathId));
        y << "--- !u!23 &" << mrId << "\n"
          << "MeshRenderer:\n"
          << "  m_GameObject: {fileID: " << goId << "}\n"
          << "  m_Enabled: 1\n"
          << "  m_Materials:\n"
          << "  - {fileID: 2100000, guid: " << matGuid << ", type: 2}\n";
    }

    y << childrenYaml.str();
}

} // namespace

Result<PrefabReconstructResult> PrefabReconstructor::RunOnProject(
    const std::filesystem::path& unityProjectDir,
    const scene::SceneDocument* liveScene) {
    PrefabReconstructResult result;
    if (!liveScene || liveScene->Nodes().empty()) {
        return Result<PrefabReconstructResult>::Ok(result);
    }

    auto prefabsDir = unityProjectDir / "Assets" / "Prefabs" / "Reconstructed";
    std::error_code ec;
    std::filesystem::create_directories(prefabsDir, ec);
    exporter::UnityProjectExporter::WriteMetaFile(
        unityProjectDir / "Assets" / "Prefabs",
        exporter::UnityProjectExporter::DeterministicGuid("folder:Prefabs"), "Folder");
    exporter::UnityProjectExporter::WriteMetaFile(
        prefabsDir, exporter::UnityProjectExporter::DeterministicGuid("folder:ReconstructedPrefabs"), "Folder");

    // Cluster root-level GameObjects by structural subtree signature
    std::unordered_map<std::string, std::vector<size_t>> clusters;
    for (size_t rootIdx : liveScene->RootIndices()) {
        const auto& n = liveScene->Nodes()[rootIdx];
        if (n.meshPathId == 0 && n.children.empty()) continue; // Skip standalone camera/lights
        std::string sig = ComputeSubtreeSignature(*liveScene, rootIdx);
        clusters[sig].push_back(rootIdx);
    }

    std::ostringstream prefabInstancesYaml;
    prefabInstancesYaml << std::fixed << std::setprecision(5);
    u64 nextInstanceFileId = 700000;

    for (const auto& [sig, indices] : clusters) {
        if (indices.size() < 2) continue; // Only reconstruct genuinely repeated structures

        size_t templateIdx = indices.front();
        std::string baseName = BasePrefabName(liveScene->Nodes()[templateIdx].name);
        auto prefabPath = prefabsDir / (baseName + ".prefab");
        std::string guid = exporter::UnityProjectExporter::DeterministicGuid("prefab:" + baseName);

        std::ostringstream y;
        y << "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n" << std::fixed << std::setprecision(5);
        u64 fileIdCounter = 100001;
        WritePrefabNodeRecursive(y, *liveScene, templateIdx, 0, fileIdCounter, true);

        auto writeRes = platform::WriteFileText(prefabPath, y.str());
        if (!writeRes) continue;
        exporter::UnityProjectExporter::WriteMetaFile(prefabPath, guid, "PrefabImporter");

        ReconstructedPrefab rp;
        rp.prefabName = baseName;
        rp.prefabPath = prefabPath;
        rp.guid = guid;
        rp.instanceCount = indices.size();

        for (size_t idx : indices) {
            const auto& inst = liveScene->Nodes()[idx];
            rp.instanceNames.push_back(inst.name);
            glm::quat q = glm::quat(glm::radians(inst.localEulerDeg));
            u64 piId = nextInstanceFileId++;
            prefabInstancesYaml
                << "--- !u!1001 &" << piId << "\n"
                << "PrefabInstance:\n"
                << "  m_ObjectHideFlags: 0\n"
                << "  serializedVersion: 2\n"
                << "  m_Modification:\n"
                << "    m_TransformParent: {fileID: 0}\n"
                << "    m_Modifications:\n"
                << "    - target: {fileID: 100001, guid: " << guid << ", type: 3}\n"
                << "      propertyPath: m_Name\n"
                << "      value: " << inst.name << "\n"
                << "      objectReference: {fileID: 0}\n"
                << "    - target: {fileID: 100002, guid: " << guid << ", type: 3}\n"
                << "      propertyPath: m_LocalPosition.x\n"
                << "      value: " << inst.localPosition.x << "\n"
                << "      objectReference: {fileID: 0}\n"
                << "    - target: {fileID: 100002, guid: " << guid << ", type: 3}\n"
                << "      propertyPath: m_LocalPosition.y\n"
                << "      value: " << inst.localPosition.y << "\n"
                << "      objectReference: {fileID: 0}\n"
                << "    - target: {fileID: 100002, guid: " << guid << ", type: 3}\n"
                << "      propertyPath: m_LocalPosition.z\n"
                << "      value: " << inst.localPosition.z << "\n"
                << "      objectReference: {fileID: 0}\n"
                << "    - target: {fileID: 100002, guid: " << guid << ", type: 3}\n"
                << "      propertyPath: m_LocalRotation.w\n"
                << "      value: " << q.w << "\n"
                << "      objectReference: {fileID: 0}\n"
                << "  m_SourcePrefab: {fileID: 100100000, guid: " << guid << ", type: 3}\n";
            ++result.sceneInstancesLinked;
        }

        result.logs.push_back("Reconstructed Prefab '" + baseName + ".prefab' from " +
                              std::to_string(rp.instanceCount) + " repeated scene hierarchies");
        result.prefabs.push_back(std::move(rp));
        ++result.prefabsCreated;
    }

    // Append PrefabInstance links into exported scene YAML
    if (result.sceneInstancesLinked > 0) {
        auto scenesDir = unityProjectDir / "Assets" / "Scenes";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(scenesDir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".unity") continue;
            auto txt = platform::ReadFileText(entry.path());
            if (txt) {
                platform::WriteFileText(entry.path(), txt.Value() + "\n" + prefabInstancesYaml.str());
                break;
            }
        }
    }

    COCO_LOG_INFO("PrefabReconstructor", "Created " + std::to_string(result.prefabsCreated) +
                                         " prefabs and linked " + std::to_string(result.sceneInstancesLinked) + " scene instances");
    return Result<PrefabReconstructResult>::Ok(std::move(result));
}

} // namespace coconut::fixup
