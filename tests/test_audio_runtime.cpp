// T-80 REAL-BACKEND control, T-81 extended, T-82 hardened. The engine unit
// tests inject a FakePlayer, so they can never prove that the Qt Multimedia
// backend accepts the shipped WAV catalog. This executable runs the
// production QtSoundPlayer against staged WAVs and asserts on QSoundEffect's
// own state:
//
//   * every staged format file loads (status Ready, not Error, not stuck
//     Loading);
//   * play() produces a PLAYING voice (device-dependent, reported as
//     DEVICE_PLAYBACK_REPORTED -- never as a human hearing anything);
//   * a preview stress over 50 GENUINELY UNIQUE source files (50 separately
//     named staged WAVs, each selected exactly once per cycle) keeps exactly
//     one preview voice and never grows the pool;
//   * runtime events stay available after the whole distinct-source sequence;
//   * Stop, Replay, Mute and natural completion behave;
//   * an ERROR source cannot poison the preview voice;
//   * repeated SoundEngine initialize/shutdown cycles with the OWNED
//     production player never reuse a destroyed player (ownership contract);
//   * a sample of the SHIPPED catalog loads through the real backend.
//
// Evidence discipline (T-82): this test prints explicit EVIDENCE lines for the
// staged WAV count, the shipped-format sample count, the distinct-preview
// counts, the voice pool capacity, the backend exit code and the audio device
// availability, and it terminates in exactly one of the statuses
//
//   BACKEND_VERIFIED / BACKEND_FAILED
//   DEVICE_PLAYBACK_REPORTED / DEVICE_PLAYBACK_NOT_REPORTED
//   HUMAN_AUDIBLE_NOT_VERIFIED
//
// QSoundEffect reaching the Playing state means the DEVICE accepted playback.
// It is NOT a human hearing the result: HUMAN_AUDIBLE_VERIFIED is recorded
// only after genuine physical acceptance by a person, never by this test.
//
// Exit codes: 0 = all checks passed and a device reported playback;
// 2 = all checks passed but no output device reported playback (BACKEND_ONLY);
// 1 = a check failed.

#include "../src/audio/sound_backend.h"
#include "../src/audio/sound_engine.h"
#include "../src/audio/sound_decoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSoundEffect>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

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

bool write_file(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    f.close();
    return true;
}

// Parameterised RIFF/WAVE writer: any bit depth, channel count and rate, so
// the staged inventory covers the formats the shipped catalog mixes.
QByteArray wav_bytes(int bits, int channels, int rate, int frames) {
    const int bytes_per_sample = bits / 8;
    const int data_bytes = frames * channels * bytes_per_sample;
    QByteArray out;
    auto put16 = [&out](int v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>((v >> 8) & 0xFF)); };
    auto put32 = [&out](int v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>((v >> 8) & 0xFF)); out.append(static_cast<char>((v >> 16) & 0xFF)); out.append(static_cast<char>((v >> 24) & 0xFF)); };
    out.append("RIFF"); put32(36 + data_bytes); out.append("WAVE");
    out.append("fmt "); put32(16); put16(1); put16(channels); put32(rate);
    put32(rate * channels * bytes_per_sample); put16(channels * bytes_per_sample); put16(bits);
    out.append("data"); put32(data_bytes);
    for (int i = 0; i < frames * channels; ++i) {
        int v = 0;
        if (bits == 8) {
            v = 128 + static_cast<int>(100.0 * (i % 8) / 8.0);  // unsigned PCM
        } else {
            v = static_cast<int>(3000.0 * (i % 8) / 8.0);
        }
        for (int b = 0; b < bytes_per_sample; ++b) {
            out.append(static_cast<char>((v >> (8 * b)) & 0xFF));
        }
    }
    return out;
}

std::string abs_path(const QString& path) {
    return QFileInfo(path).absoluteFilePath().toStdString();
}

// Bounded wait for QSoundEffect to leave the Loading state.
bool wait_ready(QSoundEffect& effect, int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeout_ms) {
        if (effect.status() != QSoundEffect::Loading) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTimer::singleShot(5, nullptr);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return effect.status() != QSoundEffect::Loading;
}

