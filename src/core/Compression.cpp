#include "core/Compression.h"
#include "core/LzmaDecoder.h"

#include <lz4.h>
#include <lz4hc.h>
#include <miniz.h>

#include <cstring>

namespace coconut {

Result<ByteBuffer> Compression::DecompressLZ4(ByteSpan compressed, size_t uncompressedSize) {
    if (uncompressedSize == 0) {
        return Result<ByteBuffer>::Ok({});
    }
    ByteBuffer out(uncompressedSize);
    int decoded = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(compressed.size()),
        static_cast<int>(uncompressedSize));
    if (decoded < 0) {
        return Result<ByteBuffer>::Err("LZ4_decompress_safe failed (corrupted stream or wrong uncompressed size)");
    }
    out.resize(static_cast<size_t>(decoded));
    return Result<ByteBuffer>::Ok(std::move(out));
}

Result<ByteBuffer> Compression::CompressLZ4(ByteSpan uncompressed, bool highCompression) {
    if (uncompressed.empty()) {
        return Result<ByteBuffer>::Ok({});
    }
    int maxDst = LZ4_compressBound(static_cast<int>(uncompressed.size()));
    ByteBuffer out(static_cast<size_t>(maxDst));
    int written = 0;
    if (highCompression) {
        written = LZ4_compress_HC(
            reinterpret_cast<const char*>(uncompressed.data()),
            reinterpret_cast<char*>(out.data()),
            static_cast<int>(uncompressed.size()),
            maxDst,
            LZ4HC_CLEVEL_DEFAULT);
    } else {
        written = LZ4_compress_default(
            reinterpret_cast<const char*>(uncompressed.data()),
            reinterpret_cast<char*>(out.data()),
            static_cast<int>(uncompressed.size()),
            maxDst);
    }
    if (written <= 0) {
        return Result<ByteBuffer>::Err("LZ4 compression failed");
    }
    out.resize(static_cast<size_t>(written));
    return Result<ByteBuffer>::Ok(std::move(out));
}

Result<ByteBuffer> Compression::DecompressLZMA(ByteSpan compressedWithProps, size_t uncompressedSize) {
    return LzmaDecoder::Decompress(compressedWithProps, uncompressedSize);
}

Result<ByteBuffer> Compression::DecompressZlib(ByteSpan compressed, size_t maxUncompressedSize) {
    if (compressed.empty()) {
        return Result<ByteBuffer>::Ok({});
    }
    size_t outLen = 0;
    void* raw = tinfl_decompress_mem_to_heap(
        compressed.data(),
        compressed.size(),
        &outLen,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (!raw) {
        // Fallback: try raw deflate without zlib header
        raw = tinfl_decompress_mem_to_heap(compressed.data(), compressed.size(), &outLen, 0);
    }
    if (!raw) {
        return Result<ByteBuffer>::Err("Zlib/Deflate decompression failed");
    }
    if (outLen > maxUncompressedSize) {
        mz_free(raw);
        return Result<ByteBuffer>::Err("Zlib decompressed size exceeds limit");
    }
    ByteBuffer out(static_cast<const u8*>(raw), static_cast<const u8*>(raw) + outLen);
    mz_free(raw);
    return Result<ByteBuffer>::Ok(std::move(out));
}

Result<ByteBuffer> Compression::CompressZlib(ByteSpan uncompressed, int level) {
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(uncompressed.size()));
    ByteBuffer out(bound);
    mz_ulong destLen = bound;
    int status = mz_compress2(
        out.data(), &destLen,
        uncompressed.data(), static_cast<mz_ulong>(uncompressed.size()),
        level);
    if (status != MZ_OK) {
        return Result<ByteBuffer>::Err("Zlib compression failed with status " + std::to_string(status));
    }
    out.resize(static_cast<size_t>(destLen));
    return Result<ByteBuffer>::Ok(std::move(out));
}

Result<ByteBuffer> Compression::Decompress(CompressionType type, ByteSpan input, size_t uncompressedSize) {
    switch (type) {
        case CompressionType::None:
            return Result<ByteBuffer>::Ok(ByteBuffer(input.begin(), input.end()));
        case CompressionType::LZMA:
            return DecompressLZMA(input, uncompressedSize);
        case CompressionType::LZ4:
        case CompressionType::LZ4HC:
            return DecompressLZ4(input, uncompressedSize);
        case CompressionType::Zlib:
            return DecompressZlib(input, uncompressedSize > 0 ? uncompressedSize : 64 * 1024 * 1024);
    }
    return Result<ByteBuffer>::Err("Unsupported compression type");
}

