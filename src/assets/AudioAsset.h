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
    i32 compressionFormat = 0; // 0 = PCM, 1 = Vorbis, 2 = ADPCM
    std::string resourceSource;
    u64 resourceOffset = 0;
    u64 resourceSize = 0;

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

    static Result<void> ExportWAV(const AudioClipData& clip, const std::filesystem::path& wavPath);
    static AudioClipData CreateSynthSineClip(const std::string& name, f32 freqHz = 440.0f, f32 durationSec = 0.5f, i32 sampleRate = 22050);
};

} // namespace coconut::assets
