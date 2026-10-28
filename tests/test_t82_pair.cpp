// T-82 regression-pair oracle.
//
// This file is the FIXED verifier half of a red/green pair: it must compile
// against BOTH the pre-fix subject (stored under .saipen/evidence/pre/) and
// the repaired subject, and it must FAIL against the pre-fix subject and PASS
// against the repaired one. Everything asserted here is a defect contract:
//
//   1. float64 WAV payloads must not be advertised as 32-bit Float while
//      carrying 64-bit samples (label/buffer agreement, exact conversion);
//   2. IEEE float32 payloads must be advertised as Float, never Int32;
//   3. a data chunk claiming more bytes than the file holds is truncated and
//      must be rejected, not decoded as a prefix;
//   4. 24-bit PCM widening must keep exact amplitudes (full Int32 scale);
//   5. FakePlayer must refuse exactly what production refuses (uninitialized,
//      muted) and a refused playback must leave no stale preview state;
//   6. mute must end preview playback and the reported preview state must
//      always agree with the playback records;
//   7. SoundEngine ownership lifecycle (seam-gated: the ownership seam does
//      not exist in the pre-fix subject, so these run only on the repair).
//
// Compile compatibility rule: use only APIs present in BOTH subject
// generations. New seams (player_for_tests / owns_player_for_tests /
// fail_initialize) are feature-detected, never assumed.

#include "../src/audio/sound_backend.h"
#include "../src/audio/sound_decoder.h"
#include "../src/audio/sound_engine.h"
#include "../src/config/audio_config.h"

#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QIODevice>

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

