#include "sound_engine.h"
#include "sound_backend.h"

#include "../core/log.h"
#include "../platform/mouse_input.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace ptd {
namespace {

constexpr const char* kPreviewKey = "@preview";
constexpr int64_t kNsPerMs = 1'000'000LL;
constexpr int64_t kDefaultCoalesceWindowNs = 18'000'000LL; // 18 ms default coalesce window

QString executable_directory_qt() {
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (len == 0 || len >= MAX_PATH * 2) return {};
    const std::filesystem::path exe =
        std::wstring(buf, buf + static_cast<std::size_t>(len));
    return QString::fromStdWString(exe.parent_path().wstring());
}

// Test/deployment override of the sound root; empty means "exe-relative".
QString g_sound_directory_override;
bool g_has_sound_directory_override = false;

} // namespace

// ---- SoundCatalog ----

QString SoundCatalog::sound_directory() {
    if (g_has_sound_directory_override) return g_sound_directory_override;
    static const QString cached =
        QDir(executable_directory_qt()).filePath(QString::fromLatin1(kSoundDirectoryName));
    return cached;
}

void SoundCatalog::set_sound_directory_for_tests(const QString& dir) {
    g_sound_directory_override = dir;
    g_has_sound_directory_override = !dir.isEmpty();
}

void SoundCatalog::clear_sound_directory_for_tests() {
    set_sound_directory_for_tests(QString());
}

QStringList SoundCatalog::sound_ids() {
    const QDir dir(SoundCatalog::sound_directory());
    if (!dir.exists()) return {};
    const QFileInfoList entries =
        dir.entryInfoList(QStringList{QStringLiteral("*.wav")},
                          QDir::Files, QDir::Name | QDir::IgnoreCase);
    QStringList ids;
    ids.reserve(entries.size());
    for (const QFileInfo& fi : entries) {
        const std::string id = fi.fileName().toStdString();
        if (!ptd::sanitize_sound_id(id).empty()) ids.append(fi.fileName());
    }
    return ids;
}

bool SoundCatalog::sound_exists(const std::string& id) {
    const std::string clean = ptd::sanitize_sound_id(id);
    if (clean.empty()) return false;
    const QFileInfo fi(SoundCatalog::resolve_sound_path(clean));
    return fi.exists() && fi.isFile();
}

QString SoundCatalog::resolve_sound_path(const std::string& id) {
    const std::string clean = ptd::sanitize_sound_id(id);
    if (clean.empty()) return {};
    const QDir dir(SoundCatalog::sound_directory());
    const QString path = dir.filePath(QString::fromStdString(clean));
    if (!path.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive)) return {};
    // Defense in depth: the name must still resolve INSIDE the sound directory
    // after full canonicalization. canonicalFilePath() follows reparse points
    // and symlinks, so a link planted in the sound directory that targets an
    // outside file resolves outside the root and is rejected -- a lexical
    // cleanPath() check alone cannot see that.
    const QString canonical_root = QFileInfo(dir.absolutePath()).canonicalFilePath();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical_root.isEmpty() || canonical.isEmpty()) return {};
    if (!canonical.startsWith(canonical_root + QLatin1Char('/'))) return {};
    // An absent catalog entry resolves to nothing: a configured sound whose
    // file was never staged (or was removed) is silent, never a missing-file
    // playback attempt.
    if (!QFileInfo(canonical).isFile()) return {};
    return canonical;
}

// ---- SoundEngine ----

SoundEngine::SoundEngine() = default;

SoundEngine::~SoundEngine() {
    shutdown();
}

uint32_t SoundEngine::next_random_u32() {
    uint32_t x = rng_state_;
    if (x == 0) x = 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return x;
}

