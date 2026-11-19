#pragma once

#include "core/Types.h"
#include "unity/SerializedFile.h"
#include "unity/GameLoader.h"
#include <filesystem>
#include <string>

namespace coconut::assets {

enum class UnityTextureFormat : i32 {
    Alpha8       = 1,
    ARGB4444     = 2,
    RGB24        = 3,
    RGBA32       = 4,
    ARGB32       = 5,
    RGB565       = 7,
    R16          = 9,
    DXT1         = 10,
    DXT5         = 12,
    RGBA4444     = 13,
    BGRA32       = 14,
    BC4          = 26,
    BC5          = 27,
    BC7          = 25,
    ETC_RGB4     = 34,
    ETC2_RGB     = 45,
    ETC2_RGBA8   = 47,
    ASTC_4x4     = 48,
    ASTC_6x6     = 50,
    ASTC_8x8     = 52,
    R8           = 63
};

struct Texture2DData {
    std::string name;
    i32 width = 0;
    i32 height = 0;
    UnityTextureFormat format = UnityTextureFormat::RGBA32;
    i32 mipCount = 1;
    std::string streamPath;
    u64 streamOffset = 0;
    u32 streamSize = 0;

    ByteBuffer encodedData; // Raw Unity texture bytes
    ByteBuffer rgbaPixels;  // Decoded RGBA8 (width * height * 4)

    bool IsValid() const { return width > 0 && height > 0 && rgbaPixels.size() == static_cast<size_t>(width) * height * 4; }
};

class TextureCodec {
public:
    static std::string_view FormatName(UnityTextureFormat fmt);

    static Result<Texture2DData> ParseFromObject(
        const unity::SerializedFile& sf,
        const unity::ObjectInfo& obj,
        const unity::LoadedGameProject* project = nullptr);

    // Decode Unity compressed or uncompressed texture formats into RGBA8
    static Result<ByteBuffer> DecodeToRGBA8(
        ByteSpan raw,
        i32 width,
        i32 height,
        UnityTextureFormat format);

    // Live image edits
    static void FlipVertical(Texture2DData& tex);
    static void InvertRGB(Texture2DData& tex);
    static void ApplyTint(Texture2DData& tex, f32 rMul, f32 gMul, f32 bMul);
    static void ConvertGrayscale(Texture2DData& tex);

    // Export PNG, TGA, DDS & Import PNG
    static Result<void> ExportPNG(const Texture2DData& tex, const std::filesystem::path& pngPath);
    static Result<void> ExportTGA(const Texture2DData& tex, const std::filesystem::path& tgaPath);
    static Result<void> ExportDDS(const Texture2DData& tex, const std::filesystem::path& ddsPath);
    static Result<Texture2DData> ImportPNG(const std::filesystem::path& pngPath, const std::string& name = {});

    // Write modified RGBA32 texture back into a Unity SerializedFile object
    static Result<void> WriteBackToObject(unity::SerializedFile& sf, unity::ObjectInfo& obj, const Texture2DData& tex);
};

} // namespace coconut::assets
