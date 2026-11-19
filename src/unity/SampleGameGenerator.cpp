#include "unity/SampleGameGenerator.h"
#include "unity/ClassIDs.h"
#include "unity/SerializedFile.h"
#include "unity/TypeTree.h"
#include "unity/UnityFS.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "core/BinaryWriter.h"
#include "core/Compression.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

namespace coconut::unity {

namespace {

// Helper to build TypeTree nodes concisely
class TreeBuilder {
public:
    TreeBuilder& Node(u8 level, std::string type, std::string name, i32 size = -1, u32 meta = 0, u8 flags = 0) {
        TypeTreeNode n;
        n.version = 1;
        n.level = level;
        n.type = std::move(type);
        n.name = std::move(name);
        n.byteSize = size;
        n.index = static_cast<i32>(tree_.nodes.size());
        n.metaFlag = meta;
        n.typeFlags = flags;
        tree_.nodes.push_back(std::move(n));
        return *this;
    }

    TreeBuilder& PPtr(u8 lvl, const std::string& typeName, const std::string& fieldName) {
        return Node(lvl, typeName, fieldName, 12)
            .Node(lvl + 1, "int", "m_FileID", 4)
            .Node(lvl + 1, "SInt64", "m_PathID", 8);
    }

    TreeBuilder& Vec3(u8 lvl, const std::string& name) {
        return Node(lvl, "Vector3f", name, 12)
            .Node(lvl + 1, "float", "x", 4)
            .Node(lvl + 1, "float", "y", 4)
            .Node(lvl + 1, "float", "z", 4);
    }

    TreeBuilder& Quat(u8 lvl, const std::string& name) {
        return Node(lvl, "Quaternionf", name, 16)
            .Node(lvl + 1, "float", "x", 4)
            .Node(lvl + 1, "float", "y", 4)
            .Node(lvl + 1, "float", "z", 4)
            .Node(lvl + 1, "float", "w", 4);
    }

    TreeBuilder& Color(u8 lvl, const std::string& name) {
        return Node(lvl, "ColorRGBA", name, 16)
            .Node(lvl + 1, "float", "r", 4)
            .Node(lvl + 1, "float", "g", 4)
            .Node(lvl + 1, "float", "b", 4)
            .Node(lvl + 1, "float", "a", 4);
    }

    TypeTree Build() { return std::move(tree_); }

private:
    TypeTree tree_;
};

SerializedType MakeGameObjectType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::GameObject);
    st.typeTree = TreeBuilder()
        .Node(0, "GameObject", "Base")
        .Node(1, "vector", "m_Component")
            .Node(2, "Array", "Array", -1, 0, 1)
                .Node(3, "int", "size", 4)
                .Node(3, "ComponentPair", "data", 12)
                    .PPtr(4, "PPtr<Component>", "component")
        .Node(1, "unsigned int", "m_Layer", 4)
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "UInt16", "m_Tag", 2)
        .Node(1, "bool", "m_IsActive", 1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeTransformType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Transform);
    st.typeTree = TreeBuilder()
        .Node(0, "Transform", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .Quat(1, "m_LocalRotation")
        .Vec3(1, "m_LocalPosition")
        .Vec3(1, "m_LocalScale")
        .Node(1, "vector", "m_Children")
            .Node(2, "Array", "Array", -1, 0, 1)
                .Node(3, "int", "size", 4)
                .PPtr(3, "PPtr<Transform>", "data")
        .PPtr(1, "PPtr<Transform>", "m_Father")
        .Build();
    return st;
}

SerializedType MakeMeshFilterType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::MeshFilter);
    st.typeTree = TreeBuilder()
        .Node(0, "MeshFilter", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .PPtr(1, "PPtr<Mesh>", "m_Mesh")
        .Build();
    return st;
}

SerializedType MakeMeshRendererType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::MeshRenderer);
    st.typeTree = TreeBuilder()
        .Node(0, "MeshRenderer", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .Node(1, "bool", "m_Enabled", 1, 0x4000)
        .Node(1, "vector", "m_Materials")
            .Node(2, "Array", "Array", -1, 0, 1)
                .Node(3, "int", "size", 4)
                .PPtr(3, "PPtr<Material>", "data")
        .Node(1, "StaticBatchInfo", "m_StaticBatchInfo", 4)
            .Node(2, "UInt16", "firstSubMesh", 2)
            .Node(2, "UInt16", "subMeshCount", 2)
        .PPtr(1, "PPtr<Transform>", "m_StaticBatchRoot")
        .Build();
    return st;
}

SerializedType MakeCameraType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Camera);
    st.typeTree = TreeBuilder()
        .Node(0, "Camera", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .Node(1, "bool", "m_Enabled", 1, 0x4000)
        .Node(1, "float", "field of view", 4)
        .Node(1, "float", "near clip plane", 4)
        .Node(1, "float", "far clip plane", 4)
        .Build();
    return st;
}

SerializedType MakeLightType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Light);
    st.typeTree = TreeBuilder()
        .Node(0, "Light", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .Node(1, "bool", "m_Enabled", 1, 0x4000)
        .Node(1, "int", "m_Type", 4)
        .Color(1, "m_Color")
        .Node(1, "float", "m_Intensity", 4)
        .Build();
    return st;
}

SerializedType MakeMeshType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Mesh);
    st.typeTree = TreeBuilder()
        .Node(0, "Mesh", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "vector", "m_SubMeshes")
            .Node(2, "Array", "Array", -1, 0, 1)
                .Node(3, "int", "size", 4)
                .Node(3, "SubMesh", "data", 20)
                    .Node(4, "unsigned int", "firstByte", 4)
                    .Node(4, "unsigned int", "indexCount", 4)
                    .Node(4, "int", "topology", 4)
                    .Node(4, "unsigned int", "firstVertex", 4)
                    .Node(4, "unsigned int", "vertexCount", 4)
        .Node(1, "int", "m_IndexFormat", 4)
        .Node(1, "vector", "m_IndexBuffer")
            .Node(2, "Array", "Array", -1, 0x4000, 1)
                .Node(3, "int", "size", 4)
                .Node(3, "UInt8", "data", 1)
        .Node(1, "VertexData", "m_VertexData")
            .Node(2, "unsigned int", "m_VertexCount", 4)
            .Node(2, "vector", "m_Channels")
                .Node(3, "Array", "Array", -1, 0, 1)
                    .Node(4, "int", "size", 4)
                    .Node(4, "ChannelInfo", "data", 4)
                        .Node(5, "UInt8", "stream", 1)
                        .Node(5, "UInt8", "offset", 1)
                        .Node(5, "UInt8", "format", 1)
                        .Node(5, "UInt8", "dimension", 1)
            .Node(2, "TypelessData", "m_DataSize", -1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeMaterialType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Material);
    st.typeTree = TreeBuilder()
        .Node(0, "Material", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .PPtr(1, "PPtr<Shader>", "m_Shader")
        .Node(1, "string", "m_ShaderName", -1, 0x4000)
        .Node(1, "UnityPropertySheet", "m_SavedProperties")
            .Node(2, "map", "m_TexEnvs")
                .Node(3, "Array", "Array", -1, 0, 1)
                    .Node(4, "int", "size", 4)
                    .Node(4, "pair", "data")
                        .Node(5, "string", "first", -1, 0x4000)
                        .Node(5, "UnityTexEnv", "second")
                            .PPtr(6, "PPtr<Texture>", "m_Texture")
            .Node(2, "map", "m_Floats")
                .Node(3, "Array", "Array", -1, 0, 1)
                    .Node(4, "int", "size", 4)
                    .Node(4, "pair", "data")
                        .Node(5, "string", "first", -1, 0x4000)
                        .Node(5, "float", "second", 4)
            .Node(2, "map", "m_Colors")
                .Node(3, "Array", "Array", -1, 0, 1)
                    .Node(4, "int", "size", 4)
                    .Node(4, "pair", "data")
                        .Node(5, "string", "first", -1, 0x4000)
                        .Color(5, "second")
        .Build();
    return st;
}

SerializedType MakeTexture2DType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::Texture2D);
    st.typeTree = TreeBuilder()
        .Node(0, "Texture2D", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "int", "m_Width", 4)
        .Node(1, "int", "m_Height", 4)
        .Node(1, "unsigned int", "m_CompleteImageSize", 4)
        .Node(1, "int", "m_TextureFormat", 4)
        .Node(1, "int", "m_MipCount", 4)
        .Node(1, "TypelessData", "image data", -1, 0x4000)
        .Node(1, "StreamingInfo", "m_StreamData")
            .Node(2, "UInt64", "offset", 8)
            .Node(2, "unsigned int", "size", 4)
            .Node(2, "string", "path", -1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeAudioClipType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::AudioClip);
    st.typeTree = TreeBuilder()
        .Node(0, "AudioClip", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "int", "m_Channels", 4)
        .Node(1, "int", "m_Frequency", 4)
        .Node(1, "int", "m_BitsPerSample", 4)
        .Node(1, "float", "m_Length", 4)
        .Node(1, "int", "m_CompressionFormat", 4)
        .Node(1, "TypelessData", "m_AudioData", -1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeTextAssetType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::TextAsset);
    st.typeTree = TreeBuilder()
        .Node(0, "TextAsset", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "string", "m_Script", -1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeMonoScriptType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::MonoScript);
    st.typeTree = TreeBuilder()
        .Node(0, "MonoScript", "Base")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "string", "m_ClassName", -1, 0x4000)
        .Node(1, "string", "m_Namespace", -1, 0x4000)
        .Node(1, "string", "m_AssemblyName", -1, 0x4000)
        .Build();
    return st;
}

