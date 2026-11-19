#include "assets/TextureAsset.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace coconut::assets {

namespace {

inline u8 ClampByte(int v) {
    return static_cast<u8>(std::clamp(v, 0, 255));
}

void DecodeRGB565(u16 c, u8& r, u8& g, u8& b) {
    r = static_cast<u8>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<u8>(((c >> 5) & 0x3F) * 255 / 63);
    b = static_cast<u8>((c & 0x1F) * 255 / 31);
}

void DecodeDXT1Block(const u8* block, u8* outRgba, int stride, int maxW, int maxH) {
    u16 c0 = static_cast<u16>(block[0] | (block[1] << 8));
    u16 c1 = static_cast<u16>(block[2] | (block[3] << 8));
    u8 col[4][4]{};
    DecodeRGB565(c0, col[0][0], col[0][1], col[0][2]); col[0][3] = 255;
    DecodeRGB565(c1, col[1][0], col[1][1], col[1][2]); col[1][3] = 255;

    if (c0 > c1) {
        for (int i = 0; i < 3; ++i) {
            col[2][i] = static_cast<u8>((2 * col[0][i] + col[1][i]) / 3);
            col[3][i] = static_cast<u8>((col[0][i] + 2 * col[1][i]) / 3);
        }
        col[2][3] = 255;
        col[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) {
            col[2][i] = static_cast<u8>((col[0][i] + col[1][i]) / 2);
            col[3][i] = 0;
        }
        col[2][3] = 255;
        col[3][3] = 0;
    }

    u32 bits = static_cast<u32>(block[4]) |
               (static_cast<u32>(block[5]) << 8) |
               (static_cast<u32>(block[6]) << 16) |
               (static_cast<u32>(block[7]) << 24);

    for (int py = 0; py < 4 && py < maxH; ++py) {
        for (int px = 0; px < 4 && px < maxW; ++px) {
            u32 code = (bits >> (2 * (py * 4 + px))) & 3u;
            u8* dst = outRgba + py * stride + px * 4;
            dst[0] = col[code][0];
            dst[1] = col[code][1];
            dst[2] = col[code][2];
            dst[3] = col[code][3];
        }
    }
}

void DecodeBC4AlphaBlock(const u8* block, u8* outChannel, int stride, int channelOffset, int maxW, int maxH) {
    u8 a0 = block[0];
    u8 a1 = block[1];
    u8 lut[8]{};
    lut[0] = a0;
    lut[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            lut[i + 1] = static_cast<u8>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            lut[i + 1] = static_cast<u8>(((5 - i) * a0 + i * a1) / 5);
        }
        lut[6] = 0;
        lut[7] = 255;
    }

    u64 bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<u64>(block[2 + i]) << (i * 8);
    }

    for (int py = 0; py < 4 && py < maxH; ++py) {
        for (int px = 0; px < 4 && px < maxW; ++px) {
            u32 idx = static_cast<u32>((bits >> (3 * (py * 4 + px))) & 7u);
            outChannel[py * stride + px * 4 + channelOffset] = lut[idx];
        }
    }
}

