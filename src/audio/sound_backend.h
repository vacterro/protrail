#pragma once

#include <QSoundEffect>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ptd {

// Bounded decoded-cache ceiling: the eight configured events plus at most one
// preview selection. Nothing else is ever decoded or kept. It lives on the
// backend header because the voice pool is sized from it.
constexpr int kMaxCacheSounds = 9;

// T-120: event-voice ceiling shared by the bounded production pool and the test
// double. The documented budget is eight concurrent event voices plus one
// preview voice, so the number exists once at namespace scope and neither class
// can drift from it. T-129: this is the ONE ceiling every policy obeys -- a
// separate "stack polyphony" constant that described sixteen parallel event
// voices existed only to be enforced nowhere and was removed with it.
constexpr int kMaxEventVoices = kMaxCacheSounds - 1;

// T-122: one playback admission outcome. A bare bool cannot tell a capacity
// refusal from an invalid source, and scheduler state must only advance for a
// voice that actually started, so the refusal classes are named once here and
// every play decision resolves to exactly one of them.
enum class PlayAdmission {
    Started,
    RefusedCapacity,
    InvalidSource,
    MutedOrDisabled,
    BackendUnavailable,
};

// T-80 / T-120: the playback backend contract. The production implementation is a Qt
// Multimedia QSoundEffect pool; tests inject FakePlayer and observe the exact
// event id, path and voice lifetime. The pool is BOUNDED: rapid preview
// changes reuse one voice slot and cancel the previous sound instead of
// accumulating voices.
class ISoundPlayer {
public:
    virtual ~ISoundPlayer() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void set_volume(double volume_0_1) = 0;
    // One playback attempt. Returns false when no voice was available or the
    // backend refused the request; that is not an error, the caller simply
    // has no audible voice for this attempt.
    virtual bool play(const std::string& event_id, const std::string& absolute_path,
                      bool is_preview) = 0;
    // T-120: Extended playback with per-voice gain multiplier (0..1) and stack policy flag.
    // Defaults to calling play() after applying gain so legacy subclasses stay compatible.
    virtual bool play_ex(const std::string& event_id, const std::string& absolute_path,
                         bool is_preview, double voice_gain_0_1, bool allow_stack,
                         int max_event_polyphony) {
        (void)voice_gain_0_1;
        (void)allow_stack;
        (void)max_event_polyphony;
        return play(event_id, absolute_path, is_preview);
    }
    virtual void stop_preview() = 0;
    virtual void stop_event(const std::string& event_id) = 0;
    virtual void stop_events() = 0;
    virtual void stop_all() = 0;
    virtual int active_voice_count() const = 0;
    virtual int active_event_voice_count(const std::string& event_id) const {
        (void)event_id;
        return 0;
    }
    // T-122: exact count of non-preview voices currently audible. Preview
    // capacity is deliberately separate from runtime capacity: an allocated
    // but idle preview slot must never make a live runtime voice invisible,
    // so lane accounting reads this instead of subtracting preview slot
    // counts from the total.
    virtual int active_playing_voice_count() const = 0;
    virtual bool is_playing_preview() const = 0;
    // Pool observability for tests: how many voice slots exist and how many of
    // them are preview slots. A preview must never need more than one slot and
    // the pool must never grow unbounded.
    virtual int voice_pool_size() const = 0;
    virtual int preview_voice_count() const = 0;
    // Backend state of the most recently assigned voice: READY / LOADING /
    // ERROR / NONE. A missing multimedia backend plugin surfaces as ERROR.
    virtual std::string backend_status() const = 0;

    // T-121: Voice completion callback for completion-driven queue draining and
    // automatic release of group priority locks.
    using VoiceCompletionCallback = std::function<void(const std::string& event_id)>;
    virtual void set_completion_callback(VoiceCompletionCallback cb) { (void)cb; }
};

// Production backend: a bounded pool of QSoundEffect voices. GUI-thread only
// (Qt Multimedia ownership); the engine guarantees it is never called from
// the raw input callback thread.
//
// The pool reserves ONE slot for preview playback: catalog browsing replaces
// the preview source in that slot and can never consume an event voice. Event
// voices are bounded to kMaxEventVoices (with Stack policy supporting bounded
// polyphony up to the per-event allowance within the pool budget). Voice liveness
// follows QSoundEffect's own playingChanged/statusChanged signals, so a
// naturally finished sound releases its slot without any timer.
class QtSoundPlayer : public ISoundPlayer {
public:
    QtSoundPlayer();
    ~QtSoundPlayer() override;

