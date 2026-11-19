#include "export/AssetRipperClient.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <thread>

namespace coconut::exporter {

namespace {

std::string UrlEncode(std::string_view s) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << static_cast<char>(c);
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

} // namespace

std::optional<std::filesystem::path> AssetRipperClient::DetectAssetRipperBinary() {
    std::vector<std::filesystem::path> candidates;
    auto exeDir = platform::GetExecutableDir();

#if defined(_WIN32)
    candidates.push_back(exeDir / "AssetRipper.GUI.Free.exe");
    candidates.push_back(exeDir / "tools" / "AssetRipper" / "AssetRipper.GUI.Free.exe");
    candidates.push_back("C:/Tools/AssetRipper/AssetRipper.GUI.Free.exe");
    if (const char* env = std::getenv("ASSETRIPPER_PATH")) candidates.emplace_back(env);
#else
    candidates.push_back(exeDir / "AssetRipper.GUI.Free");
    candidates.push_back(exeDir / "tools" / "AssetRipper" / "AssetRipper.GUI.Free");
    candidates.push_back("/usr/local/bin/AssetRipper.GUI.Free");
    if (const char* env = std::getenv("ASSETRIPPER_PATH")) candidates.emplace_back(env);
#endif

    std::error_code ec;
    for (const auto& c : candidates) {
        if (!c.empty() && std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
            return c;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> AssetRipperClient::DetectRipperBridgeBinary() {
    auto exeDir = platform::GetExecutableDir();
    std::vector<std::filesystem::path> candidates = {
        exeDir / "Coconut.RipperBridge.exe",
        exeDir / "Coconut.RipperBridge",
        exeDir / "tools" / "Coconut.RipperBridge" / "Coconut.RipperBridge.exe",
        exeDir / "tools" / "Coconut.RipperBridge" / "Coconut.RipperBridge",
        exeDir.parent_path() / "tools" / "Coconut.RipperBridge" / "bin" / "Release" / "net10.0" / "Coconut.RipperBridge.dll"
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) return c;
    }
    return std::nullopt;
}

bool AssetRipperClient::PingServer(const std::string& host, u16 port) {
    auto resp = platform::HttpRequest("GET", host, port, "/", {}, {}, 1500);
    return resp.statusCode >= 200 && resp.statusCode < 500;
}

Result<AssetRipperRunResult> AssetRipperClient::ExportViaAssetRipper(
    const std::filesystem::path& gameInputPath,
    const std::filesystem::path& outputProjectDir,
    const AssetRipperConfig& config,
    std::function<void(const std::string&)> onLog) {
    AssetRipperRunResult result;
    result.exportedProjectDir = outputProjectDir;

    auto log = [&](const std::string& msg) {
        result.logs.push_back(msg);
        COCO_LOG_INFO("AssetRipper", msg);
        if (onLog) onLog(msg);
    };

    // Mode A: Drive Coconut.RipperBridge CLI (.NET 10 companion wrapping AssetRipper libraries)
    if (config.useRipperBridge) {
        auto bridgePath = !config.executablePath.empty()
                              ? std::optional<std::filesystem::path>(config.executablePath)
                              : DetectRipperBridgeBinary();
        if (bridgePath) {
            log("Invoking Coconut.RipperBridge: " + bridgePath->string());
            std::string exe = bridgePath->string();
            std::vector<std::string> args;
            if (bridgePath->extension() == ".dll") {
                exe = "dotnet";
                args.push_back(bridgePath->string());
            }
            args.push_back("--input");  args.push_back(gameInputPath.string());
            args.push_back("--output"); args.push_back(outputProjectDir.string());
            args.push_back("--unity-version"); args.push_back(config.targetUnityVersion);
            args.push_back("--script-level");  args.push_back(config.scriptContentLevel);

            auto proc = platform::RunProcess(exe, args, {}, log);
            if (proc.exitCode == 0) {
                result.usedAssetRipper = true;
                result.summary = "Exported Unity project via Coconut.RipperBridge (AssetRipper backend)";
                return Result<AssetRipperRunResult>::Ok(std::move(result));
            }
            log("Coconut.RipperBridge exited with code " + std::to_string(proc.exitCode));
        }
    }

    // Mode B: Drive running AssetRipper HTTP API (https://github.com/AssetRipper/AssetRipper --headless)
    bool serverUp = PingServer(config.host, config.port);
    if (serverUp) {
        log("Connected to AssetRipper headless HTTP API at http://" + config.host + ":" + std::to_string(config.port));

        // 1. Reset prior session
        platform::HttpRequest("POST", config.host, config.port, "/Reset", "", "application/x-www-form-urlencoded", 10000);

        // 2. Load target game folder / APK / bundle
        std::string loadBody = "path=" + UrlEncode(gameInputPath.string());
        auto loadResp = platform::HttpRequest("POST", config.host, config.port, "/LoadFolder", loadBody,
                                              "application/x-www-form-urlencoded", 120000);
        if (loadResp.statusCode < 200 || loadResp.statusCode >= 400) {
            // Try /LoadFile if single file (APK/bundle)
            loadResp = platform::HttpRequest("POST", config.host, config.port, "/LoadFile", loadBody,
                                             "application/x-www-form-urlencoded", 120000);
        }
        if (loadResp.statusCode >= 200 && loadResp.statusCode < 400) {
            log("AssetRipper loaded game successfully (HTTP " + std::to_string(loadResp.statusCode) + ")");

            // 3. Configure export settings
            std::string settingsBody =
                "ScriptContentLevel=" + UrlEncode(config.scriptContentLevel) +
                "&MeshExportFormat=" + UrlEncode(config.meshExportFormat) +
                "&AudioExportFormat=" + UrlEncode(config.audioExportFormat) +
                "&ImageExportFormat=" + UrlEncode(config.imageExportFormat);
            platform::HttpRequest("POST", config.host, config.port, "/Settings/Update", settingsBody,
                                  "application/x-www-form-urlencoded", 15000);

            // 4. Trigger full Unity project export
            std::string exportBody = "path=" + UrlEncode(outputProjectDir.string());
            auto expResp = platform::HttpRequest("POST", config.host, config.port, "/Export/UnityProject", exportBody,
                                                 "application/x-www-form-urlencoded", 600000);
            if (expResp.statusCode >= 200 && expResp.statusCode < 400) {
                result.usedAssetRipper = true;
                result.summary = "Exported full Unity project via AssetRipper headless HTTP API";
                log(result.summary);
                return Result<AssetRipperRunResult>::Ok(std::move(result));
            }
            log("AssetRipper /Export/UnityProject returned HTTP " + std::to_string(expResp.statusCode));
        } else {
            log("AssetRipper load failed: HTTP " + std::to_string(loadResp.statusCode) + " " + loadResp.error);
        }
    } else {
        auto bin = !config.executablePath.empty()
                       ? std::optional<std::filesystem::path>(config.executablePath)
                       : DetectAssetRipperBinary();
        if (bin) {
            log("Found AssetRipper binary at " + bin->string() +
                " (start with --headless --port " + std::to_string(config.port) + " for HTTP API mode)");
        } else {
            log("AssetRipper executable not detected locally; ready to fall back to Coconut native exporter.");
        }
    }

    return Result<AssetRipperRunResult>::Err("AssetRipper server/CLI unavailable (use native exporter fallback)");
}

} // namespace coconut::exporter