// -----------------------------------------------------------------------------
// ZipArchive (miniz)
// -----------------------------------------------------------------------------

struct ZipArchive::Impl {
    mz_zip_archive archive{};
    ByteBuffer ownedMemory;
};

ZipArchive::~ZipArchive() {
    Close();
}

ZipArchive::ZipArchive(ZipArchive&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

ZipArchive& ZipArchive::operator=(ZipArchive&& other) noexcept {
    if (this != &other) {
        Close();
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

Result<void> ZipArchive::OpenFile(const std::filesystem::path& path) {
    Close();
    auto* impl = new Impl();
    std::memset(&impl->archive, 0, sizeof(impl->archive));
    std::string u8path = path.string();
    if (!mz_zip_reader_init_file(&impl->archive, u8path.c_str(), 0)) {
        delete impl;
        return Result<void>::Err("Failed to open ZIP/APK archive: " + u8path);
    }
    impl_ = impl;
    return Result<void>::Ok();
}

Result<void> ZipArchive::OpenMemory(ByteSpan buffer) {
    Close();
    auto* impl = new Impl();
    std::memset(&impl->archive, 0, sizeof(impl->archive));
    impl->ownedMemory.assign(buffer.begin(), buffer.end());
    if (!mz_zip_reader_init_mem(&impl->archive, impl->ownedMemory.data(), impl->ownedMemory.size(), 0)) {
        delete impl;
        return Result<void>::Err("Failed to open ZIP/APK archive from memory");
    }
    impl_ = impl;
    return Result<void>::Ok();
}

void ZipArchive::Close() {
    if (impl_) {
        mz_zip_reader_end(&impl_->archive);
        delete impl_;
        impl_ = nullptr;
    }
}

std::vector<ZipEntryInfo> ZipArchive::ListEntries() const {
    std::vector<ZipEntryInfo> entries;
    if (!impl_) return entries;
    mz_uint count = mz_zip_reader_get_num_files(&impl_->archive);
    entries.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&impl_->archive, i, &stat)) {
            ZipEntryInfo info;
            info.index = i;
            info.name = stat.m_filename;
            info.uncompressedSize = stat.m_uncomp_size;
            info.compressedSize = stat.m_comp_size;
            info.isDirectory = stat.m_is_directory != 0;
            entries.push_back(std::move(info));
        }
    }
    return entries;
}

bool ZipArchive::HasEntry(std::string_view name) const {
    if (!impl_) return false;
    std::string s(name);
    return mz_zip_reader_locate_file(&impl_->archive, s.c_str(), nullptr, 0) >= 0;
}

Result<ByteBuffer> ZipArchive::ReadEntry(std::string_view name) const {
    if (!impl_) return Result<ByteBuffer>::Err("ZIP archive is not open");
    std::string s(name);
    int idx = mz_zip_reader_locate_file(&impl_->archive, s.c_str(), nullptr, 0);
    if (idx < 0) {
        return Result<ByteBuffer>::Err("Entry not found in ZIP archive: " + s);
    }
    return ReadEntryByIndex(static_cast<u32>(idx));
}

Result<ByteBuffer> ZipArchive::ReadEntryByIndex(u32 index) const {
    if (!impl_) return Result<ByteBuffer>::Err("ZIP archive is not open");
    size_t uncompSize = 0;
    void* ptr = mz_zip_reader_extract_to_heap(&impl_->archive, index, &uncompSize, 0);
    if (!ptr) {
        return Result<ByteBuffer>::Err("Failed to extract ZIP entry index " + std::to_string(index));
    }
    ByteBuffer out(static_cast<const u8*>(ptr), static_cast<const u8*>(ptr) + uncompSize);
    mz_free(ptr);
    return Result<ByteBuffer>::Ok(std::move(out));
}

Result<void> ZipArchive::WriteZipFile(
    const std::filesystem::path& outPath,
    const std::vector<std::pair<std::string, ByteBuffer>>& entries) {
    mz_zip_archive zip{};
    std::string pathStr = outPath.string();
    if (!mz_zip_writer_init_file(&zip, pathStr.c_str(), 0)) {
        return Result<void>::Err("Failed to create ZIP file: " + pathStr);
    }
    for (const auto& [name, data] : entries) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            return Result<void>::Err("Failed to add entry to ZIP: " + name);
        }
    }
    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        return Result<void>::Err("Failed to finalize ZIP archive: " + pathStr);
    }
    mz_zip_writer_end(&zip);
    return Result<void>::Ok();
}

} // namespace coconut
