#pragma once

#include "core/Types.h"
#include <filesystem>

namespace coconut::unity {

// Generates real, spec-compliant Unity game builds (Mono or IL2CPP) on disk
// with real UnityFS AssetBundles, SerializedFile v22 + full TypeTrees, 3D scene
// (including static-batched combined meshes & repeated hierarchies for fixup),
// Texture2D, Materials, AudioClip, TextAsset, and real .NET PE/CLI Managed DLL
// (or IL2CPP global-metadata.dat + ELF64/PE64 native binary).
class SampleGameGenerator {
public:
    // Generates a complete Windows-layout Mono Unity game build in outDir.
    static Result<std::filesystem::path> GenerateMonoDemoGame(const std::filesystem::path& outDir);

    // Generates a complete Android-layout IL2CPP APK archive (.apk) with global-metadata.dat + ARM64 libil2cpp.so.
    static Result<std::filesystem::path> GenerateIl2CppDemoApk(const std::filesystem::path& outApkPath);

    // Builds a valid minimal ECMA-335 .NET PE/CLI DLL in memory with real CIL methods.
    static ByteBuffer BuildSampleManagedAssemblyDll();

    // Builds a valid IL2CPP global-metadata.dat (v29) + ARM64 ELF64 libil2cpp.so pair.
    static std::pair<ByteBuffer, ByteBuffer> BuildSampleIl2CppPair();
};

} // namespace coconut::unity