void DecodeETC1Block(const u8* block, u8* outRgba, int stride, int maxW, int maxH) {
    static constexpr int kEtcMod[8][4] = {
        {  2,   8,  -2,   -8 }, {  5,  17,  -5,  -17 },
        {  9,  29,  -9,  -29 }, { 13,  42, -13,  -42 },
        { 18,  60, -18,  -60 }, { 24,  80, -24,  -80 },
        { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
    };
    static constexpr int kPixOrder[4] = { 2, 3, 0, 1 };

    bool diff = (block[3] & 0x02) != 0;
    bool flip = (block[3] & 0x01) != 0;
    int r1, g1, b1, r2, g2, b2;
    if (diff) {
        int r = block[0] >> 3;
        int g = block[1] >> 3;
        int b = block[2] >> 3;
        auto sext3 = [](int v) { return (v & 4) ? (v - 8) : v; };
        int dr = sext3(block[0] & 7);
        int dg = sext3(block[1] & 7);
        int db = sext3(block[2] & 7);
        r1 = (r << 3) | (r >> 2);
        g1 = (g << 3) | (g >> 2);
        b1 = (b << 3) | (b >> 2);
        int rr2 = std::clamp(r + dr, 0, 31);
        int gg2 = std::clamp(g + dg, 0, 31);
        int bb2 = std::clamp(b + db, 0, 31);
        r2 = (rr2 << 3) | (rr2 >> 2);
        g2 = (gg2 << 3) | (gg2 >> 2);
        b2 = (bb2 << 3) | (bb2 >> 2);
    } else {
        r1 = (block[0] & 0xF0) | (block[0] >> 4);
        r2 = ((block[0] & 0x0F) << 4) | (block[0] & 0x0F);
        g1 = (block[1] & 0xF0) | (block[1] >> 4);
        g2 = ((block[1] & 0x0F) << 4) | (block[1] & 0x0F);
        b1 = (block[2] & 0xF0) | (block[2] >> 4);
        b2 = ((block[2] & 0x0F) << 4) | (block[2] & 0x0F);
    }

    int t1 = (block[3] >> 5) & 7;
    int t2 = (block[3] >> 2) & 7;
    u16 msb = static_cast<u16>((block[4] << 8) | block[5]);
    u16 lsb = static_cast<u16>((block[6] << 8) | block[7]);

    for (int px = 0; px < 4 && px < maxW; ++px) {
        for (int py = 0; py < 4 && py < maxH; ++py) {
            int bitIdx = px * 4 + py;
            int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
            bool second = flip ? (py >= 2) : (px >= 2);
            int mod = kEtcMod[second ? t2 : t1][kPixOrder[idx]];
            u8* dst = outRgba + py * stride + px * 4;
            dst[0] = ClampByte((second ? r2 : r1) + mod);
            dst[1] = ClampByte((second ? g2 : g1) + mod);
            dst[2] = ClampByte((second ? b2 : b1) + mod);
            dst[3] = 255;
        }
    }
}

} // namespace

std::string_view TextureCodec::FormatName(UnityTextureFormat fmt) {
    switch (fmt) {
        case UnityTextureFormat::Alpha8:     return "Alpha8";
        case UnityTextureFormat::ARGB4444:   return "ARGB4444";
        case UnityTextureFormat::RGB24:      return "RGB24";
        case UnityTextureFormat::RGBA32:     return "RGBA32";
        case UnityTextureFormat::ARGB32:     return "ARGB32";
        case UnityTextureFormat::RGB565:     return "RGB565";
        case UnityTextureFormat::R16:        return "R16";
        case UnityTextureFormat::DXT1:       return "DXT1 (BC1)";
        case UnityTextureFormat::DXT5:       return "DXT5 (BC3)";
        case UnityTextureFormat::RGBA4444:   return "RGBA4444";
        case UnityTextureFormat::BGRA32:     return "BGRA32";
        case UnityTextureFormat::BC4:        return "BC4";
        case UnityTextureFormat::BC5:        return "BC5";
        case UnityTextureFormat::BC7:        return "BC7";
        case UnityTextureFormat::ETC_RGB4:   return "ETC1 RGB4";
        case UnityTextureFormat::ETC2_RGB:   return "ETC2 RGB";
        case UnityTextureFormat::ETC2_RGBA8: return "ETC2 RGBA8";
        case UnityTextureFormat::ASTC_4x4:   return "ASTC 4x4";
        case UnityTextureFormat::ASTC_6x6:   return "ASTC 6x6";
        case UnityTextureFormat::ASTC_8x8:   return "ASTC 8x8";
        case UnityTextureFormat::R8:         return "R8";
    }
    return "Unknown";
}

