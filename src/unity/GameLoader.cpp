#include "unity/GameLoader.h"
#include "core/Compression.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cctype>

namespace coconut::unity {

namespace {

std::string ToLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool EndsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StartsWith(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

std::string BaseFilename(std::string_view p) {
    size_t slash = p.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string(p) : std::string(p.substr(slash + 1));
}

bool IsLikelySerializedFilename(std::string_view lowerName) {
    return lowerName == "globalgamemanagers" ||
           lowerName == "globalgamemanagers.assets" ||
           lowerName == "resources.assets" ||
           lowerName == "maindata" ||
           StartsWith(lowerName, "level") ||
           StartsWith(lowerName, "sharedassets") ||
           EndsWith(lowerName, ".assets") ||
           EndsWith(lowerName, ".unity3d") ||
           EndsWith(lowerName, ".bundle") ||
           EndsWith(lowerName, ".assetbundle") ||
           EndsWith(lowerName, ".ab");
}

} // namespace

SerializedFile* LoadedGameProject::FindSerializedFile(std::string_view fileName) {
    std::string base = ToLower(BaseFilename(fileName));
    for (auto& sf : serializedFiles) {
        if (ToLower(BaseFilename(sf.name)) == base) return &sf;
    }
    return nullptr;
}

const SerializedFile* LoadedGameProject::FindSerializedFile(std::string_view fileName) const {
    std::string base = ToLower(BaseFilename(fileName));
    for (const auto& sf : serializedFiles) {
        if (ToLower(BaseFilename(sf.name)) == base) return &sf;
    }
    return nullptr;
}

const ByteBuffer* LoadedGameProject::FindResourceStream(std::string_view resourcePath) const {
    std::string base = BaseFilename(resourcePath);
    auto it = resourceStreams.find(base);
    if (it != resourceStreams.end()) return &it->second;
    std::string lowerBase = ToLower(base);
    for (const auto& [k, v] : resourceStreams) {
        if (ToLower(k) == lowerBase) return &v;
    }
    return nullptr;
}

size_t LoadedGameProject::TotalObjectCount() const {
    size_t total = 0;
    for (const auto& sf : serializedFiles) total += sf.objects.size();
    return total;
}

Result<LoadedGameProject> GameLoader::Load(const std::filesystem::path& targetPath) {
    if (!std::filesystem::exists(targetPath)) {
        return Result<LoadedGameProject>::Err("Target path does not exist: " + targetPath.string());
    }

    if (std::filesystem::is_directory(targetPath)) {
        return LoadDirectory(targetPath);
    }

    std::string ext = ToLower(targetPath.extension().string());
    if (ext == ".apk" || ext == ".aab" || ext == ".zip" || ext == ".xapk") {
        return LoadApkOrZip(targetPath);
    }
    return LoadSingleFile(targetPath);
}

void GameLoader::IngestFileBytes(
    LoadedGameProject& project,
    const std::string& virtualName,
    const std::filesystem::path& diskPath,
    ByteBuffer bytes) {
    std::string base = BaseFilename(virtualName);
    std::string lower = ToLower(base);

    if (EndsWith(lower, ".ress") || EndsWith(lower, ".resource")) {
        project.resourceStreams[base] = std::move(bytes);
        return;
    }

    if (lower == "global-metadata.dat") {
        project.il2cppMetadataData = std::move(bytes);
        project.backend = ScriptingBackend::IL2CPP;
        COCO_LOG_INFO("GameLoader", "Detected IL2CPP global-metadata.dat (" +
                                    std::to_string(project.il2cppMetadataData.size()) + " bytes)");
        return;
    }

    if (lower == "gameassembly.dll" || lower == "libil2cpp.so" || lower == "gameassembly.dylib" || lower == "gameassembly.so") {
        if (project.il2cppBinaryData.empty() || virtualName.find("arm64") != std::string::npos) {
            project.il2cppBinaryName = virtualName;
            project.il2cppBinaryData = std::move(bytes);
            project.backend = ScriptingBackend::IL2CPP;
            COCO_LOG_INFO("GameLoader", "Loaded IL2CPP binary: " + virtualName);
        }
        return;
    }

    if (EndsWith(lower, ".dll")) {
        // Skip native Unity runtime DLLs when scanning Managed/ assemblies
        if (lower != "unityplayer.dll" && lower != "mono-2.0-bdwgc.dll" && lower != "monoposixhelper.dll") {
            AssemblyFile af;
            af.name = base;
            af.diskPath = diskPath;
            af.data = std::move(bytes);
            project.managedAssemblies.push_back(std::move(af));
            if (project.backend == ScriptingBackend::Unknown) {
                project.backend = ScriptingBackend::Mono;
            }
        }
        return;
    }

    if (UnityFSBundle::IsUnityFS(bytes)) {
        auto bundleRes = UnityFSBundle::Parse(bytes);
        if (bundleRes) {
            auto& bundle = bundleRes.Value();
            if (!project.unityVersion.IsValid() && !bundle.header.unityRevision.empty()) {
                project.unityVersion = UnityVersion::Parse(bundle.header.unityRevision);
            }
            for (const auto& node : bundle.nodes) {
                std::string nodeLower = ToLower(node.path);
                if (EndsWith(nodeLower, ".ress") || EndsWith(nodeLower, ".resource")) {
                    project.resourceStreams[BaseFilename(node.path)] = node.data;
                } else if (SerializedFile::LooksLikeSerializedFile(node.data)) {
                    auto sfRes = SerializedFile::Parse(node.data, node.path);
                    if (sfRes) {
                        auto& sf = sfRes.Value();
                        if (!project.unityVersion.IsValid() && sf.unityVersion.IsValid()) {
                            project.unityVersion = sf.unityVersion;
                        }
                        if (project.platform == TargetPlatform::Unknown && sf.targetPlatform != TargetPlatform::Unknown) {
                            project.platform = sf.targetPlatform;
                        }
                        project.serializedFiles.push_back(std::move(sf));
                    }
                }
            }
            project.bundles.push_back(std::move(bundle));
        } else {
            COCO_LOG_WARN("GameLoader", "Failed to parse UnityFS '" + virtualName + "': " + bundleRes.Error());
        }
        return;
    }

    if (SerializedFile::LooksLikeSerializedFile(bytes)) {
        auto sfRes = SerializedFile::Parse(bytes, base);
        if (sfRes) {
            auto& sf = sfRes.Value();
            sf.diskPath = diskPath;
            if (!project.unityVersion.IsValid() && sf.unityVersion.IsValid()) {
                project.unityVersion = sf.unityVersion;
            }
            if (project.platform == TargetPlatform::Unknown && sf.targetPlatform != TargetPlatform::Unknown) {
                project.platform = sf.targetPlatform;
            }
            project.serializedFiles.push_back(std::move(sf));
        }
    }
}

Result<LoadedGameProject> GameLoader::LoadApkOrZip(const std::filesystem::path& apkPath) {
    ZipArchive zip;
    auto openRes = zip.OpenFile(apkPath);
    if (!openRes) return Result<LoadedGameProject>::Err(openRes.Error());

    LoadedGameProject project;
    project.gameName = apkPath.stem().string();
    project.rootPath = apkPath;
    project.dataPath = apkPath;
    project.isApkArchive = true;
    project.platform = TargetPlatform::Android;

    auto entries = zip.ListEntries();
    COCO_LOG_INFO("GameLoader", "Scanning APK/ZIP '" + apkPath.filename().string() + "' (" +
                                std::to_string(entries.size()) + " entries)");

    for (const auto& entry : entries) {
        if (entry.isDirectory) continue;
        std::string lower = ToLower(entry.name);
        std::string base = ToLower(BaseFilename(entry.name));

        bool interesting =
            (StartsWith(lower, "assets/bin/data/") &&
             (IsLikelySerializedFilename(base) || EndsWith(base, ".ress") || EndsWith(base, ".resource") ||
              EndsWith(base, ".dll") || base == "global-metadata.dat")) ||
            base == "libil2cpp.so" ||
            StartsWith(lower, "assets/aa/") ||
            EndsWith(base, ".bundle");

        if (!interesting) continue;

        auto dataRes = zip.ReadEntryByIndex(entry.index);
        if (dataRes) {
            IngestFileBytes(project, entry.name, {}, std::move(dataRes.Value()));
        }
    }

    return Result<LoadedGameProject>::Ok(std::move(project));
}

Result<LoadedGameProject> GameLoader::LoadDirectory(const std::filesystem::path& dirPath) {
    LoadedGameProject project;
    project.rootPath = dirPath;
    project.gameName = dirPath.filename().string();
    project.dataPath = dirPath;

    // Detect standard Unity build layouts:
    // Windows/Linux: <Game>/<Game>_Data/
    // macOS:         <Game>.app/Contents/Resources/Data/
    // Android unpack:<Game>/assets/bin/Data/
    std::error_code ec;
    if (std::filesystem::exists(dirPath / "Contents" / "Resources" / "Data", ec)) {
        project.dataPath = dirPath / "Contents" / "Resources" / "Data";
        project.platform = TargetPlatform::StandaloneOSX;
    } else if (std::filesystem::exists(dirPath / "assets" / "bin" / "Data", ec)) {
        project.dataPath = dirPath / "assets" / "bin" / "Data";
        project.platform = TargetPlatform::Android;
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (EndsWith(name, "_Data")) {
                    project.dataPath = entry.path();
                    project.gameName = name.substr(0, name.size() - 5);
                    break;
                }
            }
        }
    }

