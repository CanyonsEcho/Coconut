#include "export/AssetRipperClient.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

namespace coconut::exporter {

std::optional<std::filesystem::path> AssetRipperClient::DetectInternalRipperCore() {
    auto exeDir = platform::GetExecutableDir();
    std::vector<std::filesystem::path> candidates;

#if defined(_WIN32)
    const char* exeName = "Coconut.RipperCore.exe";
#else
    const char* exeName = "Coconut.RipperCore";
#endif

    if (const char* env = std::getenv("COCONUT_RIPPER_CORE")) {
        candidates.emplace_back(env);
    }

    // Bundled next to coconut executable
    candidates.push_back(exeDir / "ripper" / exeName);
    candidates.push_back(exeDir / "ripper" / "Coconut.RipperCore.dll");
    candidates.push_back(exeDir / exeName);

    // Development / repository build outputs (walking up 1-3 parent dirs from build/)
    auto cur = exeDir;
    for (int i = 0; i < 4 && !cur.empty(); ++i) {
        candidates.push_back(cur / "internal" / "Coconut.RipperCore" / "bin" / "Release" / "net10.0" / exeName);
        candidates.push_back(cur / "internal" / "Coconut.RipperCore" / "bin" / "x64" / "Release" / "net10.0" / exeName);
        candidates.push_back(cur / "internal" / "Coconut.RipperCore" / "bin" / "Release" / "net10.0" / "Coconut.RipperCore.dll");
        candidates.push_back(cur / "internal" / "Coconut.RipperCore" / "bin" / "x64" / "Release" / "net10.0" / "Coconut.RipperCore.dll");
        candidates.push_back(cur / "internal" / "Coconut.RipperCore" / "bin" / "Debug" / "net10.0" / exeName);
        if (cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }

    std::error_code ec;
    for (const auto& c : candidates) {
        if (!c.empty() && std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
            return c;
        }
    }
    return std::nullopt;
}

Result<AssetRipperRunResult> AssetRipperClient::ExportViaInternalRipper(
    const std::filesystem::path& gameInputPath,
    const std::filesystem::path& outputProjectDir,
    const AssetRipperConfig& config,
    std::function<void(const std::string&)> onLog) {
    AssetRipperRunResult result;
    result.exportedProjectDir = outputProjectDir;

    auto log = [&](const std::string& msg) {
        result.logs.push_back(msg);
        COCO_LOG_INFO("RipperCore", msg);
        if (onLog) onLog(msg);
    };

    auto ripperPath = !config.executablePath.empty()
                          ? std::optional<std::filesystem::path>(config.executablePath)
                          : DetectInternalRipperCore();
    if (!ripperPath) {
        log("Internal Coconut.RipperCore binary not found; falling back to Coconut native C++ exporter.");
        return Result<AssetRipperRunResult>::Err("Coconut.RipperCore not built/found");
    }

    std::error_code ec;
    std::filesystem::create_directories(outputProjectDir, ec);
    auto reportPath = outputProjectDir / ".coconut_ripper_report.json";

    std::string exe = ripperPath->string();
    std::vector<std::string> args;
    if (ripperPath->extension() == ".dll") {
        exe = "dotnet";
        args.push_back(ripperPath->string());
    }

    // Map mp3 -> wav for C# core (C++ post-pass encodes MP3 via shine when mp3 requested)
    std::string ripperAudioFmt = config.audioExportFormat;
    if (ripperAudioFmt == "mp3") ripperAudioFmt = "wav";

    args.push_back("export");
    args.push_back("--input");               args.push_back(gameInputPath.string());
    args.push_back("--output");              args.push_back(outputProjectDir.string());
    args.push_back("--target-version");      args.push_back(config.targetUnityVersion);
    args.push_back("--mesh-format");         args.push_back(config.meshExportFormat);
    args.push_back("--audio-format");        args.push_back(ripperAudioFmt);
    args.push_back("--image-format");        args.push_back(config.imageExportFormat);
    args.push_back("--script-mode");         args.push_back(config.scriptExportMode);
    args.push_back("--decombine-meshes");    args.push_back(config.decombineMeshes ? "1" : "0");
    args.push_back("--reconstruct-prefabs"); args.push_back(config.reconstructPrefabs ? "1" : "0");
    args.push_back("--fix-build-errors");    args.push_back(config.fixBuildErrors ? "1" : "0");
    args.push_back("--json-report");         args.push_back(reportPath.string());

    log("Launching internal forked AssetRipper engine: " + ripperPath->filename().string());
    auto proc = platform::RunProcess(exe, args, {}, log);

    if (std::filesystem::exists(reportPath, ec)) {
        try {
            std::ifstream f(reportPath);
            nlohmann::json j = nlohmann::json::parse(f);
            bool ok = j.value("ok", false);
            result.sourceUnityVersion   = j.value("sourceUnityVersion", "");
            result.targetUnityVersion   = j.value("targetUnityVersion", config.targetUnityVersion);
            result.scenesExported       = j.value("scenesExported", size_t{0});
            result.scriptsExported      = j.value("scriptsExported", size_t{0});
            result.meshesExported       = j.value("meshesExported", size_t{0});
            result.audioExported        = j.value("audioExported", size_t{0});
            result.texturesExported     = j.value("texturesExported", size_t{0});
            result.meshesDecombined     = j.value("meshesDecombined", size_t{0});
            result.prefabsReconstructed = j.value("prefabsReconstructed", size_t{0});
            result.buildErrorsFixed     = j.value("buildErrorsFixed", size_t{0});
            result.shadersFixed         = j.value("shadersFixed", size_t{0});
            if (j.contains("outputProjectPath") && j["outputProjectPath"].is_string()) {
                std::string p = j["outputProjectPath"].get<std::string>();
                if (!p.empty()) result.exportedProjectDir = p;
            }
            if (j.contains("messages") && j["messages"].is_array()) {
                for (const auto& m : j["messages"]) {
                    if (m.is_string()) result.logs.push_back(m.get<std::string>());
                }
            }
            std::filesystem::remove(reportPath, ec);

            if (ok && proc.exitCode == 0) {
                result.usedInternalRipper = true;
                result.summary = "Exported & fixed up via Coconut Internal AssetRipper Core (1.3.14 Fork)";
                return Result<AssetRipperRunResult>::Ok(std::move(result));
            }
            std::string err = j.value("error", "Unknown RipperCore error");
            return Result<AssetRipperRunResult>::Err("Coconut.RipperCore error: " + err);
        } catch (const std::exception& e) {
            log(std::string("Failed to parse RipperCore report: ") + e.what());
        }
    }

    if (proc.exitCode == 0) {
        result.usedInternalRipper = true;
        result.summary = "Exported via Coconut Internal AssetRipper Core";
        return Result<AssetRipperRunResult>::Ok(std::move(result));
    }

    return Result<AssetRipperRunResult>::Err("Coconut.RipperCore exited with code " + std::to_string(proc.exitCode));
}

} // namespace coconut::exporter
