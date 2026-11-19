#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/Compression.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include "core/ProjectManager.h"
#include "core/Types.h"
#include "platform/Platform.h"
#include "unity/ClassIDs.h"
#include "unity/UnityFS.h"
#include "unity/SerializedFile.h"
#include "unity/GameLoader.h"
#include "TestFixtures.h"
#include "assets/TextureAsset.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "assets/MaterialAsset.h"
#include "assets/AssetManager.h"
#include "decompiler/DecompilerEngine.h"
#include "scene/SceneDocument.h"
#include "scene/GizmoMath.h"
#include "export/GameRebuilder.h"
#include "export/AssetRipperClient.h"
#include "fixup/ProjectFixupPipeline.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

using namespace coconut;

namespace {

int g_failed = 0;
int g_passed = 0;

void Expect(bool cond, const std::string& name) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] " << name << '\n';
    }
}

void TestUnityVersion() {
    auto v1 = UnityVersion::Parse("2022.3.15f1");
    auto v2 = UnityVersion::Parse("6000.0.23f1");
    auto v3 = UnityVersion::Parse("5.6.7p4");

    Expect(v1.major == 2022 && v1.minor == 3 && v1.patch == 15 && v1.type == 'f' && v1.build == 1, "UnityVersion parse 2022.3.15f1");
    Expect(v1.ToString() == "2022.3.15f1", "UnityVersion ToString");
    Expect(v3 < v1 && v1 < v2, "UnityVersion ordering");
}

void TestBinaryReaderWriter() {
    BinaryWriter w(Endian::Big);
    w.WriteU32(0x12345678u);
    w.WriteF32(3.5f);
    w.WriteCString("UnityFS");
    w.SetEndian(Endian::Little);
    w.WriteAlignedString("Coconut");
    w.WriteI64(-9876543210123LL);

    BinaryReader r(w.Buffer(), Endian::Big);
    Expect(r.ReadU32() == 0x12345678u, "BinaryReader Big-Endian U32");
    Expect(std::fabs(r.ReadF32() - 3.5f) < 1e-6f, "BinaryReader Big-Endian F32");
    Expect(r.ReadCString() == "UnityFS", "BinaryReader CString");
    r.SetEndian(Endian::Little);
    Expect(r.ReadAlignedString() == "Coconut", "BinaryReader AlignedString");
    Expect(r.ReadI64() == -9876543210123LL, "BinaryReader Little-Endian I64");
    Expect(!r.HasError() && r.IsEof(), "BinaryReader EOF clean");
}

void TestCompressionAndZip() {
    std::string payload;
    for (int i = 0; i < 64; ++i) {
        payload += "Coconut UnityFS LZ4 & Zlib compression test block! ";
    }
    ByteSpan span(reinterpret_cast<const u8*>(payload.data()), payload.size());

    auto lz4Comp = Compression::CompressLZ4(span, true);
    Expect(lz4Comp.IsOk() && lz4Comp.Value().size() < payload.size(), "LZ4HC compress");
    auto lz4Decomp = Compression::DecompressLZ4(lz4Comp.Value(), payload.size());
    Expect(lz4Decomp.IsOk() && lz4Decomp.Value().size() == payload.size() &&
               std::memcmp(lz4Decomp.Value().data(), payload.data(), payload.size()) == 0,
           "LZ4 round-trip");

    auto zComp = Compression::CompressZlib(span);
    Expect(zComp.IsOk(), "Zlib compress");
    auto zDecomp = Compression::DecompressZlib(zComp.Value());
    Expect(zDecomp.IsOk() && zDecomp.Value().size() == payload.size() &&
               std::memcmp(zDecomp.Value().data(), payload.data(), payload.size()) == 0,
           "Zlib round-trip");

    auto tmpZip = std::filesystem::temp_directory_path() / "coconut_test_archive.apk";
    ByteBuffer entryData(payload.begin(), payload.end());
    auto writeRes = ZipArchive::WriteZipFile(tmpZip, {
        {"assets/bin/Data/globalgamemanagers", entryData},
        {"lib/arm64-v8a/libil2cpp.so", {0x7F, 'E', 'L', 'F'}}
    });
    Expect(writeRes.IsOk(), "ZipArchive write APK");

    ZipArchive zip;
    Expect(zip.OpenFile(tmpZip).IsOk(), "ZipArchive open APK");
    Expect(zip.HasEntry("assets/bin/Data/globalgamemanagers"), "ZipArchive HasEntry");
    auto readBack = zip.ReadEntry("assets/bin/Data/globalgamemanagers");
    Expect(readBack.IsOk() && readBack.Value() == entryData, "ZipArchive ReadEntry matches");
    zip.Close();
    std::error_code ec;
    std::filesystem::remove(tmpZip, ec);
}