Result<Texture2DData> TextureCodec::ParseFromObject(
    const unity::SerializedFile& sf,
    const unity::ObjectInfo& obj,
    const unity::LoadedGameProject* project) {
    Texture2DData tex;
    tex.name = obj.cachedName;

    auto valRes = sf.ReadObjectValue(obj);
    if (valRes) {
        const auto& root = valRes.Value();
        if (auto* f = root.FindField("m_Name")) tex.name = f->AsString(tex.name);
        if (auto* f = root.FindField("m_Width")) tex.width = static_cast<i32>(f->AsInt());
        if (auto* f = root.FindField("m_Height")) tex.height = static_cast<i32>(f->AsInt());
        if (auto* f = root.FindField("m_TextureFormat")) tex.format = static_cast<UnityTextureFormat>(f->AsInt());
        if (auto* f = root.FindField("m_MipCount")) tex.mipCount = std::max<i32>(1, static_cast<i32>(f->AsInt()));

        if (auto* f = root.FindField("image data")) {
            if (auto* bytes = std::get_if<ByteBuffer>(&f->value)) {
                tex.encodedData = *bytes;
            }
        }
        if (auto* sd = root.FindField("m_StreamData")) {
            if (auto* off = sd->FindField("offset")) tex.streamOffset = off->AsUInt();
            if (auto* sz = sd->FindField("size")) tex.streamSize = static_cast<u32>(sz->AsUInt());
            if (auto* path = sd->FindField("path")) tex.streamPath = path->AsString();
        }
    } else {
        // Fallback binary layout for standard Unity 2019-2023 Texture2D without TypeTree
        BinaryReader r(obj.rawData, sf.header.bigEndian ? Endian::Big : Endian::Little);
        tex.name = r.ReadAlignedString();
        tex.width = r.ReadI32();
        tex.height = r.ReadI32();
        i32 completeImageSize = r.ReadI32();
        (void)completeImageSize;
        tex.format = static_cast<UnityTextureFormat>(r.ReadI32());
        tex.mipCount = std::max<i32>(1, r.ReadI32());
        i32 dataLen = r.ReadI32();
        if (dataLen > 0 && static_cast<size_t>(dataLen) <= r.Remaining()) {
            tex.encodedData = r.ReadBytes(static_cast<size_t>(dataLen));
        }
    }

    // Load external .resS stream if inline image data is empty
    if (tex.encodedData.empty() && !tex.streamPath.empty() && tex.streamSize > 0 && project) {
        if (const auto* stream = project->FindResourceStream(tex.streamPath)) {
            if (tex.streamOffset + tex.streamSize <= stream->size()) {
                tex.encodedData.assign(
                    stream->begin() + static_cast<std::ptrdiff_t>(tex.streamOffset),
                    stream->begin() + static_cast<std::ptrdiff_t>(tex.streamOffset + tex.streamSize));
            }
        }
    }

    if (tex.width <= 0 || tex.height <= 0 || tex.width > 16384 || tex.height > 16384) {
        return Result<Texture2DData>::Err("Invalid Texture2D dimensions");
    }

    if (!tex.encodedData.empty()) {
        auto rgba = DecodeToRGBA8(tex.encodedData, tex.width, tex.height, tex.format);
        if (!rgba) return Result<Texture2DData>::Err(rgba.Error());
        tex.rgbaPixels = std::move(rgba.Value());
    }
    return Result<Texture2DData>::Ok(std::move(tex));
}

