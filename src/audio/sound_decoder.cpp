#include "sound_decoder.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace ptd {
namespace {

std::uint32_t read_u32(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0]))
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

std::uint16_t read_u16(const char* p) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(p[0]))
         | static_cast<std::uint16_t>(static_cast<unsigned char>(p[1]) << 8);
}

// Minimal RIFF/WAVE reader: finds the fmt and data chunks of a canonical WAV
// file and normalises 8/16/24/32-bit PCM (word-aligned, little-endian, signed
// or unsigned per the format tag) into a QAudioFormat the platform sink can
// play. Everything else is rejected.
bool parse_wav(const std::string& path, DecodedSound& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (bytes.size() < 44) return false;
    const char* p = bytes.data();

    if (std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) {
        return false;
    }

    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 0;
    bool have_fmt = false;

    const char* data = nullptr;
    std::size_t data_size = 0;

    std::size_t pos = 12;
    const std::size_t total = bytes.size();
    while (pos + 8 <= total) {
        const char* id = p + pos;
        const std::uint32_t size = read_u32(p + pos + 4);
        const std::size_t body = pos + 8;
        if (body > total) break;
        const std::size_t avail = total - body;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            // The fmt chunk must declare a complete 16-byte core (40-byte for
            // WAVE_FORMAT_EXTENSIBLE) that lies INSIDE the file. An undersized
            // or truncated fmt is an invalid asset, never a best-effort guess.
            if (size < 16 || size > avail) return false;
            audio_format = read_u16(p + body);
            channels = read_u16(p + body + 2);
            sample_rate = read_u32(p + body + 4);
            bits = read_u16(p + body + 14);
            if (audio_format == 0xFFFE) {
                // WAVE_FORMAT_EXTENSIBLE: the real format tag is in the first
                // two bytes of SubFormat (a plain PCM GUID), which is what the
                // shipped inventory uses.
                if (size < 40 || avail < 40) return false;
                audio_format = read_u16(p + body + 24);
            }
            have_fmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            // A data chunk that claims more bytes than the file holds is
            // truncated: reject it instead of silently decoding a prefix and
            // presenting partial audio as a complete asset.
            if (size > avail) return false;
            data = p + body;
            data_size = size;
        }
        pos = body + size + (size & 1u);
    }

    if (!have_fmt || data == nullptr || data_size == 0) return false;

    // Only the bit depths the engine can normalise deterministically.
    const bool pcm_float = (audio_format == 3);  // IEEE float
    if (!(audio_format == 1 || pcm_float)) return false;
    const std::uint16_t bytes_per_sample = static_cast<std::uint16_t>(bits / 8);
    if (bytes_per_sample == 0) return false;
    if (pcm_float && !(bits == 32 || bits == 64)) return false;
    if (!pcm_float && !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) return false;
    if (channels == 0) return false;

    const std::size_t frames = data_size / (bytes_per_sample * channels);
    if (frames == 0) return false;
    const std::size_t usable = frames * bytes_per_sample * channels;

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(sample_rate));
    format.setChannelCount(static_cast<int>(channels));
    if (pcm_float) {
        // IEEE float payloads are normalized to float32 PCM, so the reported
        // sample format and the buffer representation always agree: every
        // sample in the buffer is exactly one 32-bit float.
        format.setSampleFormat(QAudioFormat::Float);
        const std::size_t samples = frames * channels;
        if (bits == 64) {
            // float64 -> float32, explicit and lossy by design: the buffer may
            // not carry 64-bit samples while advertising QAudioFormat::Float
            // (a 32-bit format), and an unsupported label is never reported as
            // a successful decode.
            QByteArray converted(static_cast<int>(samples * 4), Qt::Uninitialized);
            const unsigned char* src = reinterpret_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < samples; ++i) {
                double wide = 0.0;
                std::memcpy(&wide, src + i * 8, 8);  // IEEE-754 binary64, little-endian
                const float narrow = static_cast<float>(wide);
                std::memcpy(converted.data() + i * 4, &narrow, 4);
            }
            out.pcm = std::move(converted);
        } else {
            out.pcm = QByteArray(reinterpret_cast<const char*>(data),
                                 static_cast<int>(usable));
        }
        out.format = format;
        return true;
    }

    // PCM: 24-bit is widened to 32-bit; everything else is already aligned.
    QByteArray pcm;
    if (bits == 24) {
        pcm.resize(static_cast<int>(frames * channels * 4));
        const unsigned char* src = reinterpret_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < frames * channels; ++i) {
            const std::uint32_t raw = static_cast<std::uint32_t>(src[i * 3])
                                    | (static_cast<std::uint32_t>(src[i * 3 + 1]) << 8)
                                    | (static_cast<std::uint32_t>(src[i * 3 + 2]) << 16);
            // Explicit two's-complement conversion of the 24-bit value, then a
            // well-defined unsigned widen into the top 24 bits (sample * 256,
            // full Int32 scale). The old signed `v << 8` overflows Int32 for
            // raw values above 0x7FFFFF, which is undefined behavior; the
            // unsigned path is defined for every input and preserves exact
            // negative and positive amplitudes.
            const std::uint32_t sign_extended =
                (raw & 0x00800000u) ? (raw | 0xFF000000u) : raw;
            const std::uint32_t widened_u = sign_extended << 8;
            const std::int32_t widened = static_cast<std::int32_t>(widened_u);
            std::memcpy(pcm.data() + i * 4, &widened, 4);
        }
        format.setSampleFormat(QAudioFormat::Int32);
    } else {
        pcm = QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(usable));
        format.setSampleFormat(bits == 8 ? QAudioFormat::UInt8
                                         : (bits == 16 ? QAudioFormat::Int16
                                                       : QAudioFormat::Int32));
    }

    out.pcm = std::move(pcm);
    out.format = format;
    return true;
}

} // namespace

std::optional<DecodedSound> decode_wav_file(const std::string& absolute_path) {
    DecodedSound sound;
    if (!parse_wav(absolute_path, sound)) return std::nullopt;
    // A decoded payload with no usable format is an invalid asset.
    if (sound.format.sampleFormat() == QAudioFormat::Unknown
        || sound.format.sampleRate() <= 0 || sound.format.channelCount() <= 0
        || sound.pcm.isEmpty()) {
        return std::nullopt;
    }
    return sound;
}

} // namespace ptd
