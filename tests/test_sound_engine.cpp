// T-80: dedicated audio-engine regressions.
//
// SoundEngine is driven through the injectable FakePlayer, so every control
// below is deterministic and needs no audio hardware. Exceptions, each marked
// at its site: the owned-player lifecycle check constructs (but never plays
// through) the production QtSoundPlayer to prove the ownership contract, and
// the final check decodes the REAL shipped WAV catalog. Optional checks report
// RAN or SKIP explicitly -- a skip is never silent and never reads as a pass.

#include "../src/config/audio_config.h"
#include "../src/audio/sound_engine.h"
#include "../src/audio/sound_backend.h"
#include "../src/audio/sound_decoder.h"
#include "../src/core/log.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace ptd;

int g_failures = 0;
int g_checks = 0;
// Evidence trail: every optional check reports RAN/SKIP explicitly, so a
// release acceptance can never read a skipped check as a verified one.
std::string g_reparse_guard_status = "NOT RUN";
std::string g_shipped_catalog_status = "NOT RUN";

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

void check_eq(long long got, long long want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL %s: got %lld want %lld\n", what, got, want);
    }
}

void check_pcm(const ptd::DecodedSound& s, const std::string& want, const char* what) {
    ++g_checks;
    const std::string got(s.pcm.constData(), static_cast<std::size_t>(s.pcm.size()));
    if (got != want) {
        ++g_failures;
        std::printf("FAIL %s: pcm bytes differ (got %u bytes, want %u)\n", what,
                    static_cast<unsigned>(got.size()), static_cast<unsigned>(want.size()));
    }
}

// Volume is a float; comparing it in permille keeps the control exact without
// a float-tolerant comparison helper.
long long volume_permille(float v) {
    if (v != v) return -1;  // NaN
    return static_cast<long long>(v * 1000.0f + 0.5f);
}

void put16s(std::string& out, unsigned v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put32s(std::string& out, unsigned v) {
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
    out.push_back(static_cast<char>((v >> 16) & 0xFFu));
    out.push_back(static_cast<char>((v >> 24) & 0xFFu));
}

// Format-faithful RIFF/WAVE writer: every staged byte stream matches its
// declared bit depth, channel count and frame count (8-bit is unsigned PCM,
// wider PCM is signed little-endian), so a staged asset is a real WAV of the
// advertised format instead of a header over mismatched payload bytes.
std::string wav_bytes(int bits, int channels, int rate, int frames) {
    const int bytes_per_sample = bits / 8;
    const int data_bytes = frames * channels * bytes_per_sample;
    std::string out;
    out += "RIFF";
    put32s(out, static_cast<unsigned>(36 + data_bytes));
    out += "WAVE";
    out += "fmt ";
    put32s(out, 16);
    put16s(out, 1);  // PCM
    put16s(out, static_cast<unsigned>(channels));
    put32s(out, static_cast<unsigned>(rate));
    put32s(out, static_cast<unsigned>(rate * channels * bytes_per_sample));
    put16s(out, static_cast<unsigned>(channels * bytes_per_sample));
    put16s(out, static_cast<unsigned>(bits));
    out += "data";
    put32s(out, static_cast<unsigned>(data_bytes));
    for (int i = 0; i < frames * channels; ++i) {
        int v = static_cast<int>(3000.0 * (i % 8) / 8.0);
        if (bits == 8) v = 128 + (i % 8) * 8;  // unsigned, midpoint-centred
        for (int b = 0; b < bytes_per_sample; ++b) {
            out.push_back(static_cast<char>((v >> (8 * b)) & 0xFF));
        }
    }
    return out;
}

// Exact-bytes WAV builder for decoder boundary tests: the caller supplies the
// literal sample bytes and the declared chunk sizes, so malformed layouts
// (bad fmt size, truncated or empty data) are constructible too.
std::string wav_exact(int tag, int bits, int channels, int rate,
                      const std::string& sample_bytes, int fmt_size = 16,
                      long long declared_data_size = -1, int fmt_pad = -1) {
    std::string out;
    const long long data_size = declared_data_size >= 0
        ? declared_data_size : static_cast<long long>(sample_bytes.size());
    const int pad = fmt_pad >= 0 ? fmt_pad : (fmt_size > 16 ? fmt_size - 16 : 0);
    out += "RIFF";
    put32s(out, static_cast<unsigned>(36 + sample_bytes.size()));
    out += "WAVE";
    out += "fmt ";
    put32s(out, static_cast<unsigned>(fmt_size));
    put16s(out, static_cast<unsigned>(tag));
    put16s(out, static_cast<unsigned>(channels));
    put32s(out, static_cast<unsigned>(rate));
    put32s(out, static_cast<unsigned>(rate * channels * (bits / 8)));
    put16s(out, static_cast<unsigned>(channels * (bits / 8)));
    put16s(out, static_cast<unsigned>(bits));
    out.append(static_cast<std::size_t>(pad), '\0');
    out += "data";
    put32s(out, static_cast<unsigned>(data_size));
    out += sample_bytes;
    return out;
}

// Literal sample-byte builders, so decoder expectations are exact bytes.
std::string sample8(unsigned v) { return std::string(1, static_cast<char>(v & 0xFFu)); }
std::string sample16(unsigned v) { std::string s; put16s(s, v); return s; }
std::string sample24_raw(unsigned v) {
    std::string s;
    put16s(s, v & 0xFFFFu);
    s.push_back(static_cast<char>((v >> 16) & 0xFFu));
    return s;
}
std::string sample32_raw(unsigned v) { std::string s; put32s(s, v); return s; }
std::string samplef32(float v) { std::string s(4, '\0'); std::memcpy(s.data(), &v, 4); return s; }
std::string samplef64(double v) { std::string s(8, '\0'); std::memcpy(s.data(), &v, 8); return s; }

bool write_file(const QString& path, const std::string& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const qint64 written = f.write(bytes.data(), static_cast<qint64>(bytes.size()));
    f.close();
    return written == static_cast<qint64>(bytes.size());
}

// ---------------------------------------------------------------- wheel math

void test_accumulate_wheel() {
    int acc = 0;
    check_eq(ptd::accumulate_wheel(acc, 120), 1, "one notch up");
    check_eq(acc, 0, "notch consumed exactly");
    check_eq(ptd::accumulate_wheel(acc, -120), -1, "one notch down");
    check_eq(acc, 0, "down notch consumed");

    int frac = 0;
    check_eq(ptd::accumulate_wheel(frac, 60), 0, "half notch emits nothing");
    check_eq(ptd::accumulate_wheel(frac, 60), 1, "second half completes a notch");
    check_eq(frac, 0, "remainder carried, not dropped");

    int zero = 5;
    check_eq(ptd::accumulate_wheel(zero, 0), 0, "zero delta emits nothing");

    int huge = 0;
    const int emitted = ptd::accumulate_wheel(huge, 100000);
    check(emitted <= ptd::kMaxWheelEventsPerPacket, "huge delta is bounded per packet");
    check(huge <= ptd::kWheelDeltaUnit * ptd::kMaxWheelEventsPerPacket, "accumulator clamped");
    check(emitted == ptd::kMaxWheelEventsPerPacket, "clamped burst still emits the ceiling");

    int split = 0;
    check_eq(ptd::accumulate_wheel(split, 120 * 20), ptd::kMaxWheelEventsPerPacket,
             "20 notches at once emit at most the per-packet ceiling");
    check(split <= ptd::kWheelDeltaUnit * ptd::kMaxWheelEventsPerPacket,
          "the accumulator stays bounded after a burst");
    check_eq(ptd::accumulate_wheel(split, 0), split / ptd::kWheelDeltaUnit,
             "a zero packet cannot invent new notches");

    int cancel = 0;
    check_eq(ptd::accumulate_wheel(cancel, 40), 0, "partial up emits nothing");
    check_eq(ptd::accumulate_wheel(cancel, -40), 0, "opposite partial cancels the accumulator");
    check_eq(cancel, 0, "cancelled accumulator is zero");
    check_eq(ptd::accumulate_wheel(cancel, 40), 0, "cancelled accumulator emits nothing");
}

// ------------------------------------------------------------- config layer

void test_audio_config_validation() {
    ptd::AudioConfig c{};
    c.volume = -1.0f;
    check_eq(volume_permille(ptd::AudioConfig::validated(c).volume), 0, "volume clamped up from below");
    c.volume = 5.0f;
    check_eq(volume_permille(ptd::AudioConfig::validated(c).volume), 1000, "volume clamped down from above");
    c.volume = std::numeric_limits<float>::quiet_NaN();  // NaN must not survive
    check_eq(volume_permille(ptd::AudioConfig::validated(c).volume), 1000, "NaN volume rejected");

    check(ptd::sanitize_sound_id("").empty(), "empty id is None");
    check(ptd::sanitize_sound_id("../escape.wav").empty(), "parent reference rejected");
    check(ptd::sanitize_sound_id("..\\escape.wav").empty(), "windows parent rejected");
    check(ptd::sanitize_sound_id("C:/win/booms.wav").empty(), "drive-qualified path rejected");
    check(ptd::sanitize_sound_id("sub/dir/click.wav").empty(), "subdirectory rejected");
    check(ptd::sanitize_sound_id(".hidden.wav").empty(), "hidden name rejected");
    check(ptd::sanitize_sound_id("noextension").empty(), "missing extension rejected");
    check(ptd::sanitize_sound_id("boom.mp3").empty(), "non-wav rejected");
    check(ptd::sanitize_sound_id(std::string(ptd::AudioConfig::kMaxSoundNameLength + 1, 'a') + ".wav").empty(),
          "over-long id rejected");
    check(ptd::sanitize_sound_id("normal.wav") == "normal.wav", "plain file name accepted");

    ptd::AudioConfig d{};
    d.sounds[0] = "../escape.wav";
    d.sounds[1] = "click.wav";
    const ptd::AudioConfig v = ptd::AudioConfig::validated(d);
    check(v.sounds[0].empty(), "escaping assignment cannot persist");
    check(v.sounds[1] == "click.wav", "valid assignment persists");
}

// ------------------------------------------------------------------ catalog

void test_catalog(QTemporaryDir& dir, const QString& a, const QString& b) {
    check(ptd::SoundCatalog::sound_ids().contains(a), "catalog lists staged file A");
    check(ptd::SoundCatalog::sound_ids().contains(b), "catalog lists staged file B");
    check(ptd::SoundCatalog::sound_ids().contains("notes.txt") == false,
          "catalog ignores non-wav files");
    check(ptd::SoundCatalog::sound_exists("click.wav"), "existing sound reported present");
    check(!ptd::SoundCatalog::sound_exists("missing.wav"), "absent sound reported missing");

    check(ptd::SoundCatalog::resolve_sound_path("click.wav") ==
              QFileInfo(dir.filePath("click.wav")).absoluteFilePath(),
          "valid id resolves inside the sound directory");

    check(ptd::SoundCatalog::resolve_sound_path("../click.wav").isEmpty(),
          "parent traversal cannot resolve");
    check(ptd::SoundCatalog::resolve_sound_path("C:/click.wav").isEmpty(),
          "absolute path cannot resolve");
    check(ptd::SoundCatalog::resolve_sound_path("nested/click.wav").isEmpty(),
          "nested path cannot resolve");
    check(ptd::SoundCatalog::resolve_sound_path("click.txt").isEmpty(),
          "non-wav cannot resolve");
}

// ------------------------------------------------------------------- engine

struct Fixture {
    FakePlayer player;
    SoundEngine engine;
    bool ok = false;

    Fixture() {
        engine.set_player(&player);
        ok = engine.initialize() && player.initialize();
    }
    ~Fixture() { engine.shutdown(); }
};

int preview_records(const FakePlayer& p) {
    int n = 0;
    for (const auto& r : p.playing) {
        if (r.is_preview) ++n;
    }
    return n;
}

// T-122: playback attempts excluding preview voices, so a preview audition
// can never be miscounted as a runtime lane event.
int runtime_plays(const FakePlayer& p) {
    return p.runtime_plays;
}

// T-81: the preview lifecycle. Browsing the catalog is the Sound tab's core
// gesture, so this drives it through 50 consecutive selections across the 12
// staged files and every admission rule the user depends on.
void test_preview_lifecycle(const QStringList& ids, const QString& stage_path) {
    Fixture f;
    check(f.ok, "engine initializes with injected player");

    // Audio disabled by default: an upgraded install must stay silent for
    // RUNTIME events, but the catalog must still be auditionable.
    check(!f.engine.audio_audible(), "disabled audio is not audible");
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, 0, "disabled audio plays no runtime event");
    check(preview_records(f.player) == 0, "nothing is playing yet");

    // ---- 50 consecutive preview selections over the 12 staged files -------
    // NOTE: this is a SELECTION stress (replacement, restart, admission), not
    // a distinct-source claim: the 12 staged files cycle by design. The
    // genuinely 50-unique-source sequence lives in protrail_audio_runtime_tests.
    for (int i = 0; i < 50; ++i) {
        const std::string id = ids[static_cast<int>(i % ids.size())].toStdString();
        f.engine.preview_sound(id);
        const std::string want =
            QFileInfo(QFileInfo(stage_path).absoluteFilePath(),
                      QString::fromStdString(id))
                .canonicalFilePath()
                .toStdString();
        check(!f.player.playing.empty(), "browsing plays a preview");
        check(preview_records(f.player) == 1,
              "each selection replaces the previous preview voice");
        check(f.player.playing.back().is_preview, "the newest record is the preview");
        check(f.player.playing.back().path == want,
              "the preview source is the selected staged file");
        check_eq(f.player.total_plays, static_cast<long long>(i) + 1,
                 "every distinct selection produces exactly one play");
        check_eq(f.player.preview_voice_count(), 1, "at most one preview voice exists");
        check(f.player.voice_pool_size() <= 3, "browsing stays inside the pool bound");
    }
    {
        const std::string last = ids[49 % ids.size()].toStdString();
        check(f.player.last_path.ends_with(last), "the last selection is the current preview");
        check(f.engine.is_playing_preview(), "preview is reported active");
    }

    // ---- replaying the same selection restarts it ---------------------------
    f.engine.preview_sound(ids[0].toStdString());
    check_eq(preview_records(f.player), 1, "replay replaces, it never stacks");
    f.engine.stop_preview();
    check_eq(f.player.preview_stops, 1, "stop reached the player");
    f.engine.preview_sound(ids[0].toStdString());
    f.engine.preview_sound(ids[0].toStdString());
    check_eq(preview_records(f.player), 1, "rapid identical selections stay one voice");

    // ---- an event plays while a preview is active ---------------------------
    ptd::AudioConfig c{};
    c.enabled = true;
    c.sounds[static_cast<int>(ptd::AudioEvent::LeftDown)] = "click.wav";
    f.engine.set_master_enabled(true);
    f.engine.apply_config(c);
    check(f.engine.audio_audible(), "enabled+unmuted+master is audible");

    // A None assignment stays silent.
    f.engine.play_event(ptd::AudioEvent::RightDown);
    check_eq(f.player.total_plays, 53, "None assignment is silent");

    f.engine.preview_sound(ids[1].toStdString());
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, 55, "event plays on top of the preview");
    check(f.player.last_event == "left_down", "left_down event key used");
    check(f.player.last_is_preview == false, "runtime play is not a preview");
    check_eq(preview_records(f.player), 1, "the event did not touch the preview voice");
    int event_records = 0;
    for (const auto& r : f.player.playing) {
        if (!r.is_preview) ++event_records;
    }
    check_eq(event_records, 1, "the runtime event has its own voice");

    // Stop Preview stops only the preview; the event voice keeps playing.
    f.engine.stop_preview();
    check_eq(preview_records(f.player), 0, "stop preview silences the preview");
    check_eq(f.player.preview_stops, 2, "the second stop reached the player");
    event_records = 0;
    for (const auto& r : f.player.playing) {
        if (!r.is_preview) ++event_records;
    }
    check_eq(event_records, 1, "stop preview left the event voice playing");
    check(!f.engine.is_playing_preview(), "preview reported stopped");

    // ---- a missing asset is silent, not a crash ------------------------------
    c.sounds[static_cast<int>(ptd::AudioEvent::LeftDown)] = "missing.wav";
    f.engine.apply_config(c);
    const auto before_missing = f.player.total_plays;
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, before_missing, "missing asset plays nothing");
    c.sounds[static_cast<int>(ptd::AudioEvent::LeftDown)] = "click.wav";
    f.engine.apply_config(c);

    // ---- preview survives disabled mouse effects ------------------------------
    f.engine.preview_sound(ids[2].toStdString());
    check_eq(preview_records(f.player), 1, "preview active before the disable");
    c.enabled = false;
    f.engine.apply_config(c);
    check_eq(preview_records(f.player), 1, "disabling mouse effects keeps the preview");
    f.engine.preview_sound(ids[3].toStdString());
    check_eq(preview_records(f.player), 1, "preview allowed while audio disabled");
    check_eq(f.player.total_plays, 57, "the runtime gate did not block the preview");

    // ---- preview survives application master OFF --------------------------------
    f.engine.set_master_enabled(false);
    check(!f.engine.audio_audible(), "master OFF suppresses runtime audio");
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, 57, "master OFF plays no runtime event");
    f.engine.preview_sound(ids[4].toStdString());
    check_eq(preview_records(f.player), 1, "preview allowed with master OFF");
    check_eq(f.player.total_plays, 58, "the master gate did not block the preview");
    event_records = 0;
    for (const auto& r : f.player.playing) {
        if (!r.is_preview) ++event_records;
    }
    check_eq(event_records, 0, "master OFF removed the runtime voice");

    // ---- an assignment save must not cancel a deliberate preview ----------------
    f.engine.set_master_enabled(true);
    c.enabled = true;
    c.sounds[static_cast<int>(ptd::AudioEvent::LeftDown)] = "s04.wav";
    f.engine.preview_sound(ids[5].toStdString());
    check_eq(preview_records(f.player), 1, "preview active before the save");
    f.engine.apply_config(c);
    check_eq(preview_records(f.player), 1, "saving an assignment keeps the preview");
    check(f.engine.is_playing_preview(), "preview still reported active after save");

    // ---- volume changes reach the live preview ------------------------------------
    c.volume = 0.25f;
    f.engine.apply_config(c);
    check(!f.player.volume_changes.empty() &&
              f.player.volume_changes.back() > 0.24 &&
              f.player.volume_changes.back() < 0.26,
          "a volume change updates the live preview unchanged");
    check(f.engine.is_playing_preview(), "volume change keeps the preview playing");

    // ---- mute stops both; unmute resurrects nothing ---------------------------------
    c.muted = true;
    f.engine.apply_config(c);
    check(f.player.playing.empty(), "mute stops runtime AND preview playback");
    f.engine.preview_sound(ids[6].toStdString());
    check(preview_records(f.player) == 0, "muted preview is refused, not queued silent");
    c.muted = false;
    f.engine.apply_config(c);
    check(preview_records(f.player) == 0, "unmute does not resurrect an old preview");
    f.engine.preview_sound(ids[6].toStdString());
    check_eq(preview_records(f.player), 1, "preview works again after unmute");

    // ---- repeated Stop/Replay cycles ----------------------------------------------------
    for (int cycle = 0; cycle < 8; ++cycle) {
        f.engine.preview_sound(ids[6 + cycle % 6].toStdString());
        check_eq(preview_records(f.player), 1, "replay after stop plays");
        f.engine.stop_preview();
        check_eq(preview_records(f.player), 0, "stop after replay silences");
    }
    f.engine.preview_sound(ids[0].toStdString());
    check_eq(preview_records(f.player), 1, "playback still works after the cycles");

    // ---- invalid ids stay silent, never a crash -----------------------------------------
    f.engine.preview_sound("missing.wav");
    check(preview_records(f.player) == 0, "a missing asset previews nothing");
    f.engine.preview_sound("../escape.wav");
    check(preview_records(f.player) == 0, "an escaping id previews nothing");
    f.engine.preview_sound("");
    check(preview_records(f.player) == 0, "None previews nothing");

    // shutdown leaves nothing playing.
    f.engine.shutdown();
    check(!f.engine.is_playing_preview(), "shutdown ends previews");
    check_eq(f.engine.active_voice_count(), 0, "shutdown leaves no active voice");
    check(f.player.shutdown_called, "shutdown reached the player");
}

