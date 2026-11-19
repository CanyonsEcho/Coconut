#include "decompiler/DecompilerEngine.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace coconut::decompiler {

namespace {

std::string LowerStr(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string SanitizeFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    return out.empty() ? "UnnamedType" : out;
}

} // namespace

void DecompilerEngine::Clear() {
    monoAssemblies_.clear();
    il2cppMeta_.reset();
    nativeBinary_.reset();
    symbols_.clear();
    allXRefs_.clear();
}

Result<void> DecompilerEngine::AnalyzeProject(const unity::LoadedGameProject& project, const ProjectDatabase* db) {
    Clear();

    // 1. Parse all Mono assemblies
    for (const auto& asmFile : project.managedAssemblies) {
        auto parsed = MonoAssembly::Parse(asmFile.data, asmFile.name);
        if (!parsed) {
            COCO_LOG_WARN("Decompiler", "Skipping '" + asmFile.name + "': " + parsed.Error());
            continue;
        }
        size_t asmIdx = monoAssemblies_.size();
        monoAssemblies_.push_back(std::move(parsed.Value()));
        const auto& ma = monoAssemblies_.back();

        for (size_t ti = 0; ti < ma.types.size(); ++ti) {
            const auto& t = ma.types[ti];
            if (t.name == "<Module>") continue;

            SymbolRecord rec;
            rec.id = ma.fileName + "::" + t.FullName();
            rec.assemblyName = ma.fileName;
            rec.nameSpace = t.nameSpace;
            rec.typeName = t.name;
            rec.fullName = t.FullName();
            rec.kind = t.kind;
            rec.methodCount = t.methods.size();
            rec.fieldCount = t.fields.size();
            rec.isIl2Cpp = false;
            rec.assemblyIndex = asmIdx;
            rec.typeIndex = ti;
            symbols_.push_back(std::move(rec));

            // Collect XRefs across every method
            for (const auto& m : t.methods) {
                auto dm = CilDecompiler::DecompileMethod(ma, t, m, db);
                allXRefs_.insert(allXRefs_.end(), dm.xrefs.begin(), dm.xrefs.end());
            }
        }
    }

    // 2. Parse IL2CPP metadata & native binary if present
    if (!project.il2cppMetadataData.empty()) {
        auto metaRes = Il2CppMetadata::Parse(project.il2cppMetadataData);
        if (metaRes) {
            il2cppMeta_ = std::move(metaRes.Value());
            if (!project.il2cppBinaryData.empty()) {
                auto binRes = NativeDisassembler::ParseBinary(project.il2cppBinaryData);
                if (binRes) {
                    nativeBinary_ = std::move(binRes.Value());
                }
            }

            // Map synthetic/code section RVAs for inspection if native binary has a .text section
            u64 baseCodeRva = 0;
            if (nativeBinary_) {
                if (const auto* text = nativeBinary_->FindTextSection()) {
                    baseCodeRva = text->virtualAddress;
                }
            }

            u64 methodCursor = baseCodeRva;
            for (size_t ti = 0; ti < il2cppMeta_->types.size(); ++ti) {
                auto& t = il2cppMeta_->types[ti];
                for (auto& m : t.methods) {
                    if (m.nativeRva == 0 && baseCodeRva != 0) {
                        m.nativeRva = methodCursor;
                        methodCursor += 0x20;
                    }
                }

                SymbolRecord rec;
                rec.id = "il2cpp::" + t.FullName();
                rec.assemblyName = il2cppMeta_->images.empty() ? "IL2CPP" : il2cppMeta_->images[0].name;
                rec.nameSpace = t.nameSpace;
                rec.typeName = t.name;
                rec.fullName = t.FullName();
                rec.kind = "class";
                rec.methodCount = t.methods.size();
                rec.fieldCount = t.fields.size();
                rec.isIl2Cpp = true;
                rec.assemblyIndex = 0;
                rec.typeIndex = ti;
                symbols_.push_back(std::move(rec));
            }

            for (size_t i = 0; i < il2cppMeta_->stringLiterals.size(); ++i) {
                allXRefs_.push_back({"global-metadata.dat", static_cast<u32>(i),
                                     "\"" + il2cppMeta_->stringLiterals[i] + "\"", "STRING"});
            }
        } else {
            COCO_LOG_WARN("Decompiler", "IL2CPP metadata parse failed: " + metaRes.Error());
        }
    }

    COCO_LOG_INFO("Decompiler", "Indexed " + std::to_string(symbols_.size()) + " script types & " +
                                std::to_string(allXRefs_.size()) + " cross-references");
    return Result<void>::Ok();
}

