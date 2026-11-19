#pragma once

#include "core/Types.h"
#include <filesystem>
#include <functional>
#include <string>

namespace coconut::exporter {

struct AssetRipperConfig {
    std::filesystem::path executablePath; // Optional path to AssetRipper.GUI.Free(.exe) or Coconut.RipperBridge
    std::string host = "127.0.0.1";
    u16 port = 52890;
    std::string targetUnityVersion = "2022.3.15f1";
    std::string scriptContentLevel = "Level2"; // Level0..Level2
    std::string meshExportFormat = "Native";
    std::string audioExportFormat = "Default";
    std::string imageExportFormat = "Png";
    bool useRipperBridge = false;
    bool autoFallbackToNative = true;
};

struct AssetRipperRunResult {
    bool usedAssetRipper = false;
    bool usedNativeFallback = false;
    std::filesystem::path exportedProjectDir;
    std::string summary;
    std::vector<std::string> logs;
};

// Integrates with https://github.com/AssetRipper/AssetRipper via:
//   1. AssetRipper headless HTTP API (AssetRipper.GUI.Free --headless --port <port> -> /LoadFolder, /Settings/Update, /Export/UnityProject)
//   2. Optional Coconut.RipperBridge .NET 10 CLI companion
//   3. Built-in native C++ UnityProjectExporter fallback so export always succeeds!
class AssetRipperClient {
public:
    // Auto-discover installed AssetRipper or Coconut.RipperBridge binaries in PATH / standard locations
    static std::optional<std::filesystem::path> DetectAssetRipperBinary();
    static std::optional<std::filesystem::path> DetectRipperBridgeBinary();

    // Check if an AssetRipper headless HTTP server is already responding on host:port
    static bool PingServer(const std::string& host, u16 port);

    // Execute export using AssetRipper (HTTP API or RipperBridge)
    static Result<AssetRipperRunResult> ExportViaAssetRipper(
        const std::filesystem::path& gameInputPath,
        const std::filesystem::path& outputProjectDir,
        const AssetRipperConfig& config,
        std::function<void(const std::string&)> onLog = nullptr);
};

} // namespace coconut::exporter
