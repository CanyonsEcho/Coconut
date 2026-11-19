#include "core/ProjectManager.h"
#include "core/Compression.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace coconut {

namespace {

std::string NowUtcIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M UTC");
    return oss.str();
}

std::string MakeProjectId(const std::string& name, const std::filesystem::path& path) {
    u64 h = 0xcbf29ce484222325ULL;
    for (unsigned char c : name) { h ^= c; h *= 0x100000001b3ULL; }
    for (unsigned char c : path.string()) { h ^= c; h *= 0x100000001b3ULL; }
    std::ostringstream oss;
    oss << "proj_" << std::hex << std::setw(12) << std::setfill('0') << (h & 0xFFFFFFFFFFFFULL);
    return oss.str();
}

std::string ClassifyFileCategory(const std::string& nameLower, const std::string& extLower) {
    if (nameLower.rfind("level", 0) == 0 && extLower.empty()) return "Scene";
    if (extLower == ".assets" || nameLower == "globalgamemanagers") return "Assets";
    if (extLower == ".bundle" || extLower == ".unity3d" || extLower == ".ab") return "Bundle";
    if (extLower == ".dll" || extLower == ".exe") return "Assembly";
    if (nameLower == "global-metadata.dat") return "Metadata";
    if (extLower == ".so" || extLower == ".dylib") return "Native";
    if (extLower == ".ress" || extLower == ".resource") return "Resource";
    return "File";
}

std::string ToLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::filesystem::path ProjectManager::DefaultRegistryPath() {
#if defined(_WIN32)
    if (const char* appData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(appData) / "Coconut" / "projects.json";
    }
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(userProfile) / ".coconut" / "projects.json";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "coconut" / "projects.json";
    }
#endif
    return platform::GetExecutableDir() / "coconut_projects.json";
}

ProjectManager::ProjectManager(std::filesystem::path registryPath)
    : registryPath_(std::move(registryPath)) {}

Result<void> ProjectManager::Load() {
    projects_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(registryPath_, ec)) {
        return Result<void>::Ok();
    }
    auto textRes = platform::ReadFileText(registryPath_);
    if (!textRes) return Result<void>::Err(textRes.Error());

    try {
        nlohmann::json j = nlohmann::json::parse(textRes.Value());
        if (j.contains("projects") && j["projects"].is_array()) {
            for (const auto& item : j["projects"]) {
                CoconutProjectEntry e;
                e.id                   = item.value("id", "");
                e.name                 = item.value("name", "Untitled Project");
                e.gameBuildPath        = item.value("gameBuildPath", "");
                e.detectedUnityVersion = item.value("detectedUnityVersion", "Unknown");
                e.detectedPlatform     = item.value("detectedPlatform", "Unknown");
                e.detectedBackend      = item.value("detectedBackend", "Unknown");
                e.lastOpenedUtc        = item.value("lastOpenedUtc", "");
                e.notes                = item.value("notes", "");
                if (!e.id.empty() && !e.gameBuildPath.empty()) {
                    projects_.push_back(std::move(e));
                }
            }
        }
    } catch (const std::exception& ex) {
        return Result<void>::Err(std::string("Failed to parse projects.json: ") + ex.what());
    }
    return Result<void>::Ok();
}

Result<void> ProjectManager::Save() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : projects_) {
        arr.push_back({
            {"id", p.id},
            {"name", p.name},
            {"gameBuildPath", p.gameBuildPath.string()},
            {"detectedUnityVersion", p.detectedUnityVersion},
            {"detectedPlatform", p.detectedPlatform},
            {"detectedBackend", p.detectedBackend},
            {"lastOpenedUtc", p.lastOpenedUtc},
            {"notes", p.notes}
        });
    }
    nlohmann::json root;
    root["formatVersion"] = 2;
    root["projects"] = arr;
    return platform::WriteFileText(registryPath_, root.dump(2));
}

