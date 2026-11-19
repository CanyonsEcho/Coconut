#include "assets/MeshAsset.h"
#include "core/BinaryReader.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace coconut::assets {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string Base64Encode(ByteSpan data) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < data.size()) {
        u32 a = data[i++];
        u32 b = (i < data.size()) ? data[i++] : 0;
        u32 c = (i < data.size()) ? data[i++] : 0;
        u32 triple = (a << 16) | (b << 8) | c;
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back(kTable[(triple >> 6) & 0x3F]);
        out.push_back(kTable[triple & 0x3F]);
    }
    size_t mod = data.size() % 3;
    if (mod == 1) {
        out[out.size() - 2] = '=';
        out[out.size() - 1] = '=';
    } else if (mod == 2) {
        out[out.size() - 1] = '=';
    }
    return out;
}

} // namespace

void MeshData::RecalculateBounds() {
    if (positions.empty()) {
        boundsMin = boundsMax = glm::vec3(0.0f);
        return;
    }
    boundsMin = boundsMax = positions[0];
    for (const auto& p : positions) {
        boundsMin = glm::min(boundsMin, p);
        boundsMax = glm::max(boundsMax, p);
    }
}

void MeshData::RecalculateNormals() {
    normals.assign(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        u32 i0 = indices[i];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;
        glm::vec3 e1 = positions[i1] - positions[i0];
        glm::vec3 e2 = positions[i2] - positions[i0];
        glm::vec3 n = glm::cross(e1, e2);
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
    }
    for (auto& n : normals) {
        float len = glm::length(n);
        n = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void MeshData::Transform(const glm::mat4& matrix) {
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(matrix)));
    for (auto& p : positions) {
        glm::vec4 tp = matrix * glm::vec4(p, 1.0f);
        p = glm::vec3(tp);
    }
    for (auto& n : normals) {
        glm::vec3 tn = normalMat * n;
        float len = glm::length(tn);
        n = (len > 1e-6f) ? (tn / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    RecalculateBounds();
}

Result<MeshData> MeshCodec::ParseFromObject(
    const unity::SerializedFile& sf,
    const unity::ObjectInfo& obj,
    const unity::LoadedGameProject* project) {
    auto valRes = sf.ReadObjectValue(obj);
    if (!valRes) {
        return Result<MeshData>::Err("Mesh TypeTree parsing failed: " + valRes.Error());
    }
    const auto& root = valRes.Value();
    MeshData mesh;
    mesh.name = obj.cachedName;
    if (auto* f = root.FindField("m_Name")) mesh.name = f->AsString(mesh.name);

    i32 indexFormat = 0; // 0 = UInt16, 1 = UInt32
    if (auto* f = root.FindField("m_IndexFormat")) indexFormat = static_cast<i32>(f->AsInt());

    // SubMeshes
    if (auto* sms = root.FindField("m_SubMeshes")) {
        if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&sms->value)) {
            for (const auto& sm : *list) {
                SubMeshInfo info;
                if (auto* v = sm.FindField("firstByte")) info.firstByte = static_cast<u32>(v->AsUInt());
                if (auto* v = sm.FindField("indexCount")) info.indexCount = static_cast<u32>(v->AsUInt());
                if (auto* v = sm.FindField("topology")) info.topology = static_cast<i32>(v->AsInt());
                if (auto* v = sm.FindField("firstVertex")) info.firstVertex = static_cast<u32>(v->AsUInt());
                if (auto* v = sm.FindField("vertexCount")) info.vertexCount = static_cast<u32>(v->AsUInt());
                mesh.subMeshes.push_back(info);
            }
        }
    }

    // Index Buffer
    if (auto* ib = root.FindField("m_IndexBuffer")) {
        if (auto* bytes = std::get_if<ByteBuffer>(&ib->value)) {
            BinaryReader r(*bytes, sf.header.bigEndian ? Endian::Big : Endian::Little);
            if (indexFormat == 1) {
                size_t count = bytes->size() / 4;
                mesh.indices.reserve(count);
                for (size_t i = 0; i < count; ++i) mesh.indices.push_back(r.ReadU32());
            } else {
                size_t count = bytes->size() / 2;
                mesh.indices.reserve(count);
                for (size_t i = 0; i < count; ++i) mesh.indices.push_back(r.ReadU16());
            }
        }
    }

    // VertexData
    if (auto* vd = root.FindField("m_VertexData")) {
        u32 vertexCount = 0;
        if (auto* vc = vd->FindField("m_VertexCount")) vertexCount = static_cast<u32>(vc->AsUInt());

        struct ChannelInfo { u8 stream = 0; u8 offset = 0; u8 format = 0; u8 dimension = 0; };
        std::vector<ChannelInfo> channels;
        if (auto* chs = vd->FindField("m_Channels")) {
            if (auto* list = std::get_if<std::vector<unity::TypeValue>>(&chs->value)) {
                for (const auto& c : *list) {
                    ChannelInfo ci;
                    if (auto* v = c.FindField("stream")) ci.stream = static_cast<u8>(v->AsUInt());
                    if (auto* v = c.FindField("offset")) ci.offset = static_cast<u8>(v->AsUInt());
                    if (auto* v = c.FindField("format")) ci.format = static_cast<u8>(v->AsUInt());
                    if (auto* v = c.FindField("dimension")) ci.dimension = static_cast<u8>(v->AsUInt() & 0x0F);
                    channels.push_back(ci);
                }
            }
        }

        ByteBuffer rawVerts;
        if (auto* ds = vd->FindField("m_DataSize")) {
            if (auto* bytes = std::get_if<ByteBuffer>(&ds->value)) {
                rawVerts = *bytes;
            }
        }

        if (rawVerts.empty() && project) {
            if (auto* sd = root.FindField("m_StreamData")) {
                u64 off = sd->FindField("offset") ? sd->FindField("offset")->AsUInt() : 0;
                u64 sz  = sd->FindField("size") ? sd->FindField("size")->AsUInt() : 0;
                std::string path = sd->FindField("path") ? sd->FindField("path")->AsString() : "";
                if (!path.empty() && sz > 0) {
                    if (const auto* stream = project->FindResourceStream(path)) {
                        if (off + sz <= stream->size()) {
                            rawVerts.assign(stream->begin() + static_cast<std::ptrdiff_t>(off),
                                            stream->begin() + static_cast<std::ptrdiff_t>(off + sz));
                        }
                    }
                }
            }
        }

        if (vertexCount > 0 && !rawVerts.empty()) {
            // Compute per-stream strides and base offsets
            auto formatSize = [](u8 fmt) -> u32 {
                switch (fmt) {
                    case 0: return 4; // Float32
                    case 1: return 2; // Float16
                    case 2: return 1; // UNorm8
                    case 3: return 1; // SNorm8
                    case 4: return 2; // UNorm16
                    case 5: return 2; // SNorm16
                    case 6: return 1; // UInt8
                    case 7: return 1; // SInt8
                    case 8: return 2; // UInt16
                    case 9: return 2; // SInt16
                    case 10: return 4; // UInt32
                    case 11: return 4; // SInt32
                    default: return 4;
                }
            };

            u32 maxStream = 0;
            for (const auto& c : channels) {
                if (c.dimension > 0 && c.stream > maxStream) maxStream = c.stream;
            }
            std::vector<u32> streamStrides(maxStream + 1, 0);
            for (const auto& c : channels) {
                if (c.dimension == 0) continue;
                u32 end = c.offset + c.dimension * formatSize(c.format);
                streamStrides[c.stream] = std::max(streamStrides[c.stream], end);
            }
            std::vector<size_t> streamOffsets(maxStream + 1, 0);
            size_t cursor = 0;
            for (u32 s = 0; s <= maxStream; ++s) {
                streamOffsets[s] = cursor;
                cursor += static_cast<size_t>(streamStrides[s]) * vertexCount;
                cursor = (cursor + 15) & ~static_cast<size_t>(15);
            }

            // Fallback interleaved Pos(3f) + Normal(3f) + UV(2f) if no channels declared
            if (channels.empty()) {
                u32 stride = static_cast<u32>(rawVerts.size() / vertexCount);
                mesh.positions.resize(vertexCount);
                for (u32 i = 0; i < vertexCount; ++i) {
                    size_t base = static_cast<size_t>(i) * stride;
                    if (base + 12 <= rawVerts.size()) {
                        std::memcpy(&mesh.positions[i].x, rawVerts.data() + base, 12);
                    }
                    if (stride >= 24 && base + 24 <= rawVerts.size()) {
                        if (mesh.normals.size() < vertexCount) mesh.normals.resize(vertexCount);
                        std::memcpy(&mesh.normals[i].x, rawVerts.data() + base + 12, 12);
                    }
                    if (stride >= 32 && base + 32 <= rawVerts.size()) {
                        if (mesh.uvs.size() < vertexCount) mesh.uvs.resize(vertexCount);
                        std::memcpy(&mesh.uvs[i].x, rawVerts.data() + base + 24, 8);
                    }
                }
            } else {
                auto readChannelFloat = [&](const ChannelInfo& ch, u32 vertIdx, int comp) -> float {
                    size_t off = streamOffsets[ch.stream] + static_cast<size_t>(vertIdx) * streamStrides[ch.stream] + ch.offset;
                    u32 fsz = formatSize(ch.format);
                    off += static_cast<size_t>(comp) * fsz;
                    if (off + fsz > rawVerts.size()) return 0.0f;
                    if (ch.format == 0) {
                        float v = 0.0f;
                        std::memcpy(&v, rawVerts.data() + off, 4);
                        return v;
                    }
                    if (ch.format == 2) {
                        return rawVerts[off] / 255.0f;
                    }
                    return 0.0f;
                };

                // Channel 0 = Position, Channel 1 = Normal, Channel 3 = Color, Channel 4 = UV0
                if (channels.size() > 0 && channels[0].dimension >= 3) {
                    mesh.positions.resize(vertexCount);
                    for (u32 i = 0; i < vertexCount; ++i) {
                        mesh.positions[i] = {
                            readChannelFloat(channels[0], i, 0),
                            readChannelFloat(channels[0], i, 1),
                            readChannelFloat(channels[0], i, 2)
                        };
                    }
                }
                if (channels.size() > 1 && channels[1].dimension >= 3) {
                    mesh.normals.resize(vertexCount);
                    for (u32 i = 0; i < vertexCount; ++i) {
                        mesh.normals[i] = {
                            readChannelFloat(channels[1], i, 0),
                            readChannelFloat(channels[1], i, 1),
                            readChannelFloat(channels[1], i, 2)
                        };
                    }
                }
                if (channels.size() > 4 && channels[4].dimension >= 2) {
                    mesh.uvs.resize(vertexCount);
                    for (u32 i = 0; i < vertexCount; ++i) {
                        mesh.uvs[i] = {
                            readChannelFloat(channels[4], i, 0),
                            readChannelFloat(channels[4], i, 1)
                        };
                    }
                }
            }
        }
    }

    if (mesh.normals.size() != mesh.positions.size() && !mesh.positions.empty()) {
        mesh.RecalculateNormals();
    }
    mesh.RecalculateBounds();
    return Result<MeshData>::Ok(std::move(mesh));
}

