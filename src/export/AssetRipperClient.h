#pragma once

#include "core/Types.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace coconut::exporter {

struct AssetRipperConfig {
    std::filesystem::path executablePath; // Optional override for internal Coconut.RipperCore(.exe)
    std::string targetUnityVersion = "Match Source";
    std::string scriptExportMode = "hybrid";   // decompiled | hybrid | dll | stubs
    std::string meshExportFormat = "fbx";      // fbx | glb | obj | native
    std::string audioExportFormat = "wav";     // wav | mp3 | m4a | ogg | native
    std::string imageExportFormat = "png";     // png | tga | bmp | jpg | exr
    bool decombineMeshes = true;
    bool reconstructPrefabs = true;
    bool fixBuildErrors = true;
    bool autoFallbackToNative = true;
};

struct AssetRipperRunResult {
    bool usedInternalRipper = false;
    bool usedNativeFallback = false;
    std::filesystem::path exportedProjectDir;
    std::string sourceUnityVersion;
    std::string targetUnityVersion;
    size_t scenesExported = 0;
    size_t scriptsExported = 0;
    size_t meshesExported = 0;
    size_t audioExported = 0;
    size_t texturesExported = 0;
    size_t meshesDecombined = 0;
    size_t prefabsReconstructed = 0;
    size_t buildErrorsFixed = 0;
    size_t shadersFixed = 0;
    std::string summary;
    std::vector<std::string> logs;
};

// Drives Coconut's internal AssetRipper 1.3.14 fork (internal/Coconut.RipperCore)
// with custom static-mesh separation, prefab reconstruction, FBX/OBJ/GLB/M4A exporters,
// target Unity version retargeting, and automated build-error fixups.
class AssetRipperClient {
public:
    // Auto-discover internal Coconut.RipperCore executable or DLL
    static std::optional<std::filesystem::path> DetectInternalRipperCore();

    // Execute full export + fixup via internal Coconut.RipperCore
    static Result<AssetRipperRunResult> ExportViaInternalRipper(
        const std::filesystem::path& gameInputPath,
        const std::filesystem::path& outputProjectDir,
        const AssetRipperConfig& config,
        std::function<void(const std::string&)> onLog = nullptr);
};

} // namespace coconut::exporter