std::vector<size_t> DecompilerEngine::SearchSymbols(std::string_view query) const {
    std::vector<size_t> out;
    std::string q = LowerStr(query);
    for (size_t i = 0; i < symbols_.size(); ++i) {
        if (q.empty() || LowerStr(symbols_[i].fullName + " " + symbols_[i].assemblyName).find(q) != std::string::npos) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<CodeXRef> DecompilerEngine::FindXRefsTo(std::string_view symbolOrText) const {
    std::vector<CodeXRef> matches;
    std::string q = LowerStr(symbolOrText);
    for (const auto& xr : allXRefs_) {
        if (q.empty() ||
            LowerStr(xr.toSymbol).find(q) != std::string::npos ||
            LowerStr(xr.fromSymbol).find(q) != std::string::npos) {
            matches.push_back(xr);
        }
    }
    return matches;
}

TypeInspectionView DecompilerEngine::InspectSymbol(size_t symbolIndex, const ProjectDatabase* db) const {
    TypeInspectionView view;
    if (symbolIndex >= symbols_.size()) return view;
    const auto& sym = symbols_[symbolIndex];
    view.fullName = sym.fullName;

    if (!sym.isIl2Cpp) {
        const auto& ma = monoAssemblies_[sym.assemblyIndex];
        const auto& type = ma.types[sym.typeIndex];
        view.decompiledCSharp = CilDecompiler::DecompileType(ma, type, db, &view.xrefs);

        std::ostringstream listing;
        listing << "; Assembly: " << ma.fileName << " (Runtime " << ma.runtimeVersion << ")\n";
        listing << "; Type: " << type.FullName() << " (Token 0x" << std::hex << type.token << std::dec << ")\n\n";
        for (const auto& m : type.methods) {
            auto dm = CilDecompiler::DecompileMethod(ma, type, m, db);
            listing << dm.disassemblyListing << "\n";
        }
        view.disassemblyListing = listing.str();
    } else if (il2cppMeta_) {
        const auto& type = il2cppMeta_->types[sym.typeIndex];
        view.decompiledCSharp = il2cppMeta_->GenerateTypeStubCS(type);

        if (nativeBinary_) {
            if (const auto* text = nativeBinary_->FindTextSection()) {
                size_t startOff = static_cast<size_t>(text->fileOffset);
                size_t avail = startOff < nativeBinary_->rawImage.size()
                                   ? std::min<size_t>(static_cast<size_t>(text->size), nativeBinary_->rawImage.size() - startOff)
                                   : 0;
                ByteSpan codeSpan(nativeBinary_->rawImage.data() + startOff, avail);
                std::vector<NativeInstruction> insns;
                if (nativeBinary_->arch == BinaryArch::ARM64) {
                    insns = NativeDisassembler::DisassembleARM64(codeSpan, text->virtualAddress, 64);
                } else {
                    insns = NativeDisassembler::DisassembleX86_64(codeSpan, text->virtualAddress, 64);
                }
                view.disassemblyListing = NativeDisassembler::FormatListing(
                    insns,
                    "IL2CPP Native " + nativeBinary_->format + " (" +
                        std::string(NativeDisassembler::ArchName(nativeBinary_->arch)) + ") — " + type.FullName());
            }
        } else {
            view.disassemblyListing = "; IL2CPP metadata loaded without native binary (.so / .dll / .dylib)\n";
        }
    }

    return view;
}

Result<size_t> DecompilerEngine::ExportAllScripts(const std::filesystem::path& scriptsDir, const ProjectDatabase* db) const {
    std::error_code ec;
    std::filesystem::create_directories(scriptsDir, ec);
    size_t count = 0;

    for (size_t i = 0; i < symbols_.size(); ++i) {
        const auto& sym = symbols_[i];
        auto view = InspectSymbol(i, db);
        std::filesystem::path targetDir = scriptsDir;
        if (!sym.nameSpace.empty()) {
            std::string nsPath = sym.nameSpace;
            std::replace(nsPath.begin(), nsPath.end(), '.', '/');
            targetDir /= nsPath;
        }
        std::filesystem::create_directories(targetDir, ec);
        auto outFile = targetDir / (SanitizeFilename(sym.typeName) + ".cs");
        auto res = platform::WriteFileText(outFile, view.decompiledCSharp);
        if (!res) return Result<size_t>::Err(res.Error());
        ++count;
    }
    return Result<size_t>::Ok(count);
}

Result<void> DecompilerEngine::ExportDumpCS(const std::filesystem::path& outPath, const ProjectDatabase* db) const {
    if (il2cppMeta_) {
        return platform::WriteFileText(outPath, il2cppMeta_->GenerateDumpCS());
    }
    std::ostringstream oss;
    oss << "// Coconut Mono dump.cs\n\n";
    for (size_t i = 0; i < symbols_.size(); ++i) {
        oss << InspectSymbol(i, db).decompiledCSharp << "\n";
    }
    return platform::WriteFileText(outPath, oss.str());
}

Result<void> DecompilerEngine::ExportGhidraScriptJson(const std::filesystem::path& outPath) const {
    nlohmann::json root;
    root["generator"] = "Coconut Decompiler";
    nlohmann::json methods = nlohmann::json::array();
    nlohmann::json strings = nlohmann::json::array();

    for (const auto& ma : monoAssemblies_) {
        for (const auto& t : ma.types) {
            for (const auto& m : t.methods) {
                methods.push_back({
                    {"Address", m.rva},
                    {"Name", t.FullName() + "$$" + m.name},
                    {"Token", m.token}
                });
            }
        }
        for (const auto& [tok, val] : ma.userStrings) {
            strings.push_back({{"Token", tok}, {"Value", val}});
        }
    }
    if (il2cppMeta_) {
        for (const auto& t : il2cppMeta_->types) {
            for (const auto& m : t.methods) {
                methods.push_back({
                    {"Address", m.nativeRva},
                    {"Name", t.FullName() + "$$" + m.name},
                    {"Token", m.token}
                });
            }
        }
        for (size_t i = 0; i < il2cppMeta_->stringLiterals.size(); ++i) {
            strings.push_back({{"Index", i}, {"Value", il2cppMeta_->stringLiterals[i]}});
        }
    }

    root["ScriptMethod"] = std::move(methods);
    root["ScriptString"] = std::move(strings);
    return platform::WriteFileText(outPath, root.dump(2));
}

} // namespace coconut::decompiler
