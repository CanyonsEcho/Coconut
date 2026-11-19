#include "unity/TypeTree.h"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace coconut::unity {

namespace {

// Standard Unity CommonString table offsets (high bit 0x80000000 selects CommonString)
const std::unordered_map<u32, const char*>& CommonStrings() {
    static const std::unordered_map<u32, const char*> kMap = {
        {0, "AABB"}, {5, "AnimationClip"}, {19, "AnimationCurve"}, {34, "AnimationState"},
        {49, "Array"}, {55, "Base"}, {60, "BitField"}, {69, "bitset"}, {76, "bool"},
        {81, "char"}, {86, "ColorRGBA"}, {96, "Component"}, {106, "data"}, {111, "deque"},
        {117, "double"}, {124, "dynamic_array"}, {138, "FastPropertyName"}, {155, "first"},
        {161, "float"}, {167, "Font"}, {172, "GameObject"}, {183, "Generic Mono"},
        {196, "GradientNEW"}, {208, "GUID"}, {213, "GUIStyle"}, {222, "int"}, {226, "list"},
        {231, "long long"}, {241, "map"}, {245, "Matrix4x4f"}, {256, "MdFour"},
        {263, "MonoBehaviour"}, {277, "MonoScript"}, {288, "m_ByteSize"}, {299, "m_Curve"},
        {307, "m_EditorClassIdentifier"}, {331, "m_EditorHideFlags"}, {349, "m_Enabled"},
        {359, "m_ExtensionPtr"}, {374, "m_GameObject"}, {387, "m_Index"}, {395, "m_IsArray"},
        {405, "m_IsStatic"}, {416, "m_MetaFlag"}, {427, "m_Name"}, {434, "m_ObjectHideFlags"},
        {452, "m_PrefabInternal"}, {469, "m_PrefabParentObject"}, {490, "m_Script"},
        {499, "m_StaticEditorFlags"}, {519, "m_Type"}, {526, "m_Version"}, {536, "Object"},
        {543, "pair"}, {548, "PPtr<Component>"}, {564, "PPtr<GameObject>"},
        {581, "PPtr<Material>"}, {596, "PPtr<MonoBehaviour>"}, {616, "PPtr<MonoScript>"},
        {633, "PPtr<Object>"}, {646, "PPtr<Prefab>"}, {659, "PPtr<Sprite>"},
        {672, "PPtr<TextAsset>"}, {688, "PPtr<Texture>"}, {702, "PPtr<Texture2D>"},
        {718, "PPtr<Transform>"}, {734, "Prefab"}, {741, "Quaternionf"}, {753, "Rectf"},
        {759, "RectInt"}, {767, "RectOffset"}, {778, "second"}, {785, "set"}, {789, "short"},
        {795, "size"}, {800, "SInt16"}, {807, "SInt32"}, {814, "SInt64"}, {821, "SInt8"},
        {827, "staticvector"}, {840, "string"}, {847, "TextAsset"}, {857, "TextMesh"},
        {866, "Texture"}, {874, "Texture2D"}, {884, "Transform"}, {894, "TypelessData"},
        {907, "UInt16"}, {914, "UInt32"}, {921, "UInt64"}, {928, "UInt8"},
        {934, "unsigned int"}, {947, "unsigned long long"}, {966, "unsigned short"},
        {981, "vector"}, {988, "Vector2f"}, {997, "Vector3f"}, {1006, "Vector4f"},
        {1015, "m_ScriptingClassIdentifier"}, {1042, "Gradient"}, {1051, "Type*"},
        {1057, "int2_storage"}, {1070, "int3_storage"}, {1083, "BoundsInt"},
        {1093, "m_CorrespondingSourceObject"}, {1121, "m_PrefabInstance"},
        {1138, "m_PrefabAsset"}, {1152, "FileSize"}, {1161, "Hash128"}
    };
    return kMap;
}

struct TreeNodeIndex {
    size_t nodeIdx = 0;
    std::vector<TreeNodeIndex> children;
};

TreeNodeIndex BuildHierarchy(const std::vector<TypeTreeNode>& nodes) {
    TreeNodeIndex root;
    if (nodes.empty()) return root;
    root.nodeIdx = 0;

    std::vector<TreeNodeIndex*> stack;
    stack.push_back(&root);

    for (size_t i = 1; i < nodes.size(); ++i) {
        u8 lvl = nodes[i].level;
        while (stack.size() > static_cast<size_t>(lvl)) {
            stack.pop_back();
        }
        if (stack.empty()) {
            stack.push_back(&root);
        }
        stack.back()->children.push_back(TreeNodeIndex{i, {}});
        stack.push_back(&stack.back()->children.back());
    }
    return root;
}

TypeValue ReadNodeValue(BinaryReader& r, const std::vector<TypeTreeNode>& nodes, const TreeNodeIndex& idx) {
    const auto& node = nodes[idx.nodeIdx];
    TypeValue val;
    val.type = node.type;
    val.name = node.name;
    val.align = node.IsAlign();

    const std::string& t = node.type;
    bool alignAfter = node.IsAlign();

    if (t == "SInt8" || t == "char") {
        val.value = static_cast<i64>(r.ReadI8());
    } else if (t == "UInt8") {
        val.value = static_cast<u64>(r.ReadU8());
    } else if (t == "SInt16" || t == "short") {
        val.value = static_cast<i64>(r.ReadI16());
    } else if (t == "UInt16" || t == "unsigned short") {
        val.value = static_cast<u64>(r.ReadU16());
    } else if (t == "SInt32" || t == "int") {
        val.value = static_cast<i64>(r.ReadI32());
    } else if (t == "UInt32" || t == "unsigned int" || t == "Type*") {
        val.value = static_cast<u64>(r.ReadU32());
    } else if (t == "SInt64" || t == "long long") {
        val.value = r.ReadI64();
    } else if (t == "UInt64" || t == "unsigned long long" || t == "FileSize") {
        val.value = r.ReadU64();
    } else if (t == "float") {
        val.value = static_cast<f64>(r.ReadF32());
    } else if (t == "double") {
        val.value = r.ReadF64();
    } else if (t == "bool") {
        val.value = r.ReadBool();
    } else if (t == "string") {
        val.value = r.ReadAlignedString();
        alignAfter = false; // ReadAlignedString already aligns
    } else if (t == "TypelessData") {
        i32 len = r.ReadI32();
        if (len > 0 && static_cast<size_t>(len) <= r.Remaining()) {
            val.value = r.ReadBytes(static_cast<size_t>(len));
        } else {
            val.value = ByteBuffer{};
        }
    } else if (node.IsArray() || t == "Array") {
        val.isArray = true;
        i32 count = r.ReadI32();
        if (count < 0) count = 0;
        if (idx.children.size() >= 2) {
            const auto& elemNode = nodes[idx.children[1].nodeIdx];
            if (elemNode.type == "UInt8" || elemNode.type == "SInt8" || elemNode.type == "char") {
                size_t byteCount = std::min(static_cast<size_t>(count), r.Remaining());
                val.value = r.ReadBytes(byteCount);
                val.isArray = false;
            } else {
                std::vector<TypeValue> elems;
                size_t safeCount = std::min(static_cast<size_t>(count), static_cast<size_t>(2000000));
                elems.reserve(std::min(safeCount, static_cast<size_t>(4096)));
                for (size_t i = 0; i < safeCount && !r.HasError(); ++i) {
                    elems.push_back(ReadNodeValue(r, nodes, idx.children[1]));
                }
                val.value = std::move(elems);
            }
        } else {
            val.value = std::vector<TypeValue>{};
        }
    } else {
        // Composite struct / vector / map / pair
        if (idx.children.size() == 1 && (nodes[idx.children[0].nodeIdx].IsArray() || nodes[idx.children[0].nodeIdx].type == "Array")) {
            auto arrVal = ReadNodeValue(r, nodes, idx.children[0]);
            val.isArray = arrVal.isArray;
            val.value = std::move(arrVal.value);
            if (nodes[idx.children[0].nodeIdx].IsAlign()) alignAfter = true;
        } else {
            std::vector<TypeValue> fields;
            fields.reserve(idx.children.size());
            for (const auto& child : idx.children) {
                fields.push_back(ReadNodeValue(r, nodes, child));
            }
            val.value = std::move(fields);
        }
    }

    if (alignAfter) {
        r.Align(4);
    }
    return val;
}

void WriteNodeValue(BinaryWriter& w, const TypeValue& val, const std::vector<TypeTreeNode>& nodes, const TreeNodeIndex& idx) {
    const auto& node = nodes[idx.nodeIdx];
    const std::string& t = node.type;
    bool alignAfter = node.IsAlign();

    if (t == "SInt8" || t == "char") {
        w.WriteI8(static_cast<i8>(val.AsInt()));
    } else if (t == "UInt8") {
        w.WriteU8(static_cast<u8>(val.AsUInt()));
    } else if (t == "SInt16" || t == "short") {
        w.WriteI16(static_cast<i16>(val.AsInt()));
    } else if (t == "UInt16" || t == "unsigned short") {
        w.WriteU16(static_cast<u16>(val.AsUInt()));
    } else if (t == "SInt32" || t == "int") {
        w.WriteI32(static_cast<i32>(val.AsInt()));
    } else if (t == "UInt32" || t == "unsigned int" || t == "Type*") {
        w.WriteU32(static_cast<u32>(val.AsUInt()));
    } else if (t == "SInt64" || t == "long long") {
        w.WriteI64(val.AsInt());
    } else if (t == "UInt64" || t == "unsigned long long" || t == "FileSize") {
        w.WriteU64(val.AsUInt());
    } else if (t == "float") {
        w.WriteF32(static_cast<f32>(val.AsFloat()));
    } else if (t == "double") {
        w.WriteF64(val.AsFloat());
    } else if (t == "bool") {
        w.WriteBool(val.AsBool());
    } else if (t == "string") {
        w.WriteAlignedString(val.AsString());
        alignAfter = false;
    } else if (t == "TypelessData") {
        if (const auto* bytes = std::get_if<ByteBuffer>(&val.value)) {
            w.WriteI32(static_cast<i32>(bytes->size()));
            w.WriteBytes(*bytes);
        } else {
            w.WriteI32(0);
        }
    } else if (node.IsArray() || t == "Array") {
        if (const auto* bytes = std::get_if<ByteBuffer>(&val.value)) {
            w.WriteI32(static_cast<i32>(bytes->size()));
            w.WriteBytes(*bytes);
        } else if (const auto* elems = std::get_if<std::vector<TypeValue>>(&val.value)) {
            w.WriteI32(static_cast<i32>(elems->size()));
            if (idx.children.size() >= 2) {
                for (const auto& elem : *elems) {
                    WriteNodeValue(w, elem, nodes, idx.children[1]);
                }
            }
        } else {
            w.WriteI32(0);
        }
    } else {
        if (idx.children.size() == 1 && (nodes[idx.children[0].nodeIdx].IsArray() || nodes[idx.children[0].nodeIdx].type == "Array")) {
            WriteNodeValue(w, val, nodes, idx.children[0]);
            if (nodes[idx.children[0].nodeIdx].IsAlign()) alignAfter = true;
        } else if (const auto* fields = std::get_if<std::vector<TypeValue>>(&val.value)) {
            for (size_t i = 0; i < idx.children.size(); ++i) {
                const auto& childNode = nodes[idx.children[i].nodeIdx];
                const TypeValue* found = val.FindField(childNode.name);
                if (found) {
                    WriteNodeValue(w, *found, nodes, idx.children[i]);
                } else if (i < fields->size()) {
                    WriteNodeValue(w, (*fields)[i], nodes, idx.children[i]);
                } else {
                    TypeValue empty;
                    WriteNodeValue(w, empty, nodes, idx.children[i]);
                }
            }
        }
    }

    if (alignAfter) {
        w.Align(4);
    }
}

} // namespace