double SoundEngine::next_random_unit() {
    return static_cast<double>(next_random_u32() & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
}

void SoundEngine::set_player(ISoundPlayer* player) {
    if (player == player_) return;
    if (initialized_) shutdown();
    release_owned_player();
    player_ = player;
    if (player_) {
        player_->set_completion_callback([this](const std::string& ev_id) {
            on_voice_completed(ev_id);
        });
    }
}

void SoundEngine::release_owned_player() {
    if (!owned_player_) return;
    if (player_ == owned_player_.get()) player_ = nullptr;
    owned_player_.reset();
}

bool SoundEngine::initialize() {
    if (initialized_) return true;
    if (!player_) {
        owned_player_ = std::make_unique<QtSoundPlayer>();
        player_ = owned_player_.get();
    }
    player_->set_muted(config_.muted);
    player_->set_volume(config_.volume);
    player_->set_completion_callback([this](const std::string& ev_id) {
        on_voice_completed(ev_id);
    });
    initialized_ = player_->initialize();
    if (!initialized_) {
        ptd::log_write(ptd::LogLevel::Error, "sound: player initialization failed");
        release_owned_player();
        refresh_cache();
        return false;
    }
    refresh_cache();
    return true;
}

void SoundEngine::shutdown() {
    if (initialized_ && player_) {
        // Unbind before stopping: a completion delivered while the engine is
        // tearing itself down must never re-enter a half-destroyed scheduler.
        player_->set_completion_callback({});
        player_->stop_all();
        player_->shutdown();
    }
    specs_.clear();
    pending_queue_.clear();
    last_trigger_ns_.clear();
    last_pool_index_.clear();
    active_priority_by_lane_.clear();
    preview_id_.clear();
    preview_path_.clear();
    initialized_ = false;
    release_owned_player();
}

void SoundEngine::set_master_enabled(bool on) {
    if (master_enabled_ == on) return;
    master_enabled_ = on;
    if (!on && initialized_) {
        pending_queue_.clear();
        active_priority_by_lane_.clear();
        const InterruptTransaction txn(*this, /*drains=*/true);
        player_->stop_events();
    }
}

void SoundEngine::apply_config(const AudioConfig& config) {
    config_ = AudioConfig::validated(config);
    // refresh_cache() advances the configuration generation; queued entries
    // carry the generation they were resolved under, so anything pending from
    // before this change is expired by identity and is dropped at the next
    // drain instead of being played with an obsolete assignment.
    refresh_cache();
    if (initialized_) {
        player_->set_muted(config_.muted);
        player_->set_volume(config_.volume);
        const InterruptTransaction txn(*this, /*drains=*/true);
        if (config_.muted) {
            pending_queue_.clear();
            active_priority_by_lane_.clear();
            player_->stop_all();
        } else if (!config_.enabled || !master_enabled_) {
            pending_queue_.clear();
            active_priority_by_lane_.clear();
            player_->stop_events();
        }
    }
}

bool SoundEngine::audio_audible() const {
    return initialized_ && master_enabled_ && config_.enabled && !config_.muted;
}

bool SoundEngine::preview_admissible() const {
    return initialized_ && !config_.muted;
}

void SoundEngine::refresh_cache() {
    const int64_t generation = ++generation_;
    specs_.clear();

    auto build_spec = [&](const std::string& cid, const EventSoundConfig& ec,
                          const SoundEventDescriptor* desc) -> std::optional<EventRuntimeSpec> {
        if (!ec.enabled) return std::nullopt;
        EventRuntimeSpec spec{};
        spec.canonical_id = cid;
        const int legacy_idx = legacy_mouse_event_index(cid);
        spec.player_key = (legacy_idx >= 0) ? std::string(audio_event_name(legacy_idx)) : cid;
        spec.category = desc ? desc->category : SoundEventCategory::Custom;
        spec.enabled = ec.enabled;
        spec.use_pool = ec.use_pool;
        spec.no_immediate_repeat = ec.no_immediate_repeat;
        spec.gain_db = ec.gain_db;

        // Resolve policy & scope inheritance
        if (ec.policy != PlaybackPolicy::Inherit) {
            spec.policy = ec.policy;
        } else if (desc) {
            spec.policy = desc->default_policy;
        } else {
            spec.policy = PlaybackPolicy::Override;
        }

        if (ec.interrupt_scope != InterruptScope::Inherit) {
            spec.scope = ec.interrupt_scope;
        } else if (desc) {
            spec.scope = desc->default_scope;
        } else {
            spec.scope = InterruptScope::SameEvent;
        }

        spec.cooldown_ms = ec.cooldown_ms;
        if (desc && desc->category == SoundEventCategory::Mouse && (cid.find("scroll") != std::string::npos)) {
            const int scroll_rate_period_ms = (config_.scroll_max_rate_hz > 0) ? (1000 / config_.scroll_max_rate_hz) : 0;
            const int scroll_cd = std::max(config_.scroll_cooldown_ms, scroll_rate_period_ms);
            spec.cooldown_ms = std::max(ec.cooldown_ms, scroll_cd);
        }
        spec.min_retrigger_ms = ec.min_retrigger_ms;
        spec.priority = ec.priority;
        spec.polyphony_allowance = ec.polyphony_allowance;
        spec.generation = generation;

        if (ec.use_pool) {
            for (const auto& pe : ec.pool) {
                if (!pe.enabled) continue;
                const std::string clean = sanitize_sound_id(pe.sound_id);
                if (clean.empty()) continue;
                const QString path = SoundCatalog::resolve_sound_path(clean);
                if (path.isEmpty()) continue; // safely ignore missing WAVs in pool
                ResolvedPoolItem item{};
                item.sound_id = clean;
                item.path = path.toStdString();
                item.weight = std::clamp(pe.weight, 0.01f, 100.0f);
                item.gain_db = pe.gain_db;
                spec.pool.push_back(std::move(item));
            }
            if (spec.pool.empty()) {
                // Fallback to single sound_id if pool had no valid existing entries
                const std::string clean = sanitize_sound_id(ec.sound_id);
                if (!clean.empty()) {
                    const QString path = SoundCatalog::resolve_sound_path(clean);
                    if (!path.isEmpty()) spec.single_path = path.toStdString();
                }
            }
        } else {
            const std::string clean = sanitize_sound_id(ec.sound_id);
            if (!clean.empty()) {
                const QString path = SoundCatalog::resolve_sound_path(clean);
                if (!path.isEmpty()) spec.single_path = path.toStdString();
            }
        }

        if (spec.pool.empty() && spec.single_path.empty()) {
            return std::nullopt;
        }
        return spec;
    };

    // 1. Legacy 8 mouse slots
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        const std::string legacy_key = ptd::audio_event_name(i);
        const std::string cid = canonicalize_sound_event_id(legacy_key);
        const EventSoundConfig ec = config_.effective_event_config(cid);
        const SoundEventDescriptor* desc = find_sound_event_descriptor(cid);
        if (auto spec = build_spec(cid, ec, desc)) {
            specs_[cid] = *spec;
            specs_[legacy_key] = std::move(*spec);
        }
    }

    // 2. All configured canonical & custom events in event_configs
    for (const auto& [cid, raw_ec] : config_.event_configs) {
        const EventSoundConfig ec = config_.effective_event_config(cid);
        const SoundEventDescriptor* desc = find_sound_event_descriptor(cid);
        if (auto spec = build_spec(cid, ec, desc)) {
            specs_[cid] = *spec;
            const int legacy_idx = legacy_mouse_event_index(cid);
            if (legacy_idx >= 0) {
                specs_[audio_event_name(legacy_idx)] = *spec;
            }
        }
    }
}

