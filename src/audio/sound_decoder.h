#pragma once

#include <QAudioFormat>
#include <QByteArray>

#include <cstdint>
#include <optional>
#include <string>

namespace ptd {

// Decoded PCM payload for one sound file. The decoder is an OFFLINE tool
// only: boundary/format tests and the release self-check decode it to prove
// the shipped WAV contract. Runtime playback never uses it -- the engine
// caches a resolved file path per event slot (see SoundEngine below) and
// hands that path to QSoundEffect/QUrl, which loads the audio itself.
struct DecodedSound {
    QByteArray pcm;
    QAudioFormat format;
    int64_t generation = 0;
};

// Decodes one RIFF/WAVE asset into normalized PCM. The shipped inventory is
// plain RIFF/WAVE PCM (8/16/24/32-bit plus IEEE float); anything else is
// rejected as an invalid asset rather than handed to the audio device as
// noise. Normalization contract: 24-bit PCM is widened to Int32 with exact
// amplitudes (sample * 256); IEEE float32/float64 payloads become float32, so
// the reported sample format always matches the buffer representation;
// truncated or malformed chunks are rejected. Never called on the raw input
// callback thread.
std::optional<DecodedSound> decode_wav_file(const std::string& absolute_path);

} // namespace ptd