TypeValue* TypeValue::FindField(std::string_view fieldName) {
    auto* fields = std::get_if<std::vector<TypeValue>>(&value);
    if (!fields) return nullptr;
    for (auto& f : *fields) {
        if (f.name == fieldName) return &f;
    }
    return nullptr;
}

const TypeValue* TypeValue::FindField(std::string_view fieldName) const {
    const auto* fields = std::get_if<std::vector<TypeValue>>(&value);
    if (!fields) return nullptr;
    for (const auto& f : *fields) {
        if (f.name == fieldName) return &f;
    }
    return nullptr;
}

i64 TypeValue::AsInt(i64 fallback) const {
    if (auto* v = std::get_if<i64>(&value)) return *v;
    if (auto* v = std::get_if<u64>(&value)) return static_cast<i64>(*v);
    if (auto* v = std::get_if<f64>(&value)) return static_cast<i64>(*v);
    if (auto* v = std::get_if<bool>(&value)) return *v ? 1 : 0;
    return fallback;
}

u64 TypeValue::AsUInt(u64 fallback) const {
    if (auto* v = std::get_if<u64>(&value)) return *v;
    if (auto* v = std::get_if<i64>(&value)) return static_cast<u64>(*v);
    if (auto* v = std::get_if<bool>(&value)) return *v ? 1 : 0;
    return fallback;
}

