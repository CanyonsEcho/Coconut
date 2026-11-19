#include "fixup/MeshDecombiner.h"
#include "export/UnityProjectExporter.h"
#include "unity/ClassIDs.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <queue>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace coconut::fixup {

namespace {

std::string CleanName(std::string_view raw) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    return out.empty() ? "DecombinedMesh" : out;
}

assets::MeshData ExtractSubMesh(const assets::MeshData& src, const assets::SubMeshInfo& sm, const std::string& name) {
    assets::MeshData out;
    out.name = name;
    u32 startIdx = sm.firstByte / 4;
    if (startIdx >= src.indices.size()) startIdx = sm.firstByte / 2;
    u32 endIdx = std::min<u32>(static_cast<u32>(src.indices.size()), startIdx + sm.indexCount);

    std::unordered_map<u32, u32> remap;
    for (u32 i = startIdx; i < endIdx; ++i) {
        u32 origV = src.indices[i];
        if (origV >= src.positions.size()) continue;
        auto it = remap.find(origV);
        u32 newV = 0;
        if (it == remap.end()) {
            newV = static_cast<u32>(out.positions.size());
            remap[origV] = newV;
            out.positions.push_back(src.positions[origV]);
            if (origV < src.normals.size()) out.normals.push_back(src.normals[origV]);
            if (origV < src.uvs.size()) out.uvs.push_back(src.uvs[origV]);
        } else {
            newV = it->second;
        }
        out.indices.push_back(newV);
    }
    if (out.normals.size() != out.positions.size()) out.RecalculateNormals();
    out.subMeshes.push_back({0, static_cast<u32>(out.indices.size()), 0, 0, static_cast<u32>(out.positions.size())});
    out.RecalculateBounds();
    return out;
}

Result<assets::MeshData> LoadSimpleObj(const std::filesystem::path& objPath) {
    auto txtRes = platform::ReadFileText(objPath);
    if (!txtRes) return Result<assets::MeshData>::Err(txtRes.Error());
    assets::MeshData m;
    m.name = objPath.stem().string();
    std::istringstream iss(txtRes.Value());
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("v ", 0) == 0) {
            std::istringstream ls(line.substr(2));
            glm::vec3 p{0};
            ls >> p.x >> p.y >> p.z;
            m.positions.push_back(p);
        } else if (line.rfind("vn ", 0) == 0) {
            std::istringstream ls(line.substr(3));
            glm::vec3 n{0, 1, 0};
            ls >> n.x >> n.y >> n.z;
            m.normals.push_back(n);
        } else if (line.rfind("vt ", 0) == 0) {
            std::istringstream ls(line.substr(3));
            glm::vec2 uv{0};
            ls >> uv.x >> uv.y;
            m.uvs.push_back(uv);
        } else if (line.rfind("f ", 0) == 0) {
            std::istringstream ls(line.substr(2));
            std::string tok;
            std::vector<u32> face;
            while (ls >> tok) {
                size_t sl = tok.find('/');
                int vIdx = std::stoi(sl == std::string::npos ? tok : tok.substr(0, sl));
                if (vIdx > 0 && static_cast<size_t>(vIdx - 1) < m.positions.size()) {
                    face.push_back(static_cast<u32>(vIdx - 1));
                }
            }
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                m.indices.push_back(face[0]);
                m.indices.push_back(face[k]);
                m.indices.push_back(face[k + 1]);
            }
        }
    }
    if (m.normals.size() != m.positions.size()) m.RecalculateNormals();
    m.RecalculateBounds();
    return Result<assets::MeshData>::Ok(std::move(m));
}

} // namespace