// T-107: stop_all_sounds() is the Sound tab's "Stop All". It must silence the
// preview AND every runtime event voice -- a superset of stop_preview(), which
// by design leaves the event voices playing.
void test_stop_all_sounds(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initializes with injected player");

    ptd::AudioConfig c{};
    c.enabled = true;
    c.sounds[static_cast<int>(ptd::AudioEvent::LeftDown)] = "click.wav";
    c.sounds[static_cast<int>(ptd::AudioEvent::RightDown)] = "boomer.wav";
    f.engine.set_master_enabled(true);
    f.engine.apply_config(c);

    // One preview voice plus two runtime event voices are live at once.
    f.engine.preview_sound(ids[0].toStdString());
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    f.engine.play_event(ptd::AudioEvent::RightDown);
    check_eq(preview_records(f.player), 1, "a preview voice is live");
    check(f.engine.active_voice_count() >= 3, "two event voices plus the preview are live");

    // Stop Preview is deliberately narrower: the event voices keep playing.
    f.engine.stop_preview();
    check_eq(preview_records(f.player), 0, "stop_preview silenced the preview");
    check(f.engine.active_voice_count() >= 2,
          "stop_preview left the runtime event voices playing");

    // Stop All silences everything and clears the preview selection.
    f.engine.preview_sound(ids[1].toStdString());
    f.engine.stop_all_sounds();
    check_eq(f.engine.active_voice_count(), 0, "stop_all_sounds silences every voice");
    check(!f.engine.is_playing_preview(), "stop_all_sounds clears the preview state");
    check_eq(f.player.all_stops, 1, "stop_all_sounds reached the player's stop_all");
    check_eq(f.player.preview_stops, 1, "stop_all_sounds did not ride stop_preview");

    // The engine is still usable afterwards: a later preview works.
    f.engine.preview_sound(ids[2].toStdString());
    check_eq(preview_records(f.player), 1, "the engine still previews after Stop All");
}

void test_engine() {
    // The volume slider range is the validated range.
    {
        ptd::AudioConfig c{};
        c.enabled = true;
        c.volume = 0.42f;
        Fixture f;
        f.engine.set_master_enabled(true);
        f.engine.apply_config(c);
        check(f.player.volume_changes.size() >= 1, "volume published to the player");
        if (!f.player.volume_changes.empty()) {
            check(f.player.volume_changes.back() > 0.40 && f.player.volume_changes.back() < 0.44,
                  "volume value reaches the player unchanged");
        }
        c.volume = 9.0f;
        f.engine.apply_config(c);
        check(f.player.volume <= 1.0, "over-range volume clamped before playback");
    }
}

// Decodes a byte image through a scratch file. Returns false when the file
// cannot be written or the decoder rejects the asset.
bool decode_image(const QString& dir, const char* name, const std::string& bytes,
                  ptd::DecodedSound& out) {
    const QString path = QDir(dir).filePath(QString::fromLatin1(name));
    if (!write_file(path, bytes)) return false;
    const auto decoded = ptd::decode_wav_file(path.toStdString());
    if (!decoded) return false;
    out = *decoded;
    return true;
}

