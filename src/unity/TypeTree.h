#pragma once

#include "core/Types.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace coconut::unity {

struct TypeTreeNode {
    u16 version = 1;
    u8  level = 0;
    u8  typeFlags = 0; // 1 = IsArray
    u32 typeStrOffset = 0;
    u32 nameStrOffset = 0;
    i32 byteSize = -1;
    i32 index = 0;
    u32 metaFlag = 0; // 0x4000 = AlignBytesFlag
    u64 refTypeHash = 0;

    std::string type;
    std::string name;

    bool IsAlign() const { return (metaFlag & 0x4000u) != 0; }
    bool IsArray() const { return (typeFlags & 0x1u) != 0; }
};

struct TypeTree {
    std::vector<TypeTreeNode> nodes;
    std::string stringBuffer;

    bool Empty() const { return nodes.empty(); }
};

// Runtime editable Unity serialized value DOM (used by Inspector, Scene Editor, and Binary Patcher!)
struct TypeValue {
    using Storage = std::variant<
        std::monostate,
        bool,
        i64,
        u64,
        f64,
        std::string,
        ByteBuffer,
        std::vector<TypeValue> // struct fields or array elements
    >;

    std::string type;
    std::string name;
    bool isArray = false;
    bool align = false;
    Storage value;

    bool IsObject() const { return std::holds_alternative<std::vector<TypeValue>>(value) && !isArray; }
    bool IsArray() const { return std::holds_alternative<std::vector<TypeValue>>(value) && isArray; }

    TypeValue* FindField(std::string_view fieldName);
    const TypeValue* FindField(std::string_view fieldName) const;

    i64 AsInt(i64 fallback = 0) const;
    u64 AsUInt(u64 fallback = 0) const;
    f64 AsFloat(f64 fallback = 0.0) const;
    bool AsBool(bool fallback = false) const;
    std::string AsString(std::string_view fallback = "") const;

    std::string FormatSummary() const;
    std::string ToYaml(int indent = 0) const;
};

class TypeTreeSerializer {
public:
    static std::string ResolveCommonString(u32 offset, const std::string& localBuffer);

    static Result<TypeTree> ReadTypeTreeBlob(BinaryReader& r, u32 formatVersion);
    static void WriteTypeTreeBlob(BinaryWriter& w, const TypeTree& tree, u32 formatVersion);

    // Deserialize an object payload using its TypeTree into an editable TypeValue DOM.
    static Result<TypeValue> ReadObject(BinaryReader& r, const TypeTree& tree);

    // Re-serialize an edited TypeValue DOM back into exact Unity binary bytes!
    static Result<ByteBuffer> WriteObject(const TypeValue& root, const TypeTree& tree, Endian endian = Endian::Little);
};

} // namespace coconut::unity