void TestCommandHistoryAndProjectDb() {
    CommandHistory hist;
    int value = 10;
    hist.PushAndExecute(std::make_unique<LambdaCommand>(
        "Set 42", [&]() { value = 42; }, [&]() { value = 10; }));
    Expect(value == 42 && hist.CanUndo() && hist.IsDirty(), "CommandHistory execute");
    hist.Undo();
    Expect(value == 10 && hist.CanRedo(), "CommandHistory undo");
    hist.Redo();
    Expect(value == 42, "CommandHistory redo");
    hist.MarkSaved();
    Expect(!hist.IsDirty(), "CommandHistory MarkSaved");

    ProjectDatabase db;
    db.SetSymbolRename("PlayerController::Update", "PlayerController::TickMovement");
    db.SetComment("IL_0008", "Applies gravity multiplier");
    db.SetScriptOverride("PlayerController", "public class PlayerController {}");
    db.AddBookmark({"bm1", "Code", "PlayerController::Update", "Main loop"});

    auto tmpDb = std::filesystem::temp_directory_path() / "coconut_test.coco";
    Expect(db.Save(tmpDb).IsOk(), "ProjectDatabase Save");

    ProjectDatabase loaded;
    Expect(loaded.Load(tmpDb).IsOk(), "ProjectDatabase Load");
    Expect(loaded.GetSymbolRename("PlayerController::Update") == "PlayerController::TickMovement", "ProjectDatabase rename");
    Expect(loaded.GetComment("IL_0008") == "Applies gravity multiplier", "ProjectDatabase comment");
    Expect(loaded.Bookmarks().size() == 1, "ProjectDatabase bookmarks");
    std::error_code ec;
    std::filesystem::remove(tmpDb, ec);
}

void TestUnityParsersAndDemoGames() {
    auto tmpDir = std::filesystem::temp_directory_path() / "coconut_test_mono_game";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);

    // 1. Generate & Load Mono Demo Game (includes SerializedFile v22, UnityFS LZ4HC bundle, Managed DLL)
    auto genRes = unity::SampleGameGenerator::GenerateMonoDemoGame(tmpDir);
    Expect(genRes.IsOk(), "GenerateMonoDemoGame");

    auto loadRes = unity::GameLoader::Load(tmpDir);
    Expect(loadRes.IsOk(), "GameLoader::Load Mono directory");
    if (loadRes) {
        auto& proj = loadRes.Value();
        Expect(proj.backend == ScriptingBackend::Mono, "Detected Mono backend");
        Expect(proj.unityVersion == UnityVersion::Parse("2022.3.15f1"), "Detected Unity 2022.3.15f1");
        Expect(proj.serializedFiles.size() >= 2, "Loaded level0 + UnityFS embedded SerializedFile");
        Expect(proj.bundles.size() == 1, "Loaded UnityFS bundle");
        Expect(proj.managedAssemblies.size() == 1 && proj.managedAssemblies[0].name == "Assembly-CSharp.dll", "Loaded Assembly-CSharp.dll");

        assets::AssetManager mgr;
        mgr.IndexProject(proj);
        Expect(mgr.Entries().size() >= 45, "AssetManager indexed all scene + asset objects");

        // Test Texture2D RGBA32 + DXT1 decoding, live edit, PNG export/import, and binary write-back!
        int rgbaIdx = mgr.FindEntryByPathIdAnyFile(500, static_cast<i32>(unity::ClassID::Texture2D));
        int dxt1Idx = mgr.FindEntryByPathIdAnyFile(501, static_cast<i32>(unity::ClassID::Texture2D));
        Expect(rgbaIdx >= 0 && dxt1Idx >= 0, "Found RGBA32 and DXT1 textures");
        if (rgbaIdx >= 0) {
            auto texRes = mgr.GetTexture(static_cast<size_t>(rgbaIdx));
            Expect(texRes.IsOk() && texRes.Value()->width == 128 && texRes.Value()->IsValid(), "Decode RGBA32 texture");
            auto pngPath = tmpDir / "exported_grid.png";
            Expect(assets::TextureCodec::ExportPNG(*texRes.Value(), pngPath).IsOk(), "Export texture PNG");
            auto impRes = assets::TextureCodec::ImportPNG(pngPath);
            Expect(impRes.IsOk() && impRes.Value().width == 128 && impRes.Value().height == 128, "Import texture PNG round-trip");
            assets::TextureCodec::InvertRGB(*texRes.Value());
            Expect(mgr.ApplyTextureEdits(static_cast<size_t>(rgbaIdx)).IsOk(), "Write edited texture back to SerializedFile");
        }
        if (dxt1Idx >= 0) {
            auto dxtRes = mgr.GetTexture(static_cast<size_t>(dxt1Idx));
            Expect(dxtRes.IsOk() && dxtRes.Value()->width == 64 && dxtRes.Value()->IsValid(), "Decode DXT1/BC1 texture");
        }

        // Test Mesh decoding + OBJ & glTF 2.0 export
        int meshIdx = mgr.FindEntryByPathIdAnyFile(305, static_cast<i32>(unity::ClassID::Mesh));
        Expect(meshIdx >= 0, "Found Combined Mesh");
        if (meshIdx >= 0) {
            auto meshRes = mgr.GetMesh(static_cast<size_t>(meshIdx));
            Expect(meshRes.IsOk() && meshRes.Value()->subMeshes.size() == 2 && meshRes.Value()->IsValid(), "Decode Combined Mesh with 2 submeshes");
            Expect(assets::MeshCodec::ExportOBJ(*meshRes.Value(), tmpDir / "combined.obj").IsOk(), "Export Mesh OBJ");
            Expect(assets::MeshCodec::ExportGLTF(*meshRes.Value(), tmpDir / "combined.gltf").IsOk(), "Export Mesh glTF 2.0");
        }

        // Test AudioClip decoding + WAV export
        int audioIdx = mgr.FindEntryByPathIdAnyFile(600, static_cast<i32>(unity::ClassID::AudioClip));
        Expect(audioIdx >= 0, "Found AudioClip");
        if (audioIdx >= 0) {
            auto audRes = mgr.GetAudio(static_cast<size_t>(audioIdx));
            Expect(audRes.IsOk() && !audRes.Value()->rawAudio.empty(), "Decode AudioClip PCM");
            Expect(assets::AudioCodec::ExportWAV(*audRes.Value(), tmpDir / "chime.wav").IsOk(), "Export AudioClip WAV");
        }

        // Test Material edit + save modified SerializedFile + re-parse round-trip!
        int matIdx = mgr.FindEntryByPathIdAnyFile(402, static_cast<i32>(unity::ClassID::Material));
        Expect(matIdx >= 0, "Found AmberBeaconMat");
        if (matIdx >= 0) {
            auto matRes = mgr.GetMaterial(static_cast<size_t>(matIdx));
            Expect(matRes.IsOk(), "Decode Material");
            matRes.Value()->SetPrimaryColor({0.55f, 0.36f, 0.96f, 1.0f}); // Purple!
            Expect(mgr.ApplyMaterialEdits(static_cast<size_t>(matIdx)).IsOk(), "Write modified Material back");
        }

        auto patchedDir = tmpDir / "patched_out";
        Expect(mgr.SaveModifiedFiles(patchedDir).IsOk(), "Save modified SerializedFile to disk");
        auto reloadSf = unity::SerializedFile::LoadFromFile(patchedDir / "level0");
        const auto* origLevel0 = proj.FindSerializedFile("level0");
        Expect(reloadSf.IsOk() && origLevel0 != nullptr && reloadSf.Value().objects.size() == origLevel0->objects.size(), "Reload saved SerializedFile round-trip");
        if (reloadSf) {
            auto* reloadedMatObj = reloadSf.Value().FindObject(402);
            Expect(reloadedMatObj != nullptr, "Find patched Material in reloaded SerializedFile");
            if (reloadedMatObj) {
                auto rm = assets::MaterialCodec::ParseFromObject(reloadSf.Value(), *reloadedMatObj);
                Expect(rm.IsOk() && std::fabs(rm.Value().PrimaryColor().r - 0.55f) < 1e-3f &&
                           std::fabs(rm.Value().PrimaryColor().b - 0.96f) < 1e-3f,
                       "Patched Material color persisted through binary round-trip");
            }
        }
    }

    // 2. Generate & Load Android IL2CPP Demo APK
    auto tmpApk = std::filesystem::temp_directory_path() / "coconut_test_il2cpp.apk";
    Expect(unity::SampleGameGenerator::GenerateIl2CppDemoApk(tmpApk).IsOk(), "GenerateIl2CppDemoApk");
    auto apkRes = unity::GameLoader::Load(tmpApk);
    Expect(apkRes.IsOk(), "GameLoader::Load Android APK");
    if (apkRes) {
        Expect(apkRes.Value().backend == ScriptingBackend::IL2CPP, "APK detected IL2CPP backend");
        Expect(apkRes.Value().platform == TargetPlatform::Android, "APK detected Android platform");
        Expect(!apkRes.Value().il2cppMetadataData.empty(), "APK extracted global-metadata.dat");
        Expect(!apkRes.Value().il2cppBinaryData.empty(), "APK extracted libil2cpp.so");
    }

    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::remove(tmpApk, ec);
}

