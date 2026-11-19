#pragma once

#include "core/Types.h"
#include <string>
#include <vector>

namespace coconut::decompiler {

struct Il2CppImageInfo {
    std::string name;
    i32 typeStart = 0;
    u32 typeCount = 0;
};

struct Il2CppFieldInfo {
    std::string name;
    i32 typeIndex = 0;
    u32 token = 0;
};

struct Il2CppMethodInfo {
    std::string name;
    i32 declaringType = 0;
    u32 token = 0;
    u16 flags = 0;
    u16 paramCount = 0;
    u64 nativeRva = 0; // Populated when mapped against libil2cpp.so / GameAssembly.dll
};

struct Il2CppTypeInfo {
    std::string nameSpace;
    std::string name;
    u32 flags = 0;
    u32 token = 0;
    std::vector<Il2CppFieldInfo> fields;
    std::vector<Il2CppMethodInfo> methods;

    std::string FullName() const {
        return nameSpace.empty() ? name : (nameSpace + "." + name);
    }
};

struct Il2CppMetadata {
    i32 version = 0;
    std::vector<std::string> stringLiterals;
    std::vector<Il2CppImageInfo> images;
    std::vector<Il2CppTypeInfo> types;

    static Result<Il2CppMetadata> Parse(ByteSpan data);
    std::string GenerateDumpCS() const;
    std::string GenerateTypeStubCS(const Il2CppTypeInfo& type) const;
};

} // namespace coconut::decompiler
