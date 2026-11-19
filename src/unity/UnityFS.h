#pragma once

#include "core/Types.h"
#include "core/Compression.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::unity {

struct UnityFSBlockInfo {
    u32 uncompressedSize = 0;
    u32 compressedSize = 0;
    u16 flags = 0;

    CompressionType GetCompression() const {
        return static_cast<CompressionType>(flags & 0x3Fu);
    }
};

struct UnityFSNode {
    u64 offset = 0;
    u64 size = 0;
    u32 flags = 0;
    std::string path;
    ByteBuffer data; // Extracted slice of the uncompressed archive stream
};

struct UnityFSHeader {
    std::string signature;       // "UnityFS"
    u32 formatVersion = 6;       // 6, 7, or 8
    std::string unityWebVersion; // e.g. "5.x.x"
    std::string unityRevision;   // e.g. "2022.3.15f1"
    i64 bundleSize = 0;
    u32 compressedBlocksInfoSize = 0;
    u32 uncompressedBlocksInfoSize = 0;
    u32 flags = 0;
};

class UnityFSBundle {
public:
    static bool IsUnityFS(ByteSpan data);

    static Result<UnityFSBundle> Parse(ByteSpan data);
    static Result<UnityFSBundle> LoadFromFile(const std::filesystem::path& path);

    // Build a valid UnityFS v6 bundle (default LZ4HC compressed blocks & header).
    Result<ByteBuffer> Serialize(CompressionType compression = CompressionType::LZ4HC) const;

    UnityFSHeader header;
    std::vector<UnityFSBlockInfo> blocks;
    std::vector<UnityFSNode> nodes;
};

} // namespace coconut::unity
