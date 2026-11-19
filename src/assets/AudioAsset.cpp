#include "assets/AudioAsset.h"
#include "core/BinaryReader.h"
#include "core/BinaryWriter.h"
#include "platform/Platform.h"

extern "C" {
#include <layer3.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

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

// Pick nearest sample rate supported by MPEG-1/2/2.5 Layer III
int NearestMpegSampleRate(int rate) {
    static constexpr int kSupported[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    int best = 44100;
    int bestDiff = std::abs(rate - best);
    for (int s : kSupported) {
        int d = std::abs(rate - s);
        if (d < bestDiff) {
            bestDiff = d;
            best = s;
        }
    }
    return best;
}

// Linear PCM resampler for arbitrary Unity sample rates -> target MPEG rate
std::vector<i16> ResamplePcm16(const std::vector<i16>& src, int channels, int srcRate, int dstRate) {
    if (srcRate == dstRate || src.empty() || channels <= 0) return src;
    size_t srcFrames = src.size() / static_cast<size_t>(channels);
    if (srcFrames < 2) return src;
    size_t dstFrames = std::max<size_t>(1, static_cast<size_t>(
        static_cast<double>(srcFrames) * static_cast<double>(dstRate) / static_cast<double>(srcRate)));
    std::vector<i16> dst(dstFrames * static_cast<size_t>(channels), 0);
    double ratio = static_cast<double>(srcFrames - 1) / static_cast<double>(std::max<size_t>(1, dstFrames - 1));
    for (size_t i = 0; i < dstFrames; ++i) {
        double pos = static_cast<double>(i) * ratio;
        size_t idx0 = static_cast<size_t>(pos);
        size_t idx1 = std::min(srcFrames - 1, idx0 + 1);
        double frac = pos - static_cast<double>(idx0);
        for (int c = 0; c < channels; ++c) {
            double s0 = src[idx0 * channels + c];
            double s1 = src[idx1 * channels + c];
            double val = s0 + (s1 - s0) * frac;
            dst[i * channels + c] = static_cast<i16>(std::clamp(val, -32768.0, 32767.0));
        }
    }
    return dst;
}

// ISO-BMFF helpers for M4A (ALAC) container writing
void WriteU32BE(ByteBuffer& out, u32 v) {
    out.push_back(static_cast<u8>((v >> 24) & 0xFF));
    out.push_back(static_cast<u8>((v >> 16) & 0xFF));
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>(v & 0xFF));
}

void WriteU16BE(ByteBuffer& out, u16 v) {
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>(v & 0xFF));
}

void WriteFourCC(ByteBuffer& out, const char* s) {
    out.insert(out.end(), reinterpret_cast<const u8*>(s), reinterpret_cast<const u8*>(s) + 4);
}

void WriteIdentityMatrixBE(ByteBuffer& out) {
    static constexpr u32 kMat[9] = {0x00010000u, 0, 0, 0, 0x00010000u, 0, 0, 0, 0x40000000u};
    for (u32 v : kMat) WriteU32BE(out, v);
}

ByteBuffer BuildMp4Box(const char* fourCC, const ByteBuffer& payload) {
    ByteBuffer box;
    box.reserve(8 + payload.size());
    WriteU32BE(box, static_cast<u32>(8 + payload.size()));
    WriteFourCC(box, fourCC);
    box.insert(box.end(), payload.begin(), payload.end());
    return box;
}

