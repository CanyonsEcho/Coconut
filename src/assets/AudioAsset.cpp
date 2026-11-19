#include "assets/AudioAsset.h"
#include "core/BinaryWriter.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>

namespace coconut::assets {

namespace {

void ComputePeaks(AudioClipData& clip) {
    clip.waveformPeaks.assign(64, 0.0f);
    if (clip.rawAudio.size() < 2) return;
    size_t sampleCount = clip.rawAudio.size() / 2;
    size_t stride = std::max<size_t>(1, sampleCount / 64);
    for (size_t b = 0; b < 64; ++b) {
        float peak = 0.0f;
        for (size_t s = b * stride; s < std::min(sampleCount, (b + 1) * stride); ++s) {
            i16 sample = static_cast<i16>(clip.rawAudio[s * 2] | (clip.rawAudio[s * 2 + 1] << 8));
            peak = std::max(peak, std::fabs(static_cast<float>(sample) / 32768.0f));
        }
        clip.waveformPeaks[b] = peak;
    }
}

} // namespace

std::string_view AudioCodec::CompressionFormatName(i32 fmt) {
    switch (fmt) {
        case 0: return "PCM";
        case 1: return "Vorbis";
        case 2: return "ADPCM";
        case 3: return "MP3";
        case 4: return "VAG";
        case 5: return "HEVAG";
        case 6: return "XMA";
        case 7: return "AAC";
        case 8: return "GCADPCM";
        case 9: return "ATRAC9";
        default: return "Unknown";
    }
}

Result<AudioClipData> AudioCodec::ParseFromObject(
    const unity::SerializedFile& sf,
    const unity::ObjectInfo& obj,
    const unity::LoadedGameProject* project) {
    AudioClipData clip;
    clip.name = obj.cachedName;

    auto valRes = sf.ReadObjectValue(obj);
    if (valRes) {
        const auto& root = valRes.Value();
        if (auto* f = root.FindField("m_Name")) clip.name = f->AsString(clip.name);
        if (auto* f = root.FindField("m_Channels")) clip.channels = std::max<i32>(1, static_cast<i32>(f->AsInt(1)));
        if (auto* f = root.FindField("m_Frequency")) clip.frequency = std::max<i32>(8000, static_cast<i32>(f->AsInt(44100)));
        if (auto* f = root.FindField("m_BitsPerSample")) clip.bitsPerSample = std::max<i32>(8, static_cast<i32>(f->AsInt(16)));
        if (auto* f = root.FindField("m_Length")) clip.lengthSeconds = static_cast<f32>(f->AsFloat(0.0));
        if (auto* f = root.FindField("m_CompressionFormat")) clip.compressionFormat = static_cast<i32>(f->AsInt(0));
        if (auto* res = root.FindField("m_Resource")) {
            if (auto* s = res->FindField("m_Source")) clip.resourceSource = s->AsString();
            if (auto* o = res->FindField("m_Offset")) clip.resourceOffset = o->AsUInt();
            if (auto* sz = res->FindField("m_Size")) clip.resourceSize = sz->AsUInt();
        }
        if (auto* d = root.FindField("m_AudioData")) {
            if (auto* bytes = std::get_if<ByteBuffer>(&d->value)) clip.rawAudio = *bytes;
        }
    }

    if (clip.rawAudio.empty() && !clip.resourceSource.empty() && clip.resourceSize > 0 && project) {
        if (const auto* stream = project->FindResourceStream(clip.resourceSource)) {
            if (clip.resourceOffset + clip.resourceSize <= stream->size()) {
                clip.rawAudio.assign(
                    stream->begin() + static_cast<std::ptrdiff_t>(clip.resourceOffset),
                    stream->begin() + static_cast<std::ptrdiff_t>(clip.resourceOffset + clip.resourceSize));
            }
        }
    }

    ComputePeaks(clip);
    return Result<AudioClipData>::Ok(std::move(clip));
}

Result<void> AudioCodec::ExportWAV(const AudioClipData& clip, const std::filesystem::path& wavPath) {
    if (clip.rawAudio.empty()) return Result<void>::Err("AudioClip has no PCM payload to export");
    BinaryWriter w(Endian::Little);
    u16 channels = static_cast<u16>(std::max<i32>(1, clip.channels));
    u32 sampleRate = static_cast<u32>(std::max<i32>(8000, clip.frequency));
    u16 bits = static_cast<u16>(clip.bitsPerSample == 8 ? 8 : 16);
    u16 blockAlign = static_cast<u16>(channels * (bits / 8));
    u32 byteRate = sampleRate * blockAlign;
    u32 dataSize = static_cast<u32>(clip.rawAudio.size());

    w.WriteFixedString("RIFF");
    w.WriteU32(36 + dataSize);
    w.WriteFixedString("WAVE");
    w.WriteFixedString("fmt ");
    w.WriteU32(16);
    w.WriteU16(1); // PCM
    w.WriteU16(channels);
    w.WriteU32(sampleRate);
    w.WriteU32(byteRate);
    w.WriteU16(blockAlign);
    w.WriteU16(bits);
    w.WriteFixedString("data");
    w.WriteU32(dataSize);
    w.WriteBytes(clip.rawAudio);

    return platform::WriteFileBytes(wavPath, w.Buffer());
}

AudioClipData AudioCodec::CreateSynthSineClip(const std::string& name, f32 freqHz, f32 durationSec, i32 sampleRate) {
    AudioClipData clip;
    clip.name = name;
    clip.channels = 1;
    clip.frequency = sampleRate;
    clip.bitsPerSample = 16;
    clip.lengthSeconds = durationSec;
    clip.compressionFormat = 0;

    size_t samples = static_cast<size_t>(durationSec * sampleRate);
    clip.rawAudio.resize(samples * 2);
    for (size_t i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = std::min(1.0f, t * 20.0f) * std::min(1.0f, (durationSec - t) * 10.0f);
        float s = std::sin(2.0f * 3.14159265f * freqHz * t) * 0.6f * env;
        i16 pcm = static_cast<i16>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        clip.rawAudio[i * 2 + 0] = static_cast<u8>(pcm & 0xFF);
        clip.rawAudio[i * 2 + 1] = static_cast<u8>((pcm >> 8) & 0xFF);
    }
    ComputePeaks(clip);
    return clip;
}

} // namespace coconut::assets
