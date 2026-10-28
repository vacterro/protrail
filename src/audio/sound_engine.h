#pragma once

#include "../config/audio_config.h"
#include "sound_backend.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ptd {

class ISoundPlayer;
class MouseInput;

// T-80 / T-120: the sound catalog + the executable-relative sound directory.
//
// The ONE asset authority is the sound directory staged next to the
// executable (never the current working directory -- a portable package must
// work when launched from anywhere). The catalog is discovered lazily: sound
// ids are plain validated file names, and a file that is not present in the
// directory is reported missing instead of being invented.
class SoundCatalog {
public:
    // Absolute directory that holds the shipped WAV assets. Defaults to
    // <executable directory>\sound; a test injects its own directory.
    static constexpr const char* kSoundDirectoryName = "sound";

    static QString sound_directory();
    static void set_sound_directory_for_tests(const QString& dir);
    static void clear_sound_directory_for_tests();

    // Sorted sound ids (validated file names) present in the directory.
    static QStringList sound_ids();

    // True when the id is a syntactically valid relative name AND the file
    // exists in the sound directory.
    static bool sound_exists(const std::string& id);

    // Resolves a validated id to its absolute path inside the sound
    // directory. Returns an empty string for an invalid or escaping id --
    // a path can never escape the approved directory.
    static QString resolve_sound_path(const std::string& id);
};

// T-80 / T-120: the canonical event -> scheduler -> playback engine.
//
// Threading contract: GUI-thread only. MouseInput and KeyboardInput dispatch
// activity on the GUI thread and never block: event dispatch reads only the
// in-memory path cache (refreshed on config change) and hands it to the
// player, so no event path performs disk scanning or decoding.
//
// Supports:
// - Playback policies: STACK, OVERRIDE, CUT, QUEUE, COALESCE, STOP ALL.
// - Interruption scopes: SameEvent, SameGroup, Global.
// - Per-event cooldown, min_retrigger_ms, priority, dB gain, and polyphony allowance.
// - Random sound pools with deterministic weight normalization, no-immediate-repeat,
//   and safe fallback when pool entries are disabled or missing on disk.

// T-122: the playback lane a queued entry waits on. Every interruption scope
// resolves to exactly one lane identity, and the identity -- not the raw event
// id -- is what a queued entry stores and what queue draining re-evaluates.
enum class PlaybackLane {
    SameEvent,
    SameGroup,
    GlobalLane,
};

class SoundEngine {
public:
    static constexpr int kMaxPendingQueueSize = 16;
    static constexpr int64_t kMaxQueueAgeNs = 2'000'000'000LL; // 2 seconds max age

    struct QueuedEvent {
        std::string event_id;
        std::string resolved_path;
        double linear_gain = 1.0;
        int priority = 50;
        int64_t enqueued_ns = 0;
        // T-122: the lane this entry waits on, resolved at enqueue time, plus
        // the configuration generation it was resolved under. Both are
        // re-read by flush_queue: the lane so group ownership is preserved,
        // the generation so a config change never plays an obsolete
        // assignment through a stale entry.
        PlaybackLane lane = PlaybackLane::SameEvent;
        std::string lane_key;
        SoundEventCategory category = SoundEventCategory::Custom;
        int64_t generation = 0;
        // T-125: the cooldown identity the entry was enqueued under. The drain
        // records retrigger timing against it, exactly like the immediate path,
        // so a queue-started voice arms its own cooldown window.
        std::string canonical_id;
    };

    SoundEngine();
    ~SoundEngine();

    // Injects a non-owning player. Production does NOT inject: initialize()
    // then creates (and owns) a QtSoundPlayer. Tests inject FakePlayer, which
    // they own and whose lifetime exceeds the engine's. Must be called before
    // initialize(); injecting later shuts the engine down first so ownership
    // is never ambiguous.
    void set_player(ISoundPlayer* player);

