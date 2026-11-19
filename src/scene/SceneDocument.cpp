#include "scene/SceneDocument.h"
#include "unity/ClassIDs.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace coconut::scene {

namespace {

glm::vec3 ReadVec3(const unity::TypeValue* field, glm::vec3 def) {
    if (!field) return def;
    return {
        static_cast<f32>(field->FindField("x") ? field->FindField("x")->AsFloat(def.x) : def.x),
        static_cast<f32>(field->FindField("y") ? field->FindField("y")->AsFloat(def.y) : def.y),
        static_cast<f32>(field->FindField("z") ? field->FindField("z")->AsFloat(def.z) : def.z)
    };
}

glm::quat ReadQuat(const unity::TypeValue* field) {
    if (!field) return glm::quat(1, 0, 0, 0);
    f32 x = static_cast<f32>(field->FindField("x") ? field->FindField("x")->AsFloat(0.0) : 0.0);
    f32 y = static_cast<f32>(field->FindField("y") ? field->FindField("y")->AsFloat(0.0) : 0.0);
    f32 z = static_cast<f32>(field->FindField("z") ? field->FindField("z")->AsFloat(0.0) : 0.0);
    f32 w = static_cast<f32>(field->FindField("w") ? field->FindField("w")->AsFloat(1.0) : 1.0);
    glm::quat q(w, x, y, z);
    float len = glm::length(q);
    return len > 1e-6f ? (q / len) : glm::quat(1, 0, 0, 0);
}

void WriteVec3(unity::TypeValue* field, glm::vec3 v) {
    if (!field) return;
    if (auto* x = field->FindField("x")) x->value = static_cast<f64>(v.x);
    if (auto* y = field->FindField("y")) y->value = static_cast<f64>(v.y);
    if (auto* z = field->FindField("z")) z->value = static_cast<f64>(v.z);
}

void WriteQuat(unity::TypeValue* field, glm::quat q) {
    if (!field) return;
    if (auto* x = field->FindField("x")) x->value = static_cast<f64>(q.x);
    if (auto* y = field->FindField("y")) y->value = static_cast<f64>(q.y);
    if (auto* z = field->FindField("z")) z->value = static_cast<f64>(q.z);
    if (auto* w = field->FindField("w")) w->value = static_cast<f64>(q.w);
}

} // namespace

glm::mat4 SceneNode::LocalMatrix() const {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), localPosition);
    glm::quat q = glm::quat(glm::radians(localEulerDeg));
    glm::mat4 r = glm::mat4_cast(q);
    glm::mat4 s = glm::scale(glm::mat4(1.0f), localScale);
    return t * r * s;
}

void SceneDocument::Clear() {
    sceneName_ = "UntitledScene";
    nodes_.clear();
    rootIndices_.clear();
}