ByteBuffer EncodeAlacVerbatimPacket(const i16* pcm, size_t sampleCount, int channels) {
    ByteBuffer out;
    out.reserve(sampleCount * static_cast<size_t>(channels) * 2 + 12);
    u32 curByte = 0;
    int bitPos = 0;
    auto writeBits = [&](u32 value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            curByte = (curByte << 1) | ((value >> i) & 1u);
            if (++bitPos == 8) {
                out.push_back(static_cast<u8>(curByte));
                curByte = 0;
                bitPos = 0;
            }
        }
    };

    // Element type: ID_SCE = 0 (mono), ID_CPE = 1 (stereo)
    writeBits(channels == 1 ? 0u : 1u, 3);
    writeBits(0, 4);  // element instance tag
    writeBits(0, 12); // unused
    bool hasSize = (sampleCount != 4096);
    writeBits(hasSize ? 1u : 0u, 1);
    writeBits(0, 2);  // unusedBytes
    writeBits(1, 1);  // isNotCompressed = 1 (verbatim lossless PCM)
    if (hasSize) writeBits(static_cast<u32>(sampleCount), 32);

    for (size_t i = 0; i < sampleCount; ++i) {
        for (int c = 0; c < channels; ++c) {
            u16 s = static_cast<u16>(pcm[i * static_cast<size_t>(channels) + c]);
            writeBits(s, 16);
        }
    }

    // ID_END = 7
    writeBits(7, 3);
    if (bitPos > 0) {
        out.push_back(static_cast<u8>(curByte << (8 - bitPos)));
    }
    return out;
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

bool AudioCodec::InspectFsb5Header(ByteSpan data, std::string& outCodecName, size_t& outDataOffset) {
    outCodecName.clear();
    outDataOffset = 0;
    if (data.size() < 60) return false;
    if (std::memcmp(data.data(), "FSB5", 4) != 0) return false;

    BinaryReader r(data, Endian::Little);
    r.Skip(4); // "FSB5"
    u32 version = r.ReadU32();
    u32 numSamples = r.ReadU32();
    u32 sampleHeadersSize = r.ReadU32();
    u32 nameTableSize = r.ReadU32();
    u32 dataSize = r.ReadU32();
    u32 mode = r.ReadU32();
    (void)numSamples;
    (void)dataSize;

    size_t headerSize = (version == 0) ? 64 : 60;
    outDataOffset = headerSize + sampleHeadersSize + nameTableSize;

    switch (mode) {
        case 1: outCodecName = "PCM8"; break;
        case 2: outCodecName = "PCM16"; break;
        case 3: outCodecName = "PCM24"; break;
        case 4: outCodecName = "PCM32"; break;
        case 5: outCodecName = "PCMFLOAT"; break;
        case 6: outCodecName = "GCADPCM"; break;
        case 7: outCodecName = "IMAADPCM"; break;
        case 8: outCodecName = "VAG"; break;
        case 9: outCodecName = "HEVAG"; break;
        case 10: outCodecName = "XMA"; break;
        case 11: outCodecName = "MPEG/MP3"; break;
        case 12: outCodecName = "CELT"; break;
        case 13: outCodecName = "AT9"; break;
        case 14: outCodecName = "XWMA"; break;
        case 15: outCodecName = "Vorbis"; break;
        case 16: outCodecName = "FADPCM"; break;
        case 17: outCodecName = "Opus"; break;
        default: outCodecName = "FSB5_Mode_" + std::to_string(mode); break;
    }
    return true;
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

    size_t fsbOff = 0;
    if (InspectFsb5Header(clip.rawAudio, clip.fsb5CodecName, fsbOff)) {
        clip.isFsb5Container = true;
    }

    ComputePeaks(clip);
    return Result<AudioClipData>::Ok(std::move(clip));
}

