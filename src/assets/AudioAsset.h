#pragma once

#include "core/Types.h"
#include "unity/SerializedFile.h"
#include "unity/GameLoader.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::assets {

struct AudioClipData {
    std::string name;
    i32 channels = 1;
    i32 frequency = 44100;
    i32 bitsPerSample = 16;
    f32 lengthSeconds = 0.0f;
    i32 compressionFormat = 0; // 0 = PCM, 1 = Vorbis, 2 = ADPCM, 3 = MP3, 7 = AAC
    std::string resourceSource;
    u64 resourceOffset = 0;
    u64 resourceSize = 0;
    bool isFsb5Container = false;
    std::string fsb5CodecName;

    ByteBuffer rawAudio;
    std::vector<f32> waveformPeaks; // 64 normalized amplitude peaks for UI waveform view
};

class AudioCodec {
public:
    static std::string_view CompressionFormatName(i32 fmt);
    static Result<AudioClipData> ParseFromObject(
        const unity::SerializedFile& sf,
        const unity::ObjectInfo& obj,
        const unity::LoadedGameProject* project = nullptr);

    // Export AudioClip to standard playable audio formats
    static Result<void> ExportWAV(const AudioClipData& clip, const std::filesystem::path& wavPath);
    static Result<void> ExportMP3(const AudioClipData& clip, const std::filesystem::path& mp3Path, i32 bitrateKbps = 128);
    static Result<void> ExportM4A(const AudioClipData& clip, const std::filesystem::path& m4aPath);
    static Result<void> ExportRawOrOgg(const AudioClipData& clip, const std::filesystem::path& outPath);

    // Helper: detect & parse Unity FSB5 container header (returning codec name & substream offset)
    static bool InspectFsb5Header(ByteSpan data, std::string& outCodecName, size_t& outDataOffset);

    // Helper: extract 16-bit interleaved PCM samples from a clip (converting 8-bit if needed)
    static Result<std::vector<i16>> ExtractPcm16(const AudioClipData& clip);

    static AudioClipData CreateSynthSineClip(const std::string& name, f32 freqHz = 440.0f, f32 durationSec = 0.5f, i32 sampleRate = 22050);
};

} // namespace coconut::assets
