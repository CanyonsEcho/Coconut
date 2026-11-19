#pragma once

#include "core/Types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut {

struct BuildTreeNode {
    std::string name;
    std::string relativePath;
    std::string category; // "Folder", "Scene", "Assets", "Bundle", "Assembly", "Metadata", "Native", "Resource", "File"
    u64 sizeBytes = 0;
    int depth = 0;
    bool isDirectory = false;
};

struct CoconutProjectEntry {
    std::string id;
    std::string name;
    std::filesystem::path gameBuildPath;
    std::string detectedUnityVersion;
    std::string detectedPlatform; // "Android", "Windows", "Linux", "macOS", "Unknown"
    std::string detectedBackend;  // "Mono", "IL2CPP", "Unknown"
    std::string lastOpenedUtc;
    std::string notes;
};

struct BuildProbeInfo {
    bool valid = false;
    std::string suggestedName;
    std::string unityVersion;
    std::string platform;
    std::string backend;
    size_t fileCount = 0;
    u64 totalBytes = 0;
    std::vector<BuildTreeNode> tree;
};

// Ghidra-style Project Manager:
// Maps user-defined project names directly to Unity game build folders or Android APKs,
// persisted as JSON in the user's local Coconut config directory.
class ProjectManager {
public:
    static std::filesystem::path DefaultRegistryPath();

    explicit ProjectManager(std::filesystem::path registryPath = DefaultRegistryPath());

    Result<void> Load();
    Result<void> Save() const;

    const std::vector<CoconutProjectEntry>& Projects() const { return projects_; }
    CoconutProjectEntry* FindById(const std::string& id);
    const CoconutProjectEntry* FindById(const std::string& id) const;

    // Create a new named project pointing to a game build folder or APK
    Result<CoconutProjectEntry> AddProject(const std::string& name,
                                           const std::filesystem::path& gameBuildPath,
                                           const std::string& notes = {});
    bool RemoveProject(const std::string& id);
    bool RenameProject(const std::string& id, const std::string& newName);
    void TouchLastOpened(const std::string& id,
                         const std::string& unityVersion = {},
                         const std::string& platform = {},
                         const std::string& backend = {});

    // Fast non-invasive scan of a game build directory or .apk/.aab for the Project Manager tree view
    static BuildProbeInfo ProbeBuild(const std::filesystem::path& buildPath, size_t maxEntries = 250);

private:
    std::filesystem::path registryPath_;
    std::vector<CoconutProjectEntry> projects_;
};

} // namespace coconut