// P2: WAV decoder boundary coverage. Every case asserts the exact decoded
// sample bytes and the advertised QAudioFormat together, so a format/buffer
// mismatch (the float64 class of defect: 64-bit payload advertised as a
// 32-bit Float format) or an amplitude error (the 24-bit signed-shift class)
// fails HERE instead of surfacing as noise through the speakers.
void test_wav_decoder_boundaries(const QString& scratch) {
    ptd::DecodedSound s;

    // ---- PCM8: minimum / midpoint / maximum --------------------------------
    {
        const std::string data = sample8(0) + sample8(128) + sample8(255);
        check(decode_image(scratch, "d_pcm8.wav", wav_exact(1, 8, 1, 22050, data), s),
              "PCM8 decodes");
        check(s.format.sampleFormat() == QAudioFormat::UInt8, "PCM8 advertises UInt8");
        check_eq(s.format.channelCount(), 1, "PCM8 mono");
        check_eq(s.format.sampleRate(), 22050, "PCM8 rate preserved");
        check_pcm(s, data, "PCM8 samples are preserved exactly");
    }

    // ---- PCM16: minimum / zero / maximum -----------------------------------
    {
        const std::string data = sample16(0x8000u) + sample16(0) + sample16(0x7FFFu);
        check(decode_image(scratch, "d_pcm16.wav", wav_exact(1, 16, 1, 44100, data), s),
              "PCM16 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Int16, "PCM16 advertises Int16");
        check_pcm(s, data, "PCM16 samples are preserved exactly");
    }

    // ---- PCM24: min / -1 / zero / +1 / max ---------------------------------
    {
        const std::string data = sample24_raw(0x800000u) + sample24_raw(0xFFFFFFu)
                               + sample24_raw(0x000000u) + sample24_raw(0x000001u)
                               + sample24_raw(0x7FFFFFu);
        // Exact widening contract: 24-bit * 256 on the full Int32 scale.
        // -8388608 -> -2147483648, -1 -> -256, 0 -> 0, 1 -> 256,
        // 8388607 -> 2147483392. The maximum case overflows a signed 32-bit
        // left shift and must still land on its exact value.
        const std::string want = sample32_raw(0x80000000u) + sample32_raw(0xFFFFFF00u)
                               + sample32_raw(0x00000000u) + sample32_raw(0x00000100u)
                               + sample32_raw(0x7FFFFF00u);
        check(decode_image(scratch, "d_pcm24.wav", wav_exact(1, 24, 1, 44100, data), s),
              "PCM24 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Int32, "PCM24 widens to Int32");
        check_eq(s.pcm.size(), 20, "PCM24 widens 5 x 3 bytes to 5 x 4 bytes");
        check_pcm(s, want, "PCM24 amplitudes survive the widening exactly");
    }

    // ---- PCM32 boundary values ---------------------------------------------
    {
        const std::string data = sample32_raw(0x80000000u) + sample32_raw(0x00000000u)
                               + sample32_raw(0x7FFFFFFFu);
        check(decode_image(scratch, "d_pcm32.wav", wav_exact(1, 32, 1, 48000, data), s),
              "PCM32 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Int32, "PCM32 advertises Int32");
        check_pcm(s, data, "PCM32 boundary samples are preserved exactly");
    }

    // ---- IEEE float32: the advertised format must match the buffer ---------
    {
        const std::string data = samplef32(1.0f) + samplef32(-0.5f) + samplef32(0.0f);
        check(decode_image(scratch, "d_f32.wav", wav_exact(3, 32, 1, 44100, data), s),
              "float32 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Float,
              "float32 advertises Float, never Int32");
        check_pcm(s, data, "float32 samples are preserved exactly");
    }

    // ---- float64: converted to float32, or explicitly rejected -------------
    {
        const std::string data = samplef64(1.0) + samplef64(-0.5) + samplef64(0.0);
        const std::string want = samplef32(1.0f) + samplef32(-0.5f) + samplef32(0.0f);
        const bool decoded =
            decode_image(scratch, "d_f64.wav", wav_exact(3, 64, 1, 48000, data), s);
        if (decoded) {
            check(s.format.sampleFormat() == QAudioFormat::Float,
                  "float64 advertises Float after conversion");
            check_eq(s.pcm.size(), 12, "float64 converts to 3 x 4-byte float32 samples");
            check_pcm(s, want, "float64 converts to exact float32 values");
        } else {
            // Explicit rejection is the allowed alternative. What is NOT
            // allowed is a 64-bit payload advertised as a 32-bit format.
            check(true, "float64 explicitly rejected as unsupported");
        }
    }

    // ---- mono/stereo frame integrity ---------------------------------------
    {
        const std::string mono = sample16(1) + sample16(2) + sample16(3) + sample16(4);
        check(decode_image(scratch, "d_mono.wav", wav_exact(1, 16, 1, 22050, mono), s),
              "mono decodes");
        check_eq(s.format.channelCount(), 1, "mono reports 1 channel");
        check_pcm(s, mono, "mono frames keep order");

        const std::string stereo = sample16(1) + sample16(0xFFFFu) + sample16(2) + sample16(0xFFFEu)
                                 + sample16(3) + sample16(0xFFFDu) + sample16(4) + sample16(0xFFFCu);
        check(decode_image(scratch, "d_stereo.wav", wav_exact(1, 16, 2, 44100, stereo), s),
              "stereo decodes");
        check_eq(s.format.channelCount(), 2, "stereo reports 2 channels");
        check_eq(s.pcm.size(), 16, "stereo keeps 4 frames x 2 channels x 2 bytes");
        check_pcm(s, stereo, "stereo L/R samples interleave unchanged");
    }

    // ---- truncated RIFF chunks ---------------------------------------------
    {
        const std::string body = sample16(1) + sample16(2);
        check(!decode_image(scratch, "d_trunc.wav",
                            wav_exact(1, 16, 1, 22050, body, 16, 400), s),
              "a data chunk claiming more bytes than the file holds is rejected");
        std::string cut = wav_exact(1, 16, 1, 22050, sample16(1) + sample16(2) + sample16(3));
        cut.resize(cut.size() - 4);
        check(!decode_image(scratch, "d_cut.wav", cut, s),
              "a file cut mid-sample is rejected");
        check(!decode_image(scratch, "d_fmtbig.wav",
                            wav_exact(1, 16, 1, 22050, "", 200, 0, 0), s),
              "a fmt chunk claiming more bytes than the file holds is rejected");
    }

    // ---- incorrect fmt chunk sizes -----------------------------------------
    {
        check(!decode_image(scratch, "d_fmt12.wav",
                            wav_exact(1, 16, 1, 22050, sample16(1), 12, -1, 0), s),
              "a fmt chunk smaller than 16 bytes is rejected");
    }

    // ---- empty data chunk ---------------------------------------------------
    {
        check(!decode_image(scratch, "d_empty.wav", wav_exact(1, 16, 1, 22050, ""), s),
              "an empty data chunk is rejected");
    }

    // ---- invalid channel count ---------------------------------------------
    {
        check(!decode_image(scratch, "d_ch0.wav",
                            wav_exact(1, 16, 0, 22050, sample16(1)), s),
              "a zero channel count is rejected");
    }

    // ---- unsupported encoding ----------------------------------------------
    {
        check(!decode_image(scratch, "d_adpcm.wav",
                            wav_exact(2, 16, 1, 22050, std::string(8, '\x00')), s),
              "MS ADPCM is rejected as unsupported");
        check(!decode_image(scratch, "d_12bit.wav",
                            wav_exact(1, 12, 1, 22050, std::string(8, '\x00')), s),
              "a 12-bit PCM payload is rejected as unsupported");
    }
}

// P1: the ownership contract. An injected FakePlayer belongs to the TEST and
// must survive every engine lifecycle operation untouched.
void test_engine_injected_ownership() {
    FakePlayer player;
    SoundEngine engine;
    engine.set_player(&player);
    check(engine.player_for_tests() == &player, "the injected player is the live player");
    check(!engine.owns_player_for_tests(), "an injected player is never owned");

    for (int cycle = 0; cycle < 3; ++cycle) {
        check(engine.initialize(), "initialize succeeds on every cycle");
        engine.preview_sound("click.wav");
        check(engine.is_playing_preview(), "the preview plays on every cycle");
        engine.shutdown();
        check(!engine.is_playing_preview(), "no residual preview after shutdown");
        check_eq(engine.active_voice_count(), 0, "no residual voice after shutdown");
        check(player.playing.empty(), "shutdown drained the injected player's records");
        check(engine.player_for_tests() == &player,
              "the injected player survives shutdown untouched");
        check(!engine.owns_player_for_tests(), "no owned player appears for an injected player");
    }

    // Repeated shutdown is a no-op, never a second teardown or a crash.
    engine.shutdown();
    engine.shutdown();
    check(player.shutdown_called, "shutdown reached the player");
    check(player.initialized == false, "repeated shutdown does not resurrect the player");
    check(engine.player_for_tests() == &player, "the injected player still survives");
    check(engine.initialize(), "initialize works after repeated shutdown");
    engine.preview_sound("click.wav");
    check(engine.is_playing_preview(), "the engine still plays after repeated shutdown");
    engine.shutdown();
}

// P1: the OWNED production player (no injection). Each initialize/shutdown
// cycle must build a FRESH QtSoundPlayer and destroy the previous one, with
// the cached pointer cleared BEFORE destruction -- never reachable after it.
// Structural only: the player is constructed but never played through here,
// so the check needs no audio device. Live owned-player playback evidence is
// in protrail_audio_runtime_tests.
void test_owned_player_lifecycle() {
    SoundEngine engine;
    check(engine.player_for_tests() == nullptr, "no player exists before initialize");
    check(engine.initialize(), "the owned production player initializes");
    ISoundPlayer* first = engine.player_for_tests();
    check(first != nullptr && engine.owns_player_for_tests(),
          "initialize creates an owned production player");
    engine.shutdown();
    check(engine.player_for_tests() == nullptr,
          "shutdown clears the cached pointer to the destroyed player");
    check(!engine.owns_player_for_tests(), "shutdown destroys the owned player");
    check(!engine.is_playing_preview(), "no residual preview after shutdown");
    check_eq(engine.active_voice_count(), 0, "no residual voice after shutdown");

    // Every engine surface must tolerate the destroyed player: all of it is
    // gated on the initialized state, so no call can dereference freed memory.
    engine.play_event(ptd::AudioEvent::LeftDown);
    engine.preview_sound("click.wav");
    engine.stop_preview();
    engine.set_master_enabled(true);
    engine.set_master_enabled(false);
    engine.apply_config(ptd::AudioConfig{});
    check(engine.player_for_tests() == nullptr, "no engine call revives a destroyed player");

    // T-103: allocator-proof freshness. `second != first` compares two heap
    // addresses, and once the first player is freed the allocator may hand
    // exactly that block back to a correct rebuild, so a sound implementation
    // would fail. Counting live instances cannot alias: exactly one more means
    // this cycle constructed a new player.
    const int live_before_rebuild = ptd::QtSoundPlayer::live_instance_count();
    check(engine.initialize(), "a second initialize cycle succeeds");
    ISoundPlayer* second = engine.player_for_tests();
    check(second != nullptr
              && ptd::QtSoundPlayer::live_instance_count() == live_before_rebuild + 1,
          "each cycle builds a FRESH player, never a freed one");
    check(engine.owns_player_for_tests(), "the rebuilt player is owned again");
    engine.shutdown();
    engine.shutdown();  // idempotent
    check(engine.player_for_tests() == nullptr, "repeated shutdown leaves no player pointer");
}

// P1: a failed initialization must leave no stale ownership state, and a
// retry must work.
void test_initialization_failure_and_retry() {
    FakePlayer player;
    player.fail_initialize = true;
    SoundEngine engine;
    engine.set_player(&player);
    check(!engine.initialize(), "a failing backend reports initialization failure");
    check(!engine.owns_player_for_tests(), "failed initialization leaves no owned-player state");
    check(engine.player_for_tests() == &player, "the injected player is kept for the retry");
    check(!engine.is_playing_preview(), "a failed engine reports no preview");
    engine.shutdown();  // safe and idempotent after a failure
    engine.shutdown();
    check(engine.player_for_tests() == &player, "shutdown after failure keeps the injected player");

    player.fail_initialize = false;
    check(engine.initialize(), "retry after a failed initialization succeeds");
    engine.preview_sound("click.wav");
    check(engine.is_playing_preview(), "the retried engine plays");
    engine.shutdown();
}

// T-80 gate: the eight runtime events are independent. Each event id plays
// its own assignment on its own record, and unassigning one event silences
// exactly that event.
void test_eight_event_independence(const QStringList& ids) {
    check_eq(ptd::kAudioEventCount, 8, "the event matrix holds eight events");
    check(ids.size() >= ptd::kAudioEventCount, "enough staged files for one per event");
    FakePlayer player;
    SoundEngine engine;
    engine.set_player(&player);
    check(engine.initialize(), "engine initializes for the event matrix");
    engine.set_master_enabled(true);
    ptd::AudioConfig c{};
    c.enabled = true;
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        c.sounds[static_cast<std::size_t>(i)] = ids[i].toStdString();
    }
    engine.apply_config(c);
    check(engine.audio_audible(), "all-assigned audio is audible");

    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        engine.play_event(static_cast<ptd::AudioEvent>(i));
    }
    check_eq(player.total_plays, ptd::kAudioEventCount,
             "each of the eight events produced exactly one play");
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        const std::string key = ptd::audio_event_name(i);
        const std::string want =
            ptd::SoundCatalog::resolve_sound_path(ids[i].toStdString()).toStdString();
        bool found = false;
        for (const auto& r : player.playing) {
            if (!r.is_preview && r.event_id == key && r.path == want) found = true;
        }
        check(found, "each event plays its own assigned file on its own record");
    }
    check_eq(static_cast<long long>(player.playing.size()), ptd::kAudioEventCount,
             "eight independent event voices exist side by side");

    // Unassigning one event silences exactly that event.
    c.sounds[0] = "";
    engine.apply_config(c);
    const long long before = player.total_plays;
    engine.play_event(static_cast<ptd::AudioEvent>(0));
    check_eq(player.total_plays, before, "the unassigned event is silent");
    engine.play_event(static_cast<ptd::AudioEvent>(1));
    check_eq(player.total_plays, before + 1, "the other seven events keep playing");
    engine.shutdown();
}

// T-121 (Gap E): FIFO queue voice completion scheduling.
// Verifies that a queued event drains deterministically upon completion of the
// active voice without polling loops, and clears when the voice finishes.
void test_queue_voice_completion(const QStringList& ids) {
    check(!ids.empty(), "at least one staged sound exists for queue test");
    Fixture f;
    check(f.ok, "engine initialized with injected fake player");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    const std::string sid = ids[0].toStdString();
    c.sounds[0] = sid; // mouse.left_down

    ptd::EventSoundConfig ec;
    ec.enabled = true;
    ec.sound_id = sid;
    ec.policy = ptd::PlaybackPolicy::Queue;
    c.set_event_config("mouse.left_down", ec);
    f.engine.apply_config(c);

    check(f.engine.audio_audible(), "queue test config is audible");
    check_eq(f.engine.queued_event_count(), 0, "initial queue is empty");

    // 1. Play first event -> active voice starts
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, 1, "first voice plays immediately");
    check_eq(f.engine.queued_event_count(), 0, "first voice is active, not queued");

    // 2. Play second event while first is active -> gets queued
    f.engine.play_event(ptd::AudioEvent::LeftDown);
    check_eq(f.player.total_plays, 1, "second voice is held while first is active");
    check_eq(f.engine.queued_event_count(), 1, "second voice is stored in FIFO queue");

    // 3. Notify completion of first voice -> callback fires flush_queue
    f.player.notify_completion("left_down");
    check_eq(f.player.total_plays, 2, "queued voice drained upon completion signal");
    check_eq(f.engine.queued_event_count(), 0, "queue is drained after completion callback");

    // 4. Notify completion of second voice -> engine state returns to idle
    f.player.notify_completion("left_down");
    check_eq(f.engine.queued_event_count(), 0, "queue remains empty at idle");
}

// ============================================================================
// T-122: playback-lane scheduler regressions (handoff root causes A-D).
//
// Every check below drives the REAL scheduler through the injectable FakePlayer:
// no helper return values are inspected in place of the production execution
// path. The fake models the production preview slot's allocation lifetime
// (idle_allocated_preview_slots) so the defective arithmetic --
// active - preview_allocated -- is reproduced exactly, and fail_next_play gives
// a deterministic backend refusal for the admission checks.
// ============================================================================

namespace {

// A clipboard-category event: SameGroup scope by descriptor, so two of them
// share one group lane.
void queue_event_config(ptd::EventSoundConfig& ec, const QString& id,
                        ptd::PlaybackPolicy policy, ptd::InterruptScope scope,
                        int priority) {
    ec = ptd::EventSoundConfig{};
    ec.enabled = true;
    ec.sound_id = id.toStdString();
    ec.policy = policy;
    ec.interrupt_scope = scope;
    ec.priority = priority;
    ec.min_retrigger_ms = 0;
    ec.cooldown_ms = 0;
}

} // namespace

