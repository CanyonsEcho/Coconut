#pragma once

#include "core/Types.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace coconut {

struct Bookmark {
    std::string id;
    std::string category; // "Code", "Asset", "Scene", "Hex"
    std::string target;   // Method full name, asset key, or hex offset
    std::string note;
};

// Ghidra-style persistent project annotation database (.coco JSON)
// Stores symbol renames, address/line comments, script overrides, and bookmarks.
class ProjectDatabase {
public:
    void Clear();

    void SetSymbolRename(const std::string& originalSymbol, const std::string& newName);
    std::optional<std::string> GetSymbolRename(const std::string& originalSymbol) const;

    void SetComment(const std::string& locationKey, const std::string& comment);
    std::optional<std::string> GetComment(const std::string& locationKey) const;

    void SetScriptOverride(const std::string& typeFullName, const std::string& editedSource);
    std::optional<std::string> GetScriptOverride(const std::string& typeFullName) const;

    void AddBookmark(Bookmark bm);
    const std::vector<Bookmark>& Bookmarks() const { return bookmarks_; }
    const std::unordered_map<std::string, std::string>& SymbolRenames() const { return renames_; }
    const std::unordered_map<std::string, std::string>& Comments() const { return comments_; }
    const std::unordered_map<std::string, std::string>& ScriptOverrides() const { return scriptOverrides_; }

    Result<void> Save(const std::filesystem::path& filePath) const;
    Result<void> Load(const std::filesystem::path& filePath);

private:
    std::unordered_map<std::string, std::string> renames_;
    std::unordered_map<std::string, std::string> comments_;
    std::unordered_map<std::string, std::string> scriptOverrides_;
    std::vector<Bookmark> bookmarks_;
};

} // namespace coconut
