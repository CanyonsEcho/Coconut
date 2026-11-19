#pragma once

#include "core/Types.h"
#include "core/ProjectDatabase.h"
#include "unity/GameLoader.h"
#include "decompiler/MonoAssembly.h"
#include "decompiler/CilDecompiler.h"
#include "decompiler/Il2CppMetadata.h"
#include "decompiler/NativeDisassembler.h"

#include <filesystem>
#include <string>
#include <vector>

namespace coconut::decompiler {

struct SymbolRecord {
    std::string id;
    std::string assemblyName;
    std::string nameSpace;
    std::string typeName;
    std::string fullName;
    std::string kind; // "class", "struct", "interface", "enum"
    size_t methodCount = 0;
    size_t fieldCount = 0;
    bool isIl2Cpp = false;
    size_t assemblyIndex = 0;
    size_t typeIndex = 0;
};

struct TypeInspectionView {
    std::string fullName;
    std::string disassemblyListing;
    std::string decompiledCSharp;
    std::vector<CodeXRef> xrefs;
};

class DecompilerEngine {
public:
    void Clear();

    // Index all scripts from a LoadedGameProject (Mono Managed/*.dll OR IL2CPP global-metadata.dat + binary)
    Result<void> AnalyzeProject(const unity::LoadedGameProject& project, const ProjectDatabase* db = nullptr);

    const std::vector<SymbolRecord>& Symbols() const { return symbols_; }
    const std::vector<CodeXRef>& AllXRefs() const { return allXRefs_; }
    const std::vector<MonoAssembly>& MonoAssemblies() const { return monoAssemblies_; }
    const std::optional<Il2CppMetadata>& Il2Cpp() const { return il2cppMeta_; }
    const std::optional<NativeBinaryInfo>& NativeBinary() const { return nativeBinary_; }

    std::vector<size_t> SearchSymbols(std::string_view query) const;
    std::vector<CodeXRef> FindXRefsTo(std::string_view symbolOrText) const;

    // Generate synchronized Ghidra-style Listing + Decompiled C# view for a symbol
    TypeInspectionView InspectSymbol(size_t symbolIndex, const ProjectDatabase* db = nullptr) const;

    // Export all decompiled scripts into a folder (used by native Unity project exporter)
    Result<size_t> ExportAllScripts(const std::filesystem::path& scriptsDir, const ProjectDatabase* db = nullptr) const;

    // Export Ghidra-compatible script.json & IL2CPP/Mono dump.cs
    Result<void> ExportDumpCS(const std::filesystem::path& outPath, const ProjectDatabase* db = nullptr) const;
    Result<void> ExportGhidraScriptJson(const std::filesystem::path& outPath) const;

private:
    std::vector<MonoAssembly> monoAssemblies_;
    std::optional<Il2CppMetadata> il2cppMeta_;
    std::optional<NativeBinaryInfo> nativeBinary_;
    std::vector<SymbolRecord> symbols_;
    std::vector<CodeXRef> allXRefs_;
};

} // namespace coconut::decompiler