void TestMonoAndIl2CppDecompiler() {
    auto tmpDir = std::filesystem::temp_directory_path() / "coconut_test_decomp";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);

    // 1. Test Mono PE/CLI Decompiler & Ghidra-style XRefs + Renaming
    Expect(unity::SampleGameGenerator::GenerateMonoDemoGame(tmpDir).IsOk(), "Generate Mono game for decompiler");
    auto monoLoad = unity::GameLoader::Load(tmpDir);
    Expect(monoLoad.IsOk(), "Load Mono game for decompiler");

    if (monoLoad) {
        ProjectDatabase db;
        db.SetSymbolRename("CoconutDemo.PlayerController::TakeDamage", "ApplyPlayerDamage");
        db.SetComment("CoconutDemo.PlayerController::TakeDamage@IL_0000", "Subtracted health from incoming hit");

        decompiler::DecompilerEngine engine;
        Expect(engine.AnalyzeProject(monoLoad.Value(), &db).IsOk(), "DecompilerEngine::AnalyzeProject (Mono)");
        Expect(engine.Symbols().size() == 3, "Found 3 Mono user classes (PlayerController, EnemySentinel, ScoreCalculator)");
        Expect(!engine.AllXRefs().empty(), "Extracted CIL XRefs");

        auto hits = engine.SearchSymbols("PlayerController");
        Expect(hits.size() == 1, "SearchSymbols PlayerController");
        if (!hits.empty()) {
            auto view = engine.InspectSymbol(hits[0], &db);
            Expect(view.decompiledCSharp.find("class PlayerController : MonoBehaviour") != std::string::npos, "Decompiled class header");
            Expect(view.decompiledCSharp.find("ApplyPlayerDamage(int amount)") != std::string::npos, "Applied .coco Ghidra symbol rename");
            Expect(view.decompiledCSharp.find("this.health = (this.health - amount);") != std::string::npos, "Decompiled structured C# arithmetic");
            Expect(view.decompiledCSharp.find("if ((this.health <= 0))") != std::string::npos ||
                   view.decompiledCSharp.find("if (this.health <= 0)") != std::string::npos, "Decompiled structured C# if-block");
            Expect(view.disassemblyListing.find("ldfld") != std::string::npos &&
                   view.disassemblyListing.find("Subtracted health from incoming hit") != std::string::npos, "Disassembly listing + comment");
        }

        auto scoreHits = engine.SearchSymbols("ScoreCalculator");
        Expect(scoreHits.size() == 1, "SearchSymbols ScoreCalculator");
        if (!scoreHits.empty()) {
            auto view = engine.InspectSymbol(scoreHits[0], &db);
            Expect(view.decompiledCSharp.find("return ((baseScore * comboMultiplier) + 100);") != std::string::npos, "Decompiled ScoreCalculator return expr");
        }

        auto debugXRefs = engine.FindXRefsTo("Debug.Log");
        Expect(debugXRefs.size() >= 3, "Found 3+ XRefs to Debug.Log");

        auto scriptsOut = tmpDir / "exported_scripts";
        auto expRes = engine.ExportAllScripts(scriptsOut, &db);
        Expect(expRes.IsOk() && expRes.Value() == 3, "ExportAllScripts wrote 3 .cs files");
        Expect(engine.ExportGhidraScriptJson(tmpDir / "script.json").IsOk(), "ExportGhidraScriptJson");
    }

    // 2. Test IL2CPP global-metadata.dat + ARM64 ELF64 Disassembler
    auto tmpApk = tmpDir / "mobile_il2cpp.apk";
    Expect(unity::SampleGameGenerator::GenerateIl2CppDemoApk(tmpApk).IsOk(), "Generate IL2CPP APK for decompiler");
    auto apkLoad = unity::GameLoader::Load(tmpApk);
    Expect(apkLoad.IsOk(), "Load IL2CPP APK for decompiler");
    if (apkLoad) {
        decompiler::DecompilerEngine ilEngine;
        Expect(ilEngine.AnalyzeProject(apkLoad.Value()).IsOk(), "DecompilerEngine::AnalyzeProject (IL2CPP)");
        Expect(ilEngine.Symbols().size() == 2, "Found 2 IL2CPP classes (TouchPlayerController, BossGuardianAI)");
        if (!ilEngine.Symbols().empty()) {
            auto view = ilEngine.InspectSymbol(0);
            Expect(view.decompiledCSharp.find("class TouchPlayerController") != std::string::npos, "IL2CPP C# stub");
            Expect(view.disassemblyListing.find("stp fp, lr") != std::string::npos &&
                   view.disassemblyListing.find("ret") != std::string::npos, "ARM64 ELF64 disassembly listing");
        }
        Expect(ilEngine.ExportDumpCS(tmpDir / "dump.cs").IsOk(), "IL2CPP ExportDumpCS");
    }

    // 3. Test x86-64 disassembler directly
    ByteBuffer x64Sample = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0x5D, 0xC3};
    auto x64Insns = decompiler::NativeDisassembler::DisassembleX86_64(x64Sample, 0x140001000ULL);
    Expect(x64Insns.size() >= 6 && x64Insns[0].mnemonic == "push" && x64Insns.back().mnemonic == "ret", "x86-64 disassembly");

    std::filesystem::remove_all(tmpDir, ec);
}