namespace {

int g_failures = 0;
int g_checks = 0;

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

std::string sample24_raw(unsigned v) {
    std::string s;
    put16s(s, v & 0xFFFFu);
    s.push_back(static_cast<char>((v >> 16) & 0xFFu));
    return s;
}

std::string sample32_raw(unsigned v) { std::string s; put32s(s, v); return s; }

std::string samplef32(float v) {
    std::string s(4, '\0');
    std::memcpy(s.data(), &v, 4);
    return s;
}

std::string samplef64(double v) {
    std::string s(8, '\0');
    std::memcpy(s.data(), &v, 8);
    return s;
}

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

bool write_file(const QString& path, const std::string& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const qint64 n = f.write(bytes.data(), static_cast<qint64>(bytes.size()));
    f.close();
    return n == static_cast<qint64>(bytes.size());
}

bool decode_image(const QString& dir, const char* name, const std::string& bytes,
                  ptd::DecodedSound& out) {
    const QString path = QDir(dir).filePath(QString::fromLatin1(name));
    if (!write_file(path, bytes)) return false;
    const auto decoded = ptd::decode_wav_file(path.toStdString());
    if (!decoded) return false;
    out = *decoded;
    return true;
}

// Feature detection for seams that exist only in the repaired subject.
template <typename T, typename = void>
struct has_player_seam : std::false_type {};
template <typename T>
struct has_player_seam<T, std::void_t<decltype(std::declval<const T&>().player_for_tests())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_fail_initialize : std::false_type {};
template <typename T>
struct has_fail_initialize<T, std::void_t<decltype(std::declval<T&>().fail_initialize)>>
    : std::true_type {};

// T-103: the live-instance counter exists only in the repaired subject. The
// lookup must live behind this trait, not behind a direct `T::member` use in
// the test body: a non-dependent access is diagnosed while the template is
// still parsed, so the pre-fix subject would be asked for a member it does not
// have. Only the trait's specialisation, which is SFINAE-matched at
// instantiation time, touches the member.
template <typename T, typename = void>
struct live_instance_count_of {
    static constexpr bool present = false;
    static int get() { return -1; }
};
template <typename T>
struct live_instance_count_of<T, std::void_t<decltype(T::live_instance_count())>> {
    static constexpr bool present = true;
    static int get() { return T::live_instance_count(); }
};

void test_decoder_contract(const QString& scratch) {
    ptd::DecodedSound s;

    // 1. float64: the buffer must BE float32 while the label says Float.
    {
        const std::string data = samplef64(1.0) + samplef64(-0.5) + samplef64(0.0);
        const std::string want = samplef32(1.0f) + samplef32(-0.5f) + samplef32(0.0f);
        // The pair pins the IMPLEMENTED behavior: explicit conversion to
        // float32. An explicit rejection is a legal repair alternative, but
        // then this oracle intentionally goes red and must be revised with its
        // own new red control (never silently retargeted).
        check(decode_image(scratch, "p_f64.wav", wav_exact(3, 64, 1, 48000, data), s),
              "float64 converts (decodes successfully)");
        check(s.format.sampleFormat() == QAudioFormat::Float,
              "float64 output advertises Float");
        check_eq(s.pcm.size(), 12, "float64 output holds 3 x 4-byte float32 samples");
        check_pcm(s, want, "float64 converts to exact float32 values");
    }

    // 2. float32 label agreement.
    {
        const std::string data = samplef32(1.0f) + samplef32(-0.5f);
        check(decode_image(scratch, "p_f32.wav", wav_exact(3, 32, 1, 44100, data), s),
              "float32 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Float,
              "float32 advertises Float, never Int32");
        check_pcm(s, data, "float32 samples preserved");
    }

    // 3. truncation is rejection.
    {
        const std::string body = sample32_raw(0x00020001u);  // two int16 samples
        check(!decode_image(scratch, "p_trunc.wav",
                            wav_exact(1, 16, 1, 22050, body, 16, 400), s),
              "a data chunk claiming more bytes than the file holds is rejected");
    }

    // 4. 24-bit exact amplitudes (full Int32 scale).
    {
        const std::string data = sample24_raw(0x800000u) + sample24_raw(0xFFFFFFu)
                               + sample24_raw(0x000000u) + sample24_raw(0x000001u)
                               + sample24_raw(0x7FFFFFu);
        const std::string want = sample32_raw(0x80000000u) + sample32_raw(0xFFFFFF00u)
                               + sample32_raw(0x00000000u) + sample32_raw(0x00000100u)
                               + sample32_raw(0x7FFFFF00u);
        check(decode_image(scratch, "p_pcm24.wav", wav_exact(1, 24, 1, 44100, data), s),
              "PCM24 decodes");
        check(s.format.sampleFormat() == QAudioFormat::Int32, "PCM24 widens to Int32");
        check_eq(s.pcm.size(), 20, "PCM24 widens 5 x 3 to 5 x 4 bytes");
        check_pcm(s, want, "PCM24 amplitudes exact across the widening");
    }
}

void test_fake_player_contract() {
    ptd::FakePlayer p;

    // 5. refusals match production and leave no state behind.
    check(!p.play("left_down", "x.wav", false), "play before initialize is refused");
    check(p.playing.empty() && p.total_plays == 0, "the refusal left no record");
    p.initialize();
    p.set_muted(true);
    check(!p.play("left_down", "x.wav", false), "a muted fake refuses event playback");
    check(!p.play("@preview", "x.wav", true), "a muted fake refuses preview playback");
    check(p.playing.empty(), "muted refusals leave no records");
    check(!p.is_playing_preview(), "muted refusals leave no stale preview state");
    p.set_muted(false);

    // A refused attempt (backend refusal) must not damage the live preview.
    check(p.play("@preview", "x.wav", true), "the preview plays before the refusal");
    p.fail_next_play = true;
    check(!p.play("@preview", "y.wav", true), "the refused attempt reports failure");
    check_eq(p.preview_voice_count(), 1, "the refusal replaced nothing");
    check(!p.playing.empty(), "the surviving preview record is intact");
    if (!p.playing.empty()) {
        check(p.playing.back().path == "x.wav", "the surviving record is the old preview");
    }
    check(p.is_playing_preview() == (p.preview_voice_count() > 0),
          "reported preview state agrees with the playback records");

    // 6. mute ends preview playback and the state follows.
    p.set_muted(true);
    check(p.playing.empty(), "mute stops every playback record");
    check(!p.is_playing_preview(), "no preview is reported after the records are gone");
    p.set_muted(false);
    check(!p.is_playing_preview(), "unmute resurrects nothing");
    check(p.is_playing_preview() == (p.preview_voice_count() > 0),
          "reported preview state still agrees with the records");
}

// Template on purpose: `if constexpr` discards branches without instantiation
// ONLY in templates, so the seam-dependent checks must not be compiled at all
// when the pre-fix subject (no seams) is swapped in for the red run. All seam
// accesses go through dependent types T / P for the same reason.
template <typename T = ptd::SoundEngine, typename P = ptd::FakePlayer>
void test_ownership_contract() {
    if constexpr (!has_player_seam<T>::value) {
        std::printf("SKIP ownership seam checks: the pre-fix subject exposes no "
                    "ownership seam (the red half proves the decoder/fake defects)\n");
    } else {
        // Injected non-owning player survives repeated cycles untouched.
        P player;
        T engine;
        engine.set_player(&player);
        check(engine.player_for_tests() == &player, "the injected player is live");
        for (int cycle = 0; cycle < 3; ++cycle) {
            check(engine.initialize(), "initialize succeeds on every cycle");
            engine.preview_sound("click.wav");
            check(engine.is_playing_preview(), "the preview plays on every cycle");
            engine.shutdown();
            check(!engine.is_playing_preview(), "no residual preview after shutdown");
            check(engine.player_for_tests() == &player,
                  "the injected player survives shutdown untouched");
            check(!engine.owns_player_for_tests(), "an injected player is never owned");
        }
        engine.shutdown();
        engine.shutdown();  // idempotent
        check(engine.player_for_tests() == &player, "repeated shutdown keeps the injection");

        // The owned production player: fresh per cycle, pointer cleared before
        // destruction, never reachable after it.
        T owned;
        check(owned.player_for_tests() == nullptr, "no player before initialize");
        check(owned.initialize(), "the owned production player initializes");
        ptd::ISoundPlayer* first = owned.player_for_tests();
        check(first != nullptr && owned.owns_player_for_tests(), "the player is owned");
        owned.shutdown();
        check(owned.player_for_tests() == nullptr,
              "shutdown clears the cached pointer to the destroyed player");
        check(!owned.is_playing_preview(), "no residual preview after owned shutdown");
        check(owned.initialize(), "a second owned cycle succeeds");
        ptd::ISoundPlayer* second = owned.player_for_tests();
        // T-103: allocator-proof freshness. `second != first` compares two heap
        // addresses, and after the first player is freed the allocator may hand
        // exactly that block back to a correct rebuild -- so the check would
        // fail on a sound implementation. Counting live instances cannot
        // alias: exactly one means shutdown destroyed the first player and this
        // cycle built a new one, a leak would read two and a reuse of the freed
        // player would read zero.
        using live_count = live_instance_count_of<ptd::QtSoundPlayer>;
        if constexpr (live_count::present) {
            check(second != nullptr && live_count::get() == 1,
                  "each cycle builds a fresh player, never a freed one");
        } else {
            check(second != nullptr, "the second cycle reports a player");
        }
        owned.shutdown();
        owned.shutdown();
        check(owned.player_for_tests() == nullptr, "repeated shutdown stays clean");
    }

    if constexpr (!has_fail_initialize<P>::value) {
        std::printf("SKIP fail_initialize seam: absent in the pre-fix subject\n");
    } else {
        P player;
        player.fail_initialize = true;
        T engine;
        engine.set_player(&player);
        check(!engine.initialize(), "a failing backend reports initialization failure");
        check(engine.player_for_tests() == &player,
              "the injected player is kept for the retry");
        check(!engine.owns_player_for_tests(), "no stale ownership after a failure");
        engine.shutdown();
        player.fail_initialize = false;
        check(engine.initialize(), "retry after a failed initialization succeeds");
        engine.shutdown();
    }
}

} // namespace

int main() {
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::printf("FAIL could not create scratch directory\n");
        return 1;
    }
    QTemporaryDir stage;
    if (!stage.isValid()) {
        std::printf("FAIL could not create stage directory\n");
        return 1;
    }
    // One staged asset for the ownership checks.
    {
        std::string wav;
        wav += "RIFF";
        put32s(wav, 36 + 64);
        wav += "WAVE";
        wav += "fmt ";
        put32s(wav, 16);
        put16s(wav, 1);
        put16s(wav, 1);
        put32s(wav, 22050);
        put32s(wav, 22050 * 2);
        put16s(wav, 2);
        put16s(wav, 16);
        wav += "data";
        put32s(wav, 64);
        wav.append(64, '\0');
        write_file(QDir(stage.path()).filePath("click.wav"), wav);
    }
    ptd::SoundCatalog::set_sound_directory_for_tests(stage.path());

    test_decoder_contract(scratch.path());
    test_fake_player_contract();
    test_ownership_contract<>();

    ptd::SoundCatalog::clear_sound_directory_for_tests();
    std::printf("t82_pair: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