std::string SoundEngine::select_sound_from_spec(const EventRuntimeSpec& spec, double& out_linear_gain) {
    float entry_db = 0.0f;
    std::string chosen_path = spec.single_path;

    if (spec.use_pool && !spec.pool.empty()) {
        const std::size_t n = spec.pool.size();
        if (n == 1) {
            chosen_path = spec.pool[0].path;
            entry_db = spec.pool[0].gain_db;
            last_pool_index_[spec.canonical_id] = 0;
        } else {
            auto last_it = last_pool_index_.find(spec.canonical_id);
            const bool has_last = (last_it != last_pool_index_.end() && last_it->second < n);
            const std::size_t avoid_idx = has_last ? last_it->second : n;

            double weight_sum = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                if (spec.no_immediate_repeat && has_last && i == avoid_idx) continue;
                weight_sum += static_cast<double>(spec.pool[i].weight);
            }
            if (weight_sum <= 0.0) {
                for (std::size_t i = 0; i < n; ++i) {
                    weight_sum += static_cast<double>(spec.pool[i].weight);
                }
            }

            const double target = next_random_unit() * weight_sum;
            double accum = 0.0;
            std::size_t picked = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (spec.no_immediate_repeat && has_last && i == avoid_idx) continue;
                accum += static_cast<double>(spec.pool[i].weight);
                picked = i;
                if (target <= accum) break;
            }
            chosen_path = spec.pool[picked].path;
            entry_db = spec.pool[picked].gain_db;
            last_pool_index_[spec.canonical_id] = picked;
        }
    }

    // Gain contract: event gain and pool-entry gain combine additively in dB,
    // then attenuate the MASTER volume (1.0 here; the master multiplier is
    // applied by the backend mix). The ceiling is the platform maximum output
    // level, so a positive dB can never amplify past it.
    const float total_db = std::clamp(spec.gain_db + entry_db,
                                      EventSoundConfig::kMinGainDb,
                                      EventSoundConfig::kMaxGainDb);
    out_linear_gain = db_to_linear_gain(total_db, 1.0);
    return chosen_path;
}