// S01: an idle ALLOCATED preview slot must not reduce the count of active
// runtime voices. The production pool keeps its one preview slot after
// playback, and the old Global-lane arithmetic (active - allocated) evaluated
// a live runtime lane to zero, so a queued event started immediately instead
// of waiting for its lane.
void test_s01_idle_preview_slot_does_not_hide_runtime_voices(const QStringList& ids) {
    check(!ids.empty(), "sounds staged for the S01 regression");
    Fixture f;
    check(f.ok, "engine initialized for the S01 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.s01a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.s01b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    f.engine.apply_config(c);

    // Park an idle allocated preview slot exactly like production does.
    f.player.idle_allocated_preview_slots = 1;

    f.engine.play_event("custom.s01a");
    check_eq(f.player.total_plays, 1, "S01: the first global-lane event plays");
    check_eq(f.engine.active_playing_voice_count(), 1,
             "S01: the runtime voice is counted; the idle preview slot cannot hide it");

    f.engine.play_event("custom.s01b");
    check_eq(f.engine.queued_event_count(), 1,
             "S01: the global lane stays busy, so the second event waits");
    check_eq(f.player.total_plays, 1, "S01: no premature queue drain past a live lane");

    f.player.notify_completion("custom.s01a");
    check_eq(f.player.total_plays, 2, "S01: the lane drains only when it goes silent");
    check_eq(f.engine.queued_event_count(), 0, "S01: queue is empty after the drain");
    f.engine.shutdown();
}

// S02: a SameGroup queued entry waits for the previous voice OF THE GROUP,
// including when the completing event id differs from the queued event id.
// The old flush_queue looked only at front.event_id, so a group sibling with
// a different id started while the group was still audible.
void test_s02_same_group_queue_waits_for_group_voice(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S02 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    check_eq(f.player.total_plays, 1, "S02: the group event plays");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "S02: the group sibling is queued");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 2, "S02: FIFO order is preserved");

    // Complete the copy voice: the group lane goes silent, so the queue drains
    // the waiting entry -- driven by completion, never by polling.
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S02: the queued entry drains on completion");
    check_eq(f.engine.queued_event_count(), 1, "S02: one entry drained per completion");
    f.engine.shutdown();
}

// S03 + S14: a Global queued entry waits for the runtime lane as a whole, and
// a preview (playing or idle) never blocks or unblocks a runtime lane.
void test_s03_global_queue_waits_for_runtime_lane(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S03 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.s03a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.s03b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["key.escape"], ids[2], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.s03a");
    f.engine.play_event("custom.s03b");
    check_eq(f.engine.queued_event_count(), 1, "S03: the global lane holds the second event");

    // A preview does not block the global runtime lane.
    f.engine.preview_sound(ids[0].toStdString());
    check(f.engine.is_playing_preview(), "S03: a preview voice is live");
    check_eq(f.player.runtime_plays, 1, "S03: preview alone cannot drain the queue");

    // An unrelated runtime voice keeps the lane busy.
    f.engine.play_event("key.escape");
    check_eq(f.player.runtime_plays, 2, "S03: an unrelated event plays on its own lane");
    check_eq(f.engine.queued_event_count(), 1, "S03: the queued global entry still waits");

    f.player.notify_completion("key.escape");
    check_eq(f.engine.queued_event_count(), 1,
             "S03: an unrelated completion does not drain the global queue");
    f.player.notify_completion("custom.s03a");
    check_eq(f.player.runtime_plays, 3, "S03: the queued entry drains when the lane is silent");
    check_eq(f.engine.queued_event_count(), 0, "S03: the queue is empty after the drain");
    f.engine.shutdown();
}

// S04: a completion callback for an UNRELATED event cannot bypass queue
// ownership -- the lane identity stored with the entry is the only thing that
// may release it.
void test_s04_unrelated_completion_cannot_bypass_queue(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S04 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "S04: the group sibling is queued");

    // A completion for an event that never played in this lane.
    f.player.notify_completion("key.escape");
    check_eq(f.engine.queued_event_count(), 1,
             "S04: an idle-id completion drains nothing");
    check_eq(f.player.total_plays, 1, "S04: no voice started from a stale callback");

    // A completion for yet another event id that never played in this engine.
    f.player.notify_completion("custom.never");
    check_eq(f.engine.queued_event_count(), 1,
             "S04: a completion for an unplayed id drains nothing");

    // Even repeated completions of the live owner drain at most one entry.
    f.player.notify_completion("shortcut.copy");
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S04: repeated completions are idempotent (S13)");
    check_eq(f.engine.queued_event_count(), 0, "S04: the queue drained exactly once");
    check_eq(f.player.refused_plays, 0, "S04: the drain started a real voice");
    f.engine.shutdown();
}

// S06: group priority persists while ANY voice of the group remains active and
// is released only when the group lane goes silent -- never because an
// unrelated event's id was used as a group key.
void test_s06_group_priority_persists_while_group_voice_active(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S06 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    ptd::EventSoundConfig copy_cfg{};
    queue_event_config(copy_cfg, ids[0], ptd::PlaybackPolicy::Stack,
                       ptd::InterruptScope::SameGroup, 80);
    copy_cfg.polyphony_allowance = 4;
    c.event_configs["shortcut.copy"] = copy_cfg;
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameGroup, 40);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S06: the higher-priority event stacks two voices");

    f.engine.play_event("shortcut.paste");
    check_eq(f.player.total_plays, 2, "S06: the lower-priority event is dropped while the owner plays");

    // Complete exactly ONE of the two owner voices: the lane is still busy,
    // so the priority owner survives and the lower-priority event is still
    // refused. The old cleanup tested the category id as if it were a player
    // event id and discarded the priority here.
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S06: one owner voice remains, so the lane is still busy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.player.total_plays, 2,
             "S06: group priority survives while another group voice is active");

    // Complete the last owner voice: the lane goes silent, priority releases.
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S06: the drain is completion-driven only");
    f.engine.play_event("shortcut.paste");
    check_eq(f.player.total_plays, 3, "S06: priority released after the group lane went silent");
    f.engine.shutdown();
}

// S08: a failed playback must not record false success state -- no priority,
// no retrigger history, and no silent queue discard.
void test_s08_failed_playback_records_no_false_success(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S08 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    ptd::EventSoundConfig ec{};
    queue_event_config(ec, ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    ec.cooldown_ms = 500; // observable false-history window
    c.event_configs["custom.s08"] = ec;
    f.engine.apply_config(c);

    // The backend refuses the first attempt deterministically.
    f.player.fail_next_play = true;
    f.engine.play_event("custom.s08");
    check_eq(f.player.total_plays, 0, "S08: a refused playback starts no voice");
    check_eq(f.engine.queued_event_count(), 1, "S08: the refused attempt is queued for retry");

    // The old code recorded cooldown/history for a sound that never started,
    // so this immediate retrigger was swallowed by the cooldown gate.
    f.engine.play_event("custom.s08");
    check_eq(f.player.total_plays, 1, "S08: the retry is not blocked by false cooldown history");

    // The old code also recorded priority for a voice that never existed:
    // with a lower-priority sibling queued behind a REFUSED owner, the queue
    // must not be rejected on a phantom owner.
    f.player.fail_next_play = true;
    f.engine.play_event("custom.s08"); // refused: no phantom priority may exist
    check_eq(f.player.total_plays, 1, "S08: the second refusal changes nothing");
    check_eq(static_cast<long long>(f.engine.priority_drop_count()), 0,
             "S08: no priority was recorded for a failed play");
    f.engine.shutdown();
}

// S09: a capacity refusal must not corrupt the queue.
void test_s09_capacity_refusal_keeps_queue_intact(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S09 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "S09: the entry is queued behind the owner");

    // Drain attempt while the backend refuses: the entry stays queued.
    f.player.fail_next_play = true;
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.refused_plays, 1, "S09: the drain attempt was refused");
    check_eq(f.player.total_plays, 1, "S09: the refused drain started nothing");
    check_eq(f.engine.queued_event_count(), 1, "S09: the queue survives the refusal intact");

    // Once the backend admits again, the same entry drains -- no loss, no dup.
    f.engine.flush_queue(0);
    check_eq(f.player.total_plays, 2, "S09: the entry drains once the backend admits");
    check_eq(f.engine.queued_event_count(), 0, "S09: the queue is empty afterwards");

    // The queue remains bounded under repeated capacity refusal.
    for (int i = 0; i < ptd::SoundEngine::kMaxPendingQueueSize + 8; ++i) {
        f.engine.play_event("shortcut.paste");
    }
    check_eq(f.engine.queued_event_count() <= ptd::SoundEngine::kMaxPendingQueueSize, true,
             "S09: the queue stays inside its bounded capacity");
    f.engine.shutdown();
}

// S10: a configuration replacement must never play an obsolete assignment:
// every queued entry carries its configuration generation.
void test_s10_config_replacement_cancels_pending_events(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S10 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.copy");
    check_eq(f.engine.queued_event_count(), 1, "S10: one entry is pending");

    // Replace the sound assignment while the entry is pending.
    ptd::AudioConfig c2 = c;
    queue_event_config(c2.event_configs["shortcut.copy"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c2);

    // Any drain attempt against the new configuration must drop the stale
    // entry instead of playing the replaced WAV.
    f.engine.flush_queue(0);
    check_eq(f.engine.queued_event_count(), 0,
             "S10: an entry resolved under an older configuration is expired");

    // A request after the change uses the NEW assignment.
    f.engine.play_event("shortcut.copy");
    check_eq(f.player.total_plays, 1, "S10: the live owner keeps playing");
    check_eq(f.engine.queued_event_count(), 1, "S10: a new entry queues under the new generation");
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S10: the new-generation entry drains normally");

    // Disable the event under a new configuration: a pending entry from the
    // disabled generation is expired by identity.
    ptd::AudioConfig c3 = c2;
    c3.event_configs["shortcut.copy"].enabled = false;
    f.engine.apply_config(c3);
    f.engine.play_event("shortcut.copy");
    check_eq(f.player.total_plays, 2, "S10: a disabled event never plays");
    f.engine.shutdown();
}

// S07: a global-scope priority must not be tied to preview slot allocation.
// The old cleanup erased the @global priority whenever
// active - preview_allocated evaluated to zero, so an idle allocated preview
// slot released a lane that was still audible.
void test_s07_global_priority_not_tied_to_preview_allocation(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S07 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    ptd::EventSoundConfig owner{};
    queue_event_config(owner, ids[0], ptd::PlaybackPolicy::Stack,
                       ptd::InterruptScope::Global, 80);
    owner.polyphony_allowance = 4;
    c.event_configs["custom.s07a"] = owner;
    queue_event_config(c.event_configs["custom.s07b"], ids[1], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::Global, 40);
    f.engine.apply_config(c);

    f.engine.play_event("custom.s07a");
    f.engine.play_event("custom.s07a");
    check_eq(f.player.runtime_plays, 2, "S07: the priority owner holds two voices");
    // Park an idle allocated preview slot exactly like production does.
    f.player.idle_allocated_preview_slots = 1;

    f.engine.play_event("custom.s07b");
    check_eq(f.player.runtime_plays, 2,
             "S07: the lower-priority event is refused while the lane plays");

    // Complete ONE owner voice. The lane is still audible, so the global
    // priority owner survives; the old cleanup tested
    // active - preview_allocated, which an idle allocated slot zeroes out.
    f.player.notify_completion("custom.s07a");
    f.engine.play_event("custom.s07b");
    check_eq(f.player.runtime_plays, 2,
             "S07: an idle preview slot must not release the priority owner");

    // The last owner voice ends: the lane is silent and the priority releases.
    f.player.notify_completion("custom.s07a");
    f.engine.play_event("custom.s07b");
    check_eq(f.player.runtime_plays, 3,
             "S07: with the lane silent, the priority is released");
    f.engine.shutdown();
}

// S11/S12: Stop All, Cut and Override cannot drain or resurrect a queue
// through callbacks or stale lane state.
void test_s11_s12_stop_all_and_cut_cannot_drain_queues(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the S11/S12 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.cut"], ids[2], ptd::PlaybackPolicy::Cut,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["custom.save"], ids[3], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameGroup, 90);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "S11: an entry is queued behind the owner");

    // S11: Stop All ends every voice AND the queue.
    f.engine.stop_all_sounds();
    check_eq(f.engine.queued_event_count(), 0, "S11: Stop All clears the queue");
    check_eq(f.player.all_stops, 1, "S11: Stop All reached the backend");
    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.runtime_plays, 1, "S11: no queued voice is resurrected by a callback");

    // S12: a Cut in the same group stops the group voice and starts its own;
    // it must not also drain a stale queue.
    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "S12: the queue holds one entry again");
    f.player.runtime_plays = 0;
    f.engine.play_event("shortcut.cut");
    check_eq(f.player.runtime_plays, 1, "S12: Cut starts exactly one voice");
    check_eq(f.engine.queued_event_count(), 1, "S12: Cut does not drain the queue");

    // S12: an Override of a DIFFERENT group under a higher priority must not
    // leave a stale lane state that starts the queued entry twice.
    f.engine.play_event("custom.save");
    check_eq(f.engine.queued_event_count(), 1, "S12: Override does not drain the queue");
    f.player.notify_completion("custom.save");
    check_eq(f.engine.queued_event_count(), 1,
             "S12: an unrelated group completion does not release the queue owner");
    f.player.notify_completion("shortcut.cut");
    check_eq(f.engine.queued_event_count(), 0,
             "S12: the entry drains only when its own group lane goes silent");
    f.engine.shutdown();
}

// ============================================================================
// T-123: Sound System 2.0 corrective regressions.
//
// Root cause A: one global pending_queue_ gated EVERY immediate start
// (`!busy && pending_queue_.empty()`), and flush_queue() inspected only the
// front entry, so a queued event in one busy lane held an independent idle
// lane hostage -- it could not start, and it could silently expire while its
// own lane was free the whole time.
//
// Root cause B: db_to_linear_gain() clamped its result to 1.0, and the backend
// clamped its mix to 1.0, so the advertised +1..+12 dB range was a no-op: the
// UI offered an amplification the platform volume domain cannot deliver.
//
// Candidate C: Qt emits playingChanged synchronously from inside
// QSoundEffect::stop(), so a stop delivered its completion callbacks BEFORE
// the interrupting operation finished. Those callbacks drained the queue
// mid-interrupt (starting voices the interrupt had just canceled) and could
// re-enter flush_queue while it held deque state. The fake's
// sync_completion_on_stop reproduces that exact timing; the defaults keep
// every other suite on the old, callback-free stop timing.
// ============================================================================

