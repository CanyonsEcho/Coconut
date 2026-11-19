#include "fixup/BuildErrorFixer.h"
#include "export/UnityProjectExporter.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <regex>
#include <unordered_set>

namespace coconut::fixup {

namespace {

bool ReplaceAll(std::string& s, std::string_view from, std::string_view to) {
    if (from.empty()) return false;
    bool changed = false;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
        changed = true;
    }
    return changed;
}

bool ContainsAny(std::string_view hay, std::initializer_list<std::string_view> needles) {
    for (auto n : needles) {
        if (hay.find(n) != std::string_view::npos) return true;
    }
    return false;
}

} // namespace

Result<BuildFixReport> BuildErrorFixer::RunOnProject(
    const std::filesystem::path& unityProjectDir,
    const std::string& targetUnityVersion) {
    BuildFixReport report;
    UnityVersion targetVer = UnityVersion::Parse(targetUnityVersion);
    std::error_code ec;

    auto assetsDir = unityProjectDir / "Assets";
    if (!std::filesystem::exists(assetsDir, ec)) {
        return Result<BuildFixReport>::Err("Not a Unity project directory (missing Assets/): " + unityProjectDir.string());
    }

    bool needsUnsafe = false;
    std::unordered_set<std::string> detectedPackages;

    // 1. Scan & fix every C# script under Assets/
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();

        // Check missing .meta files
        if (ext != ".meta") {
            auto metaPath = entry.path();
            metaPath += ".meta";
            if (!std::filesystem::exists(metaPath, ec)) {
                std::string importer = "NativeFormatImporter";
                if (ext == ".cs") importer = "MonoImporter";
                else if (ext == ".png" || ext == ".jpg" || ext == ".tga") importer = "TextureImporter";
                else if (ext == ".obj" || ext == ".fbx") importer = "ModelImporter";
                else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") importer = "AudioImporter";
                exporter::UnityProjectExporter::WriteMetaFile(
                    entry.path(),
                    exporter::UnityProjectExporter::DeterministicGuid("autofix:" + entry.path().string()),
                    importer);
                ++report.missingMetasCreated;
            }
        }

        // Repair broken / placeholder shaders
        if (ext == ".shader") {
            auto txtRes = platform::ReadFileText(entry.path());
            if (txtRes) {
                const std::string& src = txtRes.Value();
                if (src.empty() || src.find("Hidden/InternalErrorShader") != std::string::npos ||
                    src.find("Shader \"") == std::string::npos) {
                    std::string shaderName = entry.path().stem().string();
                    std::string repaired =
                        "// Auto-repaired by Coconut BuildErrorFixer\n"
                        "Shader \"CoconutRecovered/" + shaderName + "\"\n"
                        "{\n"
                        "    Properties\n"
                        "    {\n"
                        "        _Color (\"Color\", Color) = (1,1,1,1)\n"
                        "        _MainTex (\"Albedo (RGB)\", 2D) = \"white\" {}\n"
                        "    }\n"
                        "    SubShader\n"
                        "    {\n"
                        "        Tags { \"RenderType\"=\"Opaque\" }\n"
                        "        LOD 200\n"
                        "        CGPROGRAM\n"
                        "        #pragma surface surf Standard fullforwardshadows\n"
                        "        sampler2D _MainTex;\n"
                        "        fixed4 _Color;\n"
                        "        struct Input { float2 uv_MainTex; };\n"
                        "        void surf (Input IN, inout SurfaceOutputStandard o) {\n"
                        "            fixed4 c = tex2D(_MainTex, IN.uv_MainTex) * _Color;\n"
                        "            o.Albedo = c.rgb; o.Alpha = c.a;\n"
                        "        }\n"
                        "        ENDCG\n"
                        "    }\n"
                        "    FallBack \"Diffuse\"\n"
                        "}\n";
                    platform::WriteFileText(entry.path(), repaired);
                    ++report.shadersRepaired;
                    report.logs.push_back("Repaired broken shader: " + entry.path().filename().string());
                }
            }
            continue;
        }

        if (ext != ".cs") continue;
        ++report.scriptsScanned;

        std::string fname = entry.path().filename().string();
        // Remove known compiler-generated duplicate files exported by decompilers
        if (fname.find("PrivateImplementationDetails") != std::string::npos ||
            fname == "EmbeddedAttribute.cs" ||
            fname == "IsReadOnlyAttribute.cs" ||
            fname == "NullableAttribute.cs" ||
            fname == "NullableContextAttribute.cs" ||
            fname == "RefSafetyRulesAttribute.cs") {
            std::filesystem::remove(entry.path(), ec);
            auto meta = entry.path(); meta += ".meta";
            std::filesystem::remove(meta, ec);
            ++report.compilerGeneratedStripped;
            report.logs.push_back("Stripped duplicate compiler-generated script: " + fname);
            continue;
        }

        auto txtRes = platform::ReadFileText(entry.path());
        if (!txtRes) continue;
        std::string src = txtRes.Value();
        bool modified = false;

        // Detect unsafe C# blocks / pointer syntax
        if (src.find("unsafe ") != std::string::npos || src.find("fixed (") != std::string::npos) {
            needsUnsafe = true;
        }

        // Detect required UPM packages from `using` directives
        if (src.find("using TMPro;") != std::string::npos) detectedPackages.insert("com.unity.textmeshpro");
        if (src.find("using UnityEngine.InputSystem;") != std::string::npos) detectedPackages.insert("com.unity.inputsystem");
        if (src.find("using UnityEngine.Rendering.Universal;") != std::string::npos) detectedPackages.insert("com.unity.render-pipelines.universal");
        if (src.find("using UnityEngine.Rendering.HighDefinition;") != std::string::npos) detectedPackages.insert("com.unity.render-pipelines.high-definition");
        if (src.find("using Newtonsoft.Json;") != std::string::npos) detectedPackages.insert("com.unity.nuget.newtonsoft-json");
        if (src.find("using Unity.Mathematics;") != std::string::npos) detectedPackages.insert("com.unity.mathematics");
        if (src.find("using Unity.Burst;") != std::string::npos) detectedPackages.insert("com.unity.burst");

        // Fix A: Strip duplicate [assembly: AssemblyVersion(...)] or [assembly: SecurityPermission(...)] in AssemblyInfo.cs
        if (fname == "AssemblyInfo.cs") {
            std::regex dupAsmAttr(R"(\[assembly:\s*(?:System\.Security\.Permissions\.SecurityPermission|AssemblyVersion|AssemblyFileVersion|Debuggable)\s*\([^\]]*\)\])");
            std::string cleaned = std::regex_replace(src, dupAsmAttr, "// [Coconut removed duplicate assembly attribute]");
            if (cleaned != src) {
                src = std::move(cleaned);
                ++report.compilerGeneratedStripped;
                modified = true;
            }
        }

        // Fix B: Strip inline compiler-generated attribute classes (Microsoft.CodeAnalysis.EmbeddedAttribute, etc.)
        if (ContainsAny(src, {"class EmbeddedAttribute", "class NullableAttribute", "class RefSafetyRulesAttribute"})) {
            src = "// [Coconut stripped compiler-generated attribute shim to avoid CS0436 conflicts]\n";
            ++report.compilerGeneratedStripped;
            modified = true;
        }

        // Fix C: Fix invalid backing field identifiers `<Prop>k__BackingField` left over from raw IL decompilation
        {
            std::regex backingFieldRe(R"(<([A-Za-z0-9_]+)>k__BackingField)");
            std::string fixed = std::regex_replace(src, backingFieldRe, "_$1_BackingField");
            if (fixed != src) {
                src = std::move(fixed);
                ++report.invalidIdentifiersFixed;
                modified = true;
            }
        }

        // Fix D: Unity Version API Migrations
        if (targetVer.major >= 2018) {
            if (ReplaceAll(src, "Application.LoadLevel(", "UnityEngine.SceneManagement.SceneManager.LoadScene(")) {
                ++report.unityApiMigrations;
                modified = true;
            }
            if (ReplaceAll(src, "Application.LoadLevelAdditive(", "UnityEngine.SceneManagement.SceneManager.LoadScene(")) {
                ++report.unityApiMigrations;
                modified = true;
            }
        }
        if (targetVer.major >= 6000) {
            if (ReplaceAll(src, "FindObjectOfType<", "FindFirstObjectByType<")) {
                ++report.unityApiMigrations;
                modified = true;
            }
            if (ReplaceAll(src, "FindObjectsOfType<", "FindObjectsByType<")) {
                ++report.unityApiMigrations;
                modified = true;
            }
        }
        if (src.find("new WWW(") != std::string::npos && src.find("#pragma warning disable CS0618") == std::string::npos) {
            src = "#pragma warning disable CS0618\n" + src;
            ++report.unityApiMigrations;
            modified = true;
        }

        if (modified) {
            platform::WriteFileText(entry.path(), src);
            ++report.scriptsFixed;
            report.logs.push_back("Auto-fixed C# script: " + fname);
        }
    }

    // 2. Generate Assets/csc.rsp (-unsafe) if pointer/unsafe code was detected
    if (needsUnsafe) {
        auto cscPath = assetsDir / "csc.rsp";
        platform::WriteFileText(cscPath, "-unsafe\n-nowarn:0618,0168,0219,0414\n");
        report.cscUnsafeGenerated = true;
        report.logs.push_back("Generated Assets/csc.rsp with -unsafe compiler flag");
    }

    // 3. Ensure Packages/manifest.json contains all detected Unity UPM packages
    auto manifestPath = unityProjectDir / "Packages" / "manifest.json";
    nlohmann::json manifest;
    if (std::filesystem::exists(manifestPath, ec)) {
        auto txt = platform::ReadFileText(manifestPath);
        if (txt) {
            try { manifest = nlohmann::json::parse(txt.Value()); } catch (...) {}
        }
    }
    if (!manifest.contains("dependencies") || !manifest["dependencies"].is_object()) {
        manifest["dependencies"] = nlohmann::json::object();
    }

    auto defaultVersionFor = [&](const std::string& pkg) -> std::string {
        if (pkg == "com.unity.textmeshpro") return targetVer.major >= 6000 ? "3.0.9" : "3.0.6";
        if (pkg == "com.unity.inputsystem") return "1.7.0";
        if (pkg == "com.unity.render-pipelines.universal") return targetVer.major >= 6000 ? "17.0.3" : "14.0.11";
        if (pkg == "com.unity.render-pipelines.high-definition") return targetVer.major >= 6000 ? "17.0.3" : "14.0.11";
        if (pkg == "com.unity.nuget.newtonsoft-json") return "3.2.1";
        if (pkg == "com.unity.mathematics") return "1.3.1";
        if (pkg == "com.unity.burst") return "1.8.15";
        return "1.0.0";
    };

    for (const auto& pkg : detectedPackages) {
        if (!manifest["dependencies"].contains(pkg)) {
            manifest["dependencies"][pkg] = defaultVersionFor(pkg);
            report.packagesAdded.push_back(pkg);
            report.logs.push_back("Added missing UPM dependency: " + pkg);
        }
    }
    platform::WriteFileText(manifestPath, manifest.dump(2));

    // 4. Stamp target Unity Editor version in ProjectSettings/ProjectVersion.txt
    if (!targetUnityVersion.empty()) {
        platform::WriteFileText(
            unityProjectDir / "ProjectSettings" / "ProjectVersion.txt",
            "m_EditorVersion: " + targetUnityVersion + "\nm_EditorVersionWithRevision: " + targetUnityVersion + " (coconut)\n");
    }

    COCO_LOG_INFO("BuildErrorFixer", "Scanned " + std::to_string(report.scriptsScanned) + " scripts, fixed " +
                                     std::to_string(report.scriptsFixed) + ", stripped " +
                                     std::to_string(report.compilerGeneratedStripped) + " compiler stubs, repaired " +
                                     std::to_string(report.shadersRepaired) + " shaders");
    return Result<BuildFixReport>::Ok(std::move(report));
}

} // namespace coconut::fixup