Result<void> MeshCodec::ExportOBJ(const MeshData& mesh, const std::filesystem::path& objPath) {
    if (!mesh.IsValid()) return Result<void>::Err("Cannot export empty mesh to OBJ");
    std::ostringstream oss;
    oss << "# Exported by Coconut Decompiler\n";
    oss << "o " << (mesh.name.empty() ? "Mesh" : mesh.name) << "\n";
    oss << std::fixed << std::setprecision(6);

    for (const auto& p : mesh.positions) {
        oss << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    }
    for (const auto& uv : mesh.uvs) {
        oss << "vt " << uv.x << ' ' << uv.y << '\n';
    }
    for (const auto& n : mesh.normals) {
        oss << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    }

    bool hasUv = mesh.uvs.size() == mesh.positions.size();
    bool hasNrm = mesh.normals.size() == mesh.positions.size();

    auto writeTri = [&](u32 a, u32 b, u32 c) {
        auto fmt = [&](u32 idx) {
            u32 v = idx + 1;
            if (hasUv && hasNrm) oss << v << '/' << v << '/' << v;
            else if (hasNrm)     oss << v << "//" << v;
            else if (hasUv)      oss << v << '/' << v;
            else                 oss << v;
        };
        oss << "f ";
        fmt(a); oss << ' ';
        fmt(b); oss << ' ';
        fmt(c); oss << '\n';
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        writeTri(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
    }
    return platform::WriteFileText(objPath, oss.str());
}

Result<void> MeshCodec::ExportGLTF(const MeshData& mesh, const std::filesystem::path& gltfPath) {
    if (!mesh.IsValid()) return Result<void>::Err("Cannot export empty mesh to glTF");

    ByteBuffer bin;
    auto append = [&](const void* ptr, size_t bytes) -> size_t {
        while (bin.size() % 4 != 0) bin.push_back(0);
        size_t off = bin.size();
        const auto* p = static_cast<const u8*>(ptr);
        bin.insert(bin.end(), p, p + bytes);
        return off;
    };

    size_t posOff = append(mesh.positions.data(), mesh.positions.size() * sizeof(glm::vec3));
    size_t posLen = mesh.positions.size() * sizeof(glm::vec3);

    size_t nrmOff = 0, nrmLen = 0;
    bool hasNrm = mesh.normals.size() == mesh.positions.size();
    if (hasNrm) {
        nrmOff = append(mesh.normals.data(), mesh.normals.size() * sizeof(glm::vec3));
        nrmLen = mesh.normals.size() * sizeof(glm::vec3);
    }

    size_t idxOff = append(mesh.indices.data(), mesh.indices.size() * sizeof(u32));
    size_t idxLen = mesh.indices.size() * sizeof(u32);

    std::string uri = "data:application/octet-stream;base64," + Base64Encode(bin);

    nlohmann::json j;
    j["asset"] = {{"version", "2.0"}, {"generator", "Coconut Decompiler"}};
    j["scene"] = 0;
    j["scenes"] = {{{"nodes", {0}}}};
    j["nodes"] = {{{"name", mesh.name}, {"mesh", 0}}};
    j["buffers"] = {{{"uri", uri}, {"byteLength", bin.size()}}};

    nlohmann::json views = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    nlohmann::json attrs = nlohmann::json::object();

    // 0: Positions
    views.push_back({{"buffer", 0}, {"byteOffset", posOff}, {"byteLength", posLen}, {"target", 34962}});
    accessors.push_back({
        {"bufferView", 0}, {"componentType", 5126}, {"count", mesh.positions.size()}, {"type", "VEC3"},
        {"min", {mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z}},
        {"max", {mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z}}
    });
    attrs["POSITION"] = 0;

    int nextIdx = 1;
    if (hasNrm) {
        views.push_back({{"buffer", 0}, {"byteOffset", nrmOff}, {"byteLength", nrmLen}, {"target", 34962}});
        accessors.push_back({{"bufferView", nextIdx}, {"componentType", 5126}, {"count", mesh.normals.size()}, {"type", "VEC3"}});
        attrs["NORMAL"] = nextIdx++;
    }

    // Indices
    views.push_back({{"buffer", 0}, {"byteOffset", idxOff}, {"byteLength", idxLen}, {"target", 34963}});
    accessors.push_back({{"bufferView", nextIdx}, {"componentType", 5125}, {"count", mesh.indices.size()}, {"type", "SCALAR"}});
    int idxAccessor = nextIdx;

    j["bufferViews"] = views;
    j["accessors"] = accessors;
    j["meshes"] = {{
        {"name", mesh.name},
        {"primitives", {{{"attributes", attrs}, {"indices", idxAccessor}}}}
    }};

    return platform::WriteFileText(gltfPath, j.dump(2));
}

Result<void> MeshCodec::ExportFBX(const MeshData& mesh, const std::filesystem::path& fbxPath) {
    if (!mesh.IsValid()) return Result<void>::Err("Cannot export empty mesh to FBX");

    std::string safeName = mesh.name.empty() ? "Mesh" : mesh.name;
    for (char& c : safeName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    }

    std::vector<i32> polyIdx;
    polyIdx.reserve(mesh.indices.size());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        i32 a = static_cast<i32>(mesh.indices[i]);
        i32 b = static_cast<i32>(mesh.indices[i + 1]);
        i32 c = static_cast<i32>(mesh.indices[i + 2]);
        if (a < static_cast<i32>(mesh.positions.size()) &&
            b < static_cast<i32>(mesh.positions.size()) &&
            c < static_cast<i32>(mesh.positions.size())) {
            polyIdx.push_back(a);
            polyIdx.push_back(c);
            polyIdx.push_back(-(b + 1)); // FBX polygon closing index
        }
    }
    if (polyIdx.empty()) return Result<void>::Err("Mesh has no valid triangles for FBX export");

    const bool hasNormals = mesh.normals.size() == mesh.positions.size();
    const bool hasUvs = mesh.uvs.size() == mesh.positions.size();

    std::ostringstream oss;
    oss << "; FBX 7.4.0 project file\n"
        << "; Exported by Coconut Decompiler\n"
        << "FBXHeaderExtension:  {\n"
        << "\tFBXHeaderVersion: 1003\n"
        << "\tFBXVersion: 7400\n"
        << "\tCreator: \"Coconut Decompiler\"\n"
        << "}\n"
        << "GlobalSettings:  {\n"
        << "\tVersion: 1000\n"
        << "\tProperties70:  {\n"
        << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        << "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        << "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        << "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
        << "\t}\n"
        << "}\n"
        << "Objects:  {\n"
        << "\tGeometry: 100001, \"Geometry::" << safeName << "\", \"Mesh\" {\n"
        << "\t\tVertices: *" << (mesh.positions.size() * 3) << " {\n"
        << "\t\t\ta: ";
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        if (i > 0) oss << ',';
        const auto& v = mesh.positions[i];
        oss << -v.x << ',' << v.y << ',' << v.z;
    }
    oss << "\n\t\t}\n"
        << "\t\tPolygonVertexIndex: *" << polyIdx.size() << " {\n"
        << "\t\t\ta: ";
    for (size_t i = 0; i < polyIdx.size(); ++i) {
        if (i > 0) oss << ',';
        oss << polyIdx[i];
    }
    oss << "\n\t\t}\n";

    if (hasNormals) {
        oss << "\t\tLayerElementNormal: 0 {\n"
            << "\t\t\tVersion: 101\n"
            << "\t\t\tName: \"\"\n"
            << "\t\t\tMappingInformationType: \"ByVertice\"\n"
            << "\t\t\tReferenceInformationType: \"Direct\"\n"
            << "\t\t\tNormals: *" << (mesh.normals.size() * 3) << " {\n"
            << "\t\t\t\ta: ";
        for (size_t i = 0; i < mesh.normals.size(); ++i) {
            if (i > 0) oss << ',';
            const auto& n = mesh.normals[i];
            oss << -n.x << ',' << n.y << ',' << n.z;
        }
        oss << "\n\t\t\t}\n\t\t}\n";
    }

    if (hasUvs) {
        oss << "\t\tLayerElementUV: 0 {\n"
            << "\t\t\tVersion: 101\n"
            << "\t\t\tName: \"UVMap\"\n"
            << "\t\t\tMappingInformationType: \"ByVertice\"\n"
            << "\t\t\tReferenceInformationType: \"Direct\"\n"
            << "\t\t\tUV: *" << (mesh.uvs.size() * 2) << " {\n"
            << "\t\t\t\ta: ";
        for (size_t i = 0; i < mesh.uvs.size(); ++i) {
            if (i > 0) oss << ',';
            const auto& uv = mesh.uvs[i];
            oss << uv.x << ',' << uv.y;
        }
        oss << "\n\t\t\t}\n\t\t}\n";
    }

    oss << "\t\tLayer: 0 {\n\t\t\tVersion: 100\n";
    if (hasNormals) {
        oss << "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
    }
    if (hasUvs) {
        oss << "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
    }
    oss << "\t\t}\n\t}\n"
        << "\tModel: 200001, \"Model::" << safeName << "\", \"Mesh\" {\n"
        << "\t\tVersion: 232\n"
        << "\t\tShading: T\n"
        << "\t\tCulling: \"CullingOff\"\n"
        << "\t}\n"
        << "}\n"
        << "Connections:  {\n"
        << "\tC: \"OO\",200001,0\n"
        << "\tC: \"OO\",100001,200001\n"
        << "}\n";

    return platform::WriteFileText(fbxPath, oss.str());
}