std::string SoundEngine::lane_key(PlaybackLane lane, const std::string& key) {
    switch (lane) {
    case PlaybackLane::SameEvent:
        return "ev:" + key;
    case PlaybackLane::SameGroup:
        return "grp:" + key;
    case PlaybackLane::GlobalLane:
        break;
    }
    return "@global";
}

bool SoundEngine::lane_busy(PlaybackLane lane, const std::string& key,
                            SoundEventCategory category) const {
    if (!player_) return false;
    switch (lane) {
    case PlaybackLane::SameEvent:
        return player_->active_event_voice_count(key) > 0;
    case PlaybackLane::SameGroup: {
        // Any live voice of an event in this category keeps the group lane
        // busy. specs_ stores canonical entries and their legacy aliases; the
        // canonical_id == key test visits each real event exactly once.
        for (const auto& [k, other] : specs_) {
            if (other.canonical_id != k || other.category != category) continue;
            if (player_->active_event_voice_count(other.player_key) > 0) return true;
        }
        return false;
    }
    case PlaybackLane::GlobalLane:
        // Exact runtime voice count: an allocated-but-idle preview slot
        // cannot make a live runtime lane look free.
        return player_->active_playing_voice_count() > 0;
    }
    return false;
}

// T-123: one coherent queue scheduler. Every queued entry waits on exactly one
// lane (resolved at enqueue time), so the queue is a per-lane FIFO: a busy
// lane blocks only its own entries, and a pending Global-lane entry is the one
// barrier that gates every other lane until the runtime lane falls silent.
bool SoundEngine::queue_has_lane(PlaybackLane lane, const std::string& raw_key) const {
    for (const QueuedEvent& entry : pending_queue_) {
        if (entry.lane == lane && entry.lane_key == raw_key) return true;
    }
    return false;
}

void SoundEngine::release_stale_lane_priorities() {
    for (auto it = active_priority_by_lane_.begin(); it != active_priority_by_lane_.end(); ) {
        const LanePriority& owner = it->second;
        if (lane_busy(owner.lane, owner.key, owner.category)) {
            ++it;
            continue;
        }
        it = active_priority_by_lane_.erase(it);
    }
}

void SoundEngine::drain_deferred_completions() {
    if (!initialized_ || !player_) return;
    release_stale_lane_priorities();
    if (!pending_queue_.empty()) {
        flush_queue(0);
    }
}

SoundEngine::InterruptTransaction::InterruptTransaction(SoundEngine& engine, bool drains)
    : engine_(engine), drains_(drains) {
    ++engine_.interrupt_depth_;
}

SoundEngine::InterruptTransaction::~InterruptTransaction() {
    if (--engine_.interrupt_depth_ == 0 && drains_) {
        // Safe boundary: the interrupt is complete, so deferred completions
        // are processed once against the post-interrupt lane state.
        engine_.drain_deferred_completions();
    }
}

PlayAdmission SoundEngine::start_playback(const std::string& player_key, const std::string& path,
                                          double linear_gain, bool allow_stack,
                                          int max_event_polyphony) {
    if (!initialized_ || !player_) return PlayAdmission::BackendUnavailable;
    if (!audio_audible()) return PlayAdmission::MutedOrDisabled;
    if (path.empty()) return PlayAdmission::InvalidSource;
    if (!player_->play_ex(player_key, path, /*is_preview=*/false, linear_gain, allow_stack,
                          max_event_polyphony)) {
        // Bounded refusal (pool full, muted, backend down): no voice started,
        // so the caller must not treat this attempt as successful playback.
        return PlayAdmission::RefusedCapacity;
    }
    return PlayAdmission::Started;
}