SerializedType MakeMonoBehaviourType() {
    SerializedType st;
    st.classId = static_cast<i32>(ClassID::MonoBehaviour);
    st.typeTree = TreeBuilder()
        .Node(0, "MonoBehaviour", "Base")
        .PPtr(1, "PPtr<GameObject>", "m_GameObject")
        .Node(1, "UInt8", "m_Enabled", 1, 0x4000)
        .PPtr(1, "PPtr<MonoScript>", "m_Script")
        .Node(1, "string", "m_Name", -1, 0x4000)
        .Node(1, "float", "moveSpeed", 4)
        .Node(1, "int", "maxHealth", 4)
        .Build();
    return st;
}

// Binary writers matching the exact TypeTrees above
ByteBuffer SerializeGameObject(const std::string& name, const std::vector<i64>& compPathIds, u32 layer = 0, bool active = true) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(static_cast<i32>(compPathIds.size()));
    for (i64 id : compPathIds) {
        w.WriteI32(0); // m_FileID
        w.WriteI64(id);
    }
    w.WriteU32(layer);
    w.WriteAlignedString(name);
    w.WriteU16(0); // m_Tag
    w.WriteBool(active);
    w.Align(4);
    return w.TakeBuffer();
}

ByteBuffer SerializeTransform(i64 goId, glm::vec3 pos, glm::vec4 rot, glm::vec3 scale,
                              const std::vector<i64>& children, i64 fatherId = 0) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteF32(rot.x); w.WriteF32(rot.y); w.WriteF32(rot.z); w.WriteF32(rot.w);
    w.WriteF32(pos.x); w.WriteF32(pos.y); w.WriteF32(pos.z);
    w.WriteF32(scale.x); w.WriteF32(scale.y); w.WriteF32(scale.z);
    w.WriteI32(static_cast<i32>(children.size()));
    for (i64 cid : children) { w.WriteI32(0); w.WriteI64(cid); }
    w.WriteI32(0); w.WriteI64(fatherId);
    return w.TakeBuffer();
}

ByteBuffer SerializeMeshFilter(i64 goId, i64 meshId) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteI32(0); w.WriteI64(meshId);
    return w.TakeBuffer();
}

ByteBuffer SerializeMeshRenderer(i64 goId, i64 matId, u16 firstSubMesh = 0, u16 subMeshCount = 0) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteBool(true); w.Align(4);
    w.WriteI32(1); w.WriteI32(0); w.WriteI64(matId);
    w.WriteU16(firstSubMesh);
    w.WriteU16(subMeshCount);
    w.WriteI32(0); w.WriteI64(0);
    return w.TakeBuffer();
}

ByteBuffer SerializeCamera(i64 goId, float fov = 60.0f) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteBool(true); w.Align(4);
    w.WriteF32(fov); w.WriteF32(0.1f); w.WriteF32(1000.0f);
    return w.TakeBuffer();
}

ByteBuffer SerializeLight(i64 goId, glm::vec4 color, float intensity = 1.25f) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteBool(true); w.Align(4);
    w.WriteI32(1); // Directional
    w.WriteF32(color.r); w.WriteF32(color.g); w.WriteF32(color.b); w.WriteF32(color.a);
    w.WriteF32(intensity);
    return w.TakeBuffer();
}

ByteBuffer SerializeMesh(const assets::MeshData& m) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(m.name);

    w.WriteI32(static_cast<i32>(m.subMeshes.size()));
    for (const auto& sm : m.subMeshes) {
        w.WriteU32(sm.firstByte);
        w.WriteU32(sm.indexCount);
        w.WriteI32(sm.topology);
        w.WriteU32(sm.firstVertex);
        w.WriteU32(sm.vertexCount);
    }

    w.WriteI32(1); // UInt32 indices
    w.WriteI32(static_cast<i32>(m.indices.size() * 4));
    for (u32 idx : m.indices) w.WriteU32(idx);
    w.Align(4);

    // VertexData: interleaved Pos(3f) + Normal(3f) + UV(2f) = 32 bytes/vert
    w.WriteU32(static_cast<u32>(m.positions.size()));
    w.WriteI32(5); // 5 channels (0=Pos, 1=Normal, 2=Tangent(0), 3=Color(0), 4=UV0)
    // Channel 0: Pos
    w.WriteU8(0); w.WriteU8(0);  w.WriteU8(0); w.WriteU8(3);
    // Channel 1: Normal
    w.WriteU8(0); w.WriteU8(12); w.WriteU8(0); w.WriteU8(3);
    // Channel 2: Tangent (unused)
    w.WriteU8(0); w.WriteU8(0);  w.WriteU8(0); w.WriteU8(0);
    // Channel 3: Color (unused)
    w.WriteU8(0); w.WriteU8(0);  w.WriteU8(0); w.WriteU8(0);
    // Channel 4: UV0
    w.WriteU8(0); w.WriteU8(24); w.WriteU8(0); w.WriteU8(2);

    BinaryWriter vb(Endian::Little);
    for (size_t i = 0; i < m.positions.size(); ++i) {
        vb.WriteF32(m.positions[i].x);
        vb.WriteF32(m.positions[i].y);
        vb.WriteF32(m.positions[i].z);
        glm::vec3 n = (i < m.normals.size()) ? m.normals[i] : glm::vec3(0, 1, 0);
        vb.WriteF32(n.x); vb.WriteF32(n.y); vb.WriteF32(n.z);
        glm::vec2 uv = (i < m.uvs.size()) ? m.uvs[i] : glm::vec2(0, 0);
        vb.WriteF32(uv.x); vb.WriteF32(uv.y);
    }
    w.WriteI32(static_cast<i32>(vb.Size()));
    w.WriteBytes(vb.Buffer());
    w.Align(4);
    return w.TakeBuffer();
}

ByteBuffer SerializeMaterial(const std::string& name, const std::string& shaderName,
                             glm::vec4 color, f32 metallic, f32 smoothness, i64 texPathId = 0) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(name);
    w.WriteI32(0); w.WriteI64(0); // shader pptr
    w.WriteAlignedString(shaderName);

    // m_TexEnvs
    if (texPathId != 0) {
        w.WriteI32(1);
        w.WriteAlignedString("_MainTex");
        w.WriteI32(0); w.WriteI64(texPathId);
    } else {
        w.WriteI32(0);
    }

    // m_Floats
    w.WriteI32(2);
    w.WriteAlignedString("_Metallic");   w.WriteF32(metallic);
    w.WriteAlignedString("_Glossiness"); w.WriteF32(smoothness);

    // m_Colors
    w.WriteI32(1);
    w.WriteAlignedString("_Color");
    w.WriteF32(color.r); w.WriteF32(color.g); w.WriteF32(color.b); w.WriteF32(color.a);
    return w.TakeBuffer();
}

ByteBuffer SerializeTexture2D(const std::string& name, i32 wPx, i32 hPx, i32 fmt, ByteSpan rawData) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(name);
    w.WriteI32(wPx);
    w.WriteI32(hPx);
    w.WriteU32(static_cast<u32>(rawData.size()));
    w.WriteI32(fmt);
    w.WriteI32(1);
    w.WriteI32(static_cast<i32>(rawData.size()));
    w.WriteBytes(rawData);
    w.Align(4);
    // m_StreamData
    w.WriteU64(0);
    w.WriteU32(0);
    w.WriteAlignedString("");
    return w.TakeBuffer();
}

ByteBuffer SerializeAudioClip(const assets::AudioClipData& clip) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(clip.name);
    w.WriteI32(clip.channels);
    w.WriteI32(clip.frequency);
    w.WriteI32(clip.bitsPerSample);
    w.WriteF32(clip.lengthSeconds);
    w.WriteI32(clip.compressionFormat);
    w.WriteI32(static_cast<i32>(clip.rawAudio.size()));
    w.WriteBytes(clip.rawAudio);
    w.Align(4);
    return w.TakeBuffer();
}

ByteBuffer SerializeTextAsset(const std::string& name, const std::string& content) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(name);
    w.WriteAlignedString(content);
    return w.TakeBuffer();
}