    bool initialize() override;
    void shutdown() override;
    void set_muted(bool muted) override;
    void set_volume(double volume_0_1) override;
    bool play(const std::string& event_id, const std::string& absolute_path,
              bool is_preview) override;
    bool play_ex(const std::string& event_id, const std::string& absolute_path,
                 bool is_preview, double voice_gain_0_1, bool allow_stack,
                 int max_event_polyphony) override;
    void stop_preview() override;
    void stop_event(const std::string& event_id) override;
    void stop_events() override;
    void stop_all() override;
    int active_voice_count() const override;
    int active_event_voice_count(const std::string& event_id) const override;
    int active_playing_voice_count() const override;
    bool is_playing_preview() const override;
    int voice_pool_size() const override;
    int preview_voice_count() const override;
    std::string backend_status() const override;
    void set_completion_callback(VoiceCompletionCallback cb) override;

    // Bounded number of QSoundEffect voices the pool may create. One slot is
    // reserved for the single preview voice, so rapid catalog browsing can
    // never starve the eight event voices.
    static constexpr int kMaxVoices = kMaxCacheSounds;
    static constexpr int kMaxEventVoices = ptd::kMaxEventVoices;
    static_assert(kMaxVoices == kMaxCacheSounds, "pool budget must track the cache ceiling");
    static_assert(kMaxEventVoices == kMaxCacheSounds - 1, "event budget reserves one preview voice");

    // T-84: the PURE event-budget decision -- may the pool admit another event
    // slot given the live event-slot count and the total pool size? Static and
    // device-independent so the order-equivalence (eight event slots whether
    // or not the catalog was previewed first) is provable without a device.
    // acquire_event_voice() is the only production caller.
    static bool event_voice_available(int event_slots, int total_voices);

    // T-103: live-instance count. Two player pointers cannot prove a fresh
    // allocation -- the allocator may hand the same block back -- so a test
    // that must show a rebuild counts constructions and destructions instead
    // of comparing two addresses.
    static int live_instance_count() { return live_instances_; }

private:
    struct Voice {
        std::unique_ptr<QSoundEffect> effect;
        std::string src;  // empty until a source was assigned
        std::string event_id;
        bool is_preview = false;
        bool playing = false;  // fast hint; QSoundEffect::isPlaying() stays authoritative
        double voice_gain = 1.0; // per-event linear gain multiplier (0..1)
        int64_t handle = 0;
        bool signals_bound = false;
    };

    // The ONE preview slot, created on first preview use and reused afterwards.
    Voice* preview_voice();
    Voice* find_event_voice(const std::string& event_id);
    Voice* acquire_event_voice();
    void bind_signals(Voice& v);
    void apply_mix(Voice& v);

    std::vector<std::unique_ptr<Voice>> voices_;
    static int live_instances_;
    bool muted_ = false;
    double volume_ = 1.0;
    bool initialized_ = false;
    int64_t next_handle_ = 1;
    VoiceCompletionCallback completion_callback_;
};

// Test double: records every call and every voice. No audio hardware, no Qt
// Multimedia linkage required by its consumers.
//
// Behavioral contract: the fake REFUSES exactly what production
// QtSoundPlayer::play() refuses (not initialized, muted, empty path) and its
// preview state is always DERIVED from the playback records, so
// is_playing_preview() can never disagree with the records the tests read.
class FakePlayer : public ISoundPlayer {
public:
    struct PlayRecord {
        std::string event_id;
        std::string path;
        bool is_preview = false;
        int64_t handle = 0;
        double voice_gain = 1.0;
    };

