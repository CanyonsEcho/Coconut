#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/Compression.h"
#include "core/CommandHistory.h"
#include "core/ProjectDatabase.h"
#include "core/Types.h"
#include "platform/Platform.h"

#include <cmath>
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

    // ZIP/APK container round-trip
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

} // namespace

int main() {
    TestUnityVersion();
    TestBinaryReaderWriter();
    TestCompressionAndZip();
    TestCommandHistoryAndProjectDb();

    std::cout << "Coconut Core Tests: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