    COCO_LOG_INFO("GameLoader", "Loading Unity directory: " + project.rootPath.string() +
                                " (Data: " + project.dataPath.string() + ")");

    for (const auto& entry : std::filesystem::recursive_directory_iterator(project.rootPath, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        std::string lower = ToLower(fname);
        std::string parentLower = ToLower(entry.path().parent_path().filename().string());

        bool shouldRead =
            IsLikelySerializedFilename(lower) ||
            EndsWith(lower, ".ress") ||
            EndsWith(lower, ".resource") ||
            lower == "global-metadata.dat" ||
            lower == "gameassembly.dll" ||
            lower == "libil2cpp.so" ||
            lower == "gameassembly.dylib" ||
            (EndsWith(lower, ".dll") && (parentLower == "managed" || parentLower == "assemblies"));

        if (!shouldRead) continue;

        auto bytes = platform::ReadFileBytes(entry.path());
        if (bytes) {
            IngestFileBytes(project, entry.path().string(), entry.path(), std::move(bytes.Value()));
        }
    }

    return Result<LoadedGameProject>::Ok(std::move(project));
}

Result<LoadedGameProject> GameLoader::LoadSingleFile(const std::filesystem::path& filePath) {
    auto bytes = platform::ReadFileBytes(filePath);
    if (!bytes) return Result<LoadedGameProject>::Err(bytes.Error());

    LoadedGameProject project;
    project.gameName = filePath.stem().string();
    project.rootPath = filePath.parent_path();
    project.dataPath = filePath.parent_path();

    IngestFileBytes(project, filePath.filename().string(), filePath, std::move(bytes.Value()));
    return Result<LoadedGameProject>::Ok(std::move(project));
}

} // namespace coconut::unity
