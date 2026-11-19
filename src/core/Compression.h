#pragma once

#include "core/Types.h"
#include <filesystem>
#include <vector>

namespace coconut {

enum class CompressionType : u32 {
    None  = 0,
    LZMA  = 1,
    LZ4   = 2,
    LZ4HC = 3,
    Zlib  = 4
};

class Compression {
public:
    static Result<ByteBuffer> DecompressLZ4(ByteSpan compressed, size_t uncompressedSize);
    static Result<ByteBuffer> CompressLZ4(ByteSpan uncompressed, bool highCompression = false);

    static Result<ByteBuffer> DecompressLZMA(ByteSpan compressedWithProps, size_t uncompressedSize);

    static Result<ByteBuffer> DecompressZlib(ByteSpan compressed, size_t maxUncompressedSize = 64 * 1024 * 1024);
    static Result<ByteBuffer> CompressZlib(ByteSpan uncompressed, int level = 6);

    static Result<ByteBuffer> Decompress(CompressionType type, ByteSpan input, size_t uncompressedSize);
};

struct ZipEntryInfo {
    u32 index = 0;
    std::string name;
    u64 uncompressedSize = 0;
    u64 compressedSize = 0;
    bool isDirectory = false;
};

// ZIP / Android APK / AAB archive reader & writer backed by miniz.
class ZipArchive {
public:
    ZipArchive() = default;
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;
    ZipArchive(ZipArchive&& other) noexcept;
    ZipArchive& operator=(ZipArchive&& other) noexcept;

    Result<void> OpenFile(const std::filesystem::path& path);
    Result<void> OpenMemory(ByteSpan buffer);
    void Close();

    bool IsOpen() const { return impl_ != nullptr; }
    std::vector<ZipEntryInfo> ListEntries() const;
    bool HasEntry(std::string_view name) const;
    Result<ByteBuffer> ReadEntry(std::string_view name) const;
    Result<ByteBuffer> ReadEntryByIndex(u32 index) const;

    // Convenience helper to build a ZIP/APK archive in memory or on disk.
    static Result<void> WriteZipFile(
        const std::filesystem::path& outPath,
        const std::vector<std::pair<std::string, ByteBuffer>>& entries);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace coconut