Result<void> SceneDocument::LoadFromProject(unity::LoadedGameProject& project, assets::AssetManager& /*assets*/) {
    Clear();

    struct RawTransform {
        size_t fileIndex = 0;
        i64 pathId = 0;
        i64 goPathId = 0;
        i64 fatherPathId = 0;
        glm::vec3 pos{0.0f};
        glm::vec3 eulerDeg{0.0f};
        glm::vec3 scale{1.0f};
    };

    std::unordered_map<i64, size_t> goIdToNodeIdx;
    std::unordered_map<i64, RawTransform> transformMap;
    std::unordered_map<i64, size_t> tfIdToNodeIdx;

    // Pass 1: Collect all GameObjects and Transforms across serialized files
    for (size_t fi = 0; fi < project.serializedFiles.size(); ++fi) {
        const auto& sf = project.serializedFiles[fi];
        bool hasGameObjects = false;

        for (const auto& obj : sf.objects) {
            if (obj.classId == static_cast<i32>(unity::ClassID::GameObject)) {
                hasGameObjects = true;
                auto valRes = sf.ReadObjectValue(obj);
                SceneNode node;
                node.id = static_cast<u64>(obj.pathId);
                node.fileIndex = fi;
                node.gameObjectPathId = obj.pathId;
                node.name = obj.cachedName;

                if (valRes) {
                    const auto& root = valRes.Value();
                    if (auto* n = root.FindField("m_Name")) node.name = n->AsString(node.name);
                    if (auto* a = root.FindField("m_IsActive")) node.active = a->AsBool(true);
                    if (auto* l = root.FindField("m_Layer")) node.layer = static_cast<u32>(l->AsUInt(0));
                    if (auto* tg = root.FindField("m_Tag")) node.tag = static_cast<u16>(tg->AsUInt(0));
                    if (auto* comps = root.FindField("m_Component")) {
                        if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&comps->value)) {
                            for (const auto& pair : *list) {
                                const auto* cp = pair.FindField("component");
                                if (!cp) cp = &pair;
                                i64 cid = cp->FindField("m_PathID") ? cp->FindField("m_PathID")->AsInt() : 0;
                                if (cid != 0) {
                                    ComponentRef cr;
                                    cr.pathId = cid;
                                    cr.fileIndex = fi;
                                    if (const auto* compObj = sf.FindObject(cid)) {
                                        cr.classId = compObj->classId;
                                        cr.className = std::string(unity::ClassIDName(compObj->classId));
                                    }
                                    node.components.push_back(std::move(cr));
                                }
                            }
                        }
                    }
                }
                size_t idx = nodes_.size();
                goIdToNodeIdx[obj.pathId] = idx;
                nodes_.push_back(std::move(node));
            }
        }
        if (hasGameObjects && sceneName_ == "UntitledScene") {
            sceneName_ = sf.name;
        }
    }

    // Pass 2: Attach Transforms, MeshFilters, MeshRenderers, Camera, Light, MonoBehaviour summaries
    for (size_t fi = 0; fi < project.serializedFiles.size(); ++fi) {
        const auto& sf = project.serializedFiles[fi];
        for (const auto& obj : sf.objects) {
            auto valRes = sf.ReadObjectValue(obj);
            if (!valRes) continue;
            const auto& root = valRes.Value();

            i64 goId = 0;
            if (auto* g = root.FindField("m_GameObject")) {
                if (auto* pid = g->FindField("m_PathID")) goId = pid->AsInt();
            }
            auto nodeIt = goIdToNodeIdx.find(goId);
            if (nodeIt == goIdToNodeIdx.end()) continue;
            SceneNode& node = nodes_[nodeIt->second];

            if (obj.classId == static_cast<i32>(unity::ClassID::Transform) ||
                obj.classId == static_cast<i32>(unity::ClassID::RectTransform)) {
                RawTransform rt;
                rt.fileIndex = fi;
                rt.pathId = obj.pathId;
                rt.goPathId = goId;
                rt.pos = ReadVec3(root.FindField("m_LocalPosition"), {0, 0, 0});
                rt.eulerDeg = glm::degrees(glm::eulerAngles(ReadQuat(root.FindField("m_LocalRotation"))));
                rt.scale = ReadVec3(root.FindField("m_LocalScale"), {1, 1, 1});
                if (auto* fa = root.FindField("m_Father")) {
                    if (auto* pid = fa->FindField("m_PathID")) rt.fatherPathId = pid->AsInt();
                }

                node.transformPathId = obj.pathId;
                node.localPosition = rt.pos;
                node.localEulerDeg = rt.eulerDeg;
                node.localScale = rt.scale;

                transformMap[obj.pathId] = rt;
                tfIdToNodeIdx[obj.pathId] = nodeIt->second;
            } else if (obj.classId == static_cast<i32>(unity::ClassID::MeshFilter)) {
                if (auto* m = root.FindField("m_Mesh")) {
                    if (auto* pid = m->FindField("m_PathID")) node.meshPathId = pid->AsInt();
                }
            } else if (obj.classId == static_cast<i32>(unity::ClassID::MeshRenderer) ||
                       obj.classId == static_cast<i32>(unity::ClassID::SkinnedMeshRenderer)) {
                if (auto* mats = root.FindField("m_Materials")) {
                    if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&mats->value)) {
                        if (!list->empty()) {
                            if (auto* pid = (*list)[0].FindField("m_PathID")) {
                                node.materialPathId = pid->AsInt();
                            }
                        }
                    }
                }
                if (auto* sb = root.FindField("m_StaticBatchInfo")) {
                    if (auto* f = sb->FindField("firstSubMesh")) node.staticBatchFirstSubMesh = static_cast<u16>(f->AsUInt());
                    if (auto* c = sb->FindField("subMeshCount")) node.staticBatchSubMeshCount = static_cast<u16>(c->AsUInt());
                }
            }

            // Annotate component summaries
            for (auto& comp : node.components) {
                if (comp.pathId == obj.pathId) {
                    if (obj.classId == static_cast<i32>(unity::ClassID::Camera)) {
                        f64 fov = root.FindField("field of view") ? root.FindField("field of view")->AsFloat(60.0) : 60.0;
                        comp.summary = "FOV " + std::to_string(static_cast<int>(fov)) + " deg";
                    } else if (obj.classId == static_cast<i32>(unity::ClassID::Light)) {
                        f64 inten = root.FindField("m_Intensity") ? root.FindField("m_Intensity")->AsFloat(1.0) : 1.0;
                        std::ostringstream oss;
                        oss << "Directional, Intensity " << std::fixed << std::setprecision(2) << inten;
                        comp.summary = oss.str();
                    } else if (obj.classId == static_cast<i32>(unity::ClassID::MonoBehaviour)) {
                        std::string sc = root.FindField("m_Name") ? root.FindField("m_Name")->AsString("Script") : "Script";
                        comp.summary = "Script: " + sc;
                    } else if (obj.classId == static_cast<i32>(unity::ClassID::MeshFilter)) {
                        comp.summary = "Mesh PathID #" + std::to_string(node.meshPathId);
                    } else if (obj.classId == static_cast<i32>(unity::ClassID::MeshRenderer)) {
                        comp.summary = node.staticBatchSubMeshCount > 0
                            ? ("Mat #" + std::to_string(node.materialPathId) + " [StaticBatch sub=" + std::to_string(node.staticBatchFirstSubMesh) + "]")
                            : ("Mat #" + std::to_string(node.materialPathId));
                    }
                }
            }
        }
    }

    // Pass 3: Wire parent-child hierarchy via Transform.m_Father
    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto tfIt = transformMap.find(nodes_[i].transformPathId);
        if (tfIt != transformMap.end() && tfIt->second.fatherPathId != 0) {
            auto parentIt = tfIdToNodeIdx.find(tfIt->second.fatherPathId);
            if (parentIt != tfIdToNodeIdx.end() && parentIt->second != i) {
                nodes_[i].parentIndex = static_cast<i64>(parentIt->second);
                nodes_[parentIt->second].children.push_back(i);
            }
        }
    }

    RebuildRoots();
    COCO_LOG_INFO("Scene", "Loaded scene '" + sceneName_ + "' with " + std::to_string(nodes_.size()) +
                           " GameObjects (" + std::to_string(rootIndices_.size()) + " roots)");
    return Result<void>::Ok();
}

