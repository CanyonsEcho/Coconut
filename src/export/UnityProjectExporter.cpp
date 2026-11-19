#include "export/UnityProjectExporter.h"
#include "unity/ClassIDs.h"
#include "assets/TextureAsset.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "assets/MaterialAsset.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace coconut::exporter {

namespace {

std::string CleanAssetFileName(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') {
            out.push_back(c == ' ' ? '_' : c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? "UnnamedAsset" : out;
}

} // namespace

std::string UnityProjectExporter::DeterministicGuid(std::string_view key) {
    // 128-bit FNV-1a-style deterministic hash -> 32 lowercase hex chars (valid Unity GUID)
    u64 h1 = 0xcbf29ce484222325ULL;
    u64 h2 = 0x84222325cbf29ce4ULL;
    for (unsigned char c : key) {
        h1 ^= c;
        h1 *= 0x100000001b3ULL;
        h2 ^= static_cast<u64>(c) + 0x9e3779b97f4a7c15ULL;
        h2 *= 0x100000001b3ULL;
    }
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0')
        << std::setw(16) << h1 << std::setw(16) << h2;
    return oss.str();
}

Result<void> UnityProjectExporter::WriteMetaFile(
    const std::filesystem::path& assetPath,
    const std::string& guid,
    std::string_view importerKind) {
    std::ostringstream m;
    m << "fileFormatVersion: 2\n";
    m << "guid: " << guid << "\n";
    if (importerKind == "Folder") {
        m << "folderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n  userData: \n";
    } else if (importerKind == "MonoImporter") {
        m << "MonoImporter:\n  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n";
    } else if (importerKind == "TextureImporter") {
        m << "TextureImporter:\n  serializedVersion: 12\n  mipmaps:\n    enableMipMap: 1\n  textureType: 0\n";
    } else if (importerKind == "ModelImporter") {
        m << "ModelImporter:\n  serializedVersion: 21300\n  meshes:\n    globalScale: 1\n    isReadable: 1\n";
    } else if (importerKind == "AudioImporter") {
        m << "AudioImporter:\n  serializedVersion: 6\n  defaultSettings:\n    loadType: 0\n";
    } else {
        m << "NativeFormatImporter:\n  externalObjects: {}\n  mainObjectFileID: 0\n";
    }
    auto metaPath = assetPath;
    metaPath += ".meta";
    return platform::WriteFileText(metaPath, m.str());
}

Result<UnityExportReport> UnityProjectExporter::ExportProject(
    unity::LoadedGameProject& project,
    assets::AssetManager& assets,
    const decompiler::DecompilerEngine& decompiler,
    const scene::SceneDocument& sceneDoc,
    const ProjectDatabase* db,
    const std::filesystem::path& outputDir,
    const UnityExportOptions& options,
    std::function<void(const std::string&)> onLog) {
    UnityExportReport report;
    report.projectDir = outputDir;
    report.unityVersion = options.targetUnityVersion.empty()
                              ? (project.unityVersion.IsValid() ? project.unityVersion.ToString() : "2022.3.15f1")
                              : options.targetUnityVersion;

    auto log = [&](const std::string& msg) {
        COCO_LOG_INFO("Exporter", msg);
        if (onLog) onLog(msg);
    };

    std::error_code ec;
    auto assetsDir   = outputDir / "Assets";
    auto scenesDir   = assetsDir / "Scenes";
    auto scriptsDir  = assetsDir / "Scripts";
    auto meshesDir   = assetsDir / "Meshes";
    auto texturesDir = assetsDir / "Textures";
    auto matsDir     = assetsDir / "Materials";
    auto audioDir    = assetsDir / "Audio";
    auto textDir     = assetsDir / "TextAssets";
    auto settingsDir = outputDir / "ProjectSettings";
    auto packagesDir = outputDir / "Packages";

    for (const auto& d : {assetsDir, scenesDir, scriptsDir, meshesDir, texturesDir, matsDir, audioDir, textDir, settingsDir, packagesDir}) {
        std::filesystem::create_directories(d, ec);
    }

    if (options.generateMetaFiles) {
        for (const auto& d : {scenesDir, scriptsDir, meshesDir, texturesDir, matsDir, audioDir, textDir}) {
            WriteMetaFile(d, DeterministicGuid("folder:" + d.filename().string()), "Folder");
            ++report.metaFilesGenerated;
        }
    }

    std::unordered_map<i64, std::string> texGuidMap;
    std::unordered_map<i64, std::string> meshGuidMap;
    std::unordered_map<i64, std::string> matGuidMap;

    // 1. Export Textures (.png + .meta)
    if (options.exportTextures) {
        for (size_t i = 0; i < assets.Entries().size(); ++i) {
            const auto& e = assets.Entries()[i];
            if (e.classId != static_cast<i32>(unity::ClassID::Texture2D)) continue;
            auto tRes = assets.GetTexture(i);
            if (!tRes || !tRes.Value()->IsValid()) continue;

            std::string fname = CleanAssetFileName(tRes.Value()->name) + "_" + std::to_string(e.pathId) + ".png";
            auto dst = texturesDir / fname;
            if (assets::TextureCodec::ExportPNG(*tRes.Value(), dst)) {
                std::string guid = DeterministicGuid("tex:" + std::to_string(e.pathId));
                texGuidMap[e.pathId] = guid;
                if (options.generateMetaFiles) {
                    WriteMetaFile(dst, guid, "TextureImporter");
                    ++report.metaFilesGenerated;
                }
                ++report.texturesExported;
            }
        }
        log("Exported " + std::to_string(report.texturesExported) + " textures");
    }

    // 2. Export Meshes (.obj + .meta)
    if (options.exportMeshes) {
        for (size_t i = 0; i < assets.Entries().size(); ++i) {
            const auto& e = assets.Entries()[i];
            if (e.classId != static_cast<i32>(unity::ClassID::Mesh)) continue;
            auto mRes = assets.GetMesh(i);
            if (!mRes || !mRes.Value()->IsValid()) continue;

            std::string fname = CleanAssetFileName(mRes.Value()->name) + "_" + std::to_string(e.pathId) + ".obj";
            auto dst = meshesDir / fname;
            if (assets::MeshCodec::ExportOBJ(*mRes.Value(), dst)) {
                std::string guid = DeterministicGuid("mesh:" + std::to_string(e.pathId));
                meshGuidMap[e.pathId] = guid;
                if (options.generateMetaFiles) {
                    WriteMetaFile(dst, guid, "ModelImporter");
                    ++report.metaFilesGenerated;
                }
                ++report.meshesExported;
            }
        }
        log("Exported " + std::to_string(report.meshesExported) + " meshes");
    }

    // 3. Export Materials (.mat YAML + .meta)
    if (options.exportMaterials) {
        for (size_t i = 0; i < assets.Entries().size(); ++i) {
            const auto& e = assets.Entries()[i];
            if (e.classId != static_cast<i32>(unity::ClassID::Material)) continue;
            auto mRes = assets.GetMaterial(i);
            if (!mRes) continue;
            const auto& mat = *mRes.Value();

            std::ostringstream y;
            y << "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!21 &2100000\nMaterial:\n";
            y << "  m_Name: " << mat.name << "\n";
            y << "  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n";
            y << "  m_SavedProperties:\n    m_Floats:\n";
            for (const auto& f : mat.floats) y << "    - " << f.name << ": " << f.value << "\n";
            y << "    m_Colors:\n";
            for (const auto& c : mat.colors) {
                y << "    - " << c.name << ": {r: " << c.value.r << ", g: " << c.value.g
                  << ", b: " << c.value.b << ", a: " << c.value.a << "}\n";
            }

            std::string fname = CleanAssetFileName(mat.name) + "_" + std::to_string(e.pathId) + ".mat";
            auto dst = matsDir / fname;
            if (platform::WriteFileText(dst, y.str())) {
                std::string guid = DeterministicGuid("mat:" + std::to_string(e.pathId));
                matGuidMap[e.pathId] = guid;
                if (options.generateMetaFiles) {
                    WriteMetaFile(dst, guid, "NativeFormatImporter");
                    ++report.metaFilesGenerated;
                }
                ++report.materialsExported;
            }
        }
        log("Exported " + std::to_string(report.materialsExported) + " materials");
    }

    // 4. Export AudioClips (.wav + .meta)
    if (options.exportAudio) {
        for (size_t i = 0; i < assets.Entries().size(); ++i) {
            const auto& e = assets.Entries()[i];
            if (e.classId != static_cast<i32>(unity::ClassID::AudioClip)) continue;
            auto aRes = assets.GetAudio(i);
            if (!aRes || aRes.Value()->rawAudio.empty()) continue;

            std::string fname = CleanAssetFileName(aRes.Value()->name) + "_" + std::to_string(e.pathId) + ".wav";
            auto dst = audioDir / fname;
            if (assets::AudioCodec::ExportWAV(*aRes.Value(), dst)) {
                if (options.generateMetaFiles) {
                    WriteMetaFile(dst, DeterministicGuid("audio:" + std::to_string(e.pathId)), "AudioImporter");
                    ++report.metaFilesGenerated;
                }
                ++report.audioExported;
            }
        }
    }

    // 5. Export TextAssets
    if (options.exportTextAssets) {
        for (size_t i = 0; i < assets.Entries().size(); ++i) {
            const auto& e = assets.Entries()[i];
            if (e.classId != static_cast<i32>(unity::ClassID::TextAsset)) continue;
            auto tRes = assets.GetTextAssetContent(i);
            if (!tRes) continue;
            auto dst = textDir / (CleanAssetFileName(e.name) + ".txt");
            if (platform::WriteFileText(dst, tRes.Value())) {
                if (options.generateMetaFiles) {
                    WriteMetaFile(dst, DeterministicGuid("txt:" + std::to_string(e.pathId)), "TextScriptImporter");
                    ++report.metaFilesGenerated;
                }
                ++report.textAssetsExported;
            }
        }
    }

    // 6. Export Decompiled Scripts (.cs + .meta)
    if (options.exportScripts) {
        auto scRes = decompiler.ExportAllScripts(scriptsDir, db);
        if (scRes) {
            report.scriptsExported = scRes.Value();
            if (options.generateMetaFiles) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir, ec)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".cs") {
                        WriteMetaFile(entry.path(), DeterministicGuid("cs:" + entry.path().filename().string()), "MonoImporter");
                        ++report.metaFilesGenerated;
                    }
                }
            }
        }
        log("Exported " + std::to_string(report.scriptsExported) + " decompiled C# scripts");
    }

    // 7. Export Unity Scene (.unity YAML + .meta)
    if (options.exportScenes && !sceneDoc.Nodes().empty()) {
        std::string sname = CleanAssetFileName(sceneDoc.SceneName()) + ".unity";
        auto scenePath = scenesDir / sname;
        auto sRes = sceneDoc.ExportUnitySceneYaml(scenePath, meshGuidMap, matGuidMap);
        if (sRes) {
            if (options.generateMetaFiles) {
                WriteMetaFile(scenePath, DeterministicGuid("scene:" + sname), "NativeFormatImporter");
                ++report.metaFilesGenerated;
            }
            ++report.scenesExported;
            log("Exported scene: " + scenePath.filename().string());
        }
    }

    // 8. Generate ProjectSettings & Packages/manifest.json for the target Unity version
    if (options.generateProjectSettings) {
        platform::WriteFileText(
            settingsDir / "ProjectVersion.txt",
            "m_EditorVersion: " + report.unityVersion + "\nm_EditorVersionWithRevision: " + report.unityVersion + " (coconut)\n");

        platform::WriteFileText(
            settingsDir / "ProjectSettings.asset",
            "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!129 &1\nPlayerSettings:\n"
            "  companyName: CoconutRecovered\n"
            "  productName: " + (project.gameName.empty() ? std::string("RecoveredGame") : project.gameName) + "\n"
            "  bundleVersion: 1.0.0\n");

        platform::WriteFileText(
            settingsDir / "EditorBuildSettings.asset",
            "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!1045 &1\nEditorBuildSettings:\n  m_Scenes:\n"
            "  - enabled: 1\n    path: Assets/Scenes/" + CleanAssetFileName(sceneDoc.SceneName()) + ".unity\n");

        nlohmann::json manifest;
        manifest["dependencies"] = {
            {"com.unity.ugui", "1.0.0"},
            {"com.unity.modules.physics", "1.0.0"},
            {"com.unity.modules.audio", "1.0.0"},
            {"com.unity.modules.jsonserialize", "1.0.0"}
        };
        if (UnityVersion::Parse(report.unityVersion).major >= 2019) {
            manifest["dependencies"]["com.unity.textmeshpro"] = "3.0.6";
        }
        platform::WriteFileText(packagesDir / "manifest.json", manifest.dump(2));
    }

    log("Native Unity project export complete -> " + outputDir.string());
    return Result<UnityExportReport>::Ok(std::move(report));
}

} // namespace coconut::exporter