Result<std::vector<i16>> AudioCodec::ExtractPcm16(const AudioClipData& clip) {
    if (clip.rawAudio.empty()) {
        return Result<std::vector<i16>>::Err("AudioClip has no audio payload");
    }

    ByteSpan payload = clip.rawAudio;
    size_t fsbOff = 0;
    std::string codec;
    if (InspectFsb5Header(payload, codec, fsbOff) && fsbOff < payload.size()) {
        payload = payload.subspan(fsbOff);
    }

    // If rawAudio is already a RIFF/WAVE container, extract its data chunk
    if (payload.size() >= 44 && std::memcmp(payload.data(), "RIFF", 4) == 0) {
        size_t pos = 12;
        while (pos + 8 <= payload.size()) {
            u32 sz = static_cast<u32>(payload[pos + 4] | (payload[pos + 5] << 8) |
                                      (payload[pos + 6] << 16) | (payload[pos + 7] << 24));
            bool isData = std::memcmp(payload.data() + pos, "data", 4) == 0;
            pos += 8;
            if (isData && pos + sz <= payload.size()) {
                payload = payload.subspan(pos, sz);
                break;
            }
            pos += sz + (sz & 1);
        }
    }

    std::vector<i16> pcm;
    if (clip.bitsPerSample == 8) {
        pcm.resize(payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            pcm[i] = static_cast<i16>((static_cast<int>(payload[i]) - 128) << 8);
        }
    } else {
        size_t count = payload.size() / 2;
        pcm.resize(count);
        for (size_t i = 0; i < count; ++i) {
            pcm[i] = static_cast<i16>(payload[i * 2] | (payload[i * 2 + 1] << 8));
        }
    }

    if (pcm.empty()) {
        return Result<std::vector<i16>>::Err("AudioClip contains 0 PCM samples");
    }
    return Result<std::vector<i16>>::Ok(std::move(pcm));
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

Result<void> AudioCodec::ExportMP3(const AudioClipData& clip, const std::filesystem::path& mp3Path, i32 bitrateKbps) {
    auto pcmRes = ExtractPcm16(clip);
    if (!pcmRes) return Result<void>::Err(pcmRes.Error());

    int channels = std::clamp(clip.channels, 1, 2);
    int srcRate = std::max(8000, clip.frequency);
    int dstRate = NearestMpegSampleRate(srcRate);
    std::vector<i16> pcm = ResamplePcm16(pcmRes.Value(), channels, srcRate, dstRate);

    // Pick a valid bitrate for the target MPEG version
    int bitr = bitrateKbps;
    if (shine_check_config(dstRate, bitr) < 0) {
        bitr = (dstRate >= 32000) ? 128 : 64;
        if (shine_check_config(dstRate, bitr) < 0) bitr = 64;
    }

    shine_config_t cfg{};
    shine_set_config_mpeg_defaults(&cfg.mpeg);
    cfg.wave.channels = (channels == 1) ? PCM_MONO : PCM_STEREO;
    cfg.wave.samplerate = dstRate;
    cfg.mpeg.mode = (channels == 1) ? MONO : STEREO;
    cfg.mpeg.bitr = bitr;

    shine_t enc = shine_initialise(&cfg);
    if (!enc) return Result<void>::Err("Failed to initialize shine MP3 encoder");

    const int samplesPerPass = shine_samples_per_pass(enc);
    const size_t totalFrames = pcm.size() / static_cast<size_t>(channels);
    ByteBuffer mp3Bytes;
    mp3Bytes.reserve(pcm.size() / 4 + 4096);

    std::vector<i16> frameBuf(static_cast<size_t>(samplesPerPass * channels), 0);
    for (size_t offset = 0; offset < totalFrames; offset += static_cast<size_t>(samplesPerPass)) {
        size_t chunk = std::min(static_cast<size_t>(samplesPerPass), totalFrames - offset);
        std::fill(frameBuf.begin(), frameBuf.end(), static_cast<i16>(0));
        std::memcpy(frameBuf.data(),
                    pcm.data() + offset * static_cast<size_t>(channels),
                    chunk * static_cast<size_t>(channels) * sizeof(i16));
        int written = 0;
        unsigned char* data = shine_encode_buffer_interleaved(enc, frameBuf.data(), &written);
        if (data && written > 0) {
            mp3Bytes.insert(mp3Bytes.end(), data, data + written);
        }
    }

    int flushed = 0;
    unsigned char* tail = shine_flush(enc, &flushed);
    if (tail && flushed > 0) {
        mp3Bytes.insert(mp3Bytes.end(), tail, tail + flushed);
    }
    shine_close(enc);

    if (mp3Bytes.empty()) {
        return Result<void>::Err("MP3 encoder produced 0 bytes");
    }
    return platform::WriteFileBytes(mp3Path, mp3Bytes);
}

Result<void> AudioCodec::ExportM4A(const AudioClipData& clip, const std::filesystem::path& m4aPath) {
    auto pcmRes = ExtractPcm16(clip);
    if (!pcmRes) return Result<void>::Err(pcmRes.Error());

    const auto& pcm = pcmRes.Value();
    const int channels = std::clamp(clip.channels, 1, 2);
    const u32 sampleRate = static_cast<u32>(std::max(8000, clip.frequency));
    const size_t totalSamples = pcm.size() / static_cast<size_t>(channels);
    if (totalSamples == 0) return Result<void>::Err("AudioClip has 0 samples for M4A export");

    constexpr size_t kMaxFrameSamples = 4096;
    std::vector<ByteBuffer> packets;
    std::vector<u32> packetDurations;
    for (size_t cursor = 0; cursor < totalSamples; cursor += kMaxFrameSamples) {
        size_t count = std::min(kMaxFrameSamples, totalSamples - cursor);
        packets.push_back(EncodeAlacVerbatimPacket(
            pcm.data() + cursor * static_cast<size_t>(channels), count, channels));
        packetDurations.push_back(static_cast<u32>(count));
    }

    // ftyp box
    ByteBuffer ftypPayload;
    WriteFourCC(ftypPayload, "M4A ");
    WriteU32BE(ftypPayload, 0);
    WriteFourCC(ftypPayload, "M4A ");
    WriteFourCC(ftypPayload, "mp42");
    WriteFourCC(ftypPayload, "isom");
    ByteBuffer ftyp = BuildMp4Box("ftyp", ftypPayload);

    u32 maxPktSize = 0;
    size_t mdatPayloadSize = 0;
    for (const auto& p : packets) {
        maxPktSize = std::max<u32>(maxPktSize, static_cast<u32>(p.size()));
        mdatPayloadSize += p.size();
    }
    const u32 totalDuration = static_cast<u32>(totalSamples);
    const u32 avgBitrate = sampleRate * static_cast<u32>(channels) * 16u;

    // alac magic cookie box
    ByteBuffer alacCfgPayload;
    WriteU32BE(alacCfgPayload, 0); // FullBox version + flags
    WriteU32BE(alacCfgPayload, static_cast<u32>(kMaxFrameSamples));
    alacCfgPayload.push_back(0);  // compatibleVersion
    alacCfgPayload.push_back(16); // bitDepth
    alacCfgPayload.push_back(40); // pb
    alacCfgPayload.push_back(10); // mb
    alacCfgPayload.push_back(14); // kb
    alacCfgPayload.push_back(static_cast<u8>(channels));
    WriteU16BE(alacCfgPayload, 255); // maxRun
    WriteU32BE(alacCfgPayload, maxPktSize);
    WriteU32BE(alacCfgPayload, avgBitrate);
    WriteU32BE(alacCfgPayload, sampleRate);
    ByteBuffer alacCfgBox = BuildMp4Box("alac", alacCfgPayload);

    // alac SampleEntry box
    ByteBuffer alacEntryPayload(6, 0);
    WriteU16BE(alacEntryPayload, 1); // data_reference_index
    alacEntryPayload.insert(alacEntryPayload.end(), 8, 0);
    WriteU16BE(alacEntryPayload, static_cast<u16>(channels));
    WriteU16BE(alacEntryPayload, 16);
    WriteU32BE(alacEntryPayload, 0);
    WriteU32BE(alacEntryPayload, sampleRate << 16);
    alacEntryPayload.insert(alacEntryPayload.end(), alacCfgBox.begin(), alacCfgBox.end());
    ByteBuffer alacEntry = BuildMp4Box("alac", alacEntryPayload);

    // stsd
    ByteBuffer stsdPayload;
    WriteU32BE(stsdPayload, 0);
    WriteU32BE(stsdPayload, 1);
    stsdPayload.insert(stsdPayload.end(), alacEntry.begin(), alacEntry.end());
    ByteBuffer stsd = BuildMp4Box("stsd", stsdPayload);

    // stts (run-length encoded packet durations)
    std::vector<std::pair<u32, u32>> runs;
    for (u32 d : packetDurations) {
        if (!runs.empty() && runs.back().second == d) runs.back().first++;
        else runs.emplace_back(1u, d);
    }
    ByteBuffer sttsPayload;
    WriteU32BE(sttsPayload, 0);
    WriteU32BE(sttsPayload, static_cast<u32>(runs.size()));
    for (auto [cnt, delta] : runs) {
        WriteU32BE(sttsPayload, cnt);
        WriteU32BE(sttsPayload, delta);
    }
    ByteBuffer stts = BuildMp4Box("stts", sttsPayload);

    // stsc
    ByteBuffer stscPayload;
    WriteU32BE(stscPayload, 0);
    WriteU32BE(stscPayload, 1);
    WriteU32BE(stscPayload, 1);
    WriteU32BE(stscPayload, static_cast<u32>(packets.size()));
    WriteU32BE(stscPayload, 1);
    ByteBuffer stsc = BuildMp4Box("stsc", stscPayload);

    // stsz
    ByteBuffer stszPayload;
    WriteU32BE(stszPayload, 0);
    WriteU32BE(stszPayload, 0);
    WriteU32BE(stszPayload, static_cast<u32>(packets.size()));
    for (const auto& p : packets) WriteU32BE(stszPayload, static_cast<u32>(p.size()));
    ByteBuffer stsz = BuildMp4Box("stsz", stszPayload);

    auto buildMoov = [&](u32 mdatDataOffset) -> ByteBuffer {
        ByteBuffer stcoPayload;
        WriteU32BE(stcoPayload, 0);
        WriteU32BE(stcoPayload, 1);
        WriteU32BE(stcoPayload, mdatDataOffset);
        ByteBuffer stco = BuildMp4Box("stco", stcoPayload);

        ByteBuffer stblPayload;
        auto append = [](ByteBuffer& dst, const ByteBuffer& src) {
            dst.insert(dst.end(), src.begin(), src.end());
        };
        append(stblPayload, stsd);
        append(stblPayload, stts);
        append(stblPayload, stsc);
        append(stblPayload, stsz);
        append(stblPayload, stco);
        ByteBuffer stbl = BuildMp4Box("stbl", stblPayload);

        ByteBuffer urlPayload;
        WriteU32BE(urlPayload, 1);
        ByteBuffer urlBox = BuildMp4Box("url ", urlPayload);

        ByteBuffer drefPayload;
        WriteU32BE(drefPayload, 0);
        WriteU32BE(drefPayload, 1);
        append(drefPayload, urlBox);
        ByteBuffer dinf = BuildMp4Box("dinf", BuildMp4Box("dref", drefPayload));

        ByteBuffer smhdPayload(8, 0);
        ByteBuffer smhd = BuildMp4Box("smhd", smhdPayload);

        ByteBuffer minfPayload;
        append(minfPayload, smhd);
        append(minfPayload, dinf);
        append(minfPayload, stbl);
        ByteBuffer minf = BuildMp4Box("minf", minfPayload);

        ByteBuffer mdhdPayload;
        WriteU32BE(mdhdPayload, 0);
        WriteU32BE(mdhdPayload, 0);
        WriteU32BE(mdhdPayload, 0);
        WriteU32BE(mdhdPayload, sampleRate);
        WriteU32BE(mdhdPayload, totalDuration);
        WriteU16BE(mdhdPayload, 0x55C4);
        WriteU16BE(mdhdPayload, 0);
        ByteBuffer mdhd = BuildMp4Box("mdhd", mdhdPayload);

        ByteBuffer hdlrPayload;
        WriteU32BE(hdlrPayload, 0);
        WriteU32BE(hdlrPayload, 0);
        WriteFourCC(hdlrPayload, "soun");
        hdlrPayload.insert(hdlrPayload.end(), 12, 0);
        const char* handlerName = "SoundHandler";
        hdlrPayload.insert(hdlrPayload.end(), handlerName, handlerName + 13);
        ByteBuffer hdlr = BuildMp4Box("hdlr", hdlrPayload);

        ByteBuffer mdiaPayload;
        append(mdiaPayload, mdhd);
        append(mdiaPayload, hdlr);
        append(mdiaPayload, minf);
        ByteBuffer mdia = BuildMp4Box("mdia", mdiaPayload);

        ByteBuffer tkhdPayload;
        WriteU32BE(tkhdPayload, 3); // enabled | in_movie
        WriteU32BE(tkhdPayload, 0);
        WriteU32BE(tkhdPayload, 0);
        WriteU32BE(tkhdPayload, 1); // track_ID
        WriteU32BE(tkhdPayload, 0);
        WriteU32BE(tkhdPayload, totalDuration);
        tkhdPayload.insert(tkhdPayload.end(), 8, 0);
        WriteU16BE(tkhdPayload, 0);
        WriteU16BE(tkhdPayload, 0);
        WriteU16BE(tkhdPayload, 0x0100);
        WriteU16BE(tkhdPayload, 0);
        WriteIdentityMatrixBE(tkhdPayload);
        WriteU32BE(tkhdPayload, 0);
        WriteU32BE(tkhdPayload, 0);
        ByteBuffer tkhd = BuildMp4Box("tkhd", tkhdPayload);

        ByteBuffer trakPayload;
        append(trakPayload, tkhd);
        append(trakPayload, mdia);
        ByteBuffer trak = BuildMp4Box("trak", trakPayload);

        ByteBuffer mvhdPayload;
        WriteU32BE(mvhdPayload, 0);
        WriteU32BE(mvhdPayload, 0);
        WriteU32BE(mvhdPayload, 0);
        WriteU32BE(mvhdPayload, sampleRate);
        WriteU32BE(mvhdPayload, totalDuration);
        WriteU32BE(mvhdPayload, 0x00010000);
        WriteU16BE(mvhdPayload, 0x0100);
        mvhdPayload.insert(mvhdPayload.end(), 10, 0);
        WriteIdentityMatrixBE(mvhdPayload);
        mvhdPayload.insert(mvhdPayload.end(), 24, 0);
        WriteU32BE(mvhdPayload, 2); // next_track_ID
        ByteBuffer mvhd = BuildMp4Box("mvhd", mvhdPayload);

        ByteBuffer moovPayload;
        append(moovPayload, mvhd);
        append(moovPayload, trak);
        return BuildMp4Box("moov", moovPayload);
    };

    ByteBuffer dummyMoov = buildMoov(0);
    u32 mdatOffset = static_cast<u32>(ftyp.size() + dummyMoov.size() + 8);
    ByteBuffer moov = buildMoov(mdatOffset);

    ByteBuffer out;
    out.reserve(ftyp.size() + moov.size() + 8 + mdatPayloadSize);
    out.insert(out.end(), ftyp.begin(), ftyp.end());
    out.insert(out.end(), moov.begin(), moov.end());
    WriteU32BE(out, static_cast<u32>(8 + mdatPayloadSize));
    WriteFourCC(out, "mdat");
    for (const auto& p : packets) {
        out.insert(out.end(), p.begin(), p.end());
    }

    return platform::WriteFileBytes(m4aPath, out);
}

Result<void> AudioCodec::ExportRawOrOgg(const AudioClipData& clip, const std::filesystem::path& outPath) {
    if (clip.rawAudio.empty()) return Result<void>::Err("AudioClip has no payload to export");
    return platform::WriteFileBytes(outPath, clip.rawAudio);
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