void SceneDocument::RebuildRoots() {
    rootIndices_.clear();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].parentIndex < 0 || static_cast<size_t>(nodes_[i].parentIndex) >= nodes_.size()) {
            rootIndices_.push_back(i);
        }
    }
}

glm::mat4 SceneDocument::WorldMatrix(size_t nodeIndex) const {
    if (nodeIndex >= nodes_.size()) return glm::mat4(1.0f);
    glm::mat4 mat = nodes_[nodeIndex].LocalMatrix();
    i64 parent = nodes_[nodeIndex].parentIndex;
    int guard = 0;
    while (parent >= 0 && static_cast<size_t>(parent) < nodes_.size() && guard++ < 64) {
        mat = nodes_[static_cast<size_t>(parent)].LocalMatrix() * mat;
        parent = nodes_[static_cast<size_t>(parent)].parentIndex;
    }
    return mat;
}

int SceneDocument::FindNodeById(u64 id) const {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

int SceneDocument::FindNodeByName(std::string_view name) const {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void SceneDocument::SetTransform(
    size_t nodeIndex,
    glm::vec3 pos,
    glm::vec3 eulerDeg,
    glm::vec3 scale,
    CommandHistory* history) {
    if (nodeIndex >= nodes_.size()) return;
    auto oldPos = nodes_[nodeIndex].localPosition;
    auto oldRot = nodes_[nodeIndex].localEulerDeg;
    auto oldScl = nodes_[nodeIndex].localScale;
    u64 targetId = nodes_[nodeIndex].id;

    auto apply = [this, targetId](glm::vec3 p, glm::vec3 r, glm::vec3 s) {
        int idx = FindNodeById(targetId);
        if (idx >= 0) {
            nodes_[static_cast<size_t>(idx)].localPosition = p;
            nodes_[static_cast<size_t>(idx)].localEulerDeg = r;
            nodes_[static_cast<size_t>(idx)].localScale = s;
            nodes_[static_cast<size_t>(idx)].dirty = true;
        }
    };

    if (history) {
        history->PushAndExecute(std::make_unique<LambdaCommand>(
            "Transform " + nodes_[nodeIndex].name,
            [=]() { apply(pos, eulerDeg, scale); },
            [=]() { apply(oldPos, oldRot, oldScl); }));
    } else {
        apply(pos, eulerDeg, scale);
    }
}

void SceneDocument::SetName(size_t nodeIndex, const std::string& newName, CommandHistory* history) {
    if (nodeIndex >= nodes_.size() || newName.empty()) return;
    std::string oldName = nodes_[nodeIndex].name;
    u64 targetId = nodes_[nodeIndex].id;

    auto apply = [this, targetId](const std::string& nm) {
        int idx = FindNodeById(targetId);
        if (idx >= 0) {
            nodes_[static_cast<size_t>(idx)].name = nm;
            nodes_[static_cast<size_t>(idx)].dirty = true;
        }
    };

    if (history) {
        history->PushAndExecute(std::make_unique<LambdaCommand>(
            "Rename " + oldName + " -> " + newName,
            [=]() { apply(newName); },
            [=]() { apply(oldName); }));
    } else {
        apply(newName);
    }
}

void SceneDocument::SetActive(size_t nodeIndex, bool active, CommandHistory* history) {
    if (nodeIndex >= nodes_.size()) return;
    bool oldActive = nodes_[nodeIndex].active;
    u64 targetId = nodes_[nodeIndex].id;

    auto apply = [this, targetId](bool a) {
        int idx = FindNodeById(targetId);
        if (idx >= 0) {
            nodes_[static_cast<size_t>(idx)].active = a;
            nodes_[static_cast<size_t>(idx)].dirty = true;
        }
    };

    if (history) {
        history->PushAndExecute(std::make_unique<LambdaCommand>(
            "Toggle Active " + nodes_[nodeIndex].name,
            [=]() { apply(active); },
            [=]() { apply(oldActive); }));
    } else {
        apply(active);
    }
}

size_t SceneDocument::CreateGameObject(const std::string& name, i64 parentIndex, i64 meshPathId, i64 matPathId) {
    SceneNode n;
    n.id = nextSyntheticId_++;
    n.name = name;
    n.parentIndex = parentIndex;
    n.meshPathId = meshPathId;
    n.materialPathId = matPathId;
    n.dirty = true;
    n.components.push_back({static_cast<i32>(unity::ClassID::Transform), "Transform", 0, 0, ""});
    if (meshPathId != 0) {
        n.components.push_back({static_cast<i32>(unity::ClassID::MeshFilter), "MeshFilter", 0, 0, "Mesh #" + std::to_string(meshPathId)});
        n.components.push_back({static_cast<i32>(unity::ClassID::MeshRenderer), "MeshRenderer", 0, 0, "Mat #" + std::to_string(matPathId)});
    }
    size_t idx = nodes_.size();
    nodes_.push_back(std::move(n));
    if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < idx) {
        nodes_[static_cast<size_t>(parentIndex)].children.push_back(idx);
    }
    RebuildRoots();
    return idx;
}

size_t SceneDocument::DuplicateGameObject(size_t nodeIndex) {
    if (nodeIndex >= nodes_.size()) return 0;
    SceneNode copy = nodes_[nodeIndex];
    copy.id = nextSyntheticId_++;
    copy.gameObjectPathId = 0;
    copy.transformPathId = 0;
    copy.name += " (Clone)";
    copy.localPosition += glm::vec3(1.25f, 0.0f, 0.0f);
    copy.children.clear();
    copy.dirty = true;
    size_t newIdx = nodes_.size();
    nodes_.push_back(std::move(copy));
    if (nodes_[newIdx].parentIndex >= 0 && static_cast<size_t>(nodes_[newIdx].parentIndex) < newIdx) {
        nodes_[static_cast<size_t>(nodes_[newIdx].parentIndex)].children.push_back(newIdx);
    }
    RebuildRoots();
    return newIdx;
}

bool SceneDocument::DeleteGameObject(size_t nodeIndex) {
    if (nodeIndex >= nodes_.size()) return false;
    // Mark inactive & detach from parent so stable indices don't invalidate sibling handles during editing
    if (nodes_[nodeIndex].parentIndex >= 0 && static_cast<size_t>(nodes_[nodeIndex].parentIndex) < nodes_.size()) {
        auto& siblings = nodes_[static_cast<size_t>(nodes_[nodeIndex].parentIndex)].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), nodeIndex), siblings.end());
    }
    // Reparent children to root
    for (size_t childIdx : nodes_[nodeIndex].children) {
        if (childIdx < nodes_.size()) nodes_[childIdx].parentIndex = -1;
    }
    nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(nodeIndex));

    // Reindex parent & children indices
    for (auto& n : nodes_) {
        if (n.parentIndex > static_cast<i64>(nodeIndex)) --n.parentIndex;
        else if (n.parentIndex == static_cast<i64>(nodeIndex)) n.parentIndex = -1;
        n.children.clear();
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].parentIndex >= 0 && static_cast<size_t>(nodes_[i].parentIndex) < nodes_.size()) {
            nodes_[static_cast<size_t>(nodes_[i].parentIndex)].children.push_back(i);
        }
    }
    RebuildRoots();
    return true;
}