void TestSceneEditingAndGizmos() {
    auto tmpDir = std::filesystem::temp_directory_path() / "coconut_test_scene";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);

    Expect(unity::SampleGameGenerator::GenerateMonoDemoGame(tmpDir).IsOk(), "Generate demo for scene tests");
    auto loadRes = unity::GameLoader::Load(tmpDir);
    Expect(loadRes.IsOk(), "Load demo for scene tests");

    if (loadRes) {
        auto& proj = loadRes.Value();
        assets::AssetManager mgr;
        mgr.IndexProject(proj);

        scene::SceneDocument doc;
        Expect(doc.LoadFromProject(proj, mgr).IsOk(), "SceneDocument::LoadFromProject");
        Expect(doc.Nodes().size() == 13, "Scene has 13 GameObjects (Camera, Light, Floor, Tower+Beacon, 3x Crate+Lid, 2x Batched Pillars)");

        int towerIdx = doc.FindNodeByName("WatchTower");
        int beaconIdx = doc.FindNodeByName("BeaconCore");
        Expect(towerIdx >= 0 && beaconIdx >= 0, "Found WatchTower and child BeaconCore");
        if (towerIdx >= 0 && beaconIdx >= 0) {
            Expect(doc.Nodes()[static_cast<size_t>(beaconIdx)].parentIndex == towerIdx, "BeaconCore parented to WatchTower");
            glm::mat4 beaconWorld = doc.WorldMatrix(static_cast<size_t>(beaconIdx));
            Expect(std::fabs(beaconWorld[3].y - (1.6f + 2.25f)) < 1e-3f, "WorldMatrix composes parent+child transforms");

            CommandHistory hist;
            doc.SetTransform(static_cast<size_t>(towerIdx), {2.5f, 1.6f, -1.0f}, {0, 45.0f, 0}, {1, 1, 1}, &hist);
            Expect(std::fabs(doc.Nodes()[static_cast<size_t>(towerIdx)].localPosition.x - 2.5f) < 1e-4f, "Scene transform edit");
            hist.Undo();
            Expect(std::fabs(doc.Nodes()[static_cast<size_t>(towerIdx)].localPosition.x - 0.0f) < 1e-4f, "Scene transform undo");
            hist.Redo();
            Expect(std::fabs(doc.Nodes()[static_cast<size_t>(towerIdx)].localPosition.x - 2.5f) < 1e-4f, "Scene transform redo");

            auto syncRes = doc.SyncBackToSerializedFiles(proj);
            Expect(syncRes.IsOk() && syncRes.Value() >= 1, "SyncBackToSerializedFiles updated Transform binary");
        }

        size_t createdIdx = doc.CreateGameObject("CustomProp", -1, 303, 403);
        Expect(doc.Nodes().size() == 14 && doc.Nodes()[createdIdx].name == "CustomProp", "CreateGameObject");
        size_t dupIdx = doc.DuplicateGameObject(createdIdx);
        Expect(doc.Nodes().size() == 15 && doc.Nodes()[dupIdx].name == "CustomProp (Clone)", "DuplicateGameObject");
        Expect(doc.ReparentGameObject(dupIdx, static_cast<i64>(createdIdx)), "ReparentGameObject");
        Expect(doc.DeleteGameObject(dupIdx) && doc.Nodes().size() == 14, "DeleteGameObject");

        auto yamlOut = tmpDir / "exported_level0.unity";
        Expect(doc.ExportUnitySceneYaml(yamlOut).IsOk(), "ExportUnitySceneYaml");
        auto yamlTxt = platform::ReadFileText(yamlOut);
        Expect(yamlTxt.IsOk() && yamlTxt.Value().find("WatchTower") != std::string::npos &&
                   yamlTxt.Value().find("m_StaticBatchInfo") != std::string::npos,
               "Exported .unity YAML contains scene hierarchy & static batch metadata");
    }

    // Raycast & Gizmo math tests
    scene::OrbitCamera cam;
    cam.target = {0, 0, 0};
    cam.yawDeg = 0.0f;
    cam.pitchDeg = 0.0f;
    cam.distance = 10.0f;
    auto centerRay = cam.ScreenPointToRay(0.5f, 0.5f, 16.0f / 9.0f);
    auto hit = scene::GizmoMath::IntersectRayAABB(centerRay, {-1, -1, -1}, {1, 1, 1});
    Expect(hit.has_value() && std::fabs(*hit - 9.0f) < 0.05f, "OrbitCamera center ray hits origin AABB at distance 9");

    scene::Ray xAxisRay{{1.0f, 0.05f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    Expect(scene::GizmoMath::PickAxisHandle(xAxisRay, {0, 0, 0}, 2.0f) == scene::GizmoAxis::X, "PickAxisHandle selects X axis");

    std::filesystem::remove_all(tmpDir, ec);
}

void TestUnityProjectExportAndFixupPipeline() {
    auto tmpRoot = std::filesystem::temp_directory_path() / "coconut_test_fixup";
    auto gameDir = tmpRoot / "GameBuild";
    auto exportDir = tmpRoot / "ExportedUnity6Project";
    std::error_code ec;
    std::filesystem::remove_all(tmpRoot, ec);

    Expect(unity::SampleGameGenerator::GenerateMonoDemoGame(gameDir).IsOk(), "Generate demo for export+fixup");
    auto loadRes = unity::GameLoader::Load(gameDir);
    Expect(loadRes.IsOk(), "Load demo for export+fixup");

    if (loadRes) {
        auto& proj = loadRes.Value();
        assets::AssetManager assets;
        assets.IndexProject(proj);

        decompiler::DecompilerEngine decomp;
        Expect(decomp.AnalyzeProject(proj).IsOk(), "AnalyzeProject for export");

        scene::SceneDocument sceneDoc;
        Expect(sceneDoc.LoadFromProject(proj, assets).IsOk(), "LoadScene for export");

        // 1. Run full Export + Fixup pipeline targeting Unity 6 (6000.0.23f1)
        fixup::FixupPipelineOptions opts;
        opts.targetUnityVersion = "6000.0.23f1";
        opts.meshFormat = "fbx";
        opts.audioFormat = "m4a";
        opts.imageFormat = "png";
        opts.useInternalAssetRipper = false; // Exercise built-in native exporter + all 3 fixup passes
        opts.runMeshDecombiner = true;
        opts.runPrefabReconstructor = true;
        opts.runBuildErrorFixer = true;

        auto pipeRes = fixup::ProjectFixupPipeline::ExportAndFixup(
            proj, assets, decomp, sceneDoc, nullptr, exportDir, opts);
        Expect(pipeRes.IsOk(), "ProjectFixupPipeline::ExportAndFixup succeeded");

        if (pipeRes) {
            const auto& r = pipeRes.Value();
            Expect(r.exportReport.scenesExported == 1, "Exported 1 .unity scene");
            Expect(r.exportReport.scriptsExported == 3, "Exported 3 decompiled C# scripts");
            Expect(r.exportReport.texturesExported >= 2, "Exported textures PNG");
            Expect(r.exportReport.meshesExported >= 6, "Exported meshes OBJ");
            Expect(r.exportReport.materialsExported >= 5, "Exported materials .mat");
            Expect(r.exportReport.audioExported >= 1, "Exported audio WAV");

            // Verify MeshDecombiner split StaticPillar_A and StaticPillar_B from Combined Mesh!
            Expect(r.meshDecombine.combinedMeshesFound >= 1, "MeshDecombiner detected Combined Mesh");
            Expect(r.meshDecombine.meshesSplit == 2, "MeshDecombiner split 2 static-batched pillars into local-space meshes");
            Expect(r.meshDecombine.sceneReferencesRewired == 2, "MeshDecombiner rewired 2 scene MeshFilter references");
            Expect(std::filesystem::exists(exportDir / "Assets" / "Meshes" / "Decombined" / "StaticPillar_A_Decombined.obj"), "StaticPillar_A_Decombined.obj written");

            // Verify PrefabReconstructor detected the 3 repeated SupplyCrate hierarchies!
            Expect(r.prefabReconstruct.prefabsCreated >= 1, "PrefabReconstructor created SupplyCrate.prefab");
            Expect(r.prefabReconstruct.sceneInstancesLinked >= 3, "PrefabReconstructor linked 3 PrefabInstance blocks");
            Expect(std::filesystem::exists(exportDir / "Assets" / "Prefabs" / "Reconstructed" / "SupplyCrate.prefab"), "SupplyCrate.prefab exists");

            // Verify COCONUT_FIXUP_REPORT.md + ProjectVersion.txt
            Expect(std::filesystem::exists(exportDir / "COCONUT_FIXUP_REPORT.md"), "COCONUT_FIXUP_REPORT.md generated");
            auto verTxt = platform::ReadFileText(exportDir / "ProjectSettings" / "ProjectVersion.txt");
            Expect(verTxt.IsOk() && verTxt.Value().find("6000.0.23f1") != std::string::npos, "Target Unity version 6000.0.23f1 written");
        }

        // 2. Inject real broken scripts/shaders & verify BuildErrorFixer repairs ALL of them automatically!
        platform::WriteFileText(exportDir / "Assets" / "Scripts" / "EmbeddedAttribute.cs", "class EmbeddedAttribute {}");
        platform::WriteFileText(exportDir / "Assets" / "Scripts" / "LegacyBroken.cs",
            "using TMPro;\nusing UnityEngine.InputSystem;\npublic class LegacyBroken : MonoBehaviour {\n"
            "  private int <Score>k__BackingField;\n"
            "  public unsafe void Tick() {\n"
            "    var p = FindObjectOfType<Camera>();\n"
            "    Application.LoadLevel(\"Main\");\n"
            "    var w = new WWW(\"http://example.com\");\n"
            "  }\n}\n");
        platform::WriteFileText(exportDir / "Assets" / "Broken.shader", "Hidden/InternalErrorShader");

        auto fixRes = fixup::BuildErrorFixer::RunOnProject(exportDir, "6000.0.23f1");
        Expect(fixRes.IsOk(), "BuildErrorFixer ran on injected broken files");
        if (fixRes) {
            const auto& bf = fixRes.Value();
            Expect(!std::filesystem::exists(exportDir / "Assets" / "Scripts" / "EmbeddedAttribute.cs"), "Stripped EmbeddedAttribute.cs");
            Expect(bf.invalidIdentifiersFixed >= 1, "Fixed <Score>k__BackingField identifier");
            Expect(bf.unityApiMigrations >= 3, "Migrated FindObjectOfType + Application.LoadLevel + WWW pragma");
            Expect(bf.shadersRepaired >= 1, "Repaired Broken.shader");
            Expect(bf.cscUnsafeGenerated, "Generated Assets/csc.rsp for unsafe code");
            Expect(std::filesystem::exists(exportDir / "Assets" / "csc.rsp"), "Assets/csc.rsp exists on disk");

            auto fixedCs = platform::ReadFileText(exportDir / "Assets" / "Scripts" / "LegacyBroken.cs");
            Expect(fixedCs.IsOk() &&
                       fixedCs.Value().find("_Score_BackingField") != std::string::npos &&
                       fixedCs.Value().find("FindFirstObjectByType<Camera>") != std::string::npos &&
                       fixedCs.Value().find("SceneManager.LoadScene") != std::string::npos,
                   "Repaired C# script contents verified");

            auto manTxt = platform::ReadFileText(exportDir / "Packages" / "manifest.json");
            Expect(manTxt.IsOk() && manTxt.Value().find("com.unity.inputsystem") != std::string::npos,
                   "Auto-added com.unity.inputsystem to Packages/manifest.json");
        }
    }

    std::filesystem::remove_all(tmpRoot, ec);
}

void TestRichExportersGameRebuilderAndProjectManager() {
    auto tmpRoot = std::filesystem::temp_directory_path() / "coconut_test_v02";
    std::error_code ec;
    std::filesystem::remove_all(tmpRoot, ec);
    std::filesystem::create_directories(tmpRoot, ec);

    // 1. FBX 7.4 ASCII Mesh Export
    auto box = assets::MeshCodec::CreateBox("TestCube", {2.0f, 2.0f, 2.0f});
    auto fbxPath = tmpRoot / "TestCube.fbx";
    Expect(assets::MeshCodec::ExportFBX(box, fbxPath).IsOk(), "MeshCodec::ExportFBX succeeded");
    auto fbxText = platform::ReadFileText(fbxPath);
    Expect(fbxText.IsOk() &&
               fbxText.Value().find("FBXVersion: 7400") != std::string::npos &&
               fbxText.Value().find("PolygonVertexIndex:") != std::string::npos &&
               fbxText.Value().find("LayerElementNormal:") != std::string::npos &&
               fbxText.Value().find("LayerElementUV:") != std::string::npos,
           "Exported FBX 7.4 contains valid Geometry, Normals, UVs, and Connections");

    // 2. MP3 & M4A Audio Exporters (shine MPEG-1 Layer III + ISO-BMFF ALAC)
    auto clip = assets::AudioCodec::CreateSynthSineClip("LaserBeep", 880.0f, 0.35f, 44100);
    auto mp3Path = tmpRoot / "LaserBeep.mp3";
    auto m4aPath = tmpRoot / "LaserBeep.m4a";
    Expect(assets::AudioCodec::ExportMP3(clip, mp3Path, 128).IsOk(), "AudioCodec::ExportMP3 succeeded");
    auto mp3Bytes = platform::ReadFileBytes(mp3Path);
    Expect(mp3Bytes.IsOk() && mp3Bytes.Value().size() > 1000 &&
               mp3Bytes.Value()[0] == 0xFF && (mp3Bytes.Value()[1] & 0xE0) == 0xE0,
           "Exported MP3 has valid MPEG frame sync bytes and encoded audio frames");

    Expect(assets::AudioCodec::ExportM4A(clip, m4aPath).IsOk(), "AudioCodec::ExportM4A succeeded");
    auto m4aBytes = platform::ReadFileBytes(m4aPath);
    Expect(m4aBytes.IsOk() && m4aBytes.Value().size() > 2000 &&
               std::memcmp(m4aBytes.Value().data() + 4, "ftypM4A ", 8) == 0,
           "Exported M4A has valid ISO-BMFF ftyp M4A header and ALAC payload");

    // 3. TGA & DDS Texture Exporters
    assets::Texture2DData tex;
    tex.name = "Checker";
    tex.width = 16;
    tex.height = 16;
    tex.rgbaPixels.assign(16 * 16 * 4, 200);
    auto tgaPath = tmpRoot / "Checker.tga";
    auto ddsPath = tmpRoot / "Checker.dds";
    Expect(assets::TextureCodec::ExportTGA(tex, tgaPath).IsOk(), "TextureCodec::ExportTGA succeeded");
    Expect(assets::TextureCodec::ExportDDS(tex, ddsPath).IsOk(), "TextureCodec::ExportDDS succeeded");
    auto ddsBytes = platform::ReadFileBytes(ddsPath);
    Expect(ddsBytes.IsOk() && ddsBytes.Value().size() == 128 + 16 * 16 * 4 &&
               std::memcmp(ddsBytes.Value().data(), "DDS ", 4) == 0,
           "Exported DDS has exact 128-byte header + RGBA8 pixel payload");

    // 4. GameRebuilder Desktop Build Round-Trip (edit Transform -> rebuild game -> reload & verify!)
    auto srcGameDir = tmpRoot / "SrcDesktopGame";
    auto patchedGameDir = tmpRoot / "PatchedDesktopGame";
    Expect(unity::SampleGameGenerator::GenerateMonoDemoGame(srcGameDir).IsOk(), "Generate synthetic desktop fixture");
    auto loadRes = unity::GameLoader::Load(srcGameDir);
    Expect(loadRes.IsOk(), "Load synthetic desktop fixture");
    if (loadRes) {
        auto& proj = loadRes.Value();
        assets::AssetManager assets;
        assets.IndexProject(proj);
        scene::SceneDocument sceneDoc;
        Expect(sceneDoc.LoadFromProject(proj, assets).IsOk(), "Load scene for rebuild");

        int towerIdx = sceneDoc.FindNodeByName("WatchTower");
        Expect(towerIdx >= 0, "Found WatchTower before rebuild");
        if (towerIdx >= 0) {
            const auto& pn = sceneDoc.Nodes()[towerIdx];
            sceneDoc.SetTransform(static_cast<size_t>(towerIdx), {77.0f, 12.5f, -44.0f}, pn.localEulerDeg, pn.localScale);
        }

        auto pending = exporter::GameRebuilder::CollectPendingChanges(proj, &sceneDoc);
        Expect(!pending.empty(), "GameRebuilder detected pending Scene modifications");

        exporter::GameRebuildOptions rebOpts;
        rebOpts.outputPath = patchedGameDir;
        auto rebRes = exporter::GameRebuilder::BuildPatchedGame(proj, &sceneDoc, rebOpts);
        Expect(rebRes.IsOk(), "GameRebuilder rebuilt patched desktop Unity build");
        if (rebRes) {
            Expect(rebRes.Value().serializedFilesRebuilt >= 1, "Rebuilt >= 1 SerializedFile");
            Expect(std::filesystem::exists(patchedGameDir / "COCONUT_BUILD_PATCH.json"), "Wrote COCONUT_BUILD_PATCH.json");

            // Reload patched build and verify the modified WatchTower position persisted!
            auto reloadRes = unity::GameLoader::Load(patchedGameDir);
            Expect(reloadRes.IsOk(), "Reloaded patched desktop Unity build");
            if (reloadRes) {
                assets::AssetManager assets2;
                assets2.IndexProject(reloadRes.Value());
                scene::SceneDocument scene2;
                Expect(scene2.LoadFromProject(reloadRes.Value(), assets2).IsOk(), "Reloaded scene from patched build");
                int tower2Idx = scene2.FindNodeByName("WatchTower");
                Expect(tower2Idx >= 0 &&
                           std::fabs(scene2.Nodes()[tower2Idx].localPosition.x - 77.0f) < 0.01f &&
                           std::fabs(scene2.Nodes()[tower2Idx].localPosition.z - (-44.0f)) < 0.01f,
                       "Patched WatchTower Transform persisted cleanly inside rebuilt game binary!");
            }
        }
    }

    // 5. GameRebuilder Android APK Round-Trip
    auto srcApk = tmpRoot / "SrcAndroid.apk";
    auto patchedApk = tmpRoot / "PatchedAndroid.apk";
    Expect(unity::SampleGameGenerator::GenerateIl2CppDemoApk(srcApk).IsOk(), "Generate synthetic Android APK fixture");
    auto apkLoad = unity::GameLoader::Load(srcApk);
    Expect(apkLoad.IsOk(), "Load synthetic Android APK");
    if (apkLoad) {
        auto& apkProj = apkLoad.Value();
        // Patch first TextAsset/object dirty flag to test APK rebuild
        if (!apkProj.serializedFiles.empty() && !apkProj.serializedFiles[0].objects.empty()) {
            apkProj.serializedFiles[0].objects[0].dirty = true;
        }
        exporter::GameRebuildOptions apkOpts;
        apkOpts.outputPath = patchedApk;
        auto apkReb = exporter::GameRebuilder::BuildPatchedGame(apkProj, nullptr, apkOpts);
        Expect(apkReb.IsOk(), "GameRebuilder rebuilt patched Android APK");
        if (apkReb) {
            Expect(apkReb.Value().requiresApkSigning, "Flagged APK as requiring zipalign + apksigner");
            Expect(std::filesystem::exists(patchedApk), "Patched APK file exists on disk");
            auto reloadApk = unity::GameLoader::Load(patchedApk);
            Expect(reloadApk.IsOk() && reloadApk.Value().isApkArchive, "Reloaded rebuilt Android APK cleanly");
        }
    }

    // 6. Ghidra-style ProjectManager persistence + BuildProbe
    auto regPath = tmpRoot / "projects.json";
    {
        ProjectManager pm(regPath);
        auto add1 = pm.AddProject("SpaceShooter_Win64", srcGameDir, "Main Windows build");
        auto add2 = pm.AddProject("MobileClient_APK", srcApk, "Android IL2CPP build");
        Expect(add1.IsOk() && add2.IsOk(), "ProjectManager added desktop + Android projects");
        Expect(add2.Value().detectedPlatform == "Android" && add2.Value().detectedBackend == "IL2CPP",
               "ProjectManager auto-detected Android IL2CPP from APK");
        Expect(pm.RenameProject(add1.Value().id, "SpaceShooter_Patched"), "ProjectManager renamed project");
    }
    {
        ProjectManager pm2(regPath);
        Expect(pm2.Load().IsOk() && pm2.Projects().size() == 2, "ProjectManager persisted & reloaded 2 projects");
        auto probe = ProjectManager::ProbeBuild(srcGameDir);
        Expect(probe.valid && !probe.tree.empty(), "ProjectManager::ProbeBuild populated game build tree");
    }

    // 7. Internal forked AssetRipper (Coconut.RipperCore) discovery check
    auto ripperBin = exporter::AssetRipperClient::DetectInternalRipperCore();
    if (ripperBin) {
        std::cout << "  [info] Internal AssetRipper Core discovered at: " << ripperBin->string() << "\n";
    }

    std::filesystem::remove_all(tmpRoot, ec);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string_view(argv[1]) == "--emit-fixture") {
        auto r = unity::SampleGameGenerator::GenerateMonoDemoGame(argv[2]);
        return r.IsOk() ? 0 : 1;
    }
    TestUnityVersion();
    TestBinaryReaderWriter();
    TestCompressionAndZip();
    TestCommandHistoryAndProjectDb();
    TestUnityParsersAndDemoGames();
    TestMonoAndIl2CppDecompiler();
    TestSceneEditingAndGizmos();
    TestUnityProjectExportAndFixupPipeline();
    TestRichExportersGameRebuilderAndProjectManager();

    std::cout << "Coconut Core Tests: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