    // Repeated initialize()/shutdown() cycles on ONE engine instance are
    // supported: each initialized state owns exactly one player generation and
    // no pointer to a destroyed player survives a shutdown. shutdown() is
    // idempotent and a failed initialize() leaves no stale ownership state.
    bool initialize();
    void shutdown();

    // Runtime gate. The application-level master switch suppresses ALL
    // runtime audio; audio.enabled is the audio feature switch; audio.muted
    // stops playback and prevents new playback.
    void set_master_enabled(bool on);
    void apply_config(const AudioConfig& config);

    // One normalized input event (button, wheel, key, or shortcut) with optional
    // monotonic timestamp in nanoseconds (0 = query current monotonic clock).
    void play_event(const std::string& event_id);
    void play_event_at(const std::string& event_id, int64_t timestamp_ns);
    void play_event(AudioEvent event);
    void play_event_at(AudioEvent event, int64_t timestamp_ns);

    // T-121: Voice completion callback for completion-driven queue draining
    void on_voice_completed(const std::string& event_id);

    // Advance/drain bounded QUEUE events that are ready to play.
    void flush_queue(int64_t now_timestamp_ns = 0);

    // Settings preview surface.
    void preview_sound(const std::string& id);
    // Preview a specific random pool or event assignment (honours master mute, does not require runtime enabled).
    void preview_event_config(const EventSoundConfig& ec);
    void stop_preview();
    bool is_playing_preview() const;

    // T-107 / T-120: stop EVERY live voice, queued playback, coalesced state,
    // runtime events and explicit preview alike. Invalidates stale state and
    // never permanently mutes future playback.
    void stop_all_sounds();

    // Clear cooldown/queue/pool history after system sleep/resume so no burst fires.
    void handle_system_resume();

    // Observability (tests).
    int active_voice_count() const;
    // T-122: exact playing runtime voices (preview excluded), and active
    // preview voices as its separate capacity question.
    int active_playing_voice_count() const;
    int active_preview_voice_count() const;
    int queued_event_count() const { return static_cast<int>(pending_queue_.size()); }
    uint64_t coalesced_drop_count() const { return coalesced_drops_; }
    uint64_t cooldown_drop_count() const { return cooldown_drops_; }
    uint64_t priority_drop_count() const { return priority_drops_; }
    bool audio_audible() const;

    // Deterministic RNG seed override for random pool unit tests.
    void set_random_seed_for_tests(uint32_t seed) { rng_state_ = (seed == 0) ? 1u : seed; }

    // Ownership seam (tests): a regression must be able to prove that an
    // owned production player is never left reachable through the cached
    // pointer after shutdown() or a failed initialize().
    ISoundPlayer* player_for_tests() const { return player_; }
    bool owns_player_for_tests() const { return owned_player_ != nullptr; }

private:
    struct ResolvedPoolItem {
        std::string sound_id;
        std::string path;
        float weight = 1.0f;
        float gain_db = 0.0f;
    };

    struct EventRuntimeSpec {
        std::string canonical_id;
        std::string player_key; // legacy key ("left_down") for the 8 mouse events, else canonical_id
        SoundEventCategory category = SoundEventCategory::Typing;
        bool enabled = true;
        bool use_pool = false;
        bool no_immediate_repeat = true;
        std::string single_path;
        std::vector<ResolvedPoolItem> pool;
        float gain_db = 0.0f;
        PlaybackPolicy policy = PlaybackPolicy::Override;
        InterruptScope scope = InterruptScope::SameEvent;
        int cooldown_ms = 0;
        int min_retrigger_ms = 0;
        int priority = 50;
        int polyphony_allowance = 4;
        int64_t generation = 0;
    };

    void refresh_cache();
    bool preview_admissible() const;
    std::string select_sound_from_spec(const EventRuntimeSpec& spec, double& out_linear_gain);
    void apply_interrupt_scope(const EventRuntimeSpec& spec);
    uint32_t next_random_u32();
    double next_random_unit();

