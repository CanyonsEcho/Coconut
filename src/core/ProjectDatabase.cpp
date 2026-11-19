#include "core/ProjectDatabase.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace coconut {

void ProjectDatabase::Clear() {
    renames_.clear();
    comments_.clear();
    scriptOverrides_.clear();
    bookmarks_.clear();
}

void ProjectDatabase::SetSymbolRename(const std::string& originalSymbol, const std::string& newName) {
    if (newName.empty()) {
        renames_.erase(originalSymbol);
    } else {
        renames_[originalSymbol] = newName;
    }
}

std::optional<std::string> ProjectDatabase::GetSymbolRename(const std::string& originalSymbol) const {
    auto it = renames_.find(originalSymbol);
    if (it == renames_.end()) return std::nullopt;
    return it->second;
}

void ProjectDatabase::SetComment(const std::string& locationKey, const std::string& comment) {
    if (comment.empty()) {
        comments_.erase(locationKey);
    } else {
        comments_[locationKey] = comment;
    }
}

std::optional<std::string> ProjectDatabase::GetComment(const std::string& locationKey) const {
    auto it = comments_.find(locationKey);
    if (it == comments_.end()) return std::nullopt;
    return it->second;
}

void ProjectDatabase::SetScriptOverride(const std::string& typeFullName, const std::string& editedSource) {
    if (editedSource.empty()) {
        scriptOverrides_.erase(typeFullName);
    } else {
        scriptOverrides_[typeFullName] = editedSource;
    }
}

std::optional<std::string> ProjectDatabase::GetScriptOverride(const std::string& typeFullName) const {
    auto it = scriptOverrides_.find(typeFullName);
    if (it == scriptOverrides_.end()) return std::nullopt;
    return it->second;
}

void ProjectDatabase::AddBookmark(Bookmark bm) {
    bookmarks_.push_back(std::move(bm));
}

Result<void> ProjectDatabase::Save(const std::filesystem::path& filePath) const {
    try {
        nlohmann::json j;
        j["format"] = "coconut-project-db";
        j["version"] = 1;
        j["renames"] = renames_;
        j["comments"] = comments_;
        j["scriptOverrides"] = scriptOverrides_;
        nlohmann::json bms = nlohmann::json::array();
        for (const auto& b : bookmarks_) {
            bms.push_back({
                {"id", b.id},
                {"category", b.category},
                {"target", b.target},
                {"note", b.note}
            });
        }
        j["bookmarks"] = std::move(bms);

        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return Result<void>::Err("Cannot open project DB for writing: " + filePath.string());
        }
        ofs << j.dump(2);
        return Result<void>::Ok();
    } catch (const std::exception& ex) {
        return Result<void>::Err(std::string("Failed to save project DB: ") + ex.what());
    }
}

Result<void> ProjectDatabase::Load(const std::filesystem::path& filePath) {
    try {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs) {
            return Result<void>::Err("Cannot open project DB: " + filePath.string());
        }
        nlohmann::json j = nlohmann::json::parse(ifs);
        Clear();
        if (j.contains("renames") && j["renames"].is_object()) {
            renames_ = j["renames"].get<std::unordered_map<std::string, std::string>>();
        }
        if (j.contains("comments") && j["comments"].is_object()) {
            comments_ = j["comments"].get<std::unordered_map<std::string, std::string>>();
        }
        if (j.contains("scriptOverrides") && j["scriptOverrides"].is_object()) {
            scriptOverrides_ = j["scriptOverrides"].get<std::unordered_map<std::string, std::string>>();
        }
        if (j.contains("bookmarks") && j["bookmarks"].is_array()) {
            for (const auto& item : j["bookmarks"]) {
                Bookmark b;
                b.id = item.value("id", "");
                b.category = item.value("category", "");
                b.target = item.value("target", "");
                b.note = item.value("note", "");
                bookmarks_.push_back(std::move(b));
            }
        }
        return Result<void>::Ok();
    } catch (const std::exception& ex) {
        return Result<void>::Err(std::string("Failed to parse project DB: ") + ex.what());
    }
}

} // namespace coconut