void SoundEngine::apply_interrupt_scope(const EventRuntimeSpec& spec) {
    if (!player_) return;
    if (spec.scope == InterruptScope::Global) {
        // Stop all non-preview runtime events globally
        player_->stop_events();
        active_priority_by_lane_.clear();
        return;
    }
    if (spec.scope == InterruptScope::SameGroup) {
        // Stop all active events belonging to the same category group
        for (const auto& [key, other] : specs_) {
            if (other.canonical_id == key && other.category == spec.category) {
                player_->stop_event(other.player_key);
            }
        }
        return;
    }
    // SameEvent
    player_->stop_event(spec.player_key);
}

void SoundEngine::play_event(const std::string& event_id) {
    play_event_at(event_id, 0);
}

void SoundEngine::play_event_at(const std::string& event_id, int64_t timestamp_ns) {
    if (!audio_audible()) return;
    auto it = specs_.find(event_id);
    if (it == specs_.end()) {
        const std::string cid = canonicalize_sound_event_id(event_id);
        if (!cid.empty()) it = specs_.find(cid);
    }
    if (it == specs_.end()) return;
    const EventRuntimeSpec& spec = it->second;

    const int64_t now = (timestamp_ns > 0) ? timestamp_ns : ptd::now_ns();

    // Drain any ready queued items first
    if (!pending_queue_.empty()) {
        flush_queue(now);
    }

    // Check cooldown_ms / min_retrigger_ms / Coalesce window
    const auto last_it = last_trigger_ns_.find(spec.canonical_id);
    if (last_it != last_trigger_ns_.end() && now >= last_it->second) {
        const int64_t elapsed_ns = now - last_it->second;
        const int effective_cooldown_ms = std::max(spec.cooldown_ms, spec.min_retrigger_ms);
        if (effective_cooldown_ms > 0 && elapsed_ns < static_cast<int64_t>(effective_cooldown_ms) * kNsPerMs) {
            ++cooldown_drops_;
            return;
        }
        if (spec.policy == PlaybackPolicy::Coalesce && timestamp_ns > 0) {
            const int64_t coalesce_ns = (effective_cooldown_ms > 0)
                ? static_cast<int64_t>(effective_cooldown_ms) * kNsPerMs
                : kDefaultCoalesceWindowNs;
            if (elapsed_ns < coalesce_ns) {
                ++coalesced_drops_;
                return;
            }
        }
    }

    // T-122: resolve this event's lane ONCE. Queue ownership, priority
    // ownership and completion re-evaluation all use this identity, so a
    // queued SameGroup entry waits for the group lane instead of the raw
    // front event id, and a Global entry waits for the real runtime lane
    // rather than on arithmetic that an idle preview slot can zero out.
    PlaybackLane lane = PlaybackLane::SameEvent;
    std::string lane_raw_key = spec.player_key;
    if (spec.scope == InterruptScope::Global) {
        lane = PlaybackLane::GlobalLane;
        lane_raw_key.clear();
    } else if (spec.scope == InterruptScope::SameGroup) {
        lane = PlaybackLane::SameGroup;
        lane_raw_key = sound_event_category_id(spec.category);
    }
    const std::string namespaced_lane_key = lane_key(lane, lane_raw_key);

    // Check priority when interrupting SameGroup or Global
    if (spec.policy == PlaybackPolicy::Override || spec.policy == PlaybackPolicy::Cut) {
        if (spec.scope == InterruptScope::SameGroup || spec.scope == InterruptScope::Global) {
            auto prio_it = active_priority_by_lane_.find(namespaced_lane_key);
            if (prio_it != active_priority_by_lane_.end() &&
                lane_busy(lane, lane_raw_key, spec.category)) {
                if (spec.priority < prio_it->second.priority) {
                    ++priority_drops_;
                    return;
                }
            }
        }
    }

    double linear_gain = 1.0;
    const std::string path = select_sound_from_spec(spec, linear_gain);
    if (path.empty()) return;

    // T-122: one admission result per attempt. Scheduler state (retrigger
    // timing, priority ownership, queue membership) is written ONLY after the
    // backend reports a voice actually started, so a refused playback leaves
    // no false history behind.
    const auto commit_success = [&](PlayAdmission admission) {
        if (admission != PlayAdmission::Started) return;
        last_trigger_ns_[spec.canonical_id] = now;
        active_priority_by_lane_[namespaced_lane_key] =
            LanePriority{spec.priority, lane, spec.category, lane_raw_key};
    };

    if (spec.policy == PlaybackPolicy::Queue) {
        const bool busy = lane_busy(lane, lane_raw_key, spec.category);
        // T-123: per-lane FIFO admission. An entry starts immediately when ITS
        // OWN lane is free, no earlier entry waits on that same lane, and no
        // Global entry is holding the queue barrier. The old
        // `!busy && pending_queue_.empty()` gate made one busy lane park every
        // independent lane behind it, which could starve an idle lane into
        // silent expiration.
        const bool global_barrier = queue_has_lane(PlaybackLane::GlobalLane, std::string());
        const bool own_lane_ahead = queue_has_lane(lane, lane_raw_key);
        if (!busy && !global_barrier && !own_lane_ahead) {
            const PlayAdmission admission = start_playback(spec.player_key, path, linear_gain,
                                                          /*allow_stack=*/false, 1);
            commit_success(admission);
            if (admission == PlayAdmission::RefusedCapacity) {
                // A recoverable capacity refusal must not discard the eligible
                // Queue request: retain it (bounded, expiring) exactly like the
                // deferred path so natural completion can retry it.
                while (!pending_queue_.empty() && (now - pending_queue_.front().enqueued_ns) > kMaxQueueAgeNs) {
                    pending_queue_.pop_front();
                }
                while (static_cast<int>(pending_queue_.size()) >= kMaxPendingQueueSize) {
                    pending_queue_.pop_front();
                }
                pending_queue_.push_back(QueuedEvent{spec.player_key, path, linear_gain, spec.priority, now,
                                                     lane, lane_raw_key, spec.category, spec.generation,
                                                     spec.canonical_id});
            }
            return;
        }
        // Evict expired items
        while (!pending_queue_.empty() && (now - pending_queue_.front().enqueued_ns) > kMaxQueueAgeNs) {
            pending_queue_.pop_front();
        }
        // Enforce bounded capacity (drop oldest when full)
        while (static_cast<int>(pending_queue_.size()) >= kMaxPendingQueueSize) {
            pending_queue_.pop_front();
        }
        pending_queue_.push_back(QueuedEvent{spec.player_key, path, linear_gain, spec.priority, now,
                                             lane, lane_raw_key, spec.category, spec.generation,
                                             spec.canonical_id});
        return;
    }

    if (spec.policy == PlaybackPolicy::Cut) {
        // T-123: one interrupt transaction covers the stop, the new voice and
        // the commit. A completion delivered inside the stop cannot drain the
        // queue while this interrupt is still unfinished.
        const InterruptTransaction txn(*this, /*drains=*/true);
        apply_interrupt_scope(spec);
        commit_success(start_playback(spec.player_key, path, linear_gain,
                                      /*allow_stack=*/false, 1));
        return;
    }

    if (spec.policy == PlaybackPolicy::Stack) {
        commit_success(start_playback(spec.player_key, path, linear_gain,
                                      /*allow_stack=*/true, spec.polyphony_allowance));
        return;
    }

    // OVERRIDE or COALESCE (after passing coalesce gate):
    {
        // T-123: the same transaction rule. An Override that stops a lane
        // voice (SameGroup/Global scope, or the per-event restart inside
        // play_ex) owns playback until its own voice has started.
        const InterruptTransaction txn(*this, /*drains=*/true);
        if (spec.scope == InterruptScope::SameGroup || spec.scope == InterruptScope::Global) {
            apply_interrupt_scope(spec);
        }
        commit_success(start_playback(spec.player_key, path, linear_gain,
                                      /*allow_stack=*/false, 1));
    }
}