// Q01: independent SameEvent lanes are independent. A queue entry parked on
// one lane can never keep another, idle lane from playing.
void test_q01_independent_lanes_play_concurrently(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q01 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q01a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q01b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q01a");
    check_eq(f.player.total_plays, 1, "Q01: lane A plays");
    f.engine.play_event("custom.q01a");
    check_eq(f.engine.queued_event_count(), 1, "Q01: a busy lane queues its own repeat");

    f.engine.play_event("custom.q01b");
    check_eq(f.player.total_plays, 2,
             "Q01: the independent idle lane B starts while A's lane is queued");
    check_eq(f.player.active_event_voice_count("custom.q01b"), 1,
             "Q01: lane B holds its own live voice");
    check_eq(f.engine.active_playing_voice_count(), 2, "Q01: both lanes play concurrently");
    check_eq(f.engine.queued_event_count(), 1,
             "Q01: only lane A's entry is pending; B was never parked");
    f.engine.shutdown();
}

// Q02: a busy SameGroup lane still preserves FIFO for its own entries.
void test_q02_busy_group_lane_preserves_fifo(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q02 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.cut"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    f.engine.play_event("shortcut.cut");
    check_eq(f.engine.queued_event_count(), 2, "Q02: two entries wait behind the group");

    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 2, "Q02: the FIFO-first entry drains first");
    check_eq(f.engine.queued_event_count(), 1, "Q02: one completion advances exactly one entry");

    f.player.notify_completion("shortcut.paste");
    check_eq(f.player.total_plays, 3, "Q02: the second entry drains in order");
    check_eq(f.engine.queued_event_count(), 0, "Q02: the group lane drained FIFO");
    f.engine.shutdown();
}

// Q03: an explicitly requested Global queue barrier must not be bypassed. A
// pending Global entry gates every other lane until the runtime lane as a
// whole falls silent -- lane independence may never undermine it.
void test_q03_global_queue_barrier_is_honored(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q03 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q03a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q03b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q03c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q03a");
    f.engine.play_event("custom.q03b");
    check_eq(f.engine.queued_event_count(), 1, "Q03: the Global entry waits for silence");

    f.engine.play_event("custom.q03c");
    check_eq(f.engine.queued_event_count(), 2,
             "Q03: an independent lane may not jump the pending Global barrier");

    f.player.notify_completion("custom.q03a");
    check_eq(f.player.total_plays, 3,
             "Q03: the barrier entry drains, and the barred lane proceeds once it lifts");
    check_eq(f.engine.queued_event_count(), 0, "Q03: the barrier drained completely");
    f.engine.shutdown();
}

// Q04: an independent idle lane must never starve. The defective scheduler
// parked lane B behind lane A's pending entry and then expired B while B's own
// lane was idle the whole time -- the sound was silently lost.
void test_q04_independent_idle_lane_is_not_starved(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q04 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q04a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q04b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    const int64_t t0 = 1'000'000'000LL;
    f.engine.play_event_at("custom.q04a", t0);
    f.engine.play_event_at("custom.q04a", t0);
    check_eq(f.engine.queued_event_count(), 1, "Q04: lane A parks its repeat");

    f.engine.play_event_at("custom.q04b", t0);
    check_eq(f.player.active_event_voice_count("custom.q04b"), 1,
             "Q04: lane B plays on its own idle lane instead of starving");

    // Age lane A's queue well past the documented 2 s policy: lane B must be
    // unaffected, because it never entered the queue at all.
    const int64_t late = t0 + ptd::SoundEngine::kMaxQueueAgeNs * 2;
    f.engine.play_event_at("custom.q04a", late);
    check_eq(f.player.active_event_voice_count("custom.q04b"), 1,
             "Q04: lane B survives another lane's queue expiry");
    check_eq(f.engine.queued_event_count(), 1, "Q04: lane A re-queued under the age policy");

    f.player.notify_completion("custom.q04a");
    check_eq(f.player.total_plays, 2, "Q04: lane A's entry drains on completion");
    check_eq(f.engine.queued_event_count(), 0, "Q04: no entry of lane B was ever lost");
    f.engine.shutdown();
}

// Q05: queue expiration follows the documented age policy for entries that
// really do sit in the queue.
void test_q05_queue_expiration_follows_age_policy(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q05 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q05a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    const int64_t t0 = 1'000'000'000LL;
    f.engine.play_event_at("custom.q05a", t0);
    f.engine.play_event_at("custom.q05a", t0);
    check_eq(f.engine.queued_event_count(), 1, "Q05: the repeat is queued");

    f.engine.play_event_at("custom.q05a", t0 + ptd::SoundEngine::kMaxQueueAgeNs + 1);
    check_eq(f.player.total_plays, 1, "Q05: the expired entry is dropped, never played");
    check_eq(f.engine.queued_event_count(), 1, "Q05: the current request re-queues");
    f.engine.shutdown();
}

// Q06: the queue stays inside its bounded total capacity.
void test_q06_queue_capacity_remains_bounded(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q06 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q06a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q06a");
    for (int i = 0; i < ptd::SoundEngine::kMaxPendingQueueSize + 8; ++i) {
        f.engine.play_event("custom.q06a");
    }
    check_eq(f.engine.queued_event_count(), ptd::SoundEngine::kMaxPendingQueueSize,
             "Q06: the queue never exceeds its bounded capacity");
    f.engine.shutdown();
}

// Q07: a natural completion advances the entry of the lane that actually fell
// silent -- never an unrelated lane's entry.
void test_q07_natural_completion_advances_the_correct_lane(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q07 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q07a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q07b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q07a");
    f.engine.play_event("custom.q07a");
    f.engine.play_event("custom.q07b");
    f.engine.play_event("custom.q07b");
    check_eq(f.engine.queued_event_count(), 2, "Q07: one pending entry per lane");

    f.player.notify_completion("custom.q07a");
    check_eq(f.player.active_event_voice_count("custom.q07a"), 1,
             "Q07: lane A's own entry drained");
    check_eq(f.engine.queued_event_count(), 1, "Q07: lane B's entry waits for lane B");

    f.player.notify_completion("custom.q07b");
    check_eq(f.player.active_event_voice_count("custom.q07b"), 1,
             "Q07: lane B's entry drained on lane B's completion");
    check_eq(f.engine.queued_event_count(), 0, "Q07: both lanes drained independently");
    f.engine.shutdown();
}

// Q08: a preview never occupies, blocks or unblocks a runtime lane.
void test_q08_preview_never_occupies_runtime_lanes(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q08 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q08a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q08a");
    f.engine.play_event("custom.q08a");
    check_eq(f.engine.queued_event_count(), 1, "Q08: one entry is pending");

    f.engine.preview_sound(ids[1].toStdString());
    check(f.engine.is_playing_preview(), "Q08: the preview voice is live");
    check_eq(f.engine.active_preview_voice_count(), 1, "Q08: exactly one preview voice");
    check_eq(f.player.runtime_plays, 1, "Q08: the preview never drains the runtime queue");
    check_eq(f.engine.queued_event_count(), 1, "Q08: the preview does not unblock the queue");

    f.engine.stop_preview();
    f.player.notify_completion("custom.q08a");
    check_eq(f.player.runtime_plays, 2, "Q08: the runtime entry drains on its own completion");
    check_eq(f.engine.queued_event_count(), 0, "Q08: the queue is empty afterwards");
    f.engine.shutdown();
}

// ---- T-124: the Global barrier and the recoverable capacity refusal --------
//
// Two scheduler contracts the T-123 suite did not pin down:
//
//  1. A pending Global entry is a BARRIER. Entries that precede it keep their
//     FIFO and lane-order rights; an entry that follows it may never bypass
//     it; the barrier itself becomes eligible only when the runtime lane has
//     fallen silent.
//  2. A recoverable capacity refusal must not lose the request. The eligible
//     Queue request stays pending (bounded, expiring) and is retried by the
//     production drain -- no manual flush, no re-trigger, no duplicate voice.

// Q09: the exact barrier sequence A1 / A2 / G / C. A1 plays, A2 waits in A1's
// own lane, G waits as the Global barrier, C waits behind the barrier. When A1
// completes, only the entry that PRECEDES the barrier may start: C must not
// bypass the pending barrier, and the barrier stays pending until the runtime
// lane falls silent again.
void test_q09_global_barrier_is_never_bypassed(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q09 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q09a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q09b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q09c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q09a");
    check_eq(f.player.total_plays, 1, "Q09: A1 started");
    f.engine.play_event("custom.q09a");
    check_eq(f.engine.queued_event_count(), 1, "Q09: A2 waits in its own lane");
    f.engine.play_event("custom.q09b");
    check_eq(f.engine.queued_event_count(), 2, "Q09: G waits as the Global barrier");
    f.engine.play_event("custom.q09c");
    check_eq(f.engine.queued_event_count(), 3, "Q09: C waits behind the pending barrier");

    f.player.notify_completion("custom.q09a");
    check_eq(f.player.total_plays, 2, "Q09: only the entry before the barrier started");
    check_eq(f.player.active_event_voice_count("custom.q09a"), 1, "Q09: A2 owns the lane");
    check_eq(f.player.active_event_voice_count("custom.q09b"), 0,
             "Q09: the barrier is not audible while the runtime is busy");
    check_eq(f.player.active_event_voice_count("custom.q09c"), 0,
             "Q09: the follower did not bypass the pending barrier");
    check_eq(f.engine.queued_event_count(), 2,
             "Q09: the barrier and its follower stay pending");

    f.player.notify_completion("custom.q09a");
    check_eq(f.player.total_plays, 4, "Q09: the barrier and then its follower started");
    check_eq(f.player.active_event_voice_count("custom.q09b"), 1, "Q09: the barrier is audible");
    check_eq(f.player.active_event_voice_count("custom.q09c"), 1, "Q09: the follower started too");
    check(f.player.last_event == "custom.q09c",
          "Q09: the follower started after the barrier, not before it");
    check_eq(f.engine.queued_event_count(), 0, "Q09: the queue is empty");
    f.engine.shutdown();
}

// Q10: without a pending Global barrier an independent lane keeps its
// progress: another lane's parked request may never hold it back.
void test_q10_independent_lane_progress_is_preserved_without_a_barrier(
    const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q10 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q10a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q10d"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q10a");
    f.engine.play_event("custom.q10a");
    check_eq(f.engine.queued_event_count(), 1, "Q10: the busy lane parks its own repeat");

    f.engine.play_event("custom.q10d");
    check_eq(f.player.active_event_voice_count("custom.q10d"), 1,
             "Q10: the independent lane started while another lane waits");
    check_eq(f.engine.queued_event_count(), 1, "Q10: only the busy lane waits");
    f.engine.shutdown();
}

// Q11: a SECOND Global barrier is a barrier too: a follower may not bypass it
// while it is pending.
void test_q11_a_second_global_barrier_is_never_bypassed(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q11 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q11a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q11b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q11c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q11d"], ids[3], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q11a");
    f.engine.play_event("custom.q11b");
    f.engine.play_event("custom.q11c");
    f.engine.play_event("custom.q11d");
    check_eq(f.engine.queued_event_count(), 3, "Q11: both barriers and the follower wait");

    f.player.notify_completion("custom.q11a");
    check_eq(f.player.total_plays, 2, "Q11: only the first barrier started");
    check_eq(f.player.active_event_voice_count("custom.q11b"), 1,
             "Q11: the first barrier is audible");
    check_eq(f.player.active_event_voice_count("custom.q11c"), 0,
             "Q11: the second barrier waits for the runtime to fall silent");
    check_eq(f.player.active_event_voice_count("custom.q11d"), 0,
             "Q11: the follower did not bypass the second barrier");
    check_eq(f.engine.queued_event_count(), 2,
             "Q11: the second barrier and the follower wait");

    f.player.notify_completion("custom.q11b");
    check_eq(f.player.total_plays, 4, "Q11: the second barrier and then the follower started");
    check(f.player.last_event == "custom.q11d",
          "Q11: the follower started after the second barrier");
    check_eq(f.engine.queued_event_count(), 0, "Q11: the queue is empty");
    f.engine.shutdown();
}

// Q12: a barrier whose drain attempt is refused by a bounded capacity refusal
// stays pending and is started exactly once by the next production drain.
void test_q12_a_refused_barrier_is_retried_exactly_once(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q12 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q12a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q12b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q12u"], ids[2], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q12a");
    f.engine.play_event("custom.q12b");
    check_eq(f.engine.queued_event_count(), 1, "Q12: the barrier waits for silence");

    f.player.fail_next_play = true;
    f.player.notify_completion("custom.q12a");
    check_eq(f.player.refused_plays, 1, "Q12: the drain attempt was refused");
    check_eq(f.player.total_plays, 1, "Q12: the refused drain started nothing");
    check_eq(f.engine.queued_event_count(), 1, "Q12: the refused barrier is still pending");

    // The backend admits again: the next production drain starts the barrier.
    f.engine.play_event("custom.q12u");
    check_eq(f.player.total_plays, 3, "Q12: the barrier and the new event started");
    check_eq(f.player.active_event_voice_count("custom.q12b"), 1, "Q12: the barrier is audible");
    check_eq(f.engine.queued_event_count(), 0, "Q12: the queue is empty");

    f.player.notify_completion("custom.q12b");
    check_eq(f.player.total_plays, 3, "Q12: the barrier started exactly once");
    f.engine.shutdown();
}