f64 TypeValue::AsFloat(f64 fallback) const {
    if (auto* v = std::get_if<f64>(&value)) return *v;
    if (auto* v = std::get_if<i64>(&value)) return static_cast<f64>(*v);
    if (auto* v = std::get_if<u64>(&value)) return static_cast<f64>(*v);
    return fallback;
}

bool TypeValue::AsBool(bool fallback) const {
    if (auto* v = std::get_if<bool>(&value)) return *v;
    if (auto* v = std::get_if<i64>(&value)) return *v != 0;
    if (auto* v = std::get_if<u64>(&value)) return *v != 0;
    return fallback;
}

std::string TypeValue::AsString(std::string_view fallback) const {
    if (auto* v = std::get_if<std::string>(&value)) return *v;
    return std::string(fallback);
}

std::string TypeValue::FormatSummary() const {
    if (auto* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (auto* i = std::get_if<i64>(&value)) return std::to_string(*i);
    if (auto* u = std::get_if<u64>(&value)) return std::to_string(*u);
    if (auto* f = std::get_if<f64>(&value)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << *f;
        return oss.str();
    }
    if (auto* s = std::get_if<std::string>(&value)) return "\"" + *s + "\"";
    if (auto* bytes = std::get_if<ByteBuffer>(&value)) return "<" + std::to_string(bytes->size()) + " bytes>";
    if (auto* list = std::get_if<std::vector<TypeValue>>(&value)) {
        return isArray ? ("[" + std::to_string(list->size()) + " items]")
                       : ("{" + std::to_string(list->size()) + " fields}");
    }
    return "null";
}

std::string TypeValue::ToYaml(int indent) const {
    std::ostringstream oss;
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    if (auto* list = std::get_if<std::vector<TypeValue>>(&value)) {
        if (isArray) {
            if (list->empty()) {
                oss << pad << name << ": []\n";
            } else {
                oss << pad << name << ":\n";
                for (const auto& elem : *list) {
                    if (elem.IsObject()) {
                        oss << pad << "  -\n";
                        for (const auto& f : std::get<std::vector<TypeValue>>(elem.value)) {
                            oss << f.ToYaml(indent + 2);
                        }
                    } else {
                        oss << pad << "  - " << elem.FormatSummary() << "\n";
                    }
                }
            }
        } else {
            if (!name.empty()) oss << pad << name << ":\n";
            for (const auto& f : *list) {
                oss << f.ToYaml(name.empty() ? indent : indent + 1);
            }
        }
    } else if (auto* s = std::get_if<std::string>(&value)) {
        oss << pad << name << ": " << *s << "\n";
    } else {
        oss << pad << name << ": " << FormatSummary() << "\n";
    }
    return oss.str();
}

std::string TypeTreeSerializer::ResolveCommonString(u32 offset, const std::string& localBuffer) {
    bool isCommon = (offset & 0x80000000u) != 0;
    u32 idx = offset & 0x7FFFFFFFu;
    if (isCommon) {
        const auto& common = CommonStrings();
        auto it = common.find(idx);
        if (it != common.end()) return it->second;
        return "Common_" + std::to_string(idx);
    }
    if (idx < localBuffer.size()) {
        const char* ptr = localBuffer.data() + idx;
        size_t maxLen = localBuffer.size() - idx;
        size_t len = 0;
        while (len < maxLen && ptr[len] != '\0') ++len;
        return std::string(ptr, len);
    }
    return {};
}

Result<TypeTree> TypeTreeSerializer::ReadTypeTreeBlob(BinaryReader& r, u32 formatVersion) {
    i32 nodeCount = r.ReadI32();
    i32 stringBufferSize = r.ReadI32();
    if (r.HasError() || nodeCount < 0 || stringBufferSize < 0 || nodeCount > 100000) {
        return Result<TypeTree>::Err("Invalid TypeTree blob header");
    }

    TypeTree tree;
    tree.nodes.resize(static_cast<size_t>(nodeCount));

    for (i32 i = 0; i < nodeCount; ++i) {
        auto& n = tree.nodes[static_cast<size_t>(i)];
        n.version = r.ReadU16();
        n.level = r.ReadU8();
        n.typeFlags = r.ReadU8();
        n.typeStrOffset = r.ReadU32();
        n.nameStrOffset = r.ReadU32();
        n.byteSize = r.ReadI32();
        n.index = r.ReadI32();
        n.metaFlag = r.ReadU32();
        if (formatVersion >= 19) {
            n.refTypeHash = r.ReadU64();
        }
    }

    tree.stringBuffer = r.ReadFixedString(static_cast<size_t>(stringBufferSize));
    if (r.HasError()) {
        return Result<TypeTree>::Err("Truncated TypeTree blob");
    }

    for (auto& n : tree.nodes) {
        n.type = ResolveCommonString(n.typeStrOffset, tree.stringBuffer);
        n.name = ResolveCommonString(n.nameStrOffset, tree.stringBuffer);
    }
    return Result<TypeTree>::Ok(std::move(tree));
}

void TypeTreeSerializer::WriteTypeTreeBlob(BinaryWriter& w, const TypeTree& tree, u32 formatVersion) {
    // Build fresh local string table so written offsets are always 100% self-contained
    std::string localStrings;
    std::unordered_map<std::string, u32> stringOffsets;
    auto intern = [&](const std::string& s) -> u32 {
        auto it = stringOffsets.find(s);
        if (it != stringOffsets.end()) return it->second;
        u32 off = static_cast<u32>(localStrings.size());
        localStrings.append(s);
        localStrings.push_back('\0');
        stringOffsets[s] = off;
        return off;
    };

    std::vector<std::pair<u32, u32>> offsets;
    offsets.reserve(tree.nodes.size());
    for (const auto& n : tree.nodes) {
        offsets.emplace_back(intern(n.type), intern(n.name));
    }

    w.WriteI32(static_cast<i32>(tree.nodes.size()));
    w.WriteI32(static_cast<i32>(localStrings.size()));
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const auto& n = tree.nodes[i];
        w.WriteU16(n.version);
        w.WriteU8(n.level);
        w.WriteU8(n.typeFlags);
        w.WriteU32(offsets[i].first);
        w.WriteU32(offsets[i].second);
        w.WriteI32(n.byteSize);
        w.WriteI32(n.index);
        w.WriteU32(n.metaFlag);
        if (formatVersion >= 19) {
            w.WriteU64(n.refTypeHash);
        }
    }
    w.WriteFixedString(localStrings);
}

Result<TypeValue> TypeTreeSerializer::ReadObject(BinaryReader& r, const TypeTree& tree) {
    if (tree.nodes.empty()) {
        return Result<TypeValue>::Err("Cannot deserialize object with empty TypeTree");
    }
    TreeNodeIndex rootIdx = BuildHierarchy(tree.nodes);
    TypeValue val = ReadNodeValue(r, tree.nodes, rootIdx);
    return Result<TypeValue>::Ok(std::move(val));
}

Result<ByteBuffer> TypeTreeSerializer::WriteObject(const TypeValue& root, const TypeTree& tree, Endian endian) {
    if (tree.nodes.empty()) {
        return Result<ByteBuffer>::Err("Cannot serialize object with empty TypeTree");
    }
    BinaryWriter w(endian);
    TreeNodeIndex rootIdx = BuildHierarchy(tree.nodes);
    WriteNodeValue(w, root, tree.nodes, rootIdx);
    return Result<ByteBuffer>::Ok(w.TakeBuffer());
}

} // namespace coconut::unity