void SoundEngine::flush_queue(int64_t now_timestamp_ns) {
    if (!audio_audible() || !player_ || pending_queue_.empty()) return;
    const int64_t now = (now_timestamp_ns > 0) ? now_timestamp_ns : ptd::now_ns();

    // T-123: a fence, not a transaction. A completion fired synchronously by a
    // start inside this drain must not re-enter it: the drain IS the safe
    // boundary, so nothing is deferred past it -- but nothing may re-enter it
    // while it holds queue state.
    const InterruptTransaction fence(*this, /*drains=*/false);

    // 1. Drop entries resolved under a replaced configuration, or older than
    //    the documented age policy, before any admission decision.
    for (auto it = pending_queue_.begin(); it != pending_queue_.end();) {
        const bool stale = it->generation != generation_;
        const bool expired = now >= it->enqueued_ns &&
                             (now - it->enqueued_ns) > kMaxQueueAgeNs;
        if (stale || expired) {
            it = pending_queue_.erase(it);
            continue;
        }
        ++it;
    }
    if (pending_queue_.empty()) return;

    // 2. A pending Global entry is the queue barrier. It waits for the whole
    //    runtime lane to fall silent, and while it waits no other lane may
    //    start -- otherwise the newly started voice could keep the barrier's
    //    own silence condition permanently out of reach.
    const auto first_global = std::find_if(pending_queue_.begin(), pending_queue_.end(),
                                          [](const QueuedEvent& e) {
                                              return e.lane == PlaybackLane::GlobalLane;
                                          });
    const bool has_global_barrier = first_global != pending_queue_.end();
    if (has_global_barrier &&
        lane_busy(PlaybackLane::GlobalLane, std::string(), SoundEventCategory::Custom)) {
        return;
    }

    // 3. Ordered per-lane drain. Entries are visited in FIFO order and each
    //    one starts when ITS OWN lane is free, so a blocked lane never holds
    //    back an independent idle one. Same-lane FIFO is preserved because the
    //    lane busy check covers every entry of that lane: once the first one
    //    starts, the rest of the lane waits behind it.
    //    A pending Global barrier is never bypassed: entries at or after it
    //    wait until it has been admitted, and it is admitted before any
    //    follower. Entries preceding it keep their documented ordering rights.
    for (auto it = pending_queue_.begin(); it != pending_queue_.end();) {
        // A copy, never a reference: a start can stop an existing voice, and a
        // synchronous completion must not be able to invalidate a deque
        // reference this loop still holds.
        const auto first_global_now = std::find_if(pending_queue_.begin(), pending_queue_.end(),
                                                   [](const QueuedEvent& e) {
                                                       return e.lane == PlaybackLane::GlobalLane;
                                                   });
        const bool has_global_barrier_now = first_global_now != pending_queue_.end();
        if (has_global_barrier_now && it >= first_global_now) {
            if (it == first_global_now) {
                if (lane_busy(PlaybackLane::GlobalLane, std::string(),
                              SoundEventCategory::Custom)) {
                    break;
                }
                const QueuedEvent entry = *it;
                if (start_playback(entry.event_id, entry.resolved_path, entry.linear_gain,
                                   /*allow_stack=*/false, 1) != PlayAdmission::Started) {
                    break;
                }
                it = pending_queue_.erase(it);
                // T-125: a drained admission is a successful playback, so it
                // arms the same per-event cooldown the immediate path arms.
                last_trigger_ns_[entry.canonical_id] = now;
                active_priority_by_lane_[lane_key(entry.lane, entry.lane_key)] =
                    LanePriority{entry.priority, entry.lane, entry.category, entry.lane_key};
                // The barrier is now live; followers may now follow their own
                // documented playback rules (including starting concurrently).
                continue;
            }
            break;
        }

        const QueuedEvent entry = *it;
        if (lane_busy(entry.lane, entry.lane_key, entry.category)) {
            ++it;
            continue;
        }
        // The entry leaves the queue only once a voice actually started: a
        // bounded capacity refusal keeps it queued (never silently discarded,
        // never retried without bound -- the age policy above expires it).
        if (start_playback(entry.event_id, entry.resolved_path, entry.linear_gain,
                           /*allow_stack=*/false, 1) != PlayAdmission::Started) {
            break;
        }
        it = pending_queue_.erase(it);
        // T-125: a drained admission is a successful playback, so it arms the
        // same per-event cooldown the immediate path arms.
        last_trigger_ns_[entry.canonical_id] = now;
        active_priority_by_lane_[lane_key(entry.lane, entry.lane_key)] =
            LanePriority{entry.priority, entry.lane, entry.category, entry.lane_key};
    }
}