    bool initialize() override {
        if (fail_initialize) {
            initialized = false;
            return false;
        }
        initialized = true;
        shutdown_called = false;
        return true;
    }
    void shutdown() override {
        initialized = false;
        playing.clear();
        shutdown_called = true;
    }
    void set_muted(bool on) override {
        muted_changes.push_back(on);
        muted = on;
        // Production set_muted(true) stops every live effect. The fake
        // mirrors that exactly: mute ends ALL playback records, including a
        // preview, so no stale preview state survives the mute.
        if (on) playing.clear();
    }
    void set_volume(double volume_0_1) override {
        volume_changes.push_back(volume_0_1);
        volume = volume_0_1;
    }
    bool play(const std::string& event_id, const std::string& absolute_path,
              bool is_preview) override {
        return play_ex(event_id, absolute_path, is_preview, 1.0, /*allow_stack=*/false, 1);
    }
    bool play_ex(const std::string& event_id, const std::string& absolute_path,
                 bool is_preview, double voice_gain_0_1, bool allow_stack,
                 int max_event_polyphony) override {
        // Refusals are decided BEFORE any state is touched: a refused
        // playback must never leave a record, a counter or a stale preview
        // state behind (same order as production).
        if (fail_next_play) {
            fail_next_play = false;
            refused_plays++;
            return false;
        }
        if (!initialized || muted || absolute_path.empty()) {
            refused_plays++;
            return false;
        }
        // Bounded: a new preview cancels the previous one instead of queueing.
        if (is_preview) {
            for (auto it = playing.begin(); it != playing.end();) {
                it = it->is_preview ? playing.erase(it) : ++it;
            }
        } else if (!allow_stack) {
            // T-129: Override / single-lane for this event_id. Production
            // QtSoundPlayer::play_ex replaces the voice of the SAME event id
            // with one bounded voice and leaves every other voice alone
            // (find_event_voice for a match, else acquire one bounded slot),
            // and a full pool refuses the attempt instead of silently
            // displacing a foreign voice. The fake models that exactly:
            // the matching event's voice is replaced, and no event-voice
            // budget or foreign voice is touched.
            int non_preview = 0;
            for (const auto& r : playing) {
                if (!r.is_preview) ++non_preview;
            }
            bool replaced = false;
            for (auto it = playing.begin(); it != playing.end();) {
                if (!it->is_preview && it->event_id == event_id) {
                    it = playing.erase(it);
                    replaced = true;
                } else {
                    ++it;
                }
            }
            // Override admits exactly one voice for this event id, so a
            // repeat never crosses the pool's event budget: the displaced
            // voice freed its own slot. A pool already holding the full
            // event budget cannot admit another DISTINCT event.
            if (!replaced && non_preview >= kMaxEventVoices) {
                refused_plays++;
                return false;
            }
        } else {
            // T-129: Stack mode. The per-event polyphony cap and the global
            // event-voice ceiling are the SAME two budgets production
            // enforces: the per-event cap from the caller's allowance, and
            // the pool's event budget that acquire_event_voice gates on
            // (event_voice_available refuses the ninth event slot).
            const int cap = (max_event_polyphony > 0) ? max_event_polyphony : kMaxEventVoices;
            int ev_count = 0;
            int total_ev_count = 0;
            for (const auto& r : playing) {
                if (!r.is_preview) {
                    ++total_ev_count;
                    if (r.event_id == event_id) ++ev_count;
                }
            }
            if (ev_count >= cap || total_ev_count >= kMaxEventVoices) {
                refused_plays++;
                return false;
            }
        }
        const int64_t handle = next_handle_++;
        playing.push_back({event_id, absolute_path, is_preview, handle, voice_gain_0_1});
        total_plays++;
        if (!is_preview) runtime_plays++;
        last_event = event_id;
        last_path = absolute_path;
        last_is_preview = is_preview;
        last_voice_gain = voice_gain_0_1;
        return true;
    }
    void stop_preview() override {
        int removed = 0;
        for (auto it = playing.begin(); it != playing.end();) {
            it = it->is_preview ? (++removed, playing.erase(it)) : ++it;
        }
        preview_stops++;
        // Deliver completions only after `playing` is mutated: a callback that
        // starts a voice must never invalidate a live iterator here.
        for (int i = 0; i < removed; ++i) notify_sync_completion("@preview");
    }
    void stop_events() override {
        std::vector<std::string> stopped_ids;
        for (auto it = playing.begin(); it != playing.end();) {
            if (!it->is_preview) {
                stopped_ids.push_back(it->event_id);
                it = playing.erase(it);
            } else {
                ++it;
            }
        }
        event_stops++;
        for (const auto& id : stopped_ids) notify_sync_completion(id);
    }
    void stop_event(const std::string& event_id) override {
        // T-129: a preview voice is never stopped by an event id. Production
        // QtSoundPlayer::stop_event skips every preview voice, so an event
        // id that also names the preview key cannot silence a preview.
        int removed = 0;
        for (auto it = playing.begin(); it != playing.end();) {
            if (!it->is_preview && it->event_id == event_id) {
                it = playing.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        event_stops++;
        last_stopped_event = event_id;
        for (int i = 0; i < removed; ++i) notify_sync_completion(event_id);
    }
    void stop_all() override {
        std::vector<std::string> stopped_ids;
        stopped_ids.reserve(playing.size());
        for (const auto& r : playing) stopped_ids.push_back(r.event_id);
        playing.clear();
        all_stops++;
        for (const auto& id : stopped_ids) notify_sync_completion(id);
    }
    int active_voice_count() const override {
        return static_cast<int>(playing.size());
    }
    int active_event_voice_count(const std::string& event_id) const override {
        int n = 0;
        for (const auto& r : playing) {
            if (!r.is_preview && r.event_id == event_id) ++n;
        }
        return n;
    }
    // T-122: a record exists only while its voice plays, and preview records
    // are excluded here, so this is the exact runtime voice count -- the
    // allocated-slot arithmetic it replaces could report zero for a lane that
    // was still audible.
    int active_playing_voice_count() const override {
        int n = 0;
        for (const auto& r : playing) {
            if (!r.is_preview) ++n;
        }
        return n;
    }
    // T-122: models the production preview slot's LIFETIME. QtSoundPlayer
    // keeps its one preview Voice allocated after playback ends, so a test can
    // park an idle allocated slot and prove it never counts against a runtime
    // lane. Zero unless a test explicitly parks one; nothing in production
    // sets it.
    int idle_allocated_preview_slots = 0;
    // Derived, never a stored flag: once every preview record is removed (by
    // stop, mute, shutdown or replacement), no preview can be reported active.
    bool is_playing_preview() const override {
        for (const auto& r : playing) {
            if (r.is_preview) return true;
        }
        return false;
    }
    int voice_pool_size() const override {
        // One slot per live record; the engine's replay path replaces records
        // instead of queueing, so this never grows unbounded in the fake.
        return static_cast<int>(playing.size());
    }
    int preview_voice_count() const override {
        // Allocation semantics, matching QtSoundPlayer::preview_voice_count():
        // a slot that was ever used stays allocated even after it goes idle.
        int n = 0;
        for (const auto& r : playing) {
            if (r.is_preview) ++n;
        }
        return n + idle_allocated_preview_slots;
    }
    std::string backend_status() const override { return "READY"; }

    void set_completion_callback(VoiceCompletionCallback cb) override {
        completion_callback = std::move(cb);
    }
    // T-123: models PRODUCTION callback timing. QSoundEffect emits
    // playingChanged synchronously from inside stop(), so a stop delivers its
    // completions INSIDE the stop call -- the timing an interrupt-reentrancy
    // regression must be proven against. Off by default, so every existing
    // suite keeps the old, stop-without-callback timing.
    bool sync_completion_on_stop = false;
    void notify_sync_completion(const std::string& event_id) {
        if (sync_completion_on_stop && completion_callback) {
            completion_callback(event_id);
        }
    }
    void notify_completion(const std::string& event_id) {
        // Drop the oldest non-preview record for this event_id (or any if empty)
        for (auto it = playing.begin(); it != playing.end(); ++it) {
            if (!it->is_preview && (event_id.empty() || it->event_id == event_id)) {
                playing.erase(it);
                break;
            }
        }
        if (completion_callback) {
            completion_callback(event_id);
        }
    }

    bool initialized = false;
    bool shutdown_called = false;
    bool muted = false;
    double volume = 1.0;
    std::vector<PlayRecord> playing;
    std::vector<bool> muted_changes;
    std::vector<double> volume_changes;
    int total_plays = 0;
    // T-122: cumulative non-preview play attempts that a voice accepted, so a
    // test can observe the runtime lane without an audition inflating it.
    int runtime_plays = 0;
    int refused_plays = 0;
    int preview_stops = 0;
    int event_stops = 0;
    int all_stops = 0;
    bool fail_next_play = false;
    // Models a backend that cannot start (e.g. no usable multimedia backend).
    bool fail_initialize = false;
    std::string last_event;
    std::string last_path;
    std::string last_stopped_event;
    bool last_is_preview = false;
    double last_voice_gain = 1.0;
    int64_t next_handle_ = 1;
    VoiceCompletionCallback completion_callback;
};

} // namespace ptd