bool SceneDocument::ReparentGameObject(size_t nodeIndex, i64 newParentIndex) {
    if (nodeIndex >= nodes_.size()) return false;
    if (newParentIndex == static_cast<i64>(nodeIndex)) return false;

    // Prevent cycles
    i64 cur = newParentIndex;
    while (cur >= 0 && static_cast<size_t>(cur) < nodes_.size()) {
        if (static_cast<size_t>(cur) == nodeIndex) return false;
        cur = nodes_[static_cast<size_t>(cur)].parentIndex;
    }

    if (nodes_[nodeIndex].parentIndex >= 0 && static_cast<size_t>(nodes_[nodeIndex].parentIndex) < nodes_.size()) {
        auto& oldKids = nodes_[static_cast<size_t>(nodes_[nodeIndex].parentIndex)].children;
        oldKids.erase(std::remove(oldKids.begin(), oldKids.end(), nodeIndex), oldKids.end());
    }
    nodes_[nodeIndex].parentIndex = newParentIndex;
    if (newParentIndex >= 0 && static_cast<size_t>(newParentIndex) < nodes_.size()) {
        nodes_[static_cast<size_t>(newParentIndex)].children.push_back(nodeIndex);
    }
    nodes_[nodeIndex].dirty = true;
    RebuildRoots();
    return true;
}