CoconutProjectEntry* ProjectManager::FindById(const std::string& id) {
    for (auto& p : projects_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const CoconutProjectEntry* ProjectManager::FindById(const std::string& id) const {
    for (const auto& p : projects_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

Result<CoconutProjectEntry> ProjectManager::AddProject(
    const std::string& name,
    const std::filesystem::path& gameBuildPath,
    const std::string& notes) {
    std::error_code ec;
    if (gameBuildPath.empty() || !std::filesystem::exists(gameBuildPath, ec)) {
        return Result<CoconutProjectEntry>::Err("Game build path does not exist: " + gameBuildPath.string());
    }

    auto probe = ProbeBuild(gameBuildPath, 64);
    CoconutProjectEntry entry;
    entry.name = name.empty() ? probe.suggestedName : name;
    entry.gameBuildPath = std::filesystem::weakly_canonical(gameBuildPath, ec);
    if (ec || entry.gameBuildPath.empty()) entry.gameBuildPath = gameBuildPath;
    entry.id = MakeProjectId(entry.name, entry.gameBuildPath);
    entry.detectedUnityVersion = probe.unityVersion.empty() ? "Auto-Detect" : probe.unityVersion;
    entry.detectedPlatform = probe.platform.empty() ? "Unknown" : probe.platform;
    entry.detectedBackend = probe.backend.empty() ? "Unknown" : probe.backend;
    entry.lastOpenedUtc = NowUtcIso();
    entry.notes = notes;

    // Replace existing entry with same ID/path if present
    for (auto& existing : projects_) {
        if (existing.id == entry.id || existing.gameBuildPath == entry.gameBuildPath) {
            existing = entry;
            Save();
            return Result<CoconutProjectEntry>::Ok(entry);
        }
    }

    projects_.insert(projects_.begin(), entry);
    Save();
    return Result<CoconutProjectEntry>::Ok(entry);
}

bool ProjectManager::RemoveProject(const std::string& id) {
    auto it = std::remove_if(projects_.begin(), projects_.end(),
                             [&](const CoconutProjectEntry& p) { return p.id == id; });
    if (it == projects_.end()) return false;
    projects_.erase(it, projects_.end());
    Save();
    return true;
}

bool ProjectManager::RenameProject(const std::string& id, const std::string& newName) {
    if (newName.empty()) return false;
    auto* p = FindById(id);
    if (!p) return false;
    p->name = newName;
    Save();
    return true;
}

void ProjectManager::TouchLastOpened(const std::string& id,
                                     const std::string& unityVersion,
                                     const std::string& platform,
                                     const std::string& backend) {
    auto* p = FindById(id);
    if (!p) return;
    p->lastOpenedUtc = NowUtcIso();
    if (!unityVersion.empty()) p->detectedUnityVersion = unityVersion;
    if (!platform.empty()) p->detectedPlatform = platform;
    if (!backend.empty()) p->detectedBackend = backend;
    Save();
}

BuildProbeInfo ProjectManager::ProbeBuild(const std::filesystem::path& buildPath, size_t maxEntries) {
    BuildProbeInfo info;
    std::error_code ec;
    if (!std::filesystem::exists(buildPath, ec)) return info;

    info.valid = true;
    info.suggestedName = buildPath.stem().string();
    if (info.suggestedName.empty()) info.suggestedName = buildPath.filename().string();
    info.platform = "Unknown";
    info.backend = "Unknown";
    info.unityVersion = "Unknown";

    bool hasMono = false;
    bool hasIl2Cpp = false;
    bool hasExe = false;
    bool hasSo = false;
    bool hasMacApp = false;

    std::string ext = ToLowerStr(buildPath.extension().string());
    if (std::filesystem::is_regular_file(buildPath, ec) &&
        (ext == ".apk" || ext == ".aab" || ext == ".zip")) {
        info.platform = "Android";
        info.totalBytes = std::filesystem::file_size(buildPath, ec);
        ZipArchive zip;
        if (zip.OpenFile(buildPath)) {
            auto entries = zip.ListEntries();
            info.fileCount = entries.size();
            for (const auto& ze : entries) {
                std::string low = ToLowerStr(ze.name);
                if (low.find("libil2cpp.so") != std::string::npos ||
                    low.find("global-metadata.dat") != std::string::npos) {
                    hasIl2Cpp = true;
                }
                if (low.find("managed/") != std::string::npos && low.ends_with(".dll")) {
                    hasMono = true;
                }
                if (info.tree.size() < maxEntries && !ze.isDirectory) {
                    std::filesystem::path p(ze.name);
                    std::string fname = p.filename().string();
                    int depth = 0;
                    for (char c : ze.name) if (c == '/') ++depth;
                    info.tree.push_back({
                        fname,
                        ze.name,
                        ClassifyFileCategory(ToLowerStr(fname), ToLowerStr(p.extension().string())),
                        ze.uncompressedSize,
                        depth,
                        false
                    });
                }
            }
        }
    } else if (std::filesystem::is_directory(buildPath, ec)) {
        if (ext == ".app") hasMacApp = true;

        std::vector<std::filesystem::path> entries;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 buildPath, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (it.depth() > 4) {
                it.disable_recursion_pending();
                continue;
            }
            entries.push_back(it->path());
            if (entries.size() >= 1200) break;
        }
        std::sort(entries.begin(), entries.end());

        for (const auto& p : entries) {
            bool isDir = std::filesystem::is_directory(p, ec);
            u64 sz = isDir ? 0 : std::filesystem::file_size(p, ec);
            if (!isDir) {
                info.fileCount++;
                info.totalBytes += sz;
            }
            auto rel = std::filesystem::relative(p, buildPath, ec);
            std::string relStr = rel.generic_string();
            std::string fname = p.filename().string();
            std::string lowName = ToLowerStr(fname);
            std::string lowExt = ToLowerStr(p.extension().string());

            if (lowExt == ".exe" || lowName == "unityplayer.dll" || lowName == "gameassembly.dll") hasExe = true;
            if (lowName == "libil2cpp.so" || lowName == "gameassembly.dll" || lowName == "global-metadata.dat") hasIl2Cpp = true;
            if (lowName == "assembly-csharp.dll" || (relStr.find("Managed/") != std::string::npos && lowExt == ".dll")) hasMono = true;
            if (lowName == "unityplayer.so" || lowExt == ".x86_64") hasSo = true;
            if (lowName == "info.plist" || lowExt == ".dylib") hasMacApp = true;

            if (info.tree.size() < maxEntries) {
                int depth = 0;
                for (char c : relStr) if (c == '/') ++depth;
                info.tree.push_back({
                    fname,
                    relStr,
                    isDir ? "Folder" : ClassifyFileCategory(lowName, lowExt),
                    sz,
                    depth,
                    isDir
                });
            }
        }

        if (hasMacApp) info.platform = "macOS";
        else if (hasExe) info.platform = "Windows";
        else if (hasSo) info.platform = "Linux";
        else if (std::filesystem::exists(buildPath / "assets" / "bin" / "Data", ec)) info.platform = "Android";
    } else if (std::filesystem::is_regular_file(buildPath, ec)) {
        info.fileCount = 1;
        info.totalBytes = std::filesystem::file_size(buildPath, ec);
        std::string fname = buildPath.filename().string();
        info.tree.push_back({
            fname,
            fname,
            ClassifyFileCategory(ToLowerStr(fname), ext),
            info.totalBytes,
            0,
            false
        });
    }

    if (hasIl2Cpp) info.backend = "IL2CPP";
    else if (hasMono) info.backend = "Mono";

    return info;
}

} // namespace coconut
