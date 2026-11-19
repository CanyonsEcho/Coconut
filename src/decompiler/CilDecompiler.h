#pragma once

#include "decompiler/MonoAssembly.h"
#include "core/ProjectDatabase.h"
#include <string>
#include <vector>

namespace coconut::decompiler {

struct CilInstruction {
    u32 offset = 0;
    ByteBuffer rawBytes;
    std::string mnemonic;
    std::string operandText;
    u32 tokenOperand = 0;
    i32 branchTarget = -1;
};

struct CodeXRef {
    std::string fromSymbol;
    u32 fromOffset = 0;
    std::string toSymbol;
    std::string kind; // "CALL", "READ", "WRITE", "STRING", "TYPE"
};

struct DecompiledMethodResult {
    std::vector<CilInstruction> instructions;
    std::string disassemblyListing;
    std::string decompiledCSharp;
    std::vector<CodeXRef> xrefs;
};

class CilDecompiler {
public:
    static std::vector<CilInstruction> Disassemble(const MonoAssembly& assembly, const MonoMethodInfo& method);

    static DecompiledMethodResult DecompileMethod(
        const MonoAssembly& assembly,
        const MonoTypeInfo& ownerType,
        const MonoMethodInfo& method,
        const ProjectDatabase* db = nullptr);

    static std::string DecompileType(
        const MonoAssembly& assembly,
        const MonoTypeInfo& type,
        const ProjectDatabase* db = nullptr,
        std::vector<CodeXRef>* outXRefs = nullptr);
};

} // namespace coconut::decompiler