Result<size_t> SceneDocument::SyncBackToSerializedFiles(unity::LoadedGameProject& project) {
    size_t updatedCount = 0;
    for (auto& node : nodes_) {
        if (!node.dirty || node.fileIndex >= project.serializedFiles.size()) continue;
        auto& sf = project.serializedFiles[node.fileIndex];

        if (node.gameObjectPathId != 0) {
            if (auto* goObj = sf.FindObject(node.gameObjectPathId)) {
                auto valRes = sf.ReadObjectValue(*goObj);
                if (valRes) {
                    if (auto* n = valRes.Value().FindField("m_Name")) n->value = node.name;
                    if (auto* a = valRes.Value().FindField("m_IsActive")) a->value = node.active;
                    auto w = sf.WriteObjectValue(*goObj, valRes.Value());
                    if (w) ++updatedCount;
                }
            }
        }
        if (node.transformPathId != 0) {
            if (auto* tfObj = sf.FindObject(node.transformPathId)) {
                auto valRes = sf.ReadObjectValue(*tfObj);
                if (valRes) {
                    WriteVec3(valRes.Value().FindField("m_LocalPosition"), node.localPosition);
                    WriteQuat(valRes.Value().FindField("m_LocalRotation"), glm::quat(glm::radians(node.localEulerDeg)));
                    WriteVec3(valRes.Value().FindField("m_LocalScale"), node.localScale);
                    auto w = sf.WriteObjectValue(*tfObj, valRes.Value());
                    if (w) ++updatedCount;
                }
            }
        }
        node.dirty = false;
    }
    return Result<size_t>::Ok(updatedCount);
}