MeshData MeshCodec::CreateBox(const std::string& name, glm::vec3 size) {
    MeshData m;
    m.name = name;
    glm::vec3 h = size * 0.5f;

    struct Face { glm::vec3 n; glm::vec3 v[4]; };
    Face faces[6] = {
        {{ 0, 0, 1}, {{-h.x,-h.y, h.z}, { h.x,-h.y, h.z}, { h.x, h.y, h.z}, {-h.x, h.y, h.z}}},
        {{ 0, 0,-1}, {{ h.x,-h.y,-h.z}, {-h.x,-h.y,-h.z}, {-h.x, h.y,-h.z}, { h.x, h.y,-h.z}}},
        {{ 1, 0, 0}, {{ h.x,-h.y, h.z}, { h.x,-h.y,-h.z}, { h.x, h.y,-h.z}, { h.x, h.y, h.z}}},
        {{-1, 0, 0}, {{-h.x,-h.y,-h.z}, {-h.x,-h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y,-h.z}}},
        {{ 0, 1, 0}, {{-h.x, h.y, h.z}, { h.x, h.y, h.z}, { h.x, h.y,-h.z}, {-h.x, h.y,-h.z}}},
        {{ 0,-1, 0}, {{-h.x,-h.y,-h.z}, { h.x,-h.y,-h.z}, { h.x,-h.y, h.z}, {-h.x,-h.y, h.z}}}
    };

    for (const auto& f : faces) {
        u32 base = static_cast<u32>(m.positions.size());
        glm::vec2 uvs[4] = {{0,0}, {1,0}, {1,1}, {0,1}};
        for (int i = 0; i < 4; ++i) {
            m.positions.push_back(f.v[i]);
            m.normals.push_back(f.n);
            m.uvs.push_back(uvs[i]);
        }
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    m.subMeshes.push_back({0, static_cast<u32>(m.indices.size()), 0, 0, static_cast<u32>(m.positions.size())});
    m.RecalculateBounds();
    return m;
}

MeshData MeshCodec::CreateSphere(const std::string& name, float radius, int stacks, int slices) {
    MeshData m;
    m.name = name;
    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / stacks;
        float phi = v * kPi;
        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / slices;
            float theta = u * 2.0f * kPi;
            glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            m.positions.push_back(n * radius);
            m.normals.push_back(n);
            m.uvs.push_back({u, 1.0f - v});
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            u32 r0 = static_cast<u32>(i * (slices + 1) + j);
            u32 r1 = static_cast<u32>((i + 1) * (slices + 1) + j);
            m.indices.insert(m.indices.end(), {r0, r1, r0 + 1, r0 + 1, r1, r1 + 1});
        }
    }
    m.subMeshes.push_back({0, static_cast<u32>(m.indices.size()), 0, 0, static_cast<u32>(m.positions.size())});
    m.RecalculateBounds();
    return m;
}