// Q13: a barrier that outlives the documented age policy expires, and its
// still-young follower is released instead of waiting forever.
void test_q13_an_expired_barrier_releases_its_followers(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q13 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q13a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q13b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q13c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q13d"], ids[3], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    const int64_t t0 = 1'000'000'000LL;
    f.engine.play_event_at("custom.q13a", t0);
    f.engine.play_event_at("custom.q13b", t0);
    const int64_t t1 = t0 + ptd::SoundEngine::kMaxQueueAgeNs / 2;
    f.engine.play_event_at("custom.q13c", t1);
    check_eq(f.engine.queued_event_count(), 2, "Q13: the barrier and the follower wait");

    // A drain past the barrier's age: the barrier expires, the follower -- still
    // inside the age policy -- may start.
    const int64_t late = t0 + ptd::SoundEngine::kMaxQueueAgeNs + 1;
    f.engine.play_event_at("custom.q13d", late);
    check_eq(f.player.active_event_voice_count("custom.q13b"), 0,
             "Q13: the expired barrier never played");
    check_eq(f.player.active_event_voice_count("custom.q13c"), 1,
             "Q13: the follower started after the barrier expired");
    check_eq(f.engine.queued_event_count(), 0, "Q13: the queue is empty");
    f.engine.shutdown();
}

// Q14: a configuration replacement invalidates a pending barrier: the entries
// were resolved under the old generation and must never play.
void test_q14_config_replacement_invalidates_a_pending_barrier(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q14 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q14a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q14b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q14c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q14a");
    f.engine.play_event("custom.q14b");
    f.engine.play_event("custom.q14c");
    check_eq(f.engine.queued_event_count(), 2, "Q14: the barrier and the follower wait");

    ptd::AudioConfig c2 = c;
    queue_event_config(c2.event_configs["custom.q14c"], ids[3], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c2);
    check_eq(f.engine.queued_event_count(), 0,
             "Q14: the obsolete entries expired with the configuration");

    f.player.notify_completion("custom.q14a");
    check_eq(f.player.total_plays, 1, "Q14: no obsolete entry played");
    check_eq(f.engine.queued_event_count(), 0, "Q14: the queue stays empty");
    f.engine.shutdown();
}

// Q15: a terminal stop drops a pending barrier and its follower: nothing may
// be resurrected by a later completion.
void test_q15_stop_all_drops_a_pending_barrier(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q15 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q15a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q15b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::Global, 60);
    queue_event_config(c.event_configs["custom.q15c"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q15a");
    f.engine.play_event("custom.q15b");
    f.engine.play_event("custom.q15c");
    check_eq(f.engine.queued_event_count(), 2, "Q15: the barrier and the follower wait");

    f.engine.stop_all_sounds();
    check_eq(f.engine.queued_event_count(), 0, "Q15: Stop All drops the pending entries");
    check_eq(f.player.active_playing_voice_count(), 0, "Q15: Stop All stops every live voice");

    f.player.notify_completion("custom.q15a");
    check_eq(f.player.total_plays, 1, "Q15: nothing started after Stop All");
    check_eq(f.engine.queued_event_count(), 0, "Q15: the queue stays empty");
    f.engine.shutdown();
}

// Q16: a recoverable capacity refusal at the immediate admission path must not
// discard the eligible Queue request: it stays pending (bounded, expiring) so
// a later drain can retry it.
void test_q16_a_refused_immediate_admission_is_retained(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q16 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q16c"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    // The event's own lane is idle, so the request takes the immediate
    // admission path and the backend refuses it.
    f.player.fail_next_play = true;
    f.engine.play_event("custom.q16c");
    check_eq(f.player.refused_plays, 1, "Q16: the backend refused the attempt");
    check_eq(f.player.total_plays, 0, "Q16: no voice started");
    check_eq(f.engine.queued_event_count(), 1,
             "Q16: the refused request is retained for a retry");
    f.engine.shutdown();
}

// Q17: the retained request recovers by itself: the natural completion of an
// unrelated voice drains it -- no manual flush, no re-trigger, no duplicate.
void test_q17_capacity_refusal_recovers_on_a_natural_completion(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q17 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q17c"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q17u"], ids[1], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q17u");
    check_eq(f.player.total_plays, 1, "Q17: the unrelated event started");

    f.player.fail_next_play = true;
    f.engine.play_event("custom.q17c");
    check_eq(f.player.refused_plays, 1, "Q17: the backend refused the attempt");
    check_eq(f.engine.queued_event_count(), 1, "Q17: the refused request waits for a retry");

    // The natural completion of the unrelated voice must recover the request.
    f.player.notify_completion("custom.q17u");
    check_eq(f.player.total_plays, 2,
             "Q17: the retained request started on the natural completion");
    check_eq(f.player.active_event_voice_count("custom.q17c"), 1,
             "Q17: the recovered voice is the refused event");
    check_eq(f.engine.queued_event_count(), 0, "Q17: the queue is empty after the recovery");

    f.player.notify_completion("custom.q17c");
    check_eq(f.player.total_plays, 2, "Q17: the recovery produced exactly one voice");
    f.engine.shutdown();
}

// Q18: repeated capacity refusals stay inside the bounded pending capacity and
// never survive a terminal stop.
void test_q18_repeated_refusals_stay_bounded_and_expire(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q18 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q18c"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    for (int i = 0; i < ptd::SoundEngine::kMaxPendingQueueSize + 8; ++i) {
        f.player.fail_next_play = true;
        f.engine.play_event("custom.q18c");
    }
    check_eq(f.engine.queued_event_count(), ptd::SoundEngine::kMaxPendingQueueSize,
             "Q18: the pending requests stay inside the bounded capacity");
    check(f.player.refused_plays >= 1, "Q18: the backend really refused");
    check_eq(f.player.total_plays, 0, "Q18: no voice started while the backend refused");

    f.engine.stop_all_sounds();
    check_eq(f.engine.queued_event_count(), 0, "Q18: the pending requests are cleared");
    f.engine.shutdown();
}

// Q19: a refused playback records no successful state: no cooldown, no lane
// priority, no false history.
void test_q19_a_refusal_records_no_cooldown_or_priority_state(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q19 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q19c"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    c.event_configs["custom.q19c"].cooldown_ms = 5000;
    f.engine.apply_config(c);

    f.player.fail_next_play = true;
    f.engine.play_event("custom.q19c");
    check_eq(f.player.refused_plays, 1, "Q19: the backend refused the attempt");
    check_eq(f.engine.cooldown_drop_count(), 0, "Q19: the refusal is not a cooldown drop");
    check_eq(f.engine.priority_drop_count(), 0, "Q19: the refusal claimed no lane priority");

    // A refused attempt never became a successful trigger. The second attempt
    // is refused again on purpose: the top-of-call drain retries the retained
    // entry, and that retry must stay a refusal too, so the only thing under
    // test is that refusals leave no cooldown state (T-125: a SUCCESSFUL drain
    // admission does arm the cooldown, so a successful retry would mask this).
    f.player.fail_next_play = true;
    f.engine.play_event("custom.q19c");
    check_eq(f.player.refused_plays, 2, "Q19: the drain retry was refused too");
    check_eq(f.engine.cooldown_drop_count(), 0,
             "Q19: a refused attempt recorded no cooldown state");
    check(f.engine.queued_event_count() >= 1,
          "Q19: the request is pending, not swallowed by a cooldown");
    f.engine.shutdown();
}

// Q20: an invalid source is NOT a recoverable refusal: it is never queued and
// never retried.
void test_q20_an_invalid_source_is_never_queued_or_retried(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q20 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q20a"], "t124-missing-sound.wav",
                       ptd::PlaybackPolicy::Queue, ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q20b"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q20a");
    check_eq(f.player.total_plays, 0, "Q20: an invalid source never plays");
    check_eq(f.engine.queued_event_count(), 0,
             "Q20: an invalid source is never queued for a retry");

    f.player.notify_completion("custom.q20a");
    check_eq(f.engine.queued_event_count(), 0, "Q20: nothing is retried for an invalid source");
    check_eq(f.player.total_plays, 0, "Q20: still nothing played");

    // A valid sibling event is unaffected by the invalid assignment.
    f.engine.play_event("custom.q20b");
    check_eq(f.player.total_plays, 1, "Q20: the valid sibling still plays");
    f.engine.shutdown();
}

// Q21: disabled playback and a muted master never queue a request.
void test_q21_disabled_and_muted_playback_never_queue(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q21 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q21a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q21b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    c.event_configs["custom.q21b"].enabled = false;
    f.engine.apply_config(c);

    f.engine.play_event("custom.q21a");
    check_eq(f.player.total_plays, 1, "Q21: the enabled event plays");
    f.engine.play_event("custom.q21b");
    check_eq(f.player.total_plays, 1, "Q21: a disabled event never plays");
    check_eq(f.engine.queued_event_count(), 0, "Q21: a disabled event never queues");

    // A second request for the busy lane waits; muting the master clears it.
    f.engine.play_event("custom.q21a");
    check_eq(f.engine.queued_event_count(), 1, "Q21: the request waits in its lane");
    ptd::AudioConfig muted = c;
    muted.muted = true;
    f.engine.apply_config(muted);
    check_eq(f.engine.queued_event_count(), 0, "Q21: a muted master clears the pending request");
    check_eq(f.player.total_plays, 1, "Q21: a muted master starts nothing");
    f.engine.shutdown();
}

// Q22: a capacity recovery preserves the lane order and produces no duplicate
// voice.
void test_q22_capacity_recovery_preserves_lane_order(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q22 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q22a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["custom.q22u"], ids[1], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.q22a");
    f.engine.play_event("custom.q22a");
    check_eq(f.engine.queued_event_count(), 1, "Q22: the lane parks its own repeat");

    // The drain attempt is refused: the lane entry must survive the refusal.
    f.player.fail_next_play = true;
    f.player.notify_completion("custom.q22a");
    check_eq(f.player.refused_plays, 1, "Q22: the drain attempt was refused");
    check_eq(f.player.total_plays, 1, "Q22: the refused drain started nothing");
    check_eq(f.engine.queued_event_count(), 1, "Q22: the lane entry survived the refusal");

    // The next production drain starts it exactly once, in lane order.
    f.engine.play_event("custom.q22u");
    check_eq(f.player.total_plays, 3,
             "Q22: the lane entry started after the refusal, then the new event");
    check_eq(f.engine.queued_event_count(), 0, "Q22: the lane queue is empty");

    f.player.notify_completion("custom.q22u");
    check_eq(f.player.total_plays, 3, "Q22: the recovery produced no duplicate voice");
    f.engine.shutdown();
}

// Q23: a voice the PRODUCTION drain starts arms the per-event cooldown exactly
// like an immediate admission does. The drain used to record only lane
// priority, so a Queue-policy event with cooldown_ms > 0 could be retriggered
// inside its own cooldown window right after the queue had started it.
void test_q23_a_queue_started_voice_arms_the_cooldown(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the Q23 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.q23a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 60);
    c.event_configs["custom.q23a"].cooldown_ms = 5000;
    queue_event_config(c.event_configs["custom.q23b"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 60);
    f.engine.apply_config(c);

    // q23b owns the shared group lane, so q23a must wait in the pending queue.
    f.engine.play_event("custom.q23b");
    check_eq(f.player.total_plays, 1, "Q23: the group blocker is the only voice");
    f.engine.play_event("custom.q23a");
    check_eq(f.engine.queued_event_count(), 1, "Q23: q23a waits for the group lane");

    // The blocker's natural completion runs the PRODUCTION drain.
    f.player.notify_completion("custom.q23b");
    check_eq(f.engine.queued_event_count(), 0, "Q23: the drain admitted q23a");
    check_eq(f.player.active_event_voice_count("custom.q23a"), 1,
             "Q23: the drained voice is live");
    check_eq(f.player.total_plays, 2, "Q23: the drain started exactly one voice");

    // A retrigger inside the 5000 ms window must be dropped by the cooldown the
    // drain armed -- neither admitted nor parked in the queue.
    f.engine.play_event("custom.q23a");
    check_eq(f.engine.cooldown_drop_count(), 1,
             "Q23: the queue-started voice armed the per-event cooldown");
    check_eq(f.player.total_plays, 2, "Q23: the retrigger started no voice");
    check_eq(f.engine.queued_event_count(), 0, "Q23: the retrigger was not queued either");
    check_eq(f.player.active_event_voice_count("custom.q23a"), 1,
             "Q23: the drained voice stays live");
    f.engine.shutdown();
}

// ---- G: the truthful gain contract (T-123 root cause B) ---------------------
//
// The QSoundEffect volume domain is 0..1, so a positive dB gain has nothing to
// amplify INTO: the only truthful contract is attenuation relative to the
// master volume, with the platform maximum output level as the ceiling. The
// UI advertises exactly kMinGainDb..kMaxGainDb, so the range is the contract.

// G01: negative dB gain attenuates correctly.
void test_g01_negative_db_gain_is_correct() {
    check_eq(volume_permille(static_cast<float>(ptd::db_to_linear_gain(-6.0f, 1.0))), 501,
             "G01: -6 dB is half amplitude");
    check_eq(volume_permille(static_cast<float>(ptd::db_to_linear_gain(-20.0f, 1.0))), 100,
             "G01: -20 dB is one tenth amplitude");
}

// G02: zero dB gain is unity.
void test_g02_zero_db_gain_is_unity() {
    check_eq(volume_permille(static_cast<float>(ptd::db_to_linear_gain(0.0f, 1.0))), 1000,
             "G02: 0 dB is unity gain");
}

// G03: positive dB gain is documented attenuation-only behavior: it clamps to
// the contract ceiling and the UI never offers it.
void test_g03_positive_db_gain_matches_documented_behavior() {
    check_eq(volume_permille(static_cast<float>(ptd::db_to_linear_gain(+6.0f, 1.0))), 1000,
             "G03: +6 dB clamps to unity, never above the platform maximum");
    check_eq(volume_permille(static_cast<float>(ptd::db_to_linear_gain(+12.0f, 1.0))), 1000,
             "G03: +12 dB clamps to unity, never above the platform maximum");
    check(ptd::EventSoundConfig::kMaxGainDb <= 0.0f,
          "G03: the advertised range is attenuation-only (no positive dB)");
    check(ptd::EventSoundConfig::kMinGainDb < 0.0f, "G03: attenuation is available");
}