void SoundEngine::play_event(AudioEvent event) {
    play_event(ptd::audio_event_name(static_cast<int>(event)));
}

void SoundEngine::play_event_at(AudioEvent event, int64_t timestamp_ns) {
    const int idx = static_cast<int>(event);
    if (idx >= 0 && idx < ptd::kAudioEventCount) {
        play_event_at(ptd::audio_event_name(idx), timestamp_ns);
    }
}

void SoundEngine::on_voice_completed(const std::string& event_id) {
    (void)event_id; // T-122: lane state is re-derived from live voices, so the
                    // completing id only triggers this re-evaluation
    if (!initialized_ || !player_) return;
    release_stale_lane_priorities();
    // T-123: the DRAIN is deferred while an interrupt transaction is open or a
    // drain is already running. Draining here would start voices in the middle
    // of an interrupt that has not finished, or re-enter a flush_queue that
    // holds queue state. The transaction's safe boundary (or the running
    // drain, which performs the same work) resumes it.
    if (interrupt_depth_ > 0) return;
    if (!pending_queue_.empty()) {
        flush_queue(0);
    }
}

void SoundEngine::preview_sound(const std::string& id) {
    const std::string clean = ptd::sanitize_sound_id(id);
    if (clean.empty()) {
        stop_preview();
        return;
    }
    const QString path = SoundCatalog::resolve_sound_path(clean);
    if (path.isEmpty()) {
        stop_preview();
        return;
    }
    preview_id_ = clean;
    preview_path_ = path.toStdString();
    if (!preview_admissible()) return;
    player_->play(kPreviewKey, preview_path_, /*is_preview=*/true);
}