std::vector<assets::MeshData> MeshDecombiner::SplitMeshIslands(const assets::MeshData& combined) {
    std::vector<assets::MeshData> islands;
    if (!combined.IsValid()) return islands;

    // 1. If mesh already has distinct SubMeshes (e.g. Unity static batching submeshes), split directly by SubMesh
    if (combined.subMeshes.size() > 1) {
        for (size_t i = 0; i < combined.subMeshes.size(); ++i) {
            auto part = ExtractSubMesh(combined, combined.subMeshes[i],
                                       combined.name + "_Part_" + std::to_string(i));
            if (part.IsValid()) islands.push_back(std::move(part));
        }
        if (islands.size() > 1) return islands;
        islands.clear();
    }

    // 2. Geometric connected-component island splitting (groups vertices by quantized spatial position + triangle adjacency)
    const size_t triCount = combined.indices.size() / 3;
    std::unordered_map<i64, std::vector<size_t>> spatialVertToTris;
    auto spatialKey = [](glm::vec3 p) -> i64 {
        i64 x = static_cast<i64>(std::llround(p.x * 1000.0f));
        i64 y = static_cast<i64>(std::llround(p.y * 1000.0f));
        i64 z = static_cast<i64>(std::llround(p.z * 1000.0f));
        return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
    };

    for (size_t t = 0; t < triCount; ++t) {
        for (int k = 0; k < 3; ++k) {
            u32 vi = combined.indices[t * 3 + k];
            if (vi < combined.positions.size()) {
                spatialVertToTris[spatialKey(combined.positions[vi])].push_back(t);
            }
        }
    }

    std::vector<bool> visited(triCount, false);
    for (size_t startT = 0; startT < triCount; ++startT) {
        if (visited[startT]) continue;
        std::vector<size_t> compTris;
        std::queue<size_t> q;
        q.push(startT);
        visited[startT] = true;

        while (!q.empty()) {
            size_t t = q.front();
            q.pop();
            compTris.push_back(t);
            for (int k = 0; k < 3; ++k) {
                u32 vi = combined.indices[t * 3 + k];
                if (vi >= combined.positions.size()) continue;
                for (size_t nbr : spatialVertToTris[spatialKey(combined.positions[vi])]) {
                    if (!visited[nbr]) {
                        visited[nbr] = true;
                        q.push(nbr);
                    }
                }
            }
        }

        assets::MeshData part;
        part.name = combined.name + "_Island_" + std::to_string(islands.size());
        std::unordered_map<u32, u32> remap;
        for (size_t t : compTris) {
            for (int k = 0; k < 3; ++k) {
                u32 origV = combined.indices[t * 3 + k];
                auto it = remap.find(origV);
                u32 nv = 0;
                if (it == remap.end()) {
                    nv = static_cast<u32>(part.positions.size());
                    remap[origV] = nv;
                    part.positions.push_back(combined.positions[origV]);
                    if (origV < combined.normals.size()) part.normals.push_back(combined.normals[origV]);
                    if (origV < combined.uvs.size()) part.uvs.push_back(combined.uvs[origV]);
                } else {
                    nv = it->second;
                }
                part.indices.push_back(nv);
            }
        }
        if (part.normals.size() != part.positions.size()) part.RecalculateNormals();
        part.subMeshes.push_back({0, static_cast<u32>(part.indices.size()), 0, 0, static_cast<u32>(part.positions.size())});
        part.RecalculateBounds();
        islands.push_back(std::move(part));
    }

    return islands;
}