Result<ByteBuffer> TextureCodec::DecodeToRGBA8(
    ByteSpan raw,
    i32 width,
    i32 height,
    UnityTextureFormat format) {
    if (width <= 0 || height <= 0) {
        return Result<ByteBuffer>::Err("Invalid texture dimensions");
    }
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    ByteBuffer out(pixelCount * 4, 255);
    const int stride = width * 4;

    switch (format) {
        case UnityTextureFormat::RGBA32: {
            size_t copy = std::min(raw.size(), out.size());
            std::memcpy(out.data(), raw.data(), copy);
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::RGB24: {
            size_t maxPix = std::min(pixelCount, raw.size() / 3);
            for (size_t i = 0; i < maxPix; ++i) {
                out[i * 4 + 0] = raw[i * 3 + 0];
                out[i * 4 + 1] = raw[i * 3 + 1];
                out[i * 4 + 2] = raw[i * 3 + 2];
                out[i * 4 + 3] = 255;
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::ARGB32: {
            size_t maxPix = std::min(pixelCount, raw.size() / 4);
            for (size_t i = 0; i < maxPix; ++i) {
                out[i * 4 + 0] = raw[i * 4 + 1];
                out[i * 4 + 1] = raw[i * 4 + 2];
                out[i * 4 + 2] = raw[i * 4 + 3];
                out[i * 4 + 3] = raw[i * 4 + 0];
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::BGRA32: {
            size_t maxPix = std::min(pixelCount, raw.size() / 4);
            for (size_t i = 0; i < maxPix; ++i) {
                out[i * 4 + 0] = raw[i * 4 + 2];
                out[i * 4 + 1] = raw[i * 4 + 1];
                out[i * 4 + 2] = raw[i * 4 + 0];
                out[i * 4 + 3] = raw[i * 4 + 3];
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::Alpha8: {
            size_t maxPix = std::min(pixelCount, raw.size());
            for (size_t i = 0; i < maxPix; ++i) {
                out[i * 4 + 0] = 255;
                out[i * 4 + 1] = 255;
                out[i * 4 + 2] = 255;
                out[i * 4 + 3] = raw[i];
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::R8: {
            size_t maxPix = std::min(pixelCount, raw.size());
            for (size_t i = 0; i < maxPix; ++i) {
                out[i * 4 + 0] = raw[i];
                out[i * 4 + 1] = raw[i];
                out[i * 4 + 2] = raw[i];
                out[i * 4 + 3] = 255;
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::R16: {
            size_t maxPix = std::min(pixelCount, raw.size() / 2);
            for (size_t i = 0; i < maxPix; ++i) {
                u8 v = raw[i * 2 + 1];
                out[i * 4 + 0] = v;
                out[i * 4 + 1] = v;
                out[i * 4 + 2] = v;
                out[i * 4 + 3] = 255;
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::RGB565: {
            size_t maxPix = std::min(pixelCount, raw.size() / 2);
            for (size_t i = 0; i < maxPix; ++i) {
                u16 c = static_cast<u16>(raw[i * 2] | (raw[i * 2 + 1] << 8));
                DecodeRGB565(c, out[i * 4 + 0], out[i * 4 + 1], out[i * 4 + 2]);
                out[i * 4 + 3] = 255;
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::RGBA4444:
        case UnityTextureFormat::ARGB4444: {
            size_t maxPix = std::min(pixelCount, raw.size() / 2);
            for (size_t i = 0; i < maxPix; ++i) {
                u16 c = static_cast<u16>(raw[i * 2] | (raw[i * 2 + 1] << 8));
                if (format == UnityTextureFormat::RGBA4444) {
                    out[i * 4 + 0] = static_cast<u8>(((c >> 12) & 0xF) * 17);
                    out[i * 4 + 1] = static_cast<u8>(((c >> 8) & 0xF) * 17);
                    out[i * 4 + 2] = static_cast<u8>(((c >> 4) & 0xF) * 17);
                    out[i * 4 + 3] = static_cast<u8>((c & 0xF) * 17);
                } else {
                    out[i * 4 + 3] = static_cast<u8>(((c >> 12) & 0xF) * 17);
                    out[i * 4 + 0] = static_cast<u8>(((c >> 8) & 0xF) * 17);
                    out[i * 4 + 1] = static_cast<u8>(((c >> 4) & 0xF) * 17);
                    out[i * 4 + 2] = static_cast<u8>((c & 0xF) * 17);
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::DXT1: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 8 > raw.size()) break;
                    DecodeDXT1Block(raw.data() + pos, out.data() + (y * 4) * stride + (x * 4) * 4,
                                    stride, width - x * 4, height - y * 4);
                    pos += 8;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::DXT5: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 16 > raw.size()) break;
                    u8* dst = out.data() + (y * 4) * stride + (x * 4) * 4;
                    DecodeDXT1Block(raw.data() + pos + 8, dst, stride, width - x * 4, height - y * 4);
                    DecodeBC4AlphaBlock(raw.data() + pos, dst, stride, 3, width - x * 4, height - y * 4);
                    pos += 16;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::BC4: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 8 > raw.size()) break;
                    u8* dst = out.data() + (y * 4) * stride + (x * 4) * 4;
                    DecodeBC4AlphaBlock(raw.data() + pos, dst, stride, 0, width - x * 4, height - y * 4);
                    for (int py = 0; py < 4 && y * 4 + py < height; ++py) {
                        for (int px = 0; px < 4 && x * 4 + px < width; ++px) {
                            u8 v = dst[py * stride + px * 4 + 0];
                            dst[py * stride + px * 4 + 1] = v;
                            dst[py * stride + px * 4 + 2] = v;
                            dst[py * stride + px * 4 + 3] = 255;
                        }
                    }
                    pos += 8;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::BC5: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 16 > raw.size()) break;
                    u8* dst = out.data() + (y * 4) * stride + (x * 4) * 4;
                    DecodeBC4AlphaBlock(raw.data() + pos, dst, stride, 0, width - x * 4, height - y * 4);
                    DecodeBC4AlphaBlock(raw.data() + pos + 8, dst, stride, 1, width - x * 4, height - y * 4);
                    for (int py = 0; py < 4 && y * 4 + py < height; ++py) {
                        for (int px = 0; px < 4 && x * 4 + px < width; ++px) {
                            float nx = dst[py * stride + px * 4 + 0] / 127.5f - 1.0f;
                            float ny = dst[py * stride + px * 4 + 1] / 127.5f - 1.0f;
                            float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                            dst[py * stride + px * 4 + 2] = ClampByte(static_cast<int>((nz * 0.5f + 0.5f) * 255.0f));
                            dst[py * stride + px * 4 + 3] = 255;
                        }
                    }
                    pos += 16;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::ETC_RGB4:
        case UnityTextureFormat::ETC2_RGB: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 8 > raw.size()) break;
                    DecodeETC1Block(raw.data() + pos, out.data() + (y * 4) * stride + (x * 4) * 4,
                                    stride, width - x * 4, height - y * 4);
                    pos += 8;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        case UnityTextureFormat::ETC2_RGBA8: {
            int bx = (width + 3) / 4;
            int by = (height + 3) / 4;
            size_t pos = 0;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    if (pos + 16 > raw.size()) break;
                    u8* dst = out.data() + (y * 4) * stride + (x * 4) * 4;
                    DecodeETC1Block(raw.data() + pos + 8, dst, stride, width - x * 4, height - y * 4);
                    pos += 16;
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
        default: {
            // ASTC / BC7 block average preview fallback so user always sees meaningful preview
            int blockDim = (format == UnityTextureFormat::ASTC_8x8) ? 8 :
                           (format == UnityTextureFormat::ASTC_6x6) ? 6 : 4;
            int bx = (width + blockDim - 1) / blockDim;
            int by = (height + blockDim - 1) / blockDim;
            for (int y = 0; y < by; ++y) {
                for (int x = 0; x < bx; ++x) {
                    size_t off = static_cast<size_t>(y * bx + x) * 16;
                    u8 r = (off + 8 < raw.size()) ? raw[off + 8] : 96;
                    u8 g = (off + 10 < raw.size()) ? raw[off + 10] : 112;
                    u8 b = (off + 12 < raw.size()) ? raw[off + 12] : 138;
                    for (int py = 0; py < blockDim && y * blockDim + py < height; ++py) {
                        for (int px = 0; px < blockDim && x * blockDim + px < width; ++px) {
                            u8* dst = out.data() + (y * blockDim + py) * stride + (x * blockDim + px) * 4;
                            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
                        }
                    }
                }
            }
            return Result<ByteBuffer>::Ok(std::move(out));
        }
    }
}

void TextureCodec::FlipVertical(Texture2DData& tex) {
    if (!tex.IsValid()) return;
    size_t rowBytes = static_cast<size_t>(tex.width) * 4;
    ByteBuffer tmp(rowBytes);
    for (int y = 0; y < tex.height / 2; ++y) {
        u8* top = tex.rgbaPixels.data() + static_cast<size_t>(y) * rowBytes;
        u8* bot = tex.rgbaPixels.data() + static_cast<size_t>(tex.height - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, tmp.data(), rowBytes);
    }
}

void TextureCodec::InvertRGB(Texture2DData& tex) {
    if (!tex.IsValid()) return;
    for (size_t i = 0; i < tex.rgbaPixels.size(); i += 4) {
        tex.rgbaPixels[i + 0] = static_cast<u8>(255 - tex.rgbaPixels[i + 0]);
        tex.rgbaPixels[i + 1] = static_cast<u8>(255 - tex.rgbaPixels[i + 1]);
        tex.rgbaPixels[i + 2] = static_cast<u8>(255 - tex.rgbaPixels[i + 2]);
    }
}

void TextureCodec::ApplyTint(Texture2DData& tex, f32 rMul, f32 gMul, f32 bMul) {
    if (!tex.IsValid()) return;
    for (size_t i = 0; i < tex.rgbaPixels.size(); i += 4) {
        tex.rgbaPixels[i + 0] = ClampByte(static_cast<int>(tex.rgbaPixels[i + 0] * rMul));
        tex.rgbaPixels[i + 1] = ClampByte(static_cast<int>(tex.rgbaPixels[i + 1] * gMul));
        tex.rgbaPixels[i + 2] = ClampByte(static_cast<int>(tex.rgbaPixels[i + 2] * bMul));
    }
}

void TextureCodec::ConvertGrayscale(Texture2DData& tex) {
    if (!tex.IsValid()) return;
    for (size_t i = 0; i < tex.rgbaPixels.size(); i += 4) {
        u8 lum = ClampByte(static_cast<int>(
            0.299f * tex.rgbaPixels[i + 0] +
            0.587f * tex.rgbaPixels[i + 1] +
            0.114f * tex.rgbaPixels[i + 2]));
        tex.rgbaPixels[i + 0] = lum;
        tex.rgbaPixels[i + 1] = lum;
        tex.rgbaPixels[i + 2] = lum;
    }
}

Result<void> TextureCodec::ExportPNG(const Texture2DData& tex, const std::filesystem::path& pngPath) {
    if (!tex.IsValid()) return Result<void>::Err("Cannot export empty texture");
    if (pngPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(pngPath.parent_path(), ec);
    }
    int ok = stbi_write_png(
        pngPath.string().c_str(), tex.width, tex.height, 4,
        tex.rgbaPixels.data(), tex.width * 4);
    if (!ok) return Result<void>::Err("stbi_write_png failed: " + pngPath.string());
    return Result<void>::Ok();
}

Result<Texture2DData> TextureCodec::ImportPNG(const std::filesystem::path& pngPath, const std::string& name) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(pngPath.string().c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        return Result<Texture2DData>::Err("Failed to load image: " + pngPath.string());
    }
    Texture2DData tex;
    tex.name = name.empty() ? pngPath.stem().string() : name;
    tex.width = w;
    tex.height = h;
    tex.format = UnityTextureFormat::RGBA32;
    tex.mipCount = 1;
    tex.rgbaPixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    tex.encodedData = tex.rgbaPixels;
    stbi_image_free(pixels);
    return Result<Texture2DData>::Ok(std::move(tex));
}

Result<void> TextureCodec::WriteBackToObject(
    unity::SerializedFile& sf,
    unity::ObjectInfo& obj,
    const Texture2DData& tex) {
    auto valRes = sf.ReadObjectValue(obj);
    if (!valRes) return Result<void>::Err(valRes.Error());
    auto& root = valRes.Value();

    if (auto* f = root.FindField("m_Name")) f->value = tex.name;
    if (auto* f = root.FindField("m_Width")) f->value = static_cast<i64>(tex.width);
    if (auto* f = root.FindField("m_Height")) f->value = static_cast<i64>(tex.height);
    if (auto* f = root.FindField("m_CompleteImageSize")) f->value = static_cast<u64>(tex.rgbaPixels.size());
    if (auto* f = root.FindField("m_TextureFormat")) f->value = static_cast<i64>(UnityTextureFormat::RGBA32);
    if (auto* f = root.FindField("m_MipCount")) f->value = static_cast<i64>(1);
    if (auto* f = root.FindField("image data")) f->value = tex.rgbaPixels;
    if (auto* sd = root.FindField("m_StreamData")) {
        if (auto* off = sd->FindField("offset")) off->value = static_cast<u64>(0);
        if (auto* sz = sd->FindField("size")) sz->value = static_cast<u64>(0);
        if (auto* p = sd->FindField("path")) p->value = std::string{};
    }
    return sf.WriteObjectValue(obj, root);
}

} // namespace coconut::assets
