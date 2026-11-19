#include "export/GameRebuilder.h"
#include "unity/ClassIDs.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <unordered_map>

namespace coconut::exporter {

namespace {

std::string NormalizeRelKey(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    return s;
}

std::string BaseFileName(const std::string& virtualName) {
    std::string norm = NormalizeRelKey(virtualName);
    auto slash = norm.find_last_of('/');
    return slash == std::string::npos ? norm : norm.substr(slash + 1);
}

bool HasDirtyObjects(const unity::SerializedFile& sf) {
    for (const auto& obj : sf.objects) {
        if (obj.dirty) return true;
    }
    return false;
}

} // namespace

std::vector<PendingGameChange> GameRebuilder::CollectPendingChanges(
    const unity::LoadedGameProject& project,
    const scene::SceneDocument* sceneDoc) {
    std::vector<PendingGameChange> changes;

    for (const auto& sf : project.serializedFiles) {
        for (const auto& obj : sf.objects) {
            if (!obj.dirty) continue;
            PendingGameChange c;
            c.fileName = sf.name;
            c.objectName = obj.cachedName.empty() ? sf.ResolveObjectName(obj) : obj.cachedName;
            c.className = std::string(unity::ClassIDName(obj.classId));
            c.pathId = obj.pathId;
            c.byteSize = obj.rawData.size();
            c.changeKind = (obj.classId == static_cast<i32>(unity::ClassID::Transform) ||
                            obj.classId == static_cast<i32>(unity::ClassID::GameObject))
                               ? "Scene / Transform Edit"
                               : "Asset / Binary Patch";
            changes.push_back(std::move(c));
        }
    }

    if (sceneDoc) {
        for (const auto& n : sceneDoc->Nodes()) {
            if (!n.dirty) continue;
            PendingGameChange c;
            c.fileName = (n.fileIndex < project.serializedFiles.size()) ? project.serializedFiles[n.fileIndex].name : sceneDoc->SceneName();
            c.objectName = n.name;
            c.className = "Transform";
            c.pathId = n.transformPathId;
            c.changeKind = "Pending Scene Edit";
            changes.push_back(std::move(c));
        }
    }

    return changes;
}

Result<GameRebuildReport> GameRebuilder::BuildPatchedGame(
    unity::LoadedGameProject& project,
    scene::SceneDocument* sceneDoc,
    const GameRebuildOptions& options,
    std::function<void(const std::string&)> onLog) {
    if (options.outputPath.empty()) {
        return Result<GameRebuildReport>::Err("Output path for Game Exporter is empty");
    }

    GameRebuildReport report;
    report.outputPath = options.outputPath;

    auto log = [&](const std::string& msg) {
        report.logs.push_back(msg);
        COCO_LOG_INFO("GameRebuilder", msg);
        if (onLog) onLog(msg);
    };

    // 1. Sync any unsaved live SceneDocument edits back into its owning SerializedFiles
    if (options.syncLiveSceneBeforeBuild && sceneDoc) {
        bool sceneHasDirty = false;
        for (const auto& n : sceneDoc->Nodes()) {
            if (n.dirty) { sceneHasDirty = true; break; }
        }
        if (sceneHasDirty) {
            log("Syncing live Scene Editor modifications back into binary SerializedFiles...");
            auto syncRes = sceneDoc->SyncBackToSerializedFiles(project);
            if (!syncRes) {
                log("Warning: Scene sync reported: " + syncRes.Error());
            } else {
                log("Synced " + std::to_string(syncRes.Value()) + " scene objects cleanly.");
            }
        }
    }

    // 2. Re-serialize all modified SerializedFiles (or all files if none explicitly flagged yet)
    bool anyDirty = false;
    for (const auto& sf : project.serializedFiles) {
        if (HasDirtyObjects(sf)) { anyDirty = true; break; }
    }

    std::unordered_map<std::string, ByteBuffer> rebuiltByRelPath;
    std::unordered_map<std::string, ByteBuffer> rebuiltByBaseName;

    for (auto& sf : project.serializedFiles) {
        bool dirty = HasDirtyObjects(sf);
        if (anyDirty && !dirty) continue;

        auto serRes = sf.Serialize();
        if (!serRes) {
            return Result<GameRebuildReport>::Err("Failed to re-serialize " + sf.name + ": " + serRes.Error());
        }

        size_t patchedInFile = 0;
        for (const auto& obj : sf.objects) {
            if (obj.dirty) ++patchedInFile;
        }
        report.objectsPatched += patchedInFile;
        report.serializedFilesRebuilt++;
        report.rebuiltFiles.push_back(sf.name);
        log("Re-serialized " + sf.name + " (" + std::to_string(serRes.Value().size()) +
            " bytes, " + std::to_string(patchedInFile) + " patched objects)");

        std::string normName = NormalizeRelKey(sf.name);
        rebuiltByBaseName[BaseFileName(normName)] = serRes.Value();
        rebuiltByRelPath[normName] = std::move(serRes.Value());
    }

    // 3. Repack any UnityFS AssetBundles whose inner nodes were modified
    std::unordered_map<std::string, ByteBuffer> repackedBundles;
    for (size_t bIdx = 0; bIdx < project.bundles.size(); ++bIdx) {
        auto& bundle = project.bundles[bIdx];
        bool bundleTouched = false;
        for (auto& node : bundle.nodes) {
            std::string key = BaseFileName(node.path);
            auto it = rebuiltByBaseName.find(key);
            if (it != rebuiltByBaseName.end()) {
                node.data = it->second;
                node.size = node.data.size();
                bundleTouched = true;
            }
        }
        if (bundleTouched || !anyDirty) {
            auto packRes = bundle.Serialize(options.bundleCompression);
            if (!packRes) {
                return Result<GameRebuildReport>::Err("Failed to repack UnityFS bundle #" +
                                                      std::to_string(bIdx) + ": " + packRes.Error());
            }
            std::string bName = "repacked_bundle_" + std::to_string(bIdx) + ".bundle";
            repackedBundles[bName] = std::move(packRes.Value());
            report.bundlesRepacked++;
            log("Repacked UnityFS bundle (" + bName + ", " +
                std::to_string(repackedBundles[bName].size()) + " bytes)");
        }
    }

    std::error_code ec;
    bool outputIsApk = project.isApkArchive || options.outputPath.extension() == ".apk";

    // 4A. Android APK Rebuild Mode
    if (outputIsApk) {
        report.targetFormat = "Android Patched APK";
        report.requiresApkSigning = true;

        std::filesystem::path apkDst = options.outputPath;
        if (apkDst.extension() != ".apk") {
            std::filesystem::create_directories(apkDst, ec);
            apkDst /= (project.gameName.empty() ? "patched_game.apk" : project.gameName + "_patched.apk");
        } else if (apkDst.has_parent_path()) {
            std::filesystem::create_directories(apkDst.parent_path(), ec);
        }
        report.outputPath = apkDst;

        std::vector<std::pair<std::string, ByteBuffer>> apkEntries;
        std::unordered_map<std::string, bool> written;

        // Clone untouched entries from original APK if present
        if (options.copyUntouchedGameFiles && !project.rootPath.empty() &&
            std::filesystem::is_regular_file(project.rootPath, ec)) {
            ZipArchive zip;
            if (zip.OpenFile(project.rootPath)) {
                for (const auto& entry : zip.ListEntries()) {
                    if (entry.isDirectory) continue;
                    std::string normEntry = NormalizeRelKey(entry.name);
                    std::string base = BaseFileName(normEntry);

                    // Skip old JAR signature files when repacking APK
                    if (normEntry.rfind("META-INF/", 0) == 0 &&
                        (normEntry.ends_with(".SF") || normEntry.ends_with(".RSA") ||
                         normEntry.ends_with(".DSA") || normEntry.ends_with(".MF"))) {
                        continue;
                    }

                    if (auto it = rebuiltByRelPath.find(normEntry); it != rebuiltByRelPath.end()) {
                        apkEntries.emplace_back(normEntry, it->second);
                        written[normEntry] = true;
                    } else if (auto it2 = rebuiltByBaseName.find(base);
                               it2 != rebuiltByBaseName.end() &&
                               normEntry.find("assets/bin/Data/") != std::string::npos) {
                        apkEntries.emplace_back(normEntry, it2->second);
                        written[normEntry] = true;
                        written[base] = true;
                    } else {
                        auto origData = zip.ReadEntry(entry.name);
                        if (origData) {
                            apkEntries.emplace_back(normEntry, std::move(origData.Value()));
                            report.untouchedFilesCopied++;
                        }
                    }
                }
            }
        }

        // Write any rebuilt files that weren't already matched in the original APK
        for (const auto& [rel, data] : rebuiltByRelPath) {
            std::string base = BaseFileName(rel);
            if (written.count(rel) || written.count(base)) continue;
            std::string entryPath = (rel.find("assets/bin/Data/") == 0) ? rel : ("assets/bin/Data/" + base);
            apkEntries.emplace_back(entryPath, data);
        }

        auto writeRes = ZipArchive::WriteZipFile(apkDst, apkEntries);
        if (!writeRes) return Result<GameRebuildReport>::Err("Failed to write patched APK: " + writeRes.Error());
        report.totalOutputBytes = std::filesystem::file_size(apkDst, ec);

        log("Built patched Android APK: " + apkDst.string() + " (" + std::to_string(report.totalOutputBytes) + " bytes)");
        log("Note: Run `zipalign -p 4` and `apksigner sign` before sideloading onto an Android device.");
    } else {
        // 4B. Desktop Build Directory Mode (Windows / Linux / macOS)
        report.targetFormat = "Desktop Patched Unity Build (" + std::string(ToString(project.platform)) + ")";
        std::filesystem::create_directories(options.outputPath, ec);

        // Step 1: Copy untouched original build directory if present & distinct from outputPath
        bool sameDir = false;
        if (!project.rootPath.empty() && std::filesystem::exists(project.rootPath, ec)) {
            auto srcCanon = std::filesystem::weakly_canonical(project.rootPath, ec);
            auto dstCanon = std::filesystem::weakly_canonical(options.outputPath, ec);
            sameDir = (!ec && srcCanon == dstCanon);
        }

        if (options.copyUntouchedGameFiles && !sameDir && !project.rootPath.empty() &&
            std::filesystem::is_directory(project.rootPath, ec)) {
            log("Cloning base game build from " + project.rootPath.string() + " ...");
            for (auto it = std::filesystem::recursive_directory_iterator(
                     project.rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
                auto rel = std::filesystem::relative(it->path(), project.rootPath, ec);
                auto dst = options.outputPath / rel;
                if (it->is_directory(ec)) {
                    std::filesystem::create_directories(dst, ec);
                } else if (it->is_regular_file(ec)) {
                    std::filesystem::create_directories(dst.parent_path(), ec);
                    std::filesystem::copy_file(it->path(), dst,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        report.untouchedFilesCopied++;
                        report.totalOutputBytes += std::filesystem::file_size(dst, ec);
                    }
                }
            }
        }

        // Step 2: Write rebuilt SerializedFiles directly into their exact relative Data/ path
        std::filesystem::path dataSubDir = options.outputPath;
        if (!project.dataPath.empty() && !project.rootPath.empty()) {
            auto relData = std::filesystem::relative(project.dataPath, project.rootPath, ec);
            if (!ec && !relData.empty() && relData != ".") {
                dataSubDir = options.outputPath / relData;
            }
        }
        std::filesystem::create_directories(dataSubDir, ec);

        for (const auto& sf : project.serializedFiles) {
            std::string normName = NormalizeRelKey(sf.name);
            auto it = rebuiltByRelPath.find(normName);
            if (it == rebuiltByRelPath.end()) continue;

            std::filesystem::path targetFile;
            if (!sf.diskPath.empty() && !project.rootPath.empty()) {
                auto rel = std::filesystem::relative(sf.diskPath, project.rootPath, ec);
                if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0) {
                    targetFile = options.outputPath / rel;
                }
            }
            if (targetFile.empty()) {
                targetFile = dataSubDir / BaseFileName(normName);
            }
            auto wRes = platform::WriteFileBytes(targetFile, it->second);
            if (!wRes) return Result<GameRebuildReport>::Err(wRes.Error());
            report.totalOutputBytes += it->second.size();
            log("Wrote patched binary: " + targetFile.string());
        }

        // Step 3: Write any repacked standalone bundles
        if (!repackedBundles.empty()) {
            auto bundleOutDir = dataSubDir / "StreamingAssets";
            std::filesystem::create_directories(bundleOutDir, ec);
            for (const auto& [bName, bBytes] : repackedBundles) {
                auto dst = bundleOutDir / bName;
                platform::WriteFileBytes(dst, bBytes);
                report.totalOutputBytes += bBytes.size();
            }
        }

        // Step 4: Write patch summary manifest
        if (options.writePatchManifest) {
            nlohmann::json manifest;
            manifest["tool"] = "Coconut Game Rebuilder";
            manifest["sourceBuild"] = project.rootPath.string();
            manifest["unityVersion"] = project.unityVersion.ToString();
            manifest["platform"] = std::string(ToString(project.platform));
            manifest["serializedFilesRebuilt"] = report.serializedFilesRebuilt;
            manifest["objectsPatched"] = report.objectsPatched;
            manifest["bundlesRepacked"] = report.bundlesRepacked;
            manifest["rebuiltFiles"] = report.rebuiltFiles;
            platform::WriteFileText(options.outputPath / "COCONUT_BUILD_PATCH.json", manifest.dump(2));
        }

        log("Patched Unity game build ready at: " + options.outputPath.string());
    }

    // Clear dirty flags after clean rebuild
    for (auto& sf : project.serializedFiles) {
        for (auto& obj : sf.objects) obj.dirty = false;
    }

    return Result<GameRebuildReport>::Ok(std::move(report));
}

} // namespace coconut::exporter