void SoundEngine::preview_event_config(const EventSoundConfig& raw_ec) {
    const EventSoundConfig ec = EventSoundConfig::validated(raw_ec);
    EventRuntimeSpec temp_spec{};
    temp_spec.canonical_id = "@preview_pool";
    temp_spec.player_key = kPreviewKey;
    temp_spec.use_pool = ec.use_pool;
    temp_spec.no_immediate_repeat = ec.no_immediate_repeat;
    temp_spec.gain_db = ec.gain_db;
    if (ec.use_pool) {
        for (const auto& pe : ec.pool) {
            if (!pe.enabled) continue;
            const std::string clean = sanitize_sound_id(pe.sound_id);
            if (clean.empty()) continue;
            const QString path = SoundCatalog::resolve_sound_path(clean);
            if (path.isEmpty()) continue;
            temp_spec.pool.push_back(ResolvedPoolItem{clean, path.toStdString(), pe.weight, pe.gain_db});
        }
    } else {
        const std::string clean = sanitize_sound_id(ec.sound_id);
        if (!clean.empty()) {
            const QString path = SoundCatalog::resolve_sound_path(clean);
            if (!path.isEmpty()) temp_spec.single_path = path.toStdString();
        }
    }
    double gain = 1.0;
    const std::string chosen = select_sound_from_spec(temp_spec, gain);
    if (chosen.empty()) {
        stop_preview();
        return;
    }
    preview_path_ = chosen;
    if (!preview_admissible()) return;
    player_->play_ex(kPreviewKey, preview_path_, /*is_preview=*/true, gain, /*allow_stack=*/false, 1);
}

void SoundEngine::stop_preview() {
    preview_id_.clear();
    preview_path_.clear();
    if (!initialized_) return;
    player_->stop_preview();
}

void SoundEngine::stop_all_sounds() {
    preview_id_.clear();
    preview_path_.clear();
    pending_queue_.clear();
    last_trigger_ns_.clear();
    active_priority_by_lane_.clear();
    if (!initialized_) return;
    // T-123: every terminal stop is an interrupt transaction. Completions
    // delivered synchronously by the stops below can never resurrect canceled
    // playback through a deferred drain, because the queue is empty and the
    // boundary drain runs after the stop has finished.
    const InterruptTransaction txn(*this, /*drains=*/true);
    player_->stop_all();
}

void SoundEngine::handle_system_resume() {
    pending_queue_.clear();
    last_trigger_ns_.clear();
    active_priority_by_lane_.clear();
    if (initialized_ && player_) {
        const InterruptTransaction txn(*this, /*drains=*/true);
        player_->stop_events();
    }
}

bool SoundEngine::is_playing_preview() const {
    return initialized_ && player_->is_playing_preview();
}

int SoundEngine::active_voice_count() const {
    return initialized_ ? player_->active_voice_count() : 0;
}

int SoundEngine::active_playing_voice_count() const {
    return (initialized_ && player_) ? player_->active_playing_voice_count() : 0;
}

int SoundEngine::active_preview_voice_count() const {
    // Preview capacity is one slot by construction, so the preview question
    // is binary and is answered from the live voice, never from an allocation
    // count that outlives playback.
    return (initialized_ && player_ && player_->is_playing_preview()) ? 1 : 0;
}

} // namespace ptd