Result<MeshDecombineResult> MeshDecombiner::RunOnProject(
    const std::filesystem::path& unityProjectDir,
    const scene::SceneDocument* liveScene,
    assets::AssetManager* liveAssets) {
    MeshDecombineResult res;
    auto decombinedDir = unityProjectDir / "Assets" / "Meshes" / "Decombined";
    std::error_code ec;
    std::filesystem::create_directories(decombinedDir, ec);
    exporter::UnityProjectExporter::WriteMetaFile(
        decombinedDir, exporter::UnityProjectExporter::DeterministicGuid("folder:Decombined"), "Folder");

    std::unordered_set<i64> seenCombinedPathIds;
    std::unordered_map<std::string, std::string> rewirePerNodeName; // gameObjectName -> newMeshGuid

    // Path A: Exact static-batch decombining using live SceneDocument + AssetManager metadata
    if (liveScene && liveAssets) {
        const auto& nodes = liveScene->Nodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (node.meshPathId == 0) continue;

            int meshEntry = liveAssets->FindEntryByPathIdAnyFile(node.meshPathId, static_cast<i32>(unity::ClassID::Mesh));
            if (meshEntry < 0) continue;
            auto mRes = liveAssets->GetMesh(static_cast<size_t>(meshEntry));
            if (!mRes || !mRes.Value()->IsValid()) continue;
            const auto& mesh = *mRes.Value();

            bool isCombined = (node.staticBatchSubMeshCount > 0) ||
                              (mesh.name.find("Combined Mesh") != std::string::npos);
            if (!isCombined) continue;

            if (seenCombinedPathIds.insert(node.meshPathId).second) {
                ++res.combinedMeshesFound;
            }

            // Extract submesh for this GameObject (or matching spatial island)
            assets::MeshData localMesh;
            if (node.staticBatchFirstSubMesh < mesh.subMeshes.size()) {
                localMesh = ExtractSubMesh(mesh, mesh.subMeshes[node.staticBatchFirstSubMesh], CleanName(node.name));
            } else {
                auto islands = SplitMeshIslands(mesh);
                if (!islands.empty()) {
                    glm::vec3 nodeWorldPos = glm::vec3(liveScene->WorldMatrix(i)[3]);
                    size_t bestIdx = 0;
                    float bestDist = 1e9f;
                    for (size_t k = 0; k < islands.size(); ++k) {
                        glm::vec3 c = (islands[k].boundsMin + islands[k].boundsMax) * 0.5f;
                        float d = glm::length(c - nodeWorldPos);
                        if (d < bestDist) { bestDist = d; bestIdx = k; }
                    }
                    localMesh = std::move(islands[bestIdx]);
                    localMesh.name = CleanName(node.name);
                }
            }

            if (!localMesh.IsValid()) continue;

            // Transform baked world-space vertices back into the GameObject's local coordinate frame!
            glm::mat4 invWorld = glm::inverse(liveScene->WorldMatrix(i));
            localMesh.Transform(invWorld);

            auto outPath = decombinedDir / (CleanName(node.name) + "_Decombined.obj");
            if (assets::MeshCodec::ExportOBJ(localMesh, outPath)) {
                std::string newGuid = exporter::UnityProjectExporter::DeterministicGuid("decombined:" + outPath.filename().string());
                exporter::UnityProjectExporter::WriteMetaFile(outPath, newGuid, "ModelImporter");

                DecombinedMeshEntry entry;
                entry.gameObjectName = node.name;
                entry.sourceMeshName = mesh.name;
                entry.outputMeshPath = outPath;
                entry.newGuid = newGuid;
                entry.vertexCount = localMesh.positions.size();
                entry.triangleCount = localMesh.indices.size() / 3;
                res.entries.push_back(entry);
                rewirePerNodeName[node.name] = newGuid;
                ++res.meshesSplit;

                res.logs.push_back("Decombined '" + node.name + "' from '" + mesh.name + "' (" +
                                   std::to_string(entry.vertexCount) + " verts, " +
                                   std::to_string(entry.triangleCount) + " tris) -> local space");
            }
        }
    }

    // Path B: Disk-only fallback for AssetRipper-exported projects (scan Assets/Meshes/ for Combined_Mesh*.obj)
    if (res.meshesSplit == 0) {
        auto meshesDir = unityProjectDir / "Assets" / "Meshes";
        if (std::filesystem::exists(meshesDir, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(meshesDir, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".obj") continue;
                std::string stem = entry.path().stem().string();
                if (stem.find("Combined") == std::string::npos) continue;

                auto mRes = LoadSimpleObj(entry.path());
                if (!mRes) continue;
                auto islands = SplitMeshIslands(mRes.Value());
                if (islands.size() <= 1) continue;

                ++res.combinedMeshesFound;
                for (size_t k = 0; k < islands.size(); ++k) {
                    auto& part = islands[k];
                    // Recenter pivot to island bounding box center
                    glm::vec3 center = (part.boundsMin + part.boundsMax) * 0.5f;
                    for (auto& p : part.positions) p -= center;
                    part.RecalculateBounds();

                    auto outPath = decombinedDir / (CleanName(stem) + "_Island_" + std::to_string(k) + ".obj");
                    if (assets::MeshCodec::ExportOBJ(part, outPath)) {
                        std::string guid = exporter::UnityProjectExporter::DeterministicGuid("decombined:" + outPath.filename().string());
                        exporter::UnityProjectExporter::WriteMetaFile(outPath, guid, "ModelImporter");
                        res.entries.push_back({part.name, stem, outPath, guid, part.positions.size(), part.indices.size() / 3});
                        ++res.meshesSplit;
                    }
                }
            }
        }
    }

    // Rewire .unity scene files: replace m_Mesh GUID on each decombined GameObject & strip m_StaticBatchInfo!
    auto scenesDir = unityProjectDir / "Assets" / "Scenes";
    if (!rewirePerNodeName.empty() && std::filesystem::exists(scenesDir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(scenesDir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".unity") continue;
            auto textRes = platform::ReadFileText(entry.path());
            if (!textRes) continue;

            // Split scene YAML into Unity document blocks (separated by "--- !u!")
            const std::string& content = textRes.Value();
            std::vector<std::string> blocks;
            size_t pos = 0;
            while (pos < content.size()) {
                size_t next = content.find("--- !u!", pos + 1);
                if (next == std::string::npos) {
                    blocks.push_back(content.substr(pos));
                    break;
                }
                blocks.push_back(content.substr(pos, next - pos));
                pos = next;
            }

            // Map GameObject fileID -> name
            std::unordered_map<std::string, std::string> goFileIdToName;
            for (const auto& blk : blocks) {
                if (blk.find("--- !u!1 &") != std::string::npos) {
                    size_t amp = blk.find('&');
                    size_t nl = blk.find('\n', amp);
                    std::string fid = blk.substr(amp + 1, nl - amp - 1);
                    size_t nmPos = blk.find("m_Name: ");
                    if (nmPos != std::string::npos) {
                        size_t nmEnd = blk.find('\n', nmPos);
                        std::string nm = blk.substr(nmPos + 8, nmEnd - (nmPos + 8));
                        while (!nm.empty() && (nm.back() == '\r' || nm.back() == ' ')) nm.pop_back();
                        goFileIdToName[fid] = nm;
                    }
                }
            }

            bool modified = false;
            for (auto& blk : blocks) {
                size_t goRef = blk.find("m_GameObject: {fileID: ");
                if (goRef == std::string::npos) continue;
                size_t idStart = goRef + 23;
                size_t idEnd = blk.find('}', idStart);
                if (idEnd == std::string::npos) continue;
                std::string goFid = blk.substr(idStart, idEnd - idStart);

                auto nameIt = goFileIdToName.find(goFid);
                if (nameIt == goFileIdToName.end()) continue;
                auto guidIt = rewirePerNodeName.find(nameIt->second);
                if (guidIt == rewirePerNodeName.end()) continue;

                // Rewire MeshFilter m_Mesh
                if (blk.find("--- !u!33 ") != std::string::npos) {
                    size_t mPos = blk.find("m_Mesh:");
                    if (mPos != std::string::npos) {
                        size_t mEnd = blk.find('\n', mPos);
                        blk.replace(mPos, mEnd - mPos,
                                    "m_Mesh: {fileID: 4300000, guid: " + guidIt->second + ", type: 2}");
                        modified = true;
                        ++res.sceneReferencesRewired;
                    }
                }
                // Strip m_StaticBatchInfo from MeshRenderer
                if (blk.find("--- !u!23 ") != std::string::npos) {
                    size_t sbPos = blk.find("  m_StaticBatchInfo:");
                    if (sbPos != std::string::npos) {
                        blk.erase(sbPos);
                        modified = true;
                    }
                }
            }

            if (modified) {
                std::string joined;
                for (const auto& blk : blocks) joined += blk;
                platform::WriteFileText(entry.path(), joined);
            }
        }
    }

    COCO_LOG_INFO("MeshDecombiner", "Decombined " + std::to_string(res.meshesSplit) + " meshes (" +
                                    std::to_string(res.sceneReferencesRewired) + " scene refs rewired)");
    return Result<MeshDecombineResult>::Ok(std::move(res));
}

} // namespace coconut::fixup