    // T-123: one coherent queue scheduler. The queue is a per-lane FIFO behind
    // one global barrier: an entry waits on ITS lane, a busy lane elsewhere can
    // never hold an independent idle lane hostage, and a pending Global entry
    // gates every other lane until the runtime lane falls silent.
    bool queue_has_lane(PlaybackLane lane, const std::string& raw_key) const;
    // Release every lane priority whose owning lane has fallen silent. A
    // priority dies with its lane, so an unrelated completion releases nothing.
    void release_stale_lane_priorities();
    // The safe boundary: release lane priorities, then drain whatever the lane
    // state now admits. Never called while an interrupt transaction is open.
    void drain_deferred_completions();

    // T-123: interruption transactions. Qt emits playingChanged synchronously
    // from inside QSoundEffect::stop(), so a stop delivers its completion
    // callbacks BEFORE the interrupting operation has finished. An open
    // transaction defers the queue drain those callbacks would run: the
    // transaction owns what is playing until it completes, and the drain
    // resumes at its safe boundary -- no voice starts inside an unfinished
    // interrupt, and a callback can never re-enter a drain that holds queue
    // state.
    class InterruptTransaction {
    public:
        // drains=true: an interrupt transaction -- deferred completions are
        // processed at its safe boundary. drains=false: a pure reentrancy
        // fence (a drain in progress) that only blocks nested callbacks.
        explicit InterruptTransaction(SoundEngine& engine, bool drains);
        ~InterruptTransaction();
        InterruptTransaction(const InterruptTransaction&) = delete;
        InterruptTransaction& operator=(const InterruptTransaction&) = delete;

    private:
        SoundEngine& engine_;
        bool drains_;
    };

    // T-122: priority bookkeeping tracks the OWNING lane, never an unrelated
    // active voice count. An entry dies with its lane, and only a lane whose
    // own voices are all silent may release its priority.
    struct LanePriority {
        int priority = 50;
        PlaybackLane lane = PlaybackLane::SameEvent;
        SoundEventCategory category = SoundEventCategory::Custom;
        std::string key; // raw lane key this priority owns
    };
    // Lane identity for an interruption scope: a distinct map key per lane.
    // A category id or a canonical event id alone could collide across lanes,
    // so the key is namespaced by lane kind.
    static std::string lane_key(PlaybackLane lane, const std::string& key);
    // Is this lane currently audible? Derived from the live player at call
    // time, so an unrelated completion cannot make a busy lane look idle.
    bool lane_busy(PlaybackLane lane, const std::string& key,
                   SoundEventCategory category) const;
    // One playback admission point: every play path resolves through here and
    // only a Started result advances scheduler state.
    PlayAdmission start_playback(const std::string& player_key, const std::string& path,
                                 double linear_gain, bool allow_stack, int max_event_polyphony);

    ISoundPlayer* player_ = nullptr;
    std::unique_ptr<ISoundPlayer> owned_player_;
    void release_owned_player();
    AudioConfig config_;
    bool master_enabled_ = false;
    bool initialized_ = false;

    // Resolved runtime specs keyed by both canonical_id and legacy mouse key
    std::unordered_map<std::string, EventRuntimeSpec> specs_;
    int64_t generation_ = 0;

    // Per-event timing and last-pool selection state
    std::unordered_map<std::string, int64_t> last_trigger_ns_;
    std::unordered_map<std::string, std::size_t> last_pool_index_;
    // T-122: priority ownership per playback lane (namespaced by lane kind).
    std::unordered_map<std::string, LanePriority> active_priority_by_lane_;
    std::deque<QueuedEvent> pending_queue_;

    // T-123: open interrupt transaction / drain depth. A completion callback
    // fired while this is positive is deferred instead of draining.
    int interrupt_depth_ = 0;

    uint64_t coalesced_drops_ = 0;
    uint64_t cooldown_drops_ = 0;
    uint64_t priority_drops_ = 0;
    uint32_t rng_state_ = 0xA341316Cu;

    std::string preview_id_;
    std::string preview_path_;
};

} // namespace ptd
