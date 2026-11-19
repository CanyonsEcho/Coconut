#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/Compression.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include "core/Types.h"
#include "platform/Platform.h"
#include "unity/ClassIDs.h"
#include "unity/UnityFS.h"
#include "unity/SerializedFile.h"
#include "unity/GameLoader.h"
#include "unity/SampleGameGenerator.h"
#include "assets/TextureAsset.h"
#include "assets/MeshAsset.h"
#include "assets/AudioAsset.h"
#include "assets/MaterialAsset.h"
#include "assets/AssetManager.h"

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

} // namespace

int main() {
    TestUnityVersion();
    TestBinaryReaderWriter();
    TestCompressionAndZip();
    TestCommandHistoryAndProjectDb();
    TestUnityParsersAndDemoGames();

    std::cout << "Coconut Core Tests: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
