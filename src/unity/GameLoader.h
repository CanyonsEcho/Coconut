#pragma once

#include "core/Types.h"
#include "unity/UnityFS.h"
#include "unity/SerializedFile.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace coconut::unity {

struct AssemblyFile {
    std::string name;
    std::filesystem::path diskPath;
    ByteBuffer data;
};

struct ResourceStreamFile {
    std::string name;
    ByteBuffer data;
};

struct LoadedGameProject {
    std::string gameName;
    std::filesystem::path rootPath;
    std::filesystem::path dataPath;
    bool isApkArchive = false;

    UnityVersion unityVersion;
    TargetPlatform platform = TargetPlatform::Unknown;
    ScriptingBackend backend = ScriptingBackend::Unknown;

    // Loaded SerializedFiles (from level*, sharedassets*, resources.assets, globalgamemanagers, or unpacked UnityFS bundles)
    std::vector<SerializedFile> serializedFiles;
    std::vector<UnityFSBundle> bundles;

    // Binary resource streams (.resS / .resource) keyed by filename
    std::unordered_map<std::string, ByteBuffer> resourceStreams;

    // Scripting artifacts
    std::vector<AssemblyFile> managedAssemblies; // Mono: Managed/*.dll
    std::string il2cppBinaryName;                // IL2CPP: GameAssembly.dll / libil2cpp.so / GameAssembly.dylib
    ByteBuffer il2cppBinaryData;
    ByteBuffer il2cppMetadataData;               // global-metadata.dat

    SerializedFile* FindSerializedFile(std::string_view fileName);
    const SerializedFile* FindSerializedFile(std::string_view fileName) const;
    const ByteBuffer* FindResourceStream(std::string_view resourcePath) const;

    size_t TotalObjectCount() const;
};

class GameLoader {
public:
    // Load any Unity game build directory (Windows *_Data, Linux *_Data, macOS .app),
    // Android APK/AAB/ZIP, individual UnityFS bundle, or single SerializedFile (.assets / level*).
    static Result<LoadedGameProject> Load(const std::filesystem::path& targetPath);

private:
    static Result<LoadedGameProject> LoadApkOrZip(const std::filesystem::path& apkPath);
    static Result<LoadedGameProject> LoadDirectory(const std::filesystem::path& dirPath);
    static Result<LoadedGameProject> LoadSingleFile(const std::filesystem::path& filePath);
    static void IngestFileBytes(LoadedGameProject& project, const std::string& virtualName,
                                const std::filesystem::path& diskPath, ByteBuffer bytes);
};

} // namespace coconut::unity