Result<void> SceneDocument::ExportUnitySceneYaml(
    const std::filesystem::path& outPath,
    const std::unordered_map<i64, std::string>& meshGuidMap,
    const std::unordered_map<i64, std::string>& materialGuidMap) const {
    std::ostringstream y;
    y << "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n";
    y << std::fixed << std::setprecision(5);

    auto goFileId = [&](size_t idx) -> u64 { return 100000 + idx * 10 + 1; };
    auto tfFileId = [&](size_t idx) -> u64 { return 100000 + idx * 10 + 4; };
    auto mfFileId = [&](size_t idx) -> u64 { return 100000 + idx * 10 + 5; };
    auto mrFileId = [&](size_t idx) -> u64 { return 100000 + idx * 10 + 6; };

    for (size_t i = 0; i < nodes_.size(); ++i) {
        const auto& n = nodes_[i];
        bool hasMesh = n.meshPathId != 0;

        // GameObject (!u!1)
        y << "--- !u!1 &" << goFileId(i) << "\n"
          << "GameObject:\n"
          << "  m_ObjectHideFlags: 0\n"
          << "  m_Component:\n"
          << "  - component: {fileID: " << tfFileId(i) << "}\n";
        if (hasMesh) {
            y << "  - component: {fileID: " << mfFileId(i) << "}\n"
              << "  - component: {fileID: " << mrFileId(i) << "}\n";
        }
        y << "  m_Layer: " << n.layer << "\n"
          << "  m_Name: " << n.name << "\n"
          << "  m_TagString: Untagged\n"
          << "  m_IsActive: " << (n.active ? 1 : 0) << "\n";

        // Transform (!u!4)
        glm::quat q = glm::quat(glm::radians(n.localEulerDeg));
        y << "--- !u!4 &" << tfFileId(i) << "\n"
          << "Transform:\n"
          << "  m_GameObject: {fileID: " << goFileId(i) << "}\n"
          << "  m_LocalRotation: {x: " << q.x << ", y: " << q.y << ", z: " << q.z << ", w: " << q.w << "}\n"
          << "  m_LocalPosition: {x: " << n.localPosition.x << ", y: " << n.localPosition.y << ", z: " << n.localPosition.z << "}\n"
          << "  m_LocalScale: {x: " << n.localScale.x << ", y: " << n.localScale.y << ", z: " << n.localScale.z << "}\n"
          << "  m_Children:\n";
        for (size_t childIdx : n.children) {
            y << "  - {fileID: " << tfFileId(childIdx) << "}\n";
        }
        u64 father = (n.parentIndex >= 0 && static_cast<size_t>(n.parentIndex) < nodes_.size())
                         ? tfFileId(static_cast<size_t>(n.parentIndex))
                         : 0;
        y << "  m_Father: {fileID: " << father << "}\n";

        if (hasMesh) {
            // MeshFilter (!u!33)
            auto meshIt = meshGuidMap.find(n.meshPathId);
            y << "--- !u!33 &" << mfFileId(i) << "\n"
              << "MeshFilter:\n"
              << "  m_GameObject: {fileID: " << goFileId(i) << "}\n";
            if (meshIt != meshGuidMap.end()) {
                y << "  m_Mesh: {fileID: 4300000, guid: " << meshIt->second << ", type: 2}\n";
            } else {
                y << "  m_Mesh: {fileID: " << n.meshPathId << "}\n";
            }

            // MeshRenderer (!u!23)
            auto matIt = materialGuidMap.find(n.materialPathId);
            y << "--- !u!23 &" << mrFileId(i) << "\n"
              << "MeshRenderer:\n"
              << "  m_GameObject: {fileID: " << goFileId(i) << "}\n"
              << "  m_Enabled: 1\n"
              << "  m_Materials:\n";
            if (matIt != materialGuidMap.end()) {
                y << "  - {fileID: 2100000, guid: " << matIt->second << ", type: 2}\n";
            } else {
                y << "  - {fileID: " << n.materialPathId << "}\n";
            }
            if (n.staticBatchSubMeshCount > 0) {
                y << "  m_StaticBatchInfo:\n"
                  << "    firstSubMesh: " << n.staticBatchFirstSubMesh << "\n"
                  << "    subMeshCount: " << n.staticBatchSubMeshCount << "\n";
            }
        }
    }

    return platform::WriteFileText(outPath, y.str());
}

} // namespace coconut::scene