ByteBuffer SerializeMonoScript(const std::string& name, const std::string& cls, const std::string& ns, const std::string& asmName) {
    BinaryWriter w(Endian::Little);
    w.WriteAlignedString(name);
    w.WriteAlignedString(cls);
    w.WriteAlignedString(ns);
    w.WriteAlignedString(asmName);
    return w.TakeBuffer();
}

ByteBuffer SerializeMonoBehaviour(i64 goId, i64 scriptId, const std::string& name, f32 moveSpeed, i32 maxHealth) {
    BinaryWriter w(Endian::Little);
    w.WriteI32(0); w.WriteI64(goId);
    w.WriteU8(1); w.Align(4);
    w.WriteI32(0); w.WriteI64(scriptId);
    w.WriteAlignedString(name);
    w.WriteF32(moveSpeed);
    w.WriteI32(maxHealth);
    return w.TakeBuffer();
}

SerializedFile BuildDemoSceneSerializedFile(TargetPlatform platform) {
    SerializedFile sf;
    sf.name = "level0";
    sf.header.version = 22;
    sf.header.bigEndian = false;
    sf.unityVersionString = "2022.3.15f1";
    sf.unityVersion = UnityVersion::Parse(sf.unityVersionString);
    sf.targetPlatform = platform;
    sf.enableTypeTree = true;

    // Type table indices:
    // 0=GameObject, 1=Transform, 2=MeshFilter, 3=MeshRenderer, 4=Camera, 5=Light,
    // 6=Mesh, 7=Material, 8=Texture2D, 9=AudioClip, 10=TextAsset, 11=MonoScript, 12=MonoBehaviour
    sf.types = {
        MakeGameObjectType(), MakeTransformType(), MakeMeshFilterType(), MakeMeshRendererType(),
        MakeCameraType(), MakeLightType(), MakeMeshType(), MakeMaterialType(),
        MakeTexture2DType(), MakeAudioClipType(), MakeTextAssetType(), MakeMonoScriptType(),
        MakeMonoBehaviourType()
    };

    auto addObj = [&](i64 pathId, i32 typeIdx, ByteBuffer raw) {
        ObjectInfo o;
        o.pathId = pathId;
        o.typeId = typeIdx;
        o.classId = sf.types[static_cast<size_t>(typeIdx)].classId;
        o.byteSize = static_cast<u32>(raw.size());
        o.rawData = std::move(raw);
        sf.objects.push_back(std::move(o));
    };

    // --- Textures (PathIDs 500-501) ---
    {
        const i32 W = 128, H = 128;
        ByteBuffer px(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool tile = ((x / 16) + (y / 16)) % 2 == 0;
                bool border = (x % 16 == 0) || (y % 16 == 0);
                size_t idx = (static_cast<size_t>(y) * W + x) * 4;
                if (border) {
                    px[idx + 0] = 232; px[idx + 1] = 155; px[idx + 2] = 60; px[idx + 3] = 255;
                } else if (tile) {
                    px[idx + 0] = 44;  px[idx + 1] = 52;  px[idx + 2] = 64; px[idx + 3] = 255;
                } else {
                    px[idx + 0] = 30;  px[idx + 1] = 36;  px[idx + 2] = 45; px[idx + 3] = 255;
                }
            }
        }
        addObj(500, 8, SerializeTexture2D("IndustrialGrid_Albedo", W, H, 4 /*RGBA32*/, px));
    }
    {
        // Real DXT1/BC1 64x64 hazard stripe texture (16x16 blocks * 8 bytes = 2048 bytes)
        ByteBuffer dxt(16 * 16 * 8, 0);
        for (int by = 0; by < 16; ++by) {
            for (int bx = 0; bx < 16; ++bx) {
                u8* blk = dxt.data() + (by * 16 + bx) * 8;
                bool amber = ((bx + by) % 2) == 0;
                // RGB565 amber (#E89B3C -> 0xECD7) vs charcoal (#242A32 -> 0x2146)
                u16 c0 = amber ? 0xECD7u : 0x2146u;
                u16 c1 = amber ? 0x8AA2u : 0x10A3u;
                blk[0] = static_cast<u8>(c0 & 0xFF); blk[1] = static_cast<u8>(c0 >> 8);
                blk[2] = static_cast<u8>(c1 & 0xFF); blk[3] = static_cast<u8>(c1 >> 8);
                blk[4] = 0x55; blk[5] = 0xAA; blk[6] = 0x55; blk[7] = 0xAA;
            }
        }
        addObj(501, 8, SerializeTexture2D("HazardStripes_DXT1", 64, 64, 10 /*DXT1*/, dxt));
    }

    // --- Materials (PathIDs 400-404) ---
    addObj(400, 7, SerializeMaterial("FloorGridMat",     "Standard", {0.26f, 0.30f, 0.36f, 1.0f}, 0.2f, 0.45f, 500));
    addObj(401, 7, SerializeMaterial("TowerSteelMat",    "Standard", {0.32f, 0.46f, 0.58f, 1.0f}, 0.75f, 0.65f, 500));
    addObj(402, 7, SerializeMaterial("AmberBeaconMat",   "Standard", {0.92f, 0.61f, 0.23f, 1.0f}, 0.15f, 0.85f, 0));
    addObj(403, 7, SerializeMaterial("SupplyCrateMat",   "Standard", {0.68f, 0.47f, 0.26f, 1.0f}, 0.10f, 0.35f, 501));
    addObj(404, 7, SerializeMaterial("BatchedPillarMat", "Standard", {0.36f, 0.76f, 0.78f, 1.0f}, 0.55f, 0.70f, 0));

    // --- Meshes (PathIDs 300-305) ---
    auto planeMesh   = assets::MeshCodec::CreatePlane("ArenaPlane_Mesh", 14.0f, 14.0f);
    auto towerMesh   = assets::MeshCodec::CreateCylinder("WatchTower_Mesh", 0.9f, 3.2f, 20);
    auto sphereMesh  = assets::MeshCodec::CreateSphere("BeaconSphere_Mesh", 0.55f, 14, 20);
    auto crateMesh   = assets::MeshCodec::CreateBox("SupplyCrate_Mesh", {1.0f, 1.0f, 1.0f});
    auto lidMesh     = assets::MeshCodec::CreateBox("CrateLid_Mesh", {1.08f, 0.16f, 1.08f});

    // Combined static-batched mesh (2 pillars baked into world space!) for MeshDecombiner testing!
    auto pillarUnit = assets::MeshCodec::CreateBox("PillarUnit", {0.65f, 2.4f, 0.65f});
    glm::mat4 pA = glm::translate(glm::mat4(1.0f), glm::vec3(-4.2f, 1.2f, -3.0f));
    glm::mat4 pB = glm::translate(glm::mat4(1.0f), glm::vec3( 4.2f, 1.2f, -3.0f));
    auto combinedPillars = assets::MeshCodec::CombineMeshes(
        "Combined Mesh (root: scene)", {{pillarUnit, pA}, {pillarUnit, pB}});

    addObj(300, 6, SerializeMesh(planeMesh));
    addObj(301, 6, SerializeMesh(towerMesh));
    addObj(302, 6, SerializeMesh(sphereMesh));
    addObj(303, 6, SerializeMesh(crateMesh));
    addObj(304, 6, SerializeMesh(lidMesh));
    addObj(305, 6, SerializeMesh(combinedPillars));

    // --- AudioClip & TextAsset & MonoScript (PathIDs 600-605) ---
    auto chime = assets::AudioCodec::CreateSynthSineClip("BeaconChime_SFX", 523.25f, 0.45f, 22050);
    addObj(600, 9, SerializeAudioClip(chime));
    addObj(601, 10, SerializeTextAsset("LevelConfig",
        "{\n  \"scene\": \"CoconutArena_01\",\n  \"gravity\": -9.81,\n  \"spawnPoints\": 3,\n  \"enableStaticBatching\": true\n}\n"));
    addObj(602, 11, SerializeMonoScript("PlayerController", "PlayerController", "CoconutDemo", "Assembly-CSharp.dll"));
    addObj(603, 11, SerializeMonoScript("EnemySentinel",    "EnemySentinel",    "CoconutDemo", "Assembly-CSharp.dll"));

    // --- Scene GameObjects & Components (PathIDs 1000+) ---
    // 1. Main Camera (GO=1000, TF=1001, Cam=1002)
    addObj(1000, 0, SerializeGameObject("Main Camera", {1001, 1002}));
    addObj(1001, 1, SerializeTransform(1000, {0.0f, 4.5f, -9.0f}, {0.15f, 0.0f, 0.0f, 0.988f}, {1, 1, 1}, {}));
    addObj(1002, 4, SerializeCamera(1000, 60.0f));

    // 2. Directional Light (GO=1010, TF=1011, Light=1012)
    addObj(1010, 0, SerializeGameObject("Directional Light", {1011, 1012}));
    addObj(1011, 1, SerializeTransform(1010, {0.0f, 8.0f, 0.0f}, {0.38f, -0.25f, 0.1f, 0.88f}, {1, 1, 1}, {}));
    addObj(1012, 5, SerializeLight(1010, {1.0f, 0.96f, 0.88f, 1.0f}, 1.35f));

    // 3. ArenaFloor (GO=1020, TF=1021, MF=1022, MR=1023)
    addObj(1020, 0, SerializeGameObject("ArenaFloor", {1021, 1022, 1023}));
    addObj(1021, 1, SerializeTransform(1020, {0.0f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {}));
    addObj(1022, 2, SerializeMeshFilter(1020, 300));
    addObj(1023, 3, SerializeMeshRenderer(1020, 400));

    // 4. WatchTower (GO=1030, TF=1031, MF=1032, MR=1033, MB=1034) + Child BeaconCore (GO=1040, TF=1041, MF=1042, MR=1043)
    addObj(1030, 0, SerializeGameObject("WatchTower", {1031, 1032, 1033, 1034}));
    addObj(1031, 1, SerializeTransform(1030, {0.0f, 1.6f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {1041}, 0));
    addObj(1032, 2, SerializeMeshFilter(1030, 301));
    addObj(1033, 3, SerializeMeshRenderer(1030, 401));
    addObj(1034, 12, SerializeMonoBehaviour(1030, 602, "PlayerController", 6.5f, 100));

    addObj(1040, 0, SerializeGameObject("BeaconCore", {1041, 1042, 1043}));
    addObj(1041, 1, SerializeTransform(1040, {0.0f, 2.25f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {}, 1031));
    addObj(1042, 2, SerializeMeshFilter(1040, 302));
    addObj(1043, 3, SerializeMeshRenderer(1040, 402));

    // 5. Three repeated SupplyCrate hierarchies (for Prefab Reconstruction!)
    glm::vec3 cratePos[3] = {{-3.2f, 0.5f, 2.2f}, {0.0f, 0.5f, 3.4f}, {3.2f, 0.5f, 2.2f}};
    for (int i = 0; i < 3; ++i) {
        i64 base = 1100 + i * 20;
        std::string idxStr = "0" + std::to_string(i + 1);
        addObj(base + 0, 0, SerializeGameObject("SupplyCrate_" + idxStr, {base + 1, base + 2, base + 3}));
        addObj(base + 1, 1, SerializeTransform(base + 0, cratePos[i], {0, 0, 0, 1}, {1, 1, 1}, {base + 11}, 0));
        addObj(base + 2, 2, SerializeMeshFilter(base + 0, 303));
        addObj(base + 3, 3, SerializeMeshRenderer(base + 0, 403));

        addObj(base + 10, 0, SerializeGameObject("CrateLid", {base + 11, base + 12, base + 13}));
        addObj(base + 11, 1, SerializeTransform(base + 10, {0.0f, 0.58f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}, {}, base + 1));
        addObj(base + 12, 2, SerializeMeshFilter(base + 10, 304));
        addObj(base + 13, 3, SerializeMeshRenderer(base + 10, 403));
    }

    // 6. Two static-batched Pillars sharing Combined Mesh (PathID 305)
    addObj(1200, 0, SerializeGameObject("StaticPillar_A", {1201, 1202, 1203}));
    addObj(1201, 1, SerializeTransform(1200, {-4.2f, 1.2f, -3.0f}, {0, 0, 0, 1}, {1, 1, 1}, {}));
    addObj(1202, 2, SerializeMeshFilter(1200, 305));
    addObj(1203, 3, SerializeMeshRenderer(1200, 404, 0, 1));

    addObj(1210, 0, SerializeGameObject("StaticPillar_B", {1211, 1212, 1213}));
    addObj(1211, 1, SerializeTransform(1210, { 4.2f, 1.2f, -3.0f}, {0, 0, 0, 1}, {1, 1, 1}, {}));
    addObj(1212, 2, SerializeMeshFilter(1210, 305));
    addObj(1213, 3, SerializeMeshRenderer(1210, 404, 1, 1));

    return sf;
}

} // namespace

// Builds a real, valid ECMA-335 PE32/CLI .NET DLL ("Assembly-CSharp.dll") with real metadata & CIL bytecode!
ByteBuffer SampleGameGenerator::BuildSampleManagedAssemblyDll() {
    // Layout inside .text section (RVA 0x2000, file offset 0x0200):
    //   +0x000: IMAGE_COR20_HEADER (72 bytes)
    //   +0x050: CIL Method Bodies (aligned)
    //   +0x180: BSJB CLI Metadata Root + #~, #Strings, #US, #GUID, #Blob streams

    constexpr u32 kTextRva = 0x2000;
    constexpr u32 kTextRaw = 0x0200;

    // 1. Build String Heap (#Strings)
    std::string strHeap;
    strHeap.push_back('\0'); // offset 0 = ""
    auto addStr = [&](const std::string& s) -> u16 {
        u16 off = static_cast<u16>(strHeap.size());
        strHeap.append(s);
        strHeap.push_back('\0');
        return off;
    };

    u16 sModule        = addStr("Assembly-CSharp.dll");
    u16 sAsmName       = addStr("Assembly-CSharp");
    u16 sMscorlib      = addStr("mscorlib");
    u16 sUnityEngine   = addStr("UnityEngine.CoreModule");
    u16 sSysNs         = addStr("System");
    u16 sObject        = addStr("Object");
    u16 sUnityNs       = addStr("UnityEngine");
    u16 sMonoBeh       = addStr("MonoBehaviour");
    u16 sDebug         = addStr("Debug");
    u16 sLog           = addStr("Log");
    u16 sModType       = addStr("<Module>");
    u16 sDemoNs        = addStr("CoconutDemo");
    u16 sPlayerCtrl    = addStr("PlayerController");
    u16 sEnemySent     = addStr("EnemySentinel");
    u16 sScoreCalc     = addStr("ScoreCalculator");
    u16 sMoveSpeed     = addStr("moveSpeed");
    u16 sHealth        = addStr("health");
    u16 sMaxHealth     = addStr("maxHealth");
    u16 sIsAlerted     = addStr("isAlerted");
    u16 sPatrolRad     = addStr("patrolRadius");
    u16 sStart         = addStr("Start");
    u16 sUpdate        = addStr("Update");
    u16 sTakeDamage    = addStr("TakeDamage");
    u16 sHeal          = addStr("Heal");
    u16 sPatrol        = addStr("Patrol");
    u16 sComputeBonus  = addStr("ComputeBonus");
    u16 sParamAmount   = addStr("amount");
    u16 sParamBase     = addStr("baseScore");
    u16 sParamCombo    = addStr("comboMultiplier");

    while (strHeap.size() % 4 != 0) strHeap.push_back('\0');

    // 2. Build UserString Heap (#US) — UTF-16LE length-prefixed strings
    ByteBuffer usHeap;
    usHeap.push_back(0);
    auto addUS = [&](const std::string& ascii) -> u32 {
        u32 token = 0x70000000u | static_cast<u32>(usHeap.size());
        u8 byteLen = static_cast<u8>(ascii.size() * 2 + 1);
        usHeap.push_back(byteLen);
        for (char c : ascii) {
            usHeap.push_back(static_cast<u8>(c));
            usHeap.push_back(0);
        }
        usHeap.push_back(0); // terminal flag
        return token;
    };

    u32 usStarted   = addUS("CoconutDemo PlayerController initialized!");
    u32 usDefeated  = addUS("Player defeated - triggering respawn.");
    u32 usPatrolMsg = addUS("Sentinel scanning perimeter...");
    while (usHeap.size() % 4 != 0) usHeap.push_back(0);

    // 3. Build Blob Heap (#Blob) — ECMA-335 signatures
    ByteBuffer blobHeap;
    blobHeap.push_back(0);
    auto addBlob = [&](std::initializer_list<u8> bytes) -> u16 {
        u16 off = static_cast<u16>(blobHeap.size());
        blobHeap.push_back(static_cast<u8>(bytes.size()));
        for (u8 b : bytes) blobHeap.push_back(b);
        return off;
    };

    // Field signatures (0x06, elemType): R4=0x0C, I4=0x08, BOOLEAN=0x02
    u16 sigFieldFloat = addBlob({0x06, 0x0C});
    u16 sigFieldInt   = addBlob({0x06, 0x08});
    u16 sigFieldBool  = addBlob({0x06, 0x02});
    // Method signatures: HASTHIS=0x20, DEFAULT=0x00; VOID=0x01, I4=0x08, STRING=0x0E
    u16 sigInstVoid0   = addBlob({0x20, 0x00, 0x01});             // instance void ()
    u16 sigInstVoidI4  = addBlob({0x20, 0x01, 0x01, 0x08});       // instance void (int32)
    u16 sigStatVoidStr = addBlob({0x00, 0x01, 0x01, 0x0E});       // static void (string)
    u16 sigStatI4I4I4  = addBlob({0x00, 0x02, 0x08, 0x08, 0x08}); // static int32 (int32, int32)
    while (blobHeap.size() % 4 != 0) blobHeap.push_back(0);

    // 4. Build GUID Heap (#GUID)
    ByteBuffer guidHeap(16, 0);
    guidHeap[0] = 0xC0; guidHeap[1] = 0xC0; guidHeap[2] = 0xAA; guidHeap[3] = 0x55;

    // 5. Build real CIL method bodies (Tiny format: header = (codeSize << 2) | 0x02)
    // Tokens:
    //   FieldDef 1..5 -> 0x04000001..0x04000005 (moveSpeed, health, maxHealth, isAlerted, patrolRadius)
    //   MemberRef 1   -> 0x0A000001 (Debug.Log)
    auto makeTinyMethod = [](const ByteBuffer& il) -> ByteBuffer {
        ByteBuffer out;
        out.push_back(static_cast<u8>((il.size() << 2) | 0x02));
        out.insert(out.end(), il.begin(), il.end());
        return out;
    };
    auto emitU32 = [](ByteBuffer& b, u32 v) {
        b.push_back(static_cast<u8>(v & 0xFF));
        b.push_back(static_cast<u8>((v >> 8) & 0xFF));
        b.push_back(static_cast<u8>((v >> 16) & 0xFF));
        b.push_back(static_cast<u8>((v >> 24) & 0xFF));
    };

    // Method 1: PlayerController.Start()
    //   this.health = this.maxHealth; Debug.Log("CoconutDemo PlayerController initialized!");
    ByteBuffer ilStart;
    ilStart.push_back(0x02); // ldarg.0
    ilStart.push_back(0x02); // ldarg.0
    ilStart.push_back(0x7B); emitU32(ilStart, 0x04000003u); // ldfld maxHealth
    ilStart.push_back(0x7D); emitU32(ilStart, 0x04000002u); // stfld health
    ilStart.push_back(0x72); emitU32(ilStart, usStarted);   // ldstr
    ilStart.push_back(0x28); emitU32(ilStart, 0x0A000001u); // call Debug.Log
    ilStart.push_back(0x2A); // ret

    // Method 2: PlayerController.Update()
    //   if (this.health <= 0) Debug.Log("Player defeated...");
    ByteBuffer ilUpdate;
    ilUpdate.push_back(0x02); // ldarg.0
    ilUpdate.push_back(0x7B); emitU32(ilUpdate, 0x04000002u); // ldfld health
    ilUpdate.push_back(0x16); // ldc.i4.0
    ilUpdate.push_back(0x30); ilUpdate.push_back(0x0A); // bgt.s +10 (to ret)
    ilUpdate.push_back(0x72); emitU32(ilUpdate, usDefeated);
    ilUpdate.push_back(0x28); emitU32(ilUpdate, 0x0A000001u);
    ilUpdate.push_back(0x2A); // ret

    // Method 3: PlayerController.TakeDamage(int amount)
    //   this.health = this.health - amount;
    ByteBuffer ilTakeDamage;
    ilTakeDamage.push_back(0x02); // ldarg.0
    ilTakeDamage.push_back(0x02); // ldarg.0
    ilTakeDamage.push_back(0x7B); emitU32(ilTakeDamage, 0x04000002u); // ldfld health
    ilTakeDamage.push_back(0x03); // ldarg.1 (amount)
    ilTakeDamage.push_back(0x59); // sub
    ilTakeDamage.push_back(0x7D); emitU32(ilTakeDamage, 0x04000002u); // stfld health
    ilTakeDamage.push_back(0x2A); // ret

    // Method 4: PlayerController.Heal(int amount)
    //   this.health = this.health + amount;
    ByteBuffer ilHeal;
    ilHeal.push_back(0x02); // ldarg.0
    ilHeal.push_back(0x02); // ldarg.0
    ilHeal.push_back(0x7B); emitU32(ilHeal, 0x04000002u); // ldfld health
    ilHeal.push_back(0x03); // ldarg.1
    ilHeal.push_back(0x58); // add
    ilHeal.push_back(0x7D); emitU32(ilHeal, 0x04000002u); // stfld health
    ilHeal.push_back(0x2A); // ret

    // Method 5: EnemySentinel.Patrol()
    //   this.isAlerted = true; Debug.Log("Sentinel scanning perimeter...");
    ByteBuffer ilPatrol;
    ilPatrol.push_back(0x02); // ldarg.0
    ilPatrol.push_back(0x17); // ldc.i4.1
    ilPatrol.push_back(0x7D); emitU32(ilPatrol, 0x04000004u); // stfld isAlerted
    ilPatrol.push_back(0x72); emitU32(ilPatrol, usPatrolMsg);
    ilPatrol.push_back(0x28); emitU32(ilPatrol, 0x0A000001u);
    ilPatrol.push_back(0x2A); // ret

    // Method 6: ScoreCalculator.ComputeBonus(int baseScore, int comboMultiplier)
    //   return (baseScore * comboMultiplier) + 100;
    ByteBuffer ilScore;
    ilScore.push_back(0x02); // ldarg.0
    ilScore.push_back(0x03); // ldarg.1
    ilScore.push_back(0x5A); // mul
    ilScore.push_back(0x1F); ilScore.push_back(100); // ldc.i4.s 100
    ilScore.push_back(0x58); // add
    ilScore.push_back(0x2A); // ret

    std::vector<ByteBuffer> methodBlobs = {
        makeTinyMethod(ilStart),
        makeTinyMethod(ilUpdate),
        makeTinyMethod(ilTakeDamage),
        makeTinyMethod(ilHeal),
        makeTinyMethod(ilPatrol),
        makeTinyMethod(ilScore)
    };

    constexpr u32 kMethodsOffsetInText = 0x50;
    ByteBuffer methodsBuf;
    std::vector<u32> methodRvas;
    for (const auto& mb : methodBlobs) {
        while (methodsBuf.size() % 4 != 0) methodsBuf.push_back(0);
        methodRvas.push_back(kTextRva + kMethodsOffsetInText + static_cast<u32>(methodsBuf.size()));
        methodsBuf.insert(methodsBuf.end(), mb.begin(), mb.end());
    }

    // 6. Build #~ Metadata Tables Stream
    // Tables present:
    //   0x00 Module (1)
    //   0x01 TypeRef (3: System.Object, UnityEngine.MonoBehaviour, UnityEngine.Debug)
    //   0x02 TypeDef (4: <Module>, PlayerController, EnemySentinel, ScoreCalculator)
    //   0x04 Field (5)
    //   0x06 MethodDef (6)
    //   0x08 Param (4)
    //   0x0A MemberRef (1: Debug.Log)
    //   0x20 Assembly (1)
    //   0x23 AssemblyRef (2: mscorlib, UnityEngine.CoreModule)
    BinaryWriter tilde(Endian::Little);
    tilde.WriteU32(0); // Reserved
    tilde.WriteU8(2);  // MajorVersion
    tilde.WriteU8(0);  // MinorVersion
    tilde.WriteU8(0);  // HeapSizes (all 2-byte indices!)
    tilde.WriteU8(1);  // Reserved
    u64 validMask = (1ULL << 0x00) | (1ULL << 0x01) | (1ULL << 0x02) | (1ULL << 0x04) |
                    (1ULL << 0x06) | (1ULL << 0x08) | (1ULL << 0x0A) | (1ULL << 0x20) | (1ULL << 0x23);
    tilde.WriteU64(validMask);
    tilde.WriteU64(0); // Sorted

    // Row counts in ascending table ID order
    tilde.WriteU32(1); // 0x00 Module
    tilde.WriteU32(3); // 0x01 TypeRef
    tilde.WriteU32(4); // 0x02 TypeDef
    tilde.WriteU32(5); // 0x04 Field
    tilde.WriteU32(6); // 0x06 MethodDef
    tilde.WriteU32(4); // 0x08 Param
    tilde.WriteU32(1); // 0x0A MemberRef
    tilde.WriteU32(1); // 0x20 Assembly
    tilde.WriteU32(2); // 0x23 AssemblyRef

    // 0x00 Module: Generation(2), Name(2), Mvid(2), EncId(2), EncBaseId(2)
    tilde.WriteU16(0); tilde.WriteU16(sModule); tilde.WriteU16(1); tilde.WriteU16(0); tilde.WriteU16(0);

    // 0x01 TypeRef: ResolutionScope(2: AssemblyRef tag=3 -> (idx<<2)|3), TypeName(2), TypeNamespace(2)
    tilde.WriteU16((1 << 2) | 3); tilde.WriteU16(sObject);  tilde.WriteU16(sSysNs);
    tilde.WriteU16((2 << 2) | 3); tilde.WriteU16(sMonoBeh); tilde.WriteU16(sUnityNs);
    tilde.WriteU16((2 << 2) | 3); tilde.WriteU16(sDebug);   tilde.WriteU16(sUnityNs);

    // 0x02 TypeDef: Flags(4), TypeName(2), TypeNamespace(2), Extends(2: TypeRef tag=1 -> (idx<<2)|1), FieldList(2), MethodList(2)
    // Row 1: <Module>
    tilde.WriteU32(0x00000000); tilde.WriteU16(sModType);    tilde.WriteU16(0);       tilde.WriteU16(0);             tilde.WriteU16(1); tilde.WriteU16(1);
    // Row 2: CoconutDemo.PlayerController : MonoBehaviour (fields 1..3, methods 1..4)
    tilde.WriteU32(0x00100001); tilde.WriteU16(sPlayerCtrl); tilde.WriteU16(sDemoNs); tilde.WriteU16((2 << 2) | 1); tilde.WriteU16(1); tilde.WriteU16(1);
    // Row 3: CoconutDemo.EnemySentinel : MonoBehaviour (fields 4..5, method 5)
    tilde.WriteU32(0x00100001); tilde.WriteU16(sEnemySent);  tilde.WriteU16(sDemoNs); tilde.WriteU16((2 << 2) | 1); tilde.WriteU16(4); tilde.WriteU16(5);
    // Row 4: CoconutDemo.ScoreCalculator : System.Object (no fields, method 6)
    tilde.WriteU32(0x00100001); tilde.WriteU16(sScoreCalc);  tilde.WriteU16(sDemoNs); tilde.WriteU16((1 << 2) | 1); tilde.WriteU16(6); tilde.WriteU16(6);

    // 0x04 Field: Flags(2), Name(2), Signature(2)
    tilde.WriteU16(0x0006); tilde.WriteU16(sMoveSpeed); tilde.WriteU16(sigFieldFloat); // public float moveSpeed
    tilde.WriteU16(0x0006); tilde.WriteU16(sHealth);    tilde.WriteU16(sigFieldInt);   // public int health
    tilde.WriteU16(0x0006); tilde.WriteU16(sMaxHealth); tilde.WriteU16(sigFieldInt);   // public int maxHealth
    tilde.WriteU16(0x0001); tilde.WriteU16(sIsAlerted); tilde.WriteU16(sigFieldBool);  // private bool isAlerted
    tilde.WriteU16(0x0006); tilde.WriteU16(sPatrolRad); tilde.WriteU16(sigFieldFloat); // public float patrolRadius

    // 0x06 MethodDef: RVA(4), ImplFlags(2), Flags(2), Name(2), Signature(2), ParamList(2)
    tilde.WriteU32(methodRvas[0]); tilde.WriteU16(0); tilde.WriteU16(0x0001); tilde.WriteU16(sStart);        tilde.WriteU16(sigInstVoid0);  tilde.WriteU16(1);
    tilde.WriteU32(methodRvas[1]); tilde.WriteU16(0); tilde.WriteU16(0x0001); tilde.WriteU16(sUpdate);       tilde.WriteU16(sigInstVoid0);  tilde.WriteU16(1);
    tilde.WriteU32(methodRvas[2]); tilde.WriteU16(0); tilde.WriteU16(0x0006); tilde.WriteU16(sTakeDamage);   tilde.WriteU16(sigInstVoidI4); tilde.WriteU16(1);
    tilde.WriteU32(methodRvas[3]); tilde.WriteU16(0); tilde.WriteU16(0x0006); tilde.WriteU16(sHeal);         tilde.WriteU16(sigInstVoidI4); tilde.WriteU16(2);
    tilde.WriteU32(methodRvas[4]); tilde.WriteU16(0); tilde.WriteU16(0x0006); tilde.WriteU16(sPatrol);       tilde.WriteU16(sigInstVoid0);  tilde.WriteU16(3);
    tilde.WriteU32(methodRvas[5]); tilde.WriteU16(0); tilde.WriteU16(0x0016); tilde.WriteU16(sComputeBonus); tilde.WriteU16(sigStatI4I4I4); tilde.WriteU16(3);

    // 0x08 Param: Flags(2), Sequence(2), Name(2)
    tilde.WriteU16(0); tilde.WriteU16(1); tilde.WriteU16(sParamAmount);
    tilde.WriteU16(0); tilde.WriteU16(1); tilde.WriteU16(sParamAmount);
    tilde.WriteU16(0); tilde.WriteU16(1); tilde.WriteU16(sParamBase);
    tilde.WriteU16(0); tilde.WriteU16(2); tilde.WriteU16(sParamCombo);

    // 0x0A MemberRef: Class(2: TypeRef tag=1 -> (3<<2)|1 = Debug), Name(2), Signature(2)
    tilde.WriteU16((3 << 2) | 1); tilde.WriteU16(sLog); tilde.WriteU16(sigStatVoidStr);

    // 0x20 Assembly: HashAlgId(4), Ver(8), Flags(4), PublicKey(2), Name(2), Culture(2)
    tilde.WriteU32(0x8004); tilde.WriteU16(1); tilde.WriteU16(0); tilde.WriteU16(0); tilde.WriteU16(0);
    tilde.WriteU32(0); tilde.WriteU16(0); tilde.WriteU16(sAsmName); tilde.WriteU16(0);

    // 0x23 AssemblyRef: Ver(8), Flags(4), PublicKeyOrToken(2), Name(2), Culture(2), HashValue(2)
    tilde.WriteU16(4); tilde.WriteU16(0); tilde.WriteU16(0); tilde.WriteU16(0);
    tilde.WriteU32(0); tilde.WriteU16(0); tilde.WriteU16(sMscorlib); tilde.WriteU16(0); tilde.WriteU16(0);
    tilde.WriteU16(0); tilde.WriteU16(0); tilde.WriteU16(0); tilde.WriteU16(0);
    tilde.WriteU32(0); tilde.WriteU16(0); tilde.WriteU16(sUnityEngine); tilde.WriteU16(0); tilde.WriteU16(0);

    tilde.Align(4);
    const ByteBuffer& tildeBytes = tilde.Buffer();

    // 7. Assemble BSJB CLI Metadata Root
    BinaryWriter meta(Endian::Little);
    meta.WriteU32(0x424A5342u); // "BSJB"
    meta.WriteU16(1); meta.WriteU16(1); meta.WriteU32(0);
    std::string verStr = "v4.0.30319";
    meta.WriteU32(12); // padded version length
    meta.WriteFixedString(verStr);
    meta.WriteZeros(12 - verStr.size());
    meta.WriteU16(0); // Flags
    meta.WriteU16(5); // 5 Streams: #~, #Strings, #US, #GUID, #Blob

    struct StreamDesc { std::string name; ByteSpan data; };
    std::vector<StreamDesc> streams = {
        {"#~",       tildeBytes},
        {"#Strings", ByteSpan(reinterpret_cast<const u8*>(strHeap.data()), strHeap.size())},
        {"#US",      usHeap},
        {"#GUID",    guidHeap},
        {"#Blob",    blobHeap}
    };

    // Compute exact header size so stream offsets are relative to BSJB start
    size_t headersSize = meta.Size();
    for (const auto& s : streams) {
        size_t namePadded = ((s.name.size() + 1) + 3) & ~size_t(3);
        headersSize += 8 + namePadded;
    }

    size_t streamCursor = headersSize;
    for (const auto& s : streams) {
        meta.WriteU32(static_cast<u32>(streamCursor));
        meta.WriteU32(static_cast<u32>(s.data.size()));
        meta.WriteCString(s.name);
        meta.Align(4);
        streamCursor += s.data.size();
    }
    for (const auto& s : streams) {
        meta.WriteBytes(s.data);
    }
    const ByteBuffer& metaBytes = meta.Buffer();

    // 8. Build .text section (COR20 header + CIL method bodies + Metadata Root)
    BinaryWriter textSec(Endian::Little);
    constexpr u32 kMetaOffsetInText = 0x180;
    // IMAGE_COR20_HEADER (72 bytes)
    textSec.WriteU32(72); // cb
    textSec.WriteU16(2);  // MajorRuntimeVersion
    textSec.WriteU16(5);  // MinorRuntimeVersion
    textSec.WriteU32(kTextRva + kMetaOffsetInText); // MetaData.VirtualAddress
    textSec.WriteU32(static_cast<u32>(metaBytes.size())); // MetaData.Size
    textSec.WriteU32(1);  // Flags (COMIMAGE_FLAGS_ILONLY)
    textSec.WriteU32(0);  // EntryPointToken
    textSec.WriteZeros(72 - 24);
    textSec.WriteZeros(kMethodsOffsetInText - textSec.Size());
    textSec.WriteBytes(methodsBuf);
    if (textSec.Size() < kMetaOffsetInText) {
        textSec.WriteZeros(kMetaOffsetInText - textSec.Size());
    }
    textSec.WriteBytes(metaBytes);
    textSec.Align(0x200);
    const ByteBuffer& textBytes = textSec.Buffer();

    // 9. Build PE32 Outer Binary
    BinaryWriter pe(Endian::Little);
    // DOS Header (128 bytes, e_lfanew = 0x80)
    pe.WriteFixedString("MZ");
    pe.WriteZeros(0x3C - 2);
    pe.WriteU32(0x80);
    pe.WriteZeros(0x80 - pe.Size());

    // PE Signature + COFF File Header
    pe.WriteFixedString("PE\0\0");
    pe.WriteU16(0x014C); // Machine: I386
    pe.WriteU16(1);      // NumberOfSections
    pe.WriteU32(0);      // TimeDateStamp
    pe.WriteU32(0);      // PointerToSymbolTable
    pe.WriteU32(0);      // NumberOfSymbols
    pe.WriteU16(224);    // SizeOfOptionalHeader (PE32)
    pe.WriteU16(0x2102); // Characteristics: DLL | EXECUTABLE_IMAGE | 32BIT

    // PE32 Optional Header (224 bytes)
    pe.WriteU16(0x010B); // Magic PE32
    pe.WriteU8(14); pe.WriteU8(0);
    pe.WriteU32(static_cast<u32>(textBytes.size())); // SizeOfCode
    pe.WriteU32(0); pe.WriteU32(0);
    pe.WriteU32(0);        // AddressOfEntryPoint
    pe.WriteU32(kTextRva); // BaseOfCode
    pe.WriteU32(0x4000);   // BaseOfData
    pe.WriteU32(0x10000000u); // ImageBase
    pe.WriteU32(0x2000);   // SectionAlignment
    pe.WriteU32(0x0200);   // FileAlignment
    pe.WriteU16(4); pe.WriteU16(0); pe.WriteU16(0); pe.WriteU16(0);
    pe.WriteU16(4); pe.WriteU16(0); pe.WriteU32(0);
    pe.WriteU32(kTextRva + static_cast<u32>(((textBytes.size() + 0x1FFF) / 0x2000) * 0x2000)); // SizeOfImage
    pe.WriteU32(kTextRaw); // SizeOfHeaders
    pe.WriteU32(0);
    pe.WriteU16(3); // Subsystem: CUI
    pe.WriteU16(0);
    pe.WriteU32(0x100000); pe.WriteU32(0x1000); pe.WriteU32(0x100000); pe.WriteU32(0x1000);
    pe.WriteU32(0);
    pe.WriteU32(16); // NumberOfRvaAndSizes

    // 16 Data Directories (entry #14 = CLI Header!)
    for (int d = 0; d < 16; ++d) {
        if (d == 14) {
            pe.WriteU32(kTextRva);
            pe.WriteU32(72);
        } else {
            pe.WriteU32(0);
            pe.WriteU32(0);
        }
    }

    // Section Header: .text (40 bytes)
    pe.WriteFixedString(".text\0\0\0");
    pe.WriteU32(static_cast<u32>(textBytes.size())); // VirtualSize
    pe.WriteU32(kTextRva);                           // VirtualAddress
    pe.WriteU32(static_cast<u32>(textBytes.size())); // SizeOfRawData
    pe.WriteU32(kTextRaw);                           // PointerToRawData
    pe.WriteZeros(12);
    pe.WriteU32(0x60000020u); // CODE | EXECUTE | READ

    if (pe.Size() < kTextRaw) pe.WriteZeros(kTextRaw - pe.Size());
    pe.WriteBytes(textBytes);
    return pe.TakeBuffer();
}

// Builds a real IL2CPP global-metadata.dat (v29) + ARM64 ELF64 libil2cpp.so pair!
std::pair<ByteBuffer, ByteBuffer> SampleGameGenerator::BuildSampleIl2CppPair() {
    // 1. Strings & String Literals
    std::string strTable;
    auto addStr = [&](const std::string& s) -> i32 {
        i32 off = static_cast<i32>(strTable.size());
        strTable.append(s);
        strTable.push_back('\0');
        return off;
    };

    i32 sImgName    = addStr("Assembly-CSharp.dll");
    i32 sNsDemo     = addStr("CoconutMobile");
    i32 sTouchCtrl  = addStr("TouchPlayerController");
    i32 sBossAI     = addStr("BossGuardianAI");
    i32 sFieldHP    = addStr("currentHealth");
    i32 sFieldDash  = addStr("dashCooldown");
    i32 sMethTick   = addStr("FixedUpdate");
    i32 sMethDamage = addStr("ApplyHitDamage");
    i32 sMethAttack = addStr("ExecuteBossAttack");

    std::vector<std::string> literals = {
        "CoconutMobile::TouchPlayerController ready",
        "BossGuardianAI phase 2 activated!",
        "CRITICAL_HIT_MULTIPLIER"
    };
    BinaryWriter litInfo(Endian::Little);
    std::string litData;
    for (const auto& lit : literals) {
        litInfo.WriteU32(static_cast<u32>(lit.size()));
        litInfo.WriteI32(static_cast<i32>(litData.size()));
        litData.append(lit);
    }

    // 2. Tables (minimal v29 struct sizes matching Il2CppMetadata parser)
    // ImageDefinition (40 bytes): nameIndex(4), assemblyIndex(4), typeStart(4), typeCount(4), ...
    BinaryWriter images(Endian::Little);
    images.WriteI32(sImgName); images.WriteI32(0); images.WriteI32(0); images.WriteU32(2);
    images.WriteZeros(40 - 16);

    // TypeDefinition (88 bytes): nameIndex(4), namespaceIndex(4), byvalTypeIndex(4), declaringTypeIndex(4),
    //   parentIndex(4), elementTypeIndex(4), genericContainerIndex(4), flags(4), fieldStart(4), methodStart(4),
    //   eventStart(4), propertyStart(4), nestedTypesStart(4), interfacesStart(4), vtableStart(4), interfaceOffsetsStart(4),
    //   method_count(2), property_count(2), field_count(2), event_count(2), nested_type_count(2), vtable_count(2),
    //   interfaces_count(2), interface_offsets_count(2), bitfield(4), token(4)
    auto writeTypeDef = [](BinaryWriter& w, i32 nameIdx, i32 nsIdx, i32 fieldStart, i32 methodStart, u16 mCount, u16 fCount, u32 tok) {
        w.WriteI32(nameIdx);
        w.WriteI32(nsIdx);
        w.WriteI32(0); w.WriteI32(-1); w.WriteI32(-1); w.WriteI32(0); w.WriteI32(-1);
        w.WriteU32(0x00100001u); // Public class
        w.WriteI32(fieldStart);
        w.WriteI32(methodStart);
        w.WriteI32(0); w.WriteI32(0); w.WriteI32(0); w.WriteI32(0); w.WriteI32(0); w.WriteI32(0);
        w.WriteU16(mCount); w.WriteU16(0); w.WriteU16(fCount); w.WriteU16(0);
        w.WriteU16(0); w.WriteU16(0); w.WriteU16(0); w.WriteU16(0);
        w.WriteU32(0);
        w.WriteU32(tok);
    };
    BinaryWriter types(Endian::Little);
    writeTypeDef(types, sTouchCtrl, sNsDemo, 0, 0, 2, 2, 0x02000002u);
    writeTypeDef(types, sBossAI,    sNsDemo, 2, 2, 1, 0, 0x02000003u);

    // FieldDefinition (12 bytes): nameIndex(4), typeIndex(4), token(4)
    BinaryWriter fields(Endian::Little);
    fields.WriteI32(sFieldHP);   fields.WriteI32(0); fields.WriteU32(0x04000001u);
    fields.WriteI32(sFieldDash); fields.WriteI32(1); fields.WriteU32(0x04000002u);

    // MethodDefinition (32 bytes): nameIndex(4), declaringType(4), returnType(4), parameterStart(4),
    //   genericContainerIndex(4), token(4), flags(2), iflags(2), slot(2), parameterCount(2)
    auto writeMethodDef = [](BinaryWriter& w, i32 nameIdx, i32 declType, u32 tok, u16 paramCount) {
        w.WriteI32(nameIdx);
        w.WriteI32(declType);
        w.WriteI32(0);
        w.WriteI32(0);
        w.WriteI32(-1);
        w.WriteU32(tok);
        w.WriteU16(0x0006); // Public
        w.WriteU16(0);
        w.WriteU16(0xFFFF);
        w.WriteU16(paramCount);
    };
    BinaryWriter methods(Endian::Little);
    writeMethodDef(methods, sMethTick,   0, 0x06000001u, 0);
    writeMethodDef(methods, sMethDamage, 0, 0x06000002u, 1);
    writeMethodDef(methods, sMethAttack, 1, 0x06000003u, 0);

    // 3. Assemble global-metadata.dat Header (v29, 256 bytes header table)
    BinaryWriter md(Endian::Little);
    md.WriteU32(0xFAB11BAFu); // Sanity magic
    md.WriteI32(29);          // Version 29 (Unity 2021.3 / 2022.3 / Unity 6)

    // Reserve 256-byte header slots
    size_t headerSize = 256;
    md.WriteZeros(headerSize - md.Size());

    auto appendSection = [&](size_t headerOffsetField, ByteSpan data) {
        md.Align(4);
        u32 off = static_cast<u32>(md.Size());
        u32 sz  = static_cast<u32>(data.size());
        md.PatchU32(headerOffsetField, off);
        md.PatchU32(headerOffsetField + 4, sz);
        md.WriteBytes(data);
    };

    // Standard v29 header field offsets:
    //   8:  stringLiteralOffset / Count
    //   16: stringLiteralDataOffset / Count
    //   24: stringOffset / Count
    //   48: methodsOffset / Count
    //   112: fieldsOffset / Count
    //   160: typeDefinitionsOffset / Count
    //   168: imagesOffset / Count
    appendSection(8,   litInfo.Buffer());
    appendSection(16,  ByteSpan(reinterpret_cast<const u8*>(litData.data()), litData.size()));
    appendSection(24,  ByteSpan(reinterpret_cast<const u8*>(strTable.data()), strTable.size()));
    appendSection(48,  methods.Buffer());
    appendSection(112, fields.Buffer());
    appendSection(160, types.Buffer());
    appendSection(168, images.Buffer());

    // 4. Build valid minimal ARM64 ELF64 (libil2cpp.so) with real AArch64 function code!
    BinaryWriter elf(Endian::Little);
    constexpr u64 kCodeOff = 0x1000;
    constexpr u64 kCodeVaddr = 0x1000;
    // Real AArch64 instructions (little-endian 32-bit words):
    //   stp x29, x30, [sp, #-16]!  = 0xA9BF7BFD
    //   mov x29, sp                = 0x910003FD
    //   ldr w8, [x0, #0x18]        = 0xB9401808
    //   sub w8, w8, w1             = 0x4B010108
    //   str w8, [x0, #0x18]        = 0xB9001808
    //   ldp x29, x30, [sp], #16    = 0xA8C17BFD
    //   ret                        = 0xD65F03C0
    std::vector<u32> arm64Code = {
        0xA9BF7BFDu, 0x910003FDu, 0xB9401808u, 0x4B010108u,
        0xB9001808u, 0xA8C17BFDu, 0xD65F03C0u, 0xD503201Fu,
        // Second function:
        0xA9BF7BFDu, 0x52800C80u, 0xA8C17BFDu, 0xD65F03C0u
    };

    // ELF64 Header (64 bytes)
    elf.WriteBytes(std::array<u8, 16>{0x7F, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    elf.WriteU16(3);   // ET_DYN
    elf.WriteU16(183); // EM_AARCH64
    elf.WriteU32(1);   // EV_CURRENT
    elf.WriteU64(kCodeVaddr);
    elf.WriteU64(64);     // e_phoff
    elf.WriteU64(0x1100); // e_shoff
    elf.WriteU32(0);
    elf.WriteU16(64); // e_ehsize
    elf.WriteU16(56); // e_phentsize
    elf.WriteU16(1);  // e_phnum
    elf.WriteU16(64); // e_shentsize
    elf.WriteU16(3);  // e_shnum (NULL, .text, .shstrtab)
    elf.WriteU16(2);  // e_shstrndx

    // Program Header (56 bytes)
    elf.WriteU32(1); // PT_LOAD
    elf.WriteU32(5); // PF_R | PF_X
    elf.WriteU64(kCodeOff);
    elf.WriteU64(kCodeVaddr);
    elf.WriteU64(kCodeVaddr);
    elf.WriteU64(arm64Code.size() * 4);
    elf.WriteU64(arm64Code.size() * 4);
    elf.WriteU64(0x1000);

    elf.WriteZeros(static_cast<size_t>(kCodeOff - elf.Size()));
    for (u32 insn : arm64Code) elf.WriteU32(insn);

    std::string shstrtab = std::string("\0.text\0.shstrtab\0", 17);
    u64 shstrOff = elf.Size();
    elf.WriteBytes(ByteSpan(reinterpret_cast<const u8*>(shstrtab.data()), shstrtab.size()));

    if (elf.Size() < 0x1100) elf.WriteZeros(0x1100 - elf.Size());
    // Section 0: NULL
    elf.WriteZeros(64);
    // Section 1: .text
    elf.WriteU32(1); elf.WriteU32(1 /*SHT_PROGBITS*/); elf.WriteU64(6 /*ALLOC|EXEC*/);
    elf.WriteU64(kCodeVaddr); elf.WriteU64(kCodeOff); elf.WriteU64(arm64Code.size() * 4);
    elf.WriteU32(0); elf.WriteU32(0); elf.WriteU64(4); elf.WriteU64(0);
    // Section 2: .shstrtab
    elf.WriteU32(7); elf.WriteU32(3 /*SHT_STRTAB*/); elf.WriteU64(0);
    elf.WriteU64(0); elf.WriteU64(shstrOff); elf.WriteU64(shstrtab.size());
    elf.WriteU32(0); elf.WriteU32(0); elf.WriteU64(1); elf.WriteU64(0);

    return {md.TakeBuffer(), elf.TakeBuffer()};
}

Result<std::filesystem::path> SampleGameGenerator::GenerateMonoDemoGame(const std::filesystem::path& outDir) {
    std::filesystem::path dataDir = outDir / "CoconutDemo_Data";
    std::filesystem::path managedDir = dataDir / "Managed";
    std::error_code ec;
    std::filesystem::create_directories(managedDir, ec);

    // 1. Write SerializedFile v22 (level0)
    SerializedFile sf = BuildDemoSceneSerializedFile(TargetPlatform::StandaloneWindows64);
    auto sfBytes = sf.Serialize();
    if (!sfBytes) return Result<std::filesystem::path>::Err(sfBytes.Error());
    auto w1 = platform::WriteFileBytes(dataDir / "level0", sfBytes.Value());
    if (!w1) return Result<std::filesystem::path>::Err(w1.Error());

    // 2. Also package an extra UnityFS AssetBundle (LZ4HC compressed) with a bonus TextAsset
    SerializedFile extraSf;
    extraSf.name = "CAB-coconut-extra";
    extraSf.header.version = 22;
    extraSf.unityVersionString = "2022.3.15f1";
    extraSf.targetPlatform = TargetPlatform::StandaloneWindows64;
    extraSf.types = {MakeTextAssetType()};
    {
        ObjectInfo o;
        o.pathId = 9001;
        o.typeId = 0;
        o.classId = static_cast<i32>(ClassID::TextAsset);
        o.rawData = SerializeTextAsset("BundleManifest", "Coconut UnityFS LZ4HC AssetBundle verified!");
        o.byteSize = static_cast<u32>(o.rawData.size());
        extraSf.objects.push_back(std::move(o));
    }
    auto extraBytes = extraSf.Serialize();
    if (!extraBytes) return Result<std::filesystem::path>::Err(extraBytes.Error());

    UnityFSBundle bundle;
    bundle.header.signature = "UnityFS";
    bundle.header.formatVersion = 6;
    bundle.header.unityWebVersion = "5.x.x";
    bundle.header.unityRevision = "2022.3.15f1";
    bundle.nodes.push_back({0, extraBytes.Value().size(), 4, "CAB-coconut-extra", extraBytes.Value()});
    auto bundleBytes = bundle.Serialize(CompressionType::LZ4HC);
    if (!bundleBytes) return Result<std::filesystem::path>::Err(bundleBytes.Error());
    auto w2 = platform::WriteFileBytes(dataDir / "extra_assets.bundle", bundleBytes.Value());
    if (!w2) return Result<std::filesystem::path>::Err(w2.Error());

    // 3. Write real ECMA-335 PE/CLI Assembly-CSharp.dll
    auto dllBytes = BuildSampleManagedAssemblyDll();
    auto w3 = platform::WriteFileBytes(managedDir / "Assembly-CSharp.dll", dllBytes);
    if (!w3) return Result<std::filesystem::path>::Err(w3.Error());

    COCO_LOG_INFO("DemoGen", "Generated Mono Unity demo game at " + outDir.string());
    return Result<std::filesystem::path>::Ok(outDir);
}

Result<std::filesystem::path> SampleGameGenerator::GenerateIl2CppDemoApk(const std::filesystem::path& outApkPath) {
    SerializedFile sf = BuildDemoSceneSerializedFile(TargetPlatform::Android);
    auto sfBytes = sf.Serialize();
    if (!sfBytes) return Result<std::filesystem::path>::Err(sfBytes.Error());

    auto [metaBytes, elfBytes] = BuildSampleIl2CppPair();

    auto zipRes = ZipArchive::WriteZipFile(outApkPath, {
        {"assets/bin/Data/level0", sfBytes.Value()},
        {"assets/bin/Data/Managed/Metadata/global-metadata.dat", metaBytes},
        {"lib/arm64-v8a/libil2cpp.so", elfBytes}
    });
    if (!zipRes) return Result<std::filesystem::path>::Err(zipRes.Error());

    COCO_LOG_INFO("DemoGen", "Generated Android IL2CPP demo APK at " + outApkPath.string());
    return Result<std::filesystem::path>::Ok(outApkPath);
}

} // namespace coconut::unity