// G04: event gain and pool-entry gain combine additively before conversion.
void test_g04_event_and_pool_gain_combine(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the G04 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    auto& ec = c.event_configs["custom.g04a"];
    ec.enabled = true;
    ec.use_pool = true;
    ec.no_immediate_repeat = false;
    ec.min_retrigger_ms = 0;
    ec.policy = ptd::PlaybackPolicy::Override;
    ec.interrupt_scope = ptd::InterruptScope::SameEvent;
    ec.gain_db = -3.0f;
    ptd::SoundPoolEntry entry;
    entry.enabled = true;
    entry.sound_id = ids[0].toStdString();
    entry.gain_db = -6.0f;
    ec.pool.push_back(entry);
    f.engine.apply_config(c);

    f.engine.play_event("custom.g04a");
    check_eq(f.player.total_plays, 1, "G04: the pooled event plays");
    check_eq(volume_permille(static_cast<float>(f.player.last_voice_gain)), 355,
             "G04: event gain plus pool-entry gain combine (-9 dB total)");
    f.engine.shutdown();
}

// G05: master mute overrides every gain, and a disabled master mutes all.
void test_g05_master_mute_overrides_all_gains(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the G05 regression");
    f.engine.set_master_enabled(true);

    ptd::AudioConfig c{};
    c.enabled = true;
    auto& ec = c.event_configs["custom.g05a"];
    ec.enabled = true;
    ec.sound_id = ids[0].toStdString();
    ec.gain_db = 0.0f;
    ec.policy = ptd::PlaybackPolicy::Override;
    f.engine.apply_config(c);

    f.engine.play_event("custom.g05a");
    check_eq(f.player.total_plays, 1, "G05: audible at unity gain");

    c.muted = true;
    f.engine.apply_config(c);
    f.engine.play_event("custom.g05a");
    check_eq(f.player.total_plays, 1, "G05: master mute wins over every event gain");
    f.engine.shutdown();
}

// G06: the maximum allowed settings never exceed the platform maximum output
// level (no unsafe clipping), at any master volume.
void test_g06_no_unsafe_clipping_at_maximum_settings() {
    const double loudest_event = ptd::db_to_linear_gain(
        ptd::EventSoundConfig::kMaxGainDb, 1.0);
    const double loudest_entry = ptd::db_to_linear_gain(
        ptd::EventSoundConfig::kMaxGainDb + ptd::EventSoundConfig::kMaxGainDb, 1.0);
    check(!(loudest_event > 1.0), "G06: the maximum gain never exceeds the output ceiling");
    check(!(loudest_entry > 1.0), "G06: even combined gains stay inside the ceiling");
    check(!(ptd::db_to_linear_gain(-3.0f, 1.0) * ptd::db_to_linear_gain(-3.0f, 1.0) > 1.0),
          "G06: chained attenuation never amplifies");
}

// G07: the gain the configuration saves is the gain playback applies.
void test_g07_saved_gain_matches_runtime_gain(const QStringList& ids) {
    ptd::AudioConfig c{};
    c.enabled = true;
    auto& ec = c.event_configs["custom.g07a"];
    ec.enabled = true;
    ec.sound_id = ids[0].toStdString();
    ec.gain_db = -6.0f;
    const ptd::AudioConfig validated = ptd::AudioConfig::validated(c);
    check(validated.event_configs.at("custom.g07a").gain_db == -6.0f,
          "G07: the saved gain survives validation");

    Fixture f;
    check(f.ok, "engine initialized for the G07 regression");
    f.engine.set_master_enabled(true);
    f.engine.apply_config(validated);
    f.engine.play_event("custom.g07a");
    check_eq(f.player.total_plays, 1, "G07: the event plays");
    check_eq(volume_permille(static_cast<float>(f.player.last_voice_gain)),
             volume_permille(static_cast<float>(
                 ptd::db_to_linear_gain(validated.event_configs.at("custom.g07a").gain_db, 1.0))),
             "G07: the runtime voice gain equals the saved gain");
    f.engine.shutdown();
}

// ---- C: stop/completion reentrancy (T-123 candidate C) ----------------------
//
// The fake's sync_completion_on_stop reproduces the production timing in which
// a completion fires INSIDE the stop call, while the interrupting operation is
// still unfinished.

// C01: Cut with a pending SameGroup queue entry. The queued sibling must not
// start inside the interrupt transaction, and it must survive for the next
// completion instead of being silently dropped.
void test_c01_cut_with_pending_same_group_queue(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C01 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.cut"], ids[2], ptd::PlaybackPolicy::Cut,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C01: paste waits behind the group");

    f.engine.play_event("shortcut.cut");
    check_eq(f.player.total_plays, 2, "C01: the cut interrupted and started exactly one voice");
    check_eq(f.engine.active_playing_voice_count(), 1,
             "C01: the interrupt transaction did not start the queued sibling mid-cut");
    check_eq(f.engine.queued_event_count(), 1,
             "C01: the legitimately pending sibling survived the cut");

    f.player.notify_completion("shortcut.cut");
    check_eq(f.player.total_plays, 3, "C01: the sibling drains at the safe boundary");
    check_eq(f.engine.queued_event_count(), 0, "C01: the queue drained completely");
    f.engine.shutdown();
}

// C02: Override with a pending queue entry behind the same group.
void test_c02_override_with_pending_queue(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C02 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.cut"], ids[2], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C02: one entry pending");

    f.engine.play_event("shortcut.cut");
    check_eq(f.player.total_plays, 2, "C02: exactly one voice started for the override");
    check_eq(f.engine.active_playing_voice_count(), 1,
             "C02: no queued entry started inside the override transaction");
    check_eq(f.engine.queued_event_count(), 1, "C02: the pending entry survived the override");

    f.player.notify_completion("shortcut.cut");
    check_eq(f.player.total_plays, 3, "C02: the entry drains after the override completes");
    f.engine.shutdown();
}

// C03: a Global interruption while a queue entry is pending. Stopping every
// runtime voice must not let a pending entry jump into the gap.
void test_c03_global_interruption_while_queue_pending(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C03 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.cut"], ids[2], ptd::PlaybackPolicy::Cut,
                       ptd::InterruptScope::Global, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C03: the sibling is pending");

    f.engine.play_event("shortcut.cut");
    check_eq(f.player.total_plays, 2, "C03: the global cut started its own voice only");
    check_eq(f.engine.active_playing_voice_count(), 1,
             "C03: the pending entry did not start inside the global interruption");
    check_eq(f.engine.queued_event_count(), 1,
             "C03: the pending entry survived the global stop");

    f.player.notify_completion("shortcut.cut");
    check_eq(f.player.total_plays, 3, "C03: the entry drains after the interruption completes");
    f.engine.shutdown();
}

// C04: Stop All with synchronous completions cancels everything and starts
// nothing -- a stop-time callback can never resurrect canceled playback.
void test_c04_stop_all_with_synchronous_completions(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C04 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C04: an entry is pending before Stop All");

    f.engine.stop_all_sounds();
    check_eq(f.player.all_stops, 1, "C04: Stop All reached the player once");
    check_eq(f.engine.active_playing_voice_count(), 0, "C04: every voice is silenced");
    check_eq(f.engine.queued_event_count(), 0, "C04: no stale queue survives Stop All");
    check_eq(f.player.total_plays, 1, "C04: Stop All started nothing, not even from a callback");

    f.engine.play_event("shortcut.copy");
    check_eq(f.player.total_plays, 2, "C04: playback resumes after Stop All");
    f.engine.shutdown();
}

// C05: one stop that completes several voices delivers several callbacks. The
// scheduler must treat them as one transaction, not several independent
// drains, and no entry may start inside it.
void test_c05_multiple_completions_from_a_single_stop(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C05 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.undo"], ids[2], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["key.escape"], ids[3], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["shortcut.cut"], ids[4], ptd::PlaybackPolicy::Cut,
                       ptd::InterruptScope::Global, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.undo");
    f.engine.play_event("key.escape");
    check_eq(f.engine.active_playing_voice_count(), 3, "C05: three voices are live");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C05: one entry is pending");

    f.engine.play_event("shortcut.cut");
    check_eq(f.player.total_plays, 4,
             "C05: the global cut played its own voice; the three stop callbacks started none");
    check_eq(f.engine.active_playing_voice_count(), 1, "C05: only the interrupting voice is live");
    check_eq(f.engine.queued_event_count(), 1, "C05: the pending entry survived all callbacks");

    f.player.notify_completion("shortcut.cut");
    check_eq(f.player.total_plays, 5, "C05: the entry drains after the transaction closes");
    check_eq(f.engine.queued_event_count(), 0, "C05: the queue is empty afterwards");
    f.engine.shutdown();
}

// C06: a completion delivered after a configuration generation change never
// plays an obsolete assignment.
void test_c06_callback_after_generation_change(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C06 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C06: one entry is pending");

    ptd::AudioConfig c2 = c;
    queue_event_config(c2.event_configs["shortcut.paste"], ids[2], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c2);

    f.player.notify_completion("shortcut.copy");
    check_eq(f.player.total_plays, 1, "C06: the stale-generation entry is dropped, not played");
    check_eq(f.engine.queued_event_count(), 0, "C06: no obsolete entry survives");
    check(f.player.last_event != "shortcut.paste",
          "C06: the replaced assignment never played");
    f.engine.shutdown();
}

// C07: shutdown while completions are pending never re-enters a torn-down
// scheduler and never starts a late voice.
void test_c07_shutdown_while_callbacks_pending(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C07 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["shortcut.copy"], ids[0], ptd::PlaybackPolicy::Override,
                       ptd::InterruptScope::SameEvent, 60);
    queue_event_config(c.event_configs["shortcut.paste"], ids[1], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameGroup, 70);
    f.engine.apply_config(c);

    f.engine.play_event("shortcut.copy");
    f.engine.play_event("shortcut.paste");
    check_eq(f.engine.queued_event_count(), 1, "C07: an entry is pending before shutdown");

    f.engine.shutdown();
    check_eq(f.player.total_plays, 1, "C07: shutdown started nothing, not even from a callback");
    check_eq(f.player.playing.size(), 0, "C07: no voice outlives the engine");
    f.engine.set_master_enabled(true);
    f.engine.play_event("shortcut.paste");
    check_eq(f.player.total_plays, 1, "C07: a torn-down engine plays nothing");
    f.engine.shutdown();
}

// C08: a natural completion without interruption still drains normally under
// the production callback timing.
void test_c08_natural_completion_without_interruption(const QStringList& ids) {
    Fixture f;
    check(f.ok, "engine initialized for the C08 regression");
    f.engine.set_master_enabled(true);
    f.player.sync_completion_on_stop = true;

    ptd::AudioConfig c{};
    c.enabled = true;
    queue_event_config(c.event_configs["custom.c08a"], ids[0], ptd::PlaybackPolicy::Queue,
                       ptd::InterruptScope::SameEvent, 60);
    f.engine.apply_config(c);

    f.engine.play_event("custom.c08a");
    f.engine.play_event("custom.c08a");
    check_eq(f.engine.queued_event_count(), 1, "C08: the repeat is queued");

    f.player.notify_completion("custom.c08a");
    check_eq(f.player.total_plays, 2, "C08: the natural completion drains the entry");
    check_eq(f.engine.queued_event_count(), 0, "C08: the queue is empty after the drain");
    f.engine.shutdown();
}

// P2: the FakePlayer behavioral contract. The fake must refuse exactly what
// production QtSoundPlayer::play refuses and its preview state must be derived
// from its own playback records, never a stale flag.
void test_fake_player_contract() {
    FakePlayer p;
    check(!p.play("left_down", "x.wav", false), "play before initialize is refused");
    check(p.playing.empty() && p.total_plays == 0, "a refused play leaves no record");
    p.initialize();

    p.set_muted(true);
    check(!p.play("left_down", "x.wav", false), "a muted fake refuses event playback");
    check(!p.play("@preview", "x.wav", true), "a muted fake refuses preview playback");
    check(p.playing.empty(), "refusals leave no playback records");
    check(!p.is_playing_preview(), "a refused preview leaves no stale preview state");
    p.set_muted(false);

    check(p.play("@preview", "x.wav", true), "an unmuted fake plays a preview");
    check(p.is_playing_preview(), "preview is active while a preview record exists");
    check_eq(p.preview_voice_count(), 1, "one preview record exists");

    // Mute stops preview playback and the derived state follows the records.
    p.set_muted(true);
    check(p.playing.empty(), "mute stops every playback record, preview included");
    check(!p.is_playing_preview(), "no preview is reported after all records are removed");
    p.set_muted(false);
    check(!p.is_playing_preview(), "unmute resurrects nothing");

    // A refused attempt must not damage or replace the live preview state.
    check(p.play("@preview", "x.wav", true), "the preview plays before the refusal");
    p.fail_next_play = true;
    check(!p.play("@preview", "y.wav", true), "the refused attempt reports failure");
    check_eq(p.preview_voice_count(), 1, "the refusal replaced nothing");
    check(p.is_playing_preview(), "the surviving preview is still reported active");
    check(p.playing.back().path == "x.wav", "the refused attempt left the old record intact");

    // stop_preview removes every preview record and the state follows.
    p.stop_preview();
    check(p.playing.empty(), "stop removes the preview record");
    check(!p.is_playing_preview(), "no preview is reported after the record is gone");

    // Empty paths are refused like production (no silent placeholder voice).
    check(!p.play("left_down", "", false), "an empty path is refused");
    check(p.playing.empty(), "the empty-path refusal leaves no record");
}

// Portable relocation: the asset root is executable-relative, never relative
// to the current working directory.
void test_portable_lookup(const QString& click_path) {
    const QString cwd_before = QDir::currentPath();
    QTemporaryDir elsewhere;
    check(QDir::setCurrent(elsewhere.path()), "moved to an unrelated working directory");
    const QString resolved = ptd::SoundCatalog::resolve_sound_path("click.wav");
    QDir::setCurrent(cwd_before);
    check(resolved == QFileInfo(click_path).absoluteFilePath(),
          "asset lookup is independent of the working directory");
}

