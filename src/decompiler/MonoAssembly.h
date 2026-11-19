#pragma once

#include "core/Types.h"
#include <string>
#include <vector>

namespace coconut::decompiler {

struct MonoParamInfo {
    u16 flags = 0;
    u16 sequence = 0;
    std::string name;
    std::string typeName;
};

struct MonoFieldInfo {
    u32 token = 0;
    u16 flags = 0;
    std::string name;
    std::string typeName;
    std::string visibility;
    bool isStatic = false;
    bool isReadOnly = false;
};

struct MonoMethodInfo {
    u32 token = 0;
    u32 rva = 0;
    u16 implFlags = 0;
    u16 flags = 0;
    std::string name;
    std::string returnType = "void";
    std::string visibility = "private";
    bool isStatic = false;
    bool isVirtual = false;
    bool isAbstract = false;
    std::vector<MonoParamInfo> params;
    u16 maxStack = 8;
    ByteBuffer ilCode;
    std::vector<std::string> localTypes;
};

struct MonoTypeInfo {
    u32 token = 0;
    u32 flags = 0;
    std::string nameSpace;
    std::string name;
    std::string baseType;
    std::string kind = "class"; // class, struct, interface, enum
    std::string visibility = "public";
    bool isAbstract = false;
    bool isSealed = false;
    std::vector<MonoFieldInfo> fields;
    std::vector<MonoMethodInfo> methods;

    std::string FullName() const {
        return nameSpace.empty() ? name : (nameSpace + "." + name);
    }
};

struct MonoTypeRefInfo {
    u32 token = 0;
    std::string nameSpace;
    std::string name;
    std::string FullName() const {
        return nameSpace.empty() ? name : (nameSpace + "." + name);
    }
};

struct MonoMemberRefInfo {
    u32 token = 0;
    std::string ownerName;
    std::string name;
    std::string signatureSummary;
};

struct MonoAssembly {
    std::string fileName;
    std::string assemblyName;
    std::string runtimeVersion;
    std::vector<std::string> assemblyRefs;
    std::vector<MonoTypeRefInfo> typeRefs;
    std::vector<MonoMemberRefInfo> memberRefs;
    std::vector<MonoTypeInfo> types;
    std::vector<std::pair<u32, std::string>> userStrings;

    std::string ResolveToken(u32 token) const;
    std::string ResolveUserString(u32 token) const;

    static Result<MonoAssembly> Parse(ByteSpan peBytes, std::string fileName = {});
};

} // namespace coconut::decompiler