MeshData MeshCodec::CreateCylinder(const std::string& name, float radius, float height, int slices) {
    MeshData m;
    m.name = name;
    float hy = height * 0.5f;
    for (int i = 0; i <= slices; ++i) {
        float u = static_cast<float>(i) / slices;
        float a = u * 2.0f * kPi;
        float c = std::cos(a), s = std::sin(a);
        m.positions.push_back({c * radius, -hy, s * radius});
        m.normals.push_back({c, 0, s});
        m.uvs.push_back({u, 0});
        m.positions.push_back({c * radius, hy, s * radius});
        m.normals.push_back({c, 0, s});
        m.uvs.push_back({u, 1});
    }
    for (int i = 0; i < slices; ++i) {
        u32 b = static_cast<u32>(i * 2);
        m.indices.insert(m.indices.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
    }
    // Top cap
    u32 topCenter = static_cast<u32>(m.positions.size());
    m.positions.push_back({0, hy, 0}); m.normals.push_back({0, 1, 0}); m.uvs.push_back({0.5f, 0.5f});
    for (int i = 0; i <= slices; ++i) {
        float a = static_cast<float>(i) / slices * 2.0f * kPi;
        m.positions.push_back({std::cos(a) * radius, hy, std::sin(a) * radius});
        m.normals.push_back({0, 1, 0});
        m.uvs.push_back({0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
    }
    for (int i = 0; i < slices; ++i) {
        m.indices.insert(m.indices.end(), {topCenter, topCenter + 1 + static_cast<u32>(i) + 1, topCenter + 1 + static_cast<u32>(i)});
    }
    m.subMeshes.push_back({0, static_cast<u32>(m.indices.size()), 0, 0, static_cast<u32>(m.positions.size())});
    m.RecalculateBounds();
    return m;
}

MeshData MeshCodec::CreatePlane(const std::string& name, float width, float depth) {
    MeshData m;
    m.name = name;
    float hw = width * 0.5f, hd = depth * 0.5f;
    m.positions = {{-hw, 0, -hd}, {hw, 0, -hd}, {hw, 0, hd}, {-hw, 0, hd}};
    m.normals = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    m.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    m.indices = {0, 2, 1, 0, 3, 2};
    m.subMeshes.push_back({0, 6, 0, 0, 4});
    m.RecalculateBounds();
    return m;
}

MeshData MeshCodec::CombineMeshes(const std::string& name, const std::vector<std::pair<MeshData, glm::mat4>>& parts) {
    MeshData combined;
    combined.name = name;
    for (const auto& [src, xf] : parts) {
        MeshData copy = src;
        copy.Transform(xf);
        u32 baseVert = static_cast<u32>(combined.positions.size());
        u32 firstIdx = static_cast<u32>(combined.indices.size());
        combined.positions.insert(combined.positions.end(), copy.positions.begin(), copy.positions.end());
        combined.normals.insert(combined.normals.end(), copy.normals.begin(), copy.normals.end());
        combined.uvs.insert(combined.uvs.end(), copy.uvs.begin(), copy.uvs.end());
        for (u32 idx : copy.indices) {
            combined.indices.push_back(baseVert + idx);
        }
        SubMeshInfo sm;
        sm.firstByte = firstIdx * 4;
        sm.indexCount = static_cast<u32>(copy.indices.size());
        sm.topology = 0;
        sm.firstVertex = baseVert;
        sm.vertexCount = static_cast<u32>(copy.positions.size());
        combined.subMeshes.push_back(sm);
    }
    combined.RecalculateBounds();
    return combined;
}

} // namespace coconut::assets