// T-81: a sound selected through the catalog must never resolve outside the
// approved sound directory, even when a reparse point is planted inside it.
// A file symlink on Windows needs elevation or Developer Mode; when it is not
// available the check reports SKIP instead of pretending it ran.
void test_reparse_point_guard(const QString& stage_path) {
    QTemporaryDir outside;
    const QString target = outside.filePath("escape.wav");
    check(write_file(target, wav_bytes(16, 1, 22050, 64)), "staged the outside target");

    const QString link = QDir(stage_path).filePath("link.wav");
    std::error_code ec;
    std::filesystem::create_symlink(target.toStdWString(), link.toStdWString(), ec);
    if (ec) {
        g_reparse_guard_status =
            "SKIP (create_symlink failed; needs elevation or Developer Mode) -- NOT VERIFIED";
        std::printf("SKIP reparse-point guard: create_symlink failed (%s); needs "
                    "elevation or Developer Mode\n",
                    ec.message().c_str());
        return;
    }
    check(ptd::SoundCatalog::resolve_sound_path("link.wav").isEmpty(),
          "a sound symlinked outside the sound directory cannot resolve");
    check(!ptd::SoundCatalog::sound_exists("link.wav"),
          "an escaping sound is reported missing, not present");

    // A sound ROOT reached through a directory symlink still resolves only the
    // contained files (canonical containment, not lexical).
    const QString link_root = QDir(outside.path()).filePath("linked_sound_root");
    std::error_code ec2;
    std::filesystem::create_symlink(stage_path.toStdWString(),
                                    link_root.toStdWString(), ec2);
    if (ec2) {
        g_reparse_guard_status =
            "PARTIAL (file guard RAN; root guard SKIP: create_symlink failed) -- root check NOT VERIFIED";
        std::printf("SKIP reparse-point root guard: create_symlink failed (%s)\n",
                    ec2.message().c_str());
        return;
    }
    g_reparse_guard_status = "RAN (file + linked-root containment guards executed)";
    ptd::SoundCatalog::set_sound_directory_for_tests(link_root);
    check(ptd::SoundCatalog::resolve_sound_path("click.wav") ==
              QFileInfo(QDir(stage_path).filePath("click.wav")).canonicalFilePath(),
          "a sound root reached through a link resolves contained files only");
    check(ptd::SoundCatalog::resolve_sound_path("../escape.wav").isEmpty(),
          "traversal out of a linked sound root is still rejected");
    ptd::SoundCatalog::set_sound_directory_for_tests(stage_path);
}

// The shipped catalog must be real decodable WAV data. Portable trees carry
// no sound directory, so the check may skip -- but it then reports an explicit
// NOT VERIFIED status, and only a completed run can claim VERIFIED.
void test_shipped_catalog_decodes() {
    // Walk up from the working directory until a sound/Click.wav shows up, so
    // the check works from the build tree and from the staged package.
    QString dir;
    QDir walk = QDir::current();
    for (int depth = 0; depth < 8 && dir.isEmpty(); ++depth) {
        const QString probe = walk.absoluteFilePath(QStringLiteral("sound"));
        if (QFileInfo::exists(probe + "/Click.wav")) {
            dir = probe;
            break;
        }
        if (!walk.cdUp()) break;
    }
    if (dir.isEmpty()) {
        g_shipped_catalog_status =
            "SKIP (no sound directory beside the sources) -- NOT VERIFIED";
        std::printf("SKIP shipped catalog decode: no sound directory beside the sources\n");
        return;
    }
    const QStringList ids = QDir(dir).entryList(QStringList{"*.wav"}, QDir::Files, QDir::Name);
    int decoded = 0;
    int failed = 0;
    for (const QString& id : ids) {
        const std::string absolute = QFileInfo(QDir(dir).filePath(id)).absoluteFilePath().toStdString();
        if (ptd::decode_wav_file(absolute)) {
            ++decoded;
        } else {
            ++failed;
            std::printf("FAIL shipped sound not decodable: %s\n", absolute.c_str());
        }
    }
    std::printf("shipped catalog: %d decoded, %d failed (%d files)\n", decoded, failed,
                 static_cast<int>(ids.size()));
    check(failed == 0, "every shipped wav decodes");
    check(decoded == static_cast<int>(ids.size()), "decoded count matches file count");
    if (failed == 0 && decoded == static_cast<int>(ids.size())) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "VERIFIED (%d files decoded, 0 failed)", decoded);
        g_shipped_catalog_status = buf;
    } else {
        g_shipped_catalog_status = "FAILED (see FAIL lines above)";
    }
}

// T-106: the runtime catalog is EXECUTABLE-relative, so a source tree that
// carries sound/ must have it staged beside the built binary -- otherwise the
// picker is empty in the very build the user runs, which is the reported bug.
// This is the gate that would have caught it: it looks at the SAME path the
// runtime uses (SoundCatalog::sound_directory(), the exe directory) and fails
// when the sources have a catalog and the executable does not. A portable tree
// without sound/ reports SKIP -- NOT VERIFIED rather than a silent pass.
void test_catalog_is_staged_beside_the_executable() {
    // Locate the source catalog the same way the decode check does.
    QString src_dir;
    QDir walk = QDir::current();
    for (int depth = 0; depth < 8 && src_dir.isEmpty(); ++depth) {
        const QString probe = walk.absoluteFilePath(QStringLiteral("sound"));
        if (QFileInfo::exists(probe + "/Click.wav")) {
            src_dir = probe;
            break;
        }
        if (!walk.cdUp()) break;
    }
    if (src_dir.isEmpty()) {
        std::printf("SKIP executable-relative catalog: no sound directory beside the sources\n");
        return;
    }

    // The runtime path, exactly as SoundEngine resolves it (no test override).
    ptd::SoundCatalog::clear_sound_directory_for_tests();
    const QString exe_dir = ptd::SoundCatalog::sound_directory();
    const QDir staged(exe_dir);
    check(staged.exists(), "the executable-relative sound directory exists");
    if (!staged.exists()) {
        std::printf("FAIL executable-relative catalog missing: %s\n",
                    exe_dir.toUtf8().constData());
        return;
    }

    const int src_wavs = QDir(src_dir).entryList(QStringList{"*.wav"}, QDir::Files).size();
    const int staged_wavs = staged.entryList(QStringList{"*.wav"}, QDir::Files).size();
    check(src_wavs > 0, "the source catalog carries wav files");
    check(staged_wavs == src_wavs,
          "every source wav is staged beside the executable");
    check(!ptd::SoundCatalog::sound_ids().isEmpty(),
          "the runtime catalog is non-empty (the picker is populated)");
    if (staged_wavs != src_wavs) {
        std::printf("FAIL staged catalog count %d != source count %d at %s\n",
                    staged_wavs, src_wavs, exe_dir.toUtf8().constData());
    }
}

} // namespace

int main() {
    // Bounded: 8 events + preview. The cache never grows past that.
    check_eq(ptd::kMaxCacheSounds, 9, "cache ceiling is the eight events plus one preview");
    test_accumulate_wheel();
    test_audio_config_validation();

    QTemporaryDir decoder_scratch;
    if (!decoder_scratch.isValid()) {
        std::printf("FAIL could not create the decoder scratch directory\n");
        return 1;
    }
    test_wav_decoder_boundaries(decoder_scratch.path());

    QTemporaryDir stage;
    if (!stage.isValid()) {
        std::printf("FAIL could not create a temporary sound directory\n");
        return 1;
    }
    // T-81: twelve DISTINCT staged sounds across the formats the shipped
    // inventory mixes (8/16/24/32-bit PCM, mono and stereo, several rates),
    // so preview browsing exercises real replacement, not one cached file.
    struct Spec {
        const char* name;
        int bits;
        int channels;
        int rate;
    };
    const Spec specs[] = {
        {"click.wav", 16, 1, 22050}, {"boomer.wav", 8, 1, 11025},
        {"s00.wav", 8, 2, 8000},     {"s01.wav", 16, 2, 44100},
        {"s02.wav", 32, 1, 48000},   {"s03.wav", 24, 1, 44100},
        {"s04.wav", 16, 1, 8000},    {"s05.wav", 16, 1, 16000},
        {"s06.wav", 32, 2, 22050},   {"s07.wav", 8, 2, 16000},
        {"s08.wav", 24, 2, 48000},   {"s09.wav", 16, 2, 32000},
    };
    QStringList ids;
    for (const Spec& s : specs) {
        const QString path = stage.filePath(QString::fromLatin1(s.name));
        check(write_file(path, wav_bytes(s.bits, s.channels, s.rate, 64)),
              "staged a distinct wav");
        ids << QString::fromLatin1(s.name);
    }
    const QString click_path = stage.filePath("click.wav");
    const QString note_path = stage.filePath("notes.txt");
    check(write_file(note_path, "not audio"), "staged non-audio sibling");

    ptd::SoundCatalog::set_sound_directory_for_tests(stage.path());
    test_catalog(stage, "boomer.wav", "click.wav");
    test_preview_lifecycle(ids, stage.path());
    test_stop_all_sounds(ids);
    test_eight_event_independence(ids);
    test_queue_voice_completion(ids);
    // T-122: playback-lane scheduler regressions (root causes A-D).
    test_s01_idle_preview_slot_does_not_hide_runtime_voices(ids);
    test_s02_same_group_queue_waits_for_group_voice(ids);
    test_s03_global_queue_waits_for_runtime_lane(ids);
    test_s04_unrelated_completion_cannot_bypass_queue(ids);
    test_s06_group_priority_persists_while_group_voice_active(ids);
    test_s08_failed_playback_records_no_false_success(ids);
    test_s09_capacity_refusal_keeps_queue_intact(ids);
    test_s10_config_replacement_cancels_pending_events(ids);
    // T-123: Sound System 2.0 corrective regressions (Q/G/C).
    test_q01_independent_lanes_play_concurrently(ids);
    test_q02_busy_group_lane_preserves_fifo(ids);
    test_q03_global_queue_barrier_is_honored(ids);
    test_q04_independent_idle_lane_is_not_starved(ids);
    test_q05_queue_expiration_follows_age_policy(ids);
    test_q06_queue_capacity_remains_bounded(ids);
    test_q07_natural_completion_advances_the_correct_lane(ids);
    test_q08_preview_never_occupies_runtime_lanes(ids);
    // T-124: the Global barrier and the recoverable capacity refusal (Q09-Q22).
    test_q09_global_barrier_is_never_bypassed(ids);
    test_q10_independent_lane_progress_is_preserved_without_a_barrier(ids);
    test_q11_a_second_global_barrier_is_never_bypassed(ids);
    test_q12_a_refused_barrier_is_retried_exactly_once(ids);
    test_q13_an_expired_barrier_releases_its_followers(ids);
    test_q14_config_replacement_invalidates_a_pending_barrier(ids);
    test_q15_stop_all_drops_a_pending_barrier(ids);
    test_q16_a_refused_immediate_admission_is_retained(ids);
    test_q17_capacity_refusal_recovers_on_a_natural_completion(ids);
    test_q18_repeated_refusals_stay_bounded_and_expire(ids);
    test_q19_a_refusal_records_no_cooldown_or_priority_state(ids);
    test_q20_an_invalid_source_is_never_queued_or_retried(ids);
    test_q21_disabled_and_muted_playback_never_queue(ids);
    test_q22_capacity_recovery_preserves_lane_order(ids);
    // T-125: a queue-started voice arms the per-event cooldown.
    test_q23_a_queue_started_voice_arms_the_cooldown(ids);
    test_g01_negative_db_gain_is_correct();
    test_g02_zero_db_gain_is_unity();
    test_g03_positive_db_gain_matches_documented_behavior();
    test_g04_event_and_pool_gain_combine(ids);
    test_g05_master_mute_overrides_all_gains(ids);
    test_g06_no_unsafe_clipping_at_maximum_settings();
    test_g07_saved_gain_matches_runtime_gain(ids);
    test_c01_cut_with_pending_same_group_queue(ids);
    test_c02_override_with_pending_queue(ids);
    test_c03_global_interruption_while_queue_pending(ids);
    test_c04_stop_all_with_synchronous_completions(ids);
    test_c05_multiple_completions_from_a_single_stop(ids);
    test_c06_callback_after_generation_change(ids);
    test_c07_shutdown_while_callbacks_pending(ids);
    test_c08_natural_completion_without_interruption(ids);
    test_s07_global_priority_not_tied_to_preview_allocation(ids);
    test_s11_s12_stop_all_and_cut_cannot_drain_queues(ids);
    test_engine();
    test_engine_injected_ownership();
    test_owned_player_lifecycle();
    test_initialization_failure_and_retry();
    test_fake_player_contract();
    test_portable_lookup(click_path);
    test_reparse_point_guard(stage.path());
    test_shipped_catalog_decodes();
    test_catalog_is_staged_beside_the_executable();
    ptd::SoundCatalog::clear_sound_directory_for_tests();

    // Explicit evidence block: each optional check's status is printed as
    // RAN/VERIFIED or SKIP -- never silent, never conflatable with a pass.
    // The staged count comes from the real executable-relative catalog that
    // test_catalog_is_staged_beside_the_executable() just proved equal to the
    // source catalog, not from the scratch fixture staged above.
    const QStringList staged_ids = ptd::SoundCatalog::sound_ids();
    std::printf("EVIDENCE staged_wav_count: %d\n", static_cast<int>(staged_ids.size()));
    std::printf("EVIDENCE distinct_preview_source_count: %d\n", static_cast<int>(ids.size()));
    std::printf("EVIDENCE reparse_symlink_guard: %s\n", g_reparse_guard_status.c_str());
    std::printf("EVIDENCE shipped_catalog_decode: %s\n", g_shipped_catalog_status.c_str());

    std::printf("test_sound_engine: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