bool wait_playing(QSoundEffect& effect, int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeout_ms) {
        if (effect.isPlaying()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return effect.isPlaying();
}

// T-84: wait until a player holds at least n LIVE voices. QSoundEffect::play()
// is asynchronous: a slot that has not reached Playing is NOT busy, and
// submitting the next distinct event against it would reuse a free slot and
// forge capacity evidence. Only a genuinely live voice counts toward n.
bool wait_active_voices(ptd::ISoundPlayer& player, int n, int timeout_ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (player.active_voice_count() >= n) return true;
    }
    return player.active_voice_count() >= n;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QTemporaryDir stage;
    if (!stage.isValid()) {
        std::printf("FAIL could not create a temporary sound directory\n");
        return 1;
    }
    // Format coverage: the bit depths / channel counts / sample rates the
    // shipped inventory mixes, plus one very short file for the natural
    // completion check and one corrupt file for the ERROR transition.
    struct Spec {
        const char* name;
        int bits;
        int channels;
        int rate;
        int frames;
    };
    const Spec specs[] = {
        {"a00.wav", 16, 1, 22050, 2205}, {"a01.wav", 8, 1, 11025, 2205},
        {"a02.wav", 8, 2, 8000, 2205},   {"a03.wav", 16, 2, 44100, 2205},
        {"a04.wav", 24, 1, 44100, 2205}, {"a05.wav", 32, 1, 48000, 2205},
        {"a06.wav", 16, 1, 8000, 2205},  {"a07.wav", 16, 1, 16000, 2205},
        {"a08.wav", 32, 2, 22050, 2205}, {"a09.wav", 24, 2, 48000, 2205},
        {"short.wav", 16, 1, 22050, 4410},  // ~200 ms: starts AND finishes while we wait
    };
    const int kFiles = static_cast<int>(sizeof(specs) / sizeof(specs[0]));
    for (int i = 0; i < kFiles; ++i) {
        const QString path = stage.filePath(QString::fromLatin1(specs[i].name));
        check(write_file(path, wav_bytes(specs[i].bits, specs[i].channels,
                                         specs[i].rate, specs[i].frames)),
              (std::string("staged ") + specs[i].name).c_str());
    }

    // T-82: the DISTINCT-SOURCE inventory. 50 separately named, individually
    // valid WAV files whose formats cycle the combinations above and whose
    // frame counts differ, so no two are byte-identical. The preview stress
    // below selects each of these EXACTLY ONCE per cycle: 50 selections over
    // 50 unique source paths, never a smaller repeating set.
    const int kUniqueFiles = 50;
    const int kCycles = 4;  // preview-stress cycles; see section 3a
    QStringList unique_names;
    for (int i = 0; i < kUniqueFiles; ++i) {
        const char name[] = {'u', static_cast<char>('0' + (i / 10) % 10),
                             static_cast<char>('0' + i % 10), '.', 'w', 'a', 'v', '\0'};
        const Spec& s = specs[i % (kFiles - 1)];  // all format combos but short.wav
        const QString path = stage.filePath(QString::fromLatin1(name));
        check(write_file(path, wav_bytes(s.bits, s.channels, s.rate, 2205 + i)),
              "staged a distinct-source wav");
        unique_names << QString::fromLatin1(name);
    }
    // The inventory claim is asserted BEFORE any stress runs: if this set ever
    // shrinks or repeats, the test fails here instead of quietly claiming
    // "50 distinct" over a smaller cycling set.
    {
        std::set<std::string> distinct;
        for (const QString& id : unique_names) distinct.insert(id.toStdString());
        check_eq(static_cast<long long>(distinct.size()), kUniqueFiles,
                 "the distinct-source inventory holds 50 separately named files");
        int valid = 0;
        for (const QString& id : unique_names) {
            if (ptd::decode_wav_file(abs_path(stage.filePath(id)))) ++valid;
        }
        check_eq(valid, kUniqueFiles,
                 "all 50 distinct-source files are valid decodable WAVs");
    }

    // A corrupt sibling: not RIFF at all. The backend must report it without
    // taking the pool down with it.
    const QString corrupt = stage.filePath("corrupt.wav");
    check(write_file(corrupt, QByteArray("this is not a wav file, not even close")),
          "staged a corrupt wav");

    // T-84: one LONG source (~3 s) so eight distinct events can be held BUSY
    // simultaneously. The short staged files (~100 ms) finish before a later
    // distinct event could reuse their slots, which would forge (or break) the
    // capacity evidence. Staged separately so the counts above stay exact.
    const std::string long_src =
        abs_path(stage.filePath("t84long.wav"));
    check(write_file(stage.filePath("t84long.wav"), wav_bytes(16, 1, 22050, 66150)),
          "staged a long t84 wav");

    ptd::SoundCatalog::set_sound_directory_for_tests(stage.path());

    // ---- 1. The real backend loads every distinct staged format file. ------
    bool backend_loaded = true;
    for (int i = 0; i < kFiles; ++i) {
        const QString path = stage.filePath(QString::fromLatin1(specs[i].name));
        QSoundEffect probe;
        probe.setSource(QUrl::fromLocalFile(path));
        const bool ready = wait_ready(probe, 3000);
        const std::string what = std::string("QSoundEffect loads ") + specs[i].name;
        check(ready, what.c_str());
        check(probe.status() != QSoundEffect::Error,
              (what + " without an error").c_str());
        if (!(ready && probe.status() == QSoundEffect::Ready)) backend_loaded = false;
    }
    // The corrupt file must be REJECTED, not accepted as silence.
    {
        QSoundEffect probe;
        probe.setSource(QUrl::fromLocalFile(corrupt));
        const bool ready = wait_ready(probe, 3000);
        check(ready, "QSoundEffect settles on the corrupt file");
        check(probe.status() == QSoundEffect::Error,
              "the corrupt file is reported as an Error, not accepted");
    }

    // ---- 2. Fake-vs-production contract: the deterministic refusals. -------
    // QtSoundPlayer::play must refuse exactly what FakePlayer::play refuses,
    // and neither may leave stale state behind on a refusal.
    {
        ptd::QtSoundPlayer refused;
        check(!refused.play("left_down", abs_path(stage.filePath("a00.wav")), false),
              "production refuses playback before initialize");
        check_eq(refused.voice_pool_size(), 0, "the refusal allocated no voice");
        check(refused.initialize(), "production backend initializes");
        refused.set_muted(true);
        check(!refused.play("left_down", abs_path(stage.filePath("a00.wav")), false),
              "production refuses event playback while muted");
        check(!refused.play("preview", abs_path(stage.filePath("a00.wav")), true),
              "production refuses preview playback while muted");
        check_eq(refused.voice_pool_size(), 0, "muted refusals allocate no voice");
        check(!refused.is_playing_preview(), "a refused preview leaves no stale preview state");
        refused.set_muted(false);
        check(!refused.play("left_down", "", false), "production refuses an empty path");
        check_eq(refused.voice_pool_size(), 0, "the empty-path refusal allocates no voice");
    }

    // ---- 3. The production voice pool over 50 unique preview sources. ------
    bool device_played = false;
    int natural_completion = -1;  // -1 = not run, 0 = SKIP, 1 = observed
    int max_live_preview_voices = 0;
    if (backend_loaded) {
        ptd::QtSoundPlayer player;
        check(player.initialize(), "production backend initializes");
        player.set_volume(0.5);
        check(player.play("left_down", abs_path(stage.filePath("a00.wav")),
                          /*is_preview=*/false),
              "real backend accepts an event play");
        player.stop_all();

        // Device probe: does any voice reach the Playing state here? That is a
        // DEVICE fact, not a hearing fact.
        {
            QSoundEffect probe2;
            probe2.setSource(QUrl::fromLocalFile(stage.filePath("a00.wav")));
            wait_ready(probe2, 3000);
            probe2.setVolume(0.5);
            probe2.play();
            device_played = wait_playing(probe2, 1500);
            probe2.stop();
        }
        if (!device_played) {
            std::printf("NOTE the multimedia backend accepted the files but no "
                        "voice reached the Playing state (no output device?)\n");
        }

        // ---- 3a. The distinct-source preview stress (device-independent) ----
        // 50 selections per cycle, each over a DIFFERENT source path, repeated
        // for several cycles. Everything asserted here is pool bookkeeping and
        // play() acceptance, which hold with or without an output device.
        bool all_switches_ok = true;
        long long total_selections = 0;
        std::set<std::string> all_cycle_paths;
        for (int cycle = 0; cycle < kCycles; ++cycle) {
            std::set<std::string> cycle_paths;
            for (int i = 0; i < kUniqueFiles; ++i) {
                const std::string abs = abs_path(stage.filePath(unique_names[i]));
                cycle_paths.insert(abs);
                all_cycle_paths.insert(abs);
                const bool ok = player.play("preview", abs, /*is_preview=*/true);
                if (!ok) all_switches_ok = false;
                if (player.preview_voice_count() != 1) all_switches_ok = false;
                if (player.preview_voice_count() > max_live_preview_voices) {
                    max_live_preview_voices = player.preview_voice_count();
                }
                if (player.voice_pool_size() > ptd::QtSoundPlayer::kMaxVoices) {
                    all_switches_ok = false;
                }
                ++total_selections;
            }
            if (static_cast<long long>(cycle_paths.size()) != kUniqueFiles) {
                all_switches_ok = false;  // the cycle repeated sources: claim is dead
            }
        }
        check(all_switches_ok,
              "every preview switch is accepted and stays at one preview voice");
        check_eq(total_selections, static_cast<long long>(kCycles) * kUniqueFiles,
                 "the stress ran the full selection count");
        check_eq(static_cast<long long>(all_cycle_paths.size()), kUniqueFiles,
                 "the stress touched 50 unique source files, never a smaller repeating set");
        check_eq(player.preview_voice_count(), 1,
                 "browsing keeps exactly one preview voice");
        check(player.voice_pool_size() <= ptd::QtSoundPlayer::kMaxVoices,
              "the pool never grows past its bound");

        std::printf("PREVIEW_STRESS selections: %lld (%d per cycle x %d cycles)\n",
                    total_selections, kUniqueFiles, kCycles);
        std::printf("PREVIEW_STRESS unique source files: %d\n",
                    static_cast<int>(all_cycle_paths.size()));
        std::printf("PREVIEW_STRESS voice pool capacity: %d (live preview voices: %d)\n",
                    ptd::QtSoundPlayer::kMaxVoices, player.preview_voice_count());

        // The mirror case: 50 REPEATED previews of ONE WAV. Replaying the
        // same source fifty times must restart it every time and never stack
        // voices or grow the pool.
        bool repeats_ok = true;
        for (int i = 0; i < 50; ++i) {
            if (!player.play("preview", abs_path(stage.filePath("a00.wav")),
                             /*is_preview=*/true)) {
                repeats_ok = false;
            }
            if (player.preview_voice_count() != 1) repeats_ok = false;
            if (player.voice_pool_size() > ptd::QtSoundPlayer::kMaxVoices) repeats_ok = false;
        }
        check(repeats_ok, "50 repeated previews of one WAV stay at one preview voice");
        std::printf("PREVIEW_REPEAT selections: 50 over 1 unique source file "
                    "(live preview voices: %d)\n", player.preview_voice_count());

        // Replay of the CURRENT selection: restart, never stack.
        check(player.play("preview", abs_path(stage.filePath(unique_names[kUniqueFiles - 1])),
                          /*is_preview=*/true),
              "replaying the current selection restarts it");
        check_eq(player.preview_voice_count(), 1, "a replay replaces, it never stacks");

        // ---- 3b. Runtime events after the entire distinct-source sequence --
        check(player.play("right_down", abs_path(stage.filePath("a03.wav")),
                          /*is_preview=*/false),
              "an event is still playable after the whole distinct-source sequence");

        // Stop Preview stops only the preview (the reserved preview SLOT stays
        // allocated by design; it just owns no live voice).
        player.stop_preview();
        check(!player.is_playing_preview(), "stop cancels the preview");
        check_eq(player.preview_voice_count(), 1, "the single preview slot is preserved");
        check(player.voice_pool_size() <= ptd::QtSoundPlayer::kMaxVoices,
              "stop preview keeps the pool bounded");

        // Mute refuses new playback and silences everything; unmute recovers.
        player.set_muted(true);
        check(player.active_voice_count() == 0, "mute leaves no audible voice");
        const bool muted_play = player.play(
            "left_down", abs_path(stage.filePath("a00.wav")), /*is_preview=*/false);
        check(!muted_play, "a muted backend refuses playback outright");
        player.set_muted(false);
        check(player.play("left_down", abs_path(stage.filePath("a00.wav")),
                          /*is_preview=*/false),
              "unmute allows playback again");
        player.stop_all();

        // ---- 3c. An ERROR source cannot poison the preview voice. -----------
        player.play("preview", abs_path(corrupt), /*is_preview=*/true);
        // The error may arrive asynchronously with the Loading transition.
        std::string corrupt_status = player.backend_status();
        {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < 3000 && corrupt_status == "LOADING") {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                corrupt_status = player.backend_status();
            }
        }
        check(corrupt_status == "ERROR",
              "the corrupt source settles as an ERROR, not silence");
        check(player.play("preview", abs_path(stage.filePath("a05.wav")),
                          /*is_preview=*/true),
              "a valid preview still plays after the error");
        check_eq(player.preview_voice_count(), 1,
                 "the error did not consume the preview voice");

        // ---- 3d. Device-reported playback checks ----------------------------
        if (device_played) {
            check(player.play("left_down", abs_path(stage.filePath("a00.wav")),
                              /*is_preview=*/false),
                  "an event play is accepted");
            const int live = player.active_voice_count();
            check(live >= 1, "a played event owns a voice");
            check(player.play("preview", abs_path(stage.filePath("a04.wav")),
                              /*is_preview=*/true),
                  "the preview plays");
            check(player.is_playing_preview(),
                  "the preview voice reports Playing (device-reported, not heard)");
            player.stop_all();

            // Natural completion: a ~5 ms file finishes on its own and its
            // voice returns to the reusable state. Only claimed when the voice
            // actually STARTED on this device first.
            check(player.play("preview", abs_path(stage.filePath("short.wav")),
                              /*is_preview=*/true),
                  "the short preview plays");
            bool started = false;
            {
                QElapsedTimer clock;
                clock.start();
                while (clock.elapsed() < 1500 && !started) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    started = player.is_playing_preview();
                }
            }
            if (!started) {
                natural_completion = 0;
                std::printf("SKIP natural completion: the short preview never "
                            "reached Playing on this device -- NOT VERIFIED\n");
            } else {
                bool finished = false;
                {
                    QElapsedTimer clock;
                    clock.start();
                    while (clock.elapsed() < 4000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                        if (!player.is_playing_preview()) {
                            finished = true;
                            break;
                        }
                    }
                }
                check(finished, "natural completion is observed");
                check(!player.is_playing_preview(),
                      "the completed voice is no longer reported as preview");
                natural_completion = 1;
            }
            const int pool_after = player.voice_pool_size();
            check(player.play("preview", abs_path(stage.filePath("a04.wav")),
                              /*is_preview=*/true),
                  "the completed voice is reusable");
            check_eq(player.voice_pool_size(), pool_after,
                     "completion reuses the slot instead of allocating");
        } else {
            std::printf("SKIP device-reported playback checks: no output device "
                        "produced a Playing voice -- DEVICE_PLAYBACK_NOT_REPORTED\n");
        }

        player.shutdown();
        check(player.active_voice_count() == 0, "shutdown leaves no active voice");
        check(!player.is_playing_preview(), "shutdown ends previews");
        check(player.voice_pool_size() == 0, "shutdown releases the voice pool");
    } else {
        std::printf("FAIL multimedia backend could not load a staged wav\n");
    }

    // ---- 3e. T-84: the 8-event + 1-preview allocation contract. -------------
    // The old acquire_event_voice gate (`voices_.size() >= kMaxVoices - 1`)
    // counted the preview voice as an event slot, so preview-first order left
    // only seven event voices: capacity was order-dependent on whether the
    // catalog was auditioned first. Two independent layers prove the fix:
    //
    //   (1) DETERMINISTIC budget control -- the pure static predicate
    //       QtSoundPlayer::event_voice_available decides the gate on EVENT
    //       SLOTS ALONE, never on the preview slot. That is the exact defect
    //       site and needs no output device, so it ALWAYS runs.
    //   (2) REAL-BACKEND integration -- eight distinct events stay accepted in
    //       every arrival order while a ninth is refused and a preview never
    //       costs an event slot. Holding eight voices LIVE needs a device, so
    //       this layer is gated on device_played and reported as such; the
    //       deterministic control above is the claim that holds without one.
    {
        // (1) Deterministic budget control (device-independent).
        check_eq(ptd::QtSoundPlayer::kMaxEventVoices, 8,
                 "the event budget is exactly eight slots");
        // The order-equivalence the defect broke: a preview voice sits in the
        // TOTAL but never in the EVENT count, so when seven event slots and
        // the preview slot coexist (total 8) the eighth event is still
        // admitted. The old gate refused here because it tested
        // voices_.size() >= 8.
        check(ptd::QtSoundPlayer::event_voice_available(7, 8),
              "the 8th event slot is admitted with a preview voice in the pool (total 8)");
        check(!ptd::QtSoundPlayer::event_voice_available(8, 9),
              "a 9th event slot is refused with a preview voice in the pool (total 9)");
        check(ptd::QtSoundPlayer::event_voice_available(0, 1),
              "the first event slot is admitted when only a preview voice exists");
        check(!ptd::QtSoundPlayer::event_voice_available(8, 8),
              "a 9th event slot is refused when no preview voice exists");
        check(!ptd::QtSoundPlayer::event_voice_available(0, ptd::QtSoundPlayer::kMaxVoices),
              "the overall kMaxVoices ceiling refuses growth past the budget");
        check(ptd::QtSoundPlayer::event_voice_available(0, 0),
              "an empty pool admits an event slot");
        check(!ptd::QtSoundPlayer::event_voice_available(ptd::QtSoundPlayer::kMaxEventVoices,
                                                         ptd::QtSoundPlayer::kMaxVoices - 1),
              "a full event budget is refused even with the preview slot free");
        std::printf("T84_DETERMINISTIC event_voice_available(7 events, total 8) = %s "
                    "(the old gate refused this preview-first case)\n",
                    ptd::QtSoundPlayer::event_voice_available(7, 8) ? "ADMIT" : "REFUSE");
    }

    if (backend_loaded && device_played) {
        // (2) Real-backend integration. Every distinct event is submitted only
        // after the previous live voices are VERIFIED (wait_active_voices), so
        // a not-yet-playing slot can never be mistaken for a busy one.
        const std::string ev[8] = {"t84e0", "t84e1", "t84e2", "t84e3",
                                   "t84e4", "t84e5", "t84e6", "t84e7"};
        const std::string ninesrc = abs_path(stage.filePath("a00.wav"));

        // A. Events first, then the preview. Eight distinct events stay
        //    accepted and live; a ninth is refused; the preview afterwards
        //    still claims its own slot and the pool fills to exactly 9.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            bool all_ok = true;
            for (int i = 0; i < 8; ++i) {
                if (!p.play(ev[i], long_src, false)) all_ok = false;
                if (!wait_active_voices(p, i + 1, 3000)) all_ok = false;  // liveness before the next distinct event
            }
            check(all_ok, "[A events-first] eight distinct events are all accepted and live");
            check(!p.play("t84e8", ninesrc, false),
                  "[A events-first] a ninth distinct event is refused");
            check(p.play("preview", abs_path(stage.filePath("a00.wav")), true),
                  "[A events-first] the preview still gets its slot after eight events");
            check_eq(p.preview_voice_count(), 1,
                     "[A events-first] exactly one preview voice after the event burst");
            check_eq(p.voice_pool_size(), ptd::QtSoundPlayer::kMaxVoices,
                     "[A events-first] 8 event + 1 preview fills the pool exactly");
            p.shutdown();
        }

        // B. Preview first, then the events -- the case the defect broke. The
        //    preview claims its slot; eight distinct events STILL fit; a ninth
        //    is refused; the preview keeps its single slot throughout.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            check(p.play("preview", abs_path(stage.filePath("a00.wav")), true),
                  "[B preview-first] the preview claims its slot before any event");
            wait_active_voices(p, 1, 3000);
            bool all_ok = true;
            for (int i = 0; i < 8; ++i) {
                if (!p.play(ev[i], long_src, false)) all_ok = false;
                if (!wait_active_voices(p, i + 2, 3000)) all_ok = false;  // preview + (i+1) events
            }
            check(all_ok, "[B preview-first] eight distinct events are all accepted after the preview");
            check(!p.play("t84e8", ninesrc, false),
                  "[B preview-first] a ninth distinct event is refused after the preview");
            check_eq(p.preview_voice_count(), 1,
                     "[B preview-first] the preview never lost its slot to the events");
            check_eq(p.voice_pool_size(), ptd::QtSoundPlayer::kMaxVoices,
                     "[B preview-first] 8 event + 1 preview fills the pool, preview-first");
            p.shutdown();
        }

        // C. Interleaved: catalog browsing between events. The preview reuses
        //    its one slot on every selection while eight event voices grow.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            bool all_ok = true;
            for (int i = 0; i < 8; ++i) {
                if (!p.play("preview", abs_path(stage.filePath(unique_names[i])), true)) all_ok = false;
                if (!wait_active_voices(p, 1, 3000)) all_ok = false;  // the single preview slot is live
                if (!p.play(ev[i], long_src, false)) all_ok = false;
                if (!wait_active_voices(p, i + 2, 3000)) all_ok = false;
            }
            check(all_ok, "[C interleaved] interleaved preview/event stays fully accepted");
            check(!p.play("t84e8", ninesrc, false),
                  "[C interleaved] a ninth distinct event is refused, interleaved");
            check_eq(p.preview_voice_count(), 1,
                     "[C interleaved] interleaving keeps exactly one preview voice");
            check_eq(p.voice_pool_size(), ptd::QtSoundPlayer::kMaxVoices,
                     "[C interleaved] 8 event + 1 preview fills the pool, interleaved");
            p.shutdown();
        }

        // D. Natural-completion reuse: a finished event's slot is reused by a
        //    different event; the preview slot is never converted to an event.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            check(p.play("t84short", abs_path(stage.filePath("short.wav")), false),
                  "[D completion] the short event plays");
            bool finished = false;
            {
                QElapsedTimer c;
                c.start();
                while (c.elapsed() < 4000) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (p.active_voice_count() == 0) { finished = true; break; }
                }
            }
            check(finished, "[D completion] the short event finishes naturally");
            check(p.play("t84reuse", abs_path(stage.filePath("a00.wav")), false),
                  "[D completion] a different event reuses the freed slot");
            wait_active_voices(p, 1, 3000);
            check_eq(p.active_voice_count(), 1,
                     "[D completion] reuse keeps one live event voice, no second slot");
            check(p.play("preview", abs_path(stage.filePath("a00.wav")), true),
                  "[D completion] the preview still gets its slot after reuse");
            check_eq(p.preview_voice_count(), 1,
                     "[D completion] the preview voice is never converted to an event voice");
            p.shutdown();
        }

        // E. Preview replacement while eight event voices are active: 50
        //    distinct preview sources must all be accepted onto the ONE preview
        //    slot without touching or starving an event voice.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            bool all_ok = true;
            for (int i = 0; i < 8; ++i) {
                if (!p.play(ev[i], long_src, false)) all_ok = false;
                if (!wait_active_voices(p, i + 1, 3000)) all_ok = false;
            }
            for (int i = 0; i < kUniqueFiles; ++i) {
                if (!p.play("preview", abs_path(stage.filePath(unique_names[i])), true)) all_ok = false;
                if (p.preview_voice_count() != 1) all_ok = false;
                if (p.voice_pool_size() > ptd::QtSoundPlayer::kMaxVoices) all_ok = false;
            }
            check(all_ok, "[E replacement] 50 preview selections on one slot with 8 events live");
            check_eq(p.preview_voice_count(), 1,
                     "[E replacement] exactly one preview voice during the replacement storm");
            check_eq(p.active_voice_count(), 9,
                     "[E replacement] 8 events + 1 preview stay live and distinct");
            p.shutdown();
        }

        // F. Stop and mute never corrupt the capacity bookkeeping: stop_events
        //    frees the event voices (the preview slot persists), mute silences
        //    everything and refuses new playback, unmute recovers, and the
        //    eighth event is again admitted afterwards.
        {
            ptd::QtSoundPlayer p;
            p.initialize();
            for (int i = 0; i < 8; ++i) {
                p.play(ev[i], abs_path(stage.filePath(unique_names[i])), false);
                wait_active_voices(p, i + 1, 3000);
            }
            p.play("preview", abs_path(stage.filePath("a00.wav")), true);
            p.stop_events();
            check(p.active_voice_count() <= 1, "[F stop/mute] stop_events ends the event voices");
            check_eq(p.preview_voice_count(), 1,
                     "[F stop/mute] the preview slot survives stop_events");
            p.set_muted(true);
            check(!p.play("t84mute", ninesrc, false),
                  "[F stop/mute] a muted backend refuses new event playback");
            check(!p.play("preview", abs_path(stage.filePath("a00.wav")), true),
                  "[F stop/mute] a muted backend refuses new preview playback");
            check(p.active_voice_count() == 0, "[F stop/mute] mute leaves no live voice");
            p.set_muted(false);
            bool eight_again = true;
            for (int i = 0; i < 8; ++i) {
                if (!p.play(ev[i], abs_path(stage.filePath(unique_names[i])), false)) eight_again = false;
                if (!wait_active_voices(p, i + 1, 3000)) eight_again = false;
            }
            check(eight_again, "[F stop/mute] eight events are admitted again after unmute");
            p.shutdown();
        }

        std::printf("T84_INTEGRATION capacity contract verified with a real output device\n");
    } else {
        std::printf("T84_INTEGRATION capacity-via-live-voices SKIPPED (no output device): "
                    "the deterministic event_voice_available control above is the device-independent "
                    "proof of the allocation contract -- DEVICE_PLAYBACK_NOT_REPORTED\n");
    }

    // ---- 4. SoundEngine with its OWNED production player: reinitialization.
    // The ownership contract (T-82/P1): each initialize/shutdown cycle builds
    // a fresh QtSoundPlayer, the cached pointer is cleared before the old one
    // is destroyed, no preview survives shutdown, and no duplicate live pool
    // appears after reinitialization.
    {
        ptd::SoundEngine engine;  // no injection: the engine OWNS its player
        for (int cycle = 0; cycle < 3; ++cycle) {
            check(engine.initialize(), "the owned production player initializes");
            ptd::ISoundPlayer* live = engine.player_for_tests();
            check(live != nullptr && engine.owns_player_for_tests(),
                  "the engine owns exactly one production player");
            check(engine.player_for_tests() == live, "the live player is the owned player");
            engine.preview_sound("u00.wav");
            check_eq(live->preview_voice_count(), 1, "one preview voice per cycle");
            if (live->preview_voice_count() > max_live_preview_voices) {
                max_live_preview_voices = live->preview_voice_count();
            }
            check(live->voice_pool_size() <= ptd::QtSoundPlayer::kMaxVoices,
                  "no duplicate live QSoundEffect pool after reinitialization");
            engine.shutdown();
            check(engine.player_for_tests() == nullptr,
                  "shutdown clears the cached pointer to the destroyed player");
            check(!engine.owns_player_for_tests(), "the owned player is destroyed");
            check(!engine.is_playing_preview(), "no residual preview after shutdown");
            check_eq(engine.active_voice_count(), 0, "no residual voice after shutdown");
        }
        engine.shutdown();  // idempotent
        check(engine.player_for_tests() == nullptr, "repeated shutdown stays clean");
    }

    // ---- 5. A sample of the SHIPPED catalog through the real backend. -------
    // A synthetic WAV passing proves nothing about the user's inventory, so
    // walk up to the repository sound/ directory and exercise representative
    // formats through QSoundEffect itself.
    QString shipped;
    {
        QDir walk = QDir::current();
        for (int depth = 0; depth < 8; ++depth) {
            const QString probe = walk.absoluteFilePath(QStringLiteral("sound"));
            if (QFileInfo::exists(probe + "/Click.wav")) {
                shipped = probe;
                break;
            }
            if (!walk.cdUp()) break;
        }
    }
    std::string shipped_sample_status = "NOT RUN";
    if (shipped.isEmpty()) {
        shipped_sample_status = "SKIP (no sound directory beside the sources) -- NOT VERIFIED";
        std::printf("SKIP shipped catalog backend sample: no sound directory beside "
                    "the sources\n");
    } else {
        // Pick up to 8 files, preferring distinct (bits, channels, rate)
        // combinations, so every sampled format family is represented.
        const QStringList ids =
            QDir(shipped).entryList(QStringList{"*.wav"}, QDir::Files, QDir::Name);
        struct Sampled {
            QString path;
            int bits;
            int channels;
            int rate;
        };
        std::vector<Sampled> sample;
        auto key_of = [](const QAudioFormat& f) {
            return f.sampleFormat() * 100000 + f.channelCount() * 1000 + f.sampleRate();
        };
        std::vector<int> seen;
        for (const QString& id : ids) {
            const std::string absolute =
                QFileInfo(QDir(shipped).filePath(id)).absoluteFilePath().toStdString();
            const auto decoded = ptd::decode_wav_file(absolute);
            if (!decoded) continue;
            const int key = key_of(decoded->format);
            bool known = false;
            for (int k : seen) {
                if (k == key) known = true;
            }
            if (known) continue;
            seen.push_back(key);
            Sampled s;
            s.path = QFileInfo(QDir(shipped).filePath(id)).absoluteFilePath();
            s.bits = 16;
            switch (decoded->format.sampleFormat()) {
                case QAudioFormat::UInt8: s.bits = 8; break;
                case QAudioFormat::Int16: s.bits = 16; break;
                case QAudioFormat::Int32: s.bits = 32; break;
                case QAudioFormat::Float: s.bits = 32; break;
                default: s.bits = 32; break;
            }
            s.channels = decoded->format.channelCount();
            s.rate = decoded->format.sampleRate();
            sample.push_back(s);
            if (static_cast<int>(sample.size()) >= 8) break;
        }
        std::printf("shipped catalog: %d files, %d distinct formats sampled\n",
                    static_cast<int>(ids.size()), static_cast<int>(sample.size()));
        int backend_ok = 0;
        int backend_bad = 0;
        for (const Sampled& s : sample) {
            QSoundEffect probe;
            probe.setSource(QUrl::fromLocalFile(s.path));
            const bool ready = wait_ready(probe, 3000);
            if (ready && probe.status() == QSoundEffect::Ready) {
                ++backend_ok;
                std::printf("  shipped OK   %d-bit %dch %dHz  %s\n", s.bits, s.channels,
                            s.rate, s.path.toStdString().c_str());
            } else {
                ++backend_bad;
                std::printf("FAIL shipped sound rejected by the backend: %s "
                            "(status %d)\n",
                            s.path.toStdString().c_str(),
                            static_cast<int>(probe.status()));
            }
        }
        check(backend_bad == 0, "every sampled shipped sound loads in the backend");
        check(backend_ok == static_cast<int>(sample.size()),
              "sampled shipped sound count matches backend acceptances");
        if (backend_bad == 0 && backend_ok == static_cast<int>(sample.size())) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "VERIFIED (%d files, %d distinct formats sampled)",
                          static_cast<int>(ids.size()), static_cast<int>(sample.size()));
            shipped_sample_status = buf;
        } else {
            shipped_sample_status = "FAILED (see FAIL lines above)";
        }
    }

    ptd::SoundCatalog::clear_sound_directory_for_tests();

    // ---- 6. Explicit evidence + statuses ------------------------------------
    const int staged_total = kFiles + kUniqueFiles + 1;  // formats + unique + corrupt
    std::printf("EVIDENCE staged_wav_count: %d (%d format + %d unique-source + 1 corrupt)\n",
                staged_total, kFiles, kUniqueFiles);
    std::printf("EVIDENCE shipped_format_sample_count: %s\n", shipped_sample_status.c_str());
    std::printf("EVIDENCE distinct_preview_test_count: %d selections x %d cycles "
                "= %d selections over %d unique source files\n",
                kUniqueFiles, kCycles, kCycles * kUniqueFiles, kUniqueFiles);
    std::printf("EVIDENCE voice_pool_capacity: %d (max live preview voices: %d)\n",
                ptd::QtSoundPlayer::kMaxVoices, max_live_preview_voices);
    std::printf("EVIDENCE t84_event_capacity: %d event slots + 1 preview slot, order-independent (%s)\n",
                ptd::QtSoundPlayer::kMaxEventVoices,
                (backend_loaded && device_played)
                    ? "integration verified with a real device"
                    : "deterministic control only -- DEVICE_PLAYBACK_NOT_REPORTED");
    std::printf("EVIDENCE audio_device_available: %s\n", device_played ? "yes" : "no");
    std::printf("EVIDENCE natural_completion: %s\n",
                natural_completion == 1 ? "VERIFIED"
                : natural_completion == 0 ? "SKIP (voice never started) -- NOT VERIFIED"
                                          : "NOT RUN (no device)");
    const int exit_code = g_failures > 0 ? 1 : (device_played ? 0 : 2);
    std::printf("EVIDENCE backend_test_exit_code: %d\n", exit_code);
    std::printf("STATUS %s\n", (g_failures == 0 && backend_loaded) ? "BACKEND_VERIFIED"
                                                                   : "BACKEND_FAILED");
    std::printf("STATUS %s\n", device_played ? "DEVICE_PLAYBACK_REPORTED"
                                             : "DEVICE_PLAYBACK_NOT_REPORTED");
    std::printf("STATUS HUMAN_AUDIBLE_NOT_VERIFIED\n");
    std::printf("(HUMAN_AUDIBLE_VERIFIED is recorded only after genuine physical "
                "acceptance by a person, never by this test)\n");

    std::printf("test_audio_runtime: %d checks, %d failures\n", g_checks, g_failures);
    return exit_code;
}
