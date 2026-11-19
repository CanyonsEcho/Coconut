#include "unity/UnityFS.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "core/Logger.h"
#include "platform/Platform.h"

#include <cstring>

namespace coconut::unity {

namespace {

constexpr u32 kFlagCompressionMask       = 0x3Fu;
constexpr u32 kFlagBlocksAndDirCombined  = 0x40u;
constexpr u32 kFlagBlocksInfoAtEnd       = 0x80u;
constexpr u32 kFlagBlockInfoNeedPadding  = 0x200u;

} // namespace

bool UnityFSBundle::IsUnityFS(ByteSpan data) {
    return data.size() >= 8 && std::memcmp(data.data(), "UnityFS\0", 8) == 0;
}

Result<UnityFSBundle> UnityFSBundle::LoadFromFile(const std::filesystem::path& path) {
    auto bytes = platform::ReadFileBytes(path);
    if (!bytes) return Result<UnityFSBundle>::Err(bytes.Error());
    return Parse(bytes.Value());
}

Result<UnityFSBundle> UnityFSBundle::Parse(ByteSpan data) {
    if (!IsUnityFS(data)) {
        return Result<UnityFSBundle>::Err("Not a UnityFS bundle (missing 'UnityFS' signature)");
    }

    BinaryReader r(data, Endian::Big);
    UnityFSBundle bundle;

    bundle.header.signature = r.ReadCString();
    bundle.header.formatVersion = r.ReadU32();
    bundle.header.unityWebVersion = r.ReadCString();
    bundle.header.unityRevision = r.ReadCString();
    bundle.header.bundleSize = r.ReadI64();
    bundle.header.compressedBlocksInfoSize = r.ReadU32();
    bundle.header.uncompressedBlocksInfoSize = r.ReadU32();
    bundle.header.flags = r.ReadU32();

    if (r.HasError()) {
        return Result<UnityFSBundle>::Err("Truncated UnityFS header");
    }

    if (bundle.header.formatVersion >= 7) {
        r.Align(16);
    }

    size_t blocksStartOffset = r.Position();
    ByteSpan compressedBlocksInfo;

    if ((bundle.header.flags & kFlagBlocksInfoAtEnd) != 0) {
        if (bundle.header.compressedBlocksInfoSize > data.size()) {
            return Result<UnityFSBundle>::Err("UnityFS blocksInfo size exceeds file size");
        }
        size_t infoPos = data.size() - bundle.header.compressedBlocksInfoSize;
        compressedBlocksInfo = data.subspan(infoPos, bundle.header.compressedBlocksInfoSize);
    } else {
        compressedBlocksInfo = r.ReadSpan(bundle.header.compressedBlocksInfoSize);
        if (r.HasError()) {
            return Result<UnityFSBundle>::Err("Truncated UnityFS blocksInfo at header");
        }
        if ((bundle.header.flags & kFlagBlockInfoNeedPadding) != 0) {
            r.Align(16);
        }
        blocksStartOffset = r.Position();
    }

    auto headerComp = static_cast<CompressionType>(bundle.header.flags & kFlagCompressionMask);
    auto blocksInfoRes = Compression::Decompress(
        headerComp, compressedBlocksInfo, bundle.header.uncompressedBlocksInfoSize);
    if (!blocksInfoRes) {
        return Result<UnityFSBundle>::Err("Failed to decompress UnityFS blocksInfo: " + blocksInfoRes.Error());
    }

    BinaryReader br(blocksInfoRes.Value(), Endian::Big);
    br.Skip(16); // uncompressedDataHash (16 bytes MD5/hash)

    i32 blockCount = br.ReadI32();
    if (blockCount < 0 || blockCount > 1000000) {
        return Result<UnityFSBundle>::Err("Invalid UnityFS block count");
    }
    bundle.blocks.reserve(static_cast<size_t>(blockCount));
    size_t totalUncompressed = 0;
    for (i32 i = 0; i < blockCount; ++i) {
        UnityFSBlockInfo bi;
        bi.uncompressedSize = br.ReadU32();
        bi.compressedSize = br.ReadU32();
        bi.flags = br.ReadU16();
        totalUncompressed += bi.uncompressedSize;
        bundle.blocks.push_back(bi);
    }

    i32 nodeCount = br.ReadI32();
    if (nodeCount < 0 || nodeCount > 1000000) {
        return Result<UnityFSBundle>::Err("Invalid UnityFS directory node count");
    }
    bundle.nodes.reserve(static_cast<size_t>(nodeCount));
    for (i32 i = 0; i < nodeCount; ++i) {
        UnityFSNode node;
        node.offset = br.ReadU64();
        node.size = br.ReadU64();
        node.flags = br.ReadU32();
        node.path = br.ReadCString();
        bundle.nodes.push_back(std::move(node));
    }

    if (br.HasError()) {
        return Result<UnityFSBundle>::Err("Truncated UnityFS directory table");
    }

    // Reconstruct full uncompressed archive payload across all blocks
    ByteBuffer stream;
    stream.reserve(totalUncompressed);
    r.Seek(blocksStartOffset);
    for (size_t i = 0; i < bundle.blocks.size(); ++i) {
        const auto& b = bundle.blocks[i];
        ByteSpan blockSpan = r.ReadSpan(b.compressedSize);
        if (r.HasError()) {
            return Result<UnityFSBundle>::Err("Truncated UnityFS block " + std::to_string(i));
        }
        auto decoded = Compression::Decompress(b.GetCompression(), blockSpan, b.uncompressedSize);
        if (!decoded) {
            return Result<UnityFSBundle>::Err("Failed to decompress UnityFS block " + std::to_string(i) + ": " + decoded.Error());
        }
        stream.insert(stream.end(), decoded.Value().begin(), decoded.Value().end());
    }

    for (auto& node : bundle.nodes) {
        if (node.offset + node.size > stream.size()) {
            return Result<UnityFSBundle>::Err("UnityFS node '" + node.path + "' out of stream bounds");
        }
        node.data.assign(
            stream.begin() + static_cast<std::ptrdiff_t>(node.offset),
            stream.begin() + static_cast<std::ptrdiff_t>(node.offset + node.size));
    }

    COCO_LOG_INFO("UnityFS", "Parsed bundle (" + bundle.header.unityRevision + ") with " +
                             std::to_string(bundle.nodes.size()) + " nodes across " +
                             std::to_string(bundle.blocks.size()) + " blocks");
    return Result<UnityFSBundle>::Ok(std::move(bundle));
}

Result<ByteBuffer> UnityFSBundle::Serialize(CompressionType compression) const {
    if (compression == CompressionType::LZMA) {
        compression = CompressionType::LZ4HC;
    }

    // Concatenate node payloads into unified archive stream
    ByteBuffer rawStream;
    std::vector<UnityFSNode> layoutNodes = nodes;
    for (auto& n : layoutNodes) {
        n.offset = rawStream.size();
        n.size = n.data.size();
        rawStream.insert(rawStream.end(), n.data.begin(), n.data.end());
    }

    // Compress data blocks in 128 KiB chunks (standard UnityFS chunk size)
    constexpr size_t kChunkSize = 128 * 1024;
    std::vector<UnityFSBlockInfo> outBlocks;
    ByteBuffer compressedPayload;

    size_t pos = 0;
    while (pos < rawStream.size() || (rawStream.empty() && outBlocks.empty())) {
        size_t len = std::min(kChunkSize, rawStream.size() - pos);
        ByteSpan chunk(rawStream.data() + pos, len);
        UnityFSBlockInfo bi;
        bi.uncompressedSize = static_cast<u32>(len);

        if (compression == CompressionType::LZ4 || compression == CompressionType::LZ4HC) {
            auto comp = Compression::CompressLZ4(chunk, compression == CompressionType::LZ4HC);
            if (comp && comp.Value().size() < len) {
                bi.compressedSize = static_cast<u32>(comp.Value().size());
                bi.flags = static_cast<u16>(compression);
                compressedPayload.insert(compressedPayload.end(), comp.Value().begin(), comp.Value().end());
            } else {
                bi.compressedSize = static_cast<u32>(len);
                bi.flags = static_cast<u16>(CompressionType::None);
                compressedPayload.insert(compressedPayload.end(), chunk.begin(), chunk.end());
            }
        } else {
            bi.compressedSize = static_cast<u32>(len);
            bi.flags = static_cast<u16>(CompressionType::None);
            compressedPayload.insert(compressedPayload.end(), chunk.begin(), chunk.end());
        }

        outBlocks.push_back(bi);
        pos += len;
        if (rawStream.empty()) break;
    }

    // Build blocksInfo + directory table
    BinaryWriter infoW(Endian::Big);
    infoW.WriteZeros(16); // hash
    infoW.WriteI32(static_cast<i32>(outBlocks.size()));
    for (const auto& bi : outBlocks) {
        infoW.WriteU32(bi.uncompressedSize);
        infoW.WriteU32(bi.compressedSize);
        infoW.WriteU16(bi.flags);
    }
    infoW.WriteI32(static_cast<i32>(layoutNodes.size()));
    for (const auto& n : layoutNodes) {
        infoW.WriteU64(n.offset);
        infoW.WriteU64(n.size);
        infoW.WriteU32(n.flags);
        infoW.WriteCString(n.path);
    }

    const ByteBuffer& rawInfo = infoW.Buffer();
    ByteBuffer compInfo = rawInfo;
    u32 headerFlags = kFlagBlocksAndDirCombined;
    if (compression == CompressionType::LZ4 || compression == CompressionType::LZ4HC) {
        auto c = Compression::CompressLZ4(rawInfo, true);
        if (c && c.Value().size() < rawInfo.size()) {
            compInfo = std::move(c.Value());
            headerFlags |= static_cast<u32>(compression);
        }
    }

    BinaryWriter w(Endian::Big);
    w.WriteCString(header.signature.empty() ? "UnityFS" : header.signature);
    w.WriteU32(header.formatVersion >= 6 ? header.formatVersion : 6);
    w.WriteCString(header.unityWebVersion.empty() ? "5.x.x" : header.unityWebVersion);
    w.WriteCString(header.unityRevision.empty() ? "2022.3.15f1" : header.unityRevision);
    size_t sizePatchOffset = w.Size();
    w.WriteI64(0); // bundleSize patched below
    w.WriteU32(static_cast<u32>(compInfo.size()));
    w.WriteU32(static_cast<u32>(rawInfo.size()));
    w.WriteU32(headerFlags);
    if ((header.formatVersion >= 7)) {
        w.Align(16);
    }
    w.WriteBytes(compInfo);
    w.WriteBytes(compressedPayload);
    w.PatchI64(sizePatchOffset, static_cast<i64>(w.Size()));

    return Result<ByteBuffer>::Ok(w.TakeBuffer());
}

} // namespace coconut::unity
