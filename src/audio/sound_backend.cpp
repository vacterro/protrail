#include "sound_backend.h"

namespace ptd {
namespace {

// Clamps to the platform-observable 0..1 range. QSoundEffect uses a 0..1
// double, so the whole domain is representable; the clamp only guards NaN.
double clamp_unit(double v) {
    if (!(v >= 0.0)) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

} // namespace

int QtSoundPlayer::live_instances_ = 0;

QtSoundPlayer::QtSoundPlayer() { ++live_instances_; }

QtSoundPlayer::~QtSoundPlayer() {
    --live_instances_;
    shutdown();
}

bool QtSoundPlayer::initialize() {
    if (initialized_) return true;
    shutdown();
    // Voices are created lazily by acquire_voice(); the pool stays bounded.
    initialized_ = true;
    return true;
}

void QtSoundPlayer::shutdown() {
    for (auto& v : voices_) {
        if (v->effect) {
            v->effect->stop();
            v->effect.reset();
        }
    }
    voices_.clear();
    initialized_ = false;
}

void QtSoundPlayer::set_muted(bool muted) {
    muted_ = muted;
    for (auto& v : voices_) {
        apply_mix(*v);
        if (muted && v->effect) v->effect->stop();
    }
}

void QtSoundPlayer::set_volume(double volume_0_1) {
    volume_ = clamp_unit(volume_0_1);
    for (auto& v : voices_) apply_mix(*v);
}

void QtSoundPlayer::apply_mix(Voice& v) {
    if (!v.effect) return;
    v.effect->setMuted(muted_);
    v.effect->setVolume(clamp_unit(volume_ * clamp_unit(v.voice_gain)));
}

QtSoundPlayer::Voice* QtSoundPlayer::preview_voice() {
    for (auto& v : voices_) {
        if (v->is_preview) return v.get();
    }
    // Exactly one preview slot, created lazily. The event budget below keeps a
    // slot free for it, so a preview can never be refused by a full pool.
    if (static_cast<int>(voices_.size()) >= kMaxVoices) return nullptr;
    voices_.push_back(std::make_unique<Voice>());
    Voice* v = voices_.back().get();
    v->is_preview = true;
    return v;
}

QtSoundPlayer::Voice* QtSoundPlayer::find_event_voice(const std::string& event_id) {
    for (auto& v : voices_) {
        if (!v->is_preview && v->event_id == event_id) return v.get();
    }
    return nullptr;
}

QtSoundPlayer::Voice* QtSoundPlayer::acquire_event_voice() {
    // T-84: the event budget counts EVENT SLOTS, never the preview slot. The
    // old `voices_.size() >= kMaxVoices - 1` gate counted a preview voice as
    // though it were an event slot, so preview-first order left only seven
    // event voices. Counting non-preview slots makes the budget independent
    // of arrival order: eight distinct events are admitted whether or not the
    // catalog was auditioned first.
    int event_slots = 0;
    for (auto& v : voices_) {
        if (v->is_preview) continue;
        ++event_slots;
        if (!v->effect || !v->effect->isPlaying()) return v.get();  // free slot, reuse it
    }
    int total_for_gate = static_cast<int>(voices_.size());
    // When a preview slot is already present, total_for_gate == event_slots + 1;
    // event_voice_available(event_slots, total) allows up to 8 event slots + 1 preview = 9.
    if (!event_voice_available(event_slots, total_for_gate - (preview_voice_count() > 0 ? 1 : 0))) {
        return nullptr;
    }
    voices_.push_back(std::make_unique<Voice>());
    return voices_.back().get();
}

bool QtSoundPlayer::event_voice_available(int event_slots, int total_voices) {
    // Both budgets must clear: the event pool admits at most kMaxEventVoices
    // event slots, and the pool as a whole never exceeds kMaxVoices even when
    // an event budget and a preview budget are counted against one vector.
    return event_slots < kMaxEventVoices && total_voices < kMaxVoices;
}

void QtSoundPlayer::bind_signals(Voice& v) {
    if (!v.effect || v.signals_bound) return;
    Voice* self = &v;
    // QSoundEffect owns completion truth: playingChanged fires when playback
    // finishes naturally, on stop(), and on mute. The flag is only a hint for
    // fast pool scans; every admission decision reads isPlaying(). Both Qt6
    // signals are argument-less, and the lifetime of the connection is bound to
    // the effect itself.
    QObject::connect(v.effect.get(), &QSoundEffect::playingChanged, v.effect.get(),
                     [self, this] {
                         const bool still_playing = self->effect && self->effect->isPlaying();
                         const bool was_playing = self->playing;
                         self->playing = still_playing;
                         if (was_playing && !still_playing) {
                             if (completion_callback_) {
                                 completion_callback_(self->event_id);
                             }
                         }
                     });
    // A source that fails to decode (Error) must not hold a slot hostage: the
    // voice returns to the reusable state immediately.
    QObject::connect(v.effect.get(), &QSoundEffect::statusChanged, v.effect.get(),
                     [self, this] {
                         if (self->effect && self->effect->status() == QSoundEffect::Error) {
                             const bool was_playing = self->playing;
                             self->playing = false;
                             if (was_playing && completion_callback_) {
                                 completion_callback_(self->event_id);
                             }
                         }
                     });
    v.signals_bound = true;
}

void QtSoundPlayer::set_completion_callback(VoiceCompletionCallback cb) {
    completion_callback_ = std::move(cb);
}

std::string QtSoundPlayer::backend_status() const {
    // Live, not cached: setSource() is asynchronous and a refused source only
    // becomes an Error later, so a stored string would report the stale value.
    const Voice* target = nullptr;
    for (const auto& v : voices_) {
        if (v->is_preview) {
            target = v.get();
            break;
        }
    }
    if (target == nullptr) {
        for (const auto& v : voices_) {
            if (v->effect) {
                target = v.get();
                break;
            }
        }
    }
    if (target == nullptr || !target->effect) return "NONE";
    switch (target->effect->status()) {
        case QSoundEffect::Loading: return "LOADING";
        case QSoundEffect::Ready: return "READY";
        case QSoundEffect::Error: return "ERROR";
    }
    return "NONE";
}

bool QtSoundPlayer::play(const std::string& event_id, const std::string& absolute_path,
                         bool is_preview) {
    return play_ex(event_id, absolute_path, is_preview, 1.0, /*allow_stack=*/false, 1);
}

bool QtSoundPlayer::play_ex(const std::string& event_id, const std::string& absolute_path,
                            bool is_preview, double voice_gain_0_1, bool allow_stack,
                            int max_event_polyphony) {
    if (!initialized_) return false;
    if (absolute_path.empty()) return false;
    // Defense in depth: a muted backend refuses playback instead of producing
    // a silent voice that still occupies a pool slot.
    if (muted_) return false;

    if (is_preview) {
        // The dedicated preview slot: changing the selection replaces the
        // previous preview, replaying the same file restarts it, and no other
        // voice is ever touched.
        Voice* voice = preview_voice();
        if (voice == nullptr) {
            return false;
        }
        if (!voice->effect) {
            voice->effect = std::make_unique<QSoundEffect>();
            bind_signals(*voice);
        }
        if (voice->effect->isPlaying()) voice->effect->stop();
        if (voice->src != absolute_path) {
            voice->src = absolute_path;
            voice->effect->setSource(
                QUrl::fromLocalFile(QString::fromStdString(absolute_path)));
        }
        voice->event_id = event_id;
        voice->voice_gain = clamp_unit(voice_gain_0_1);
        voice->handle = next_handle_++;
        voice->playing = true;  // corrected by playingChanged on natural end
        apply_mix(*voice);
        voice->effect->play();
        return true;
    }

    Voice* voice = nullptr;
    if (allow_stack) {
        const int cap = (max_event_polyphony > 0) ? max_event_polyphony : kMaxEventVoices;
        if (active_event_voice_count(event_id) >= cap) {
            return false;
        }
        voice = acquire_event_voice();
        if (voice == nullptr) return false;
        if (!voice->effect) {
            voice->effect = std::make_unique<QSoundEffect>();
            bind_signals(*voice);
        }
    } else {
        // Runtime event (Override / single lane per event_id): the same event id
        // restarts on its own voice; a different event takes a free slot; a full
        // pool refuses the attempt instead of growing an unbounded number of voices.
        voice = find_event_voice(event_id);
        if (voice == nullptr) {
            voice = acquire_event_voice();
            if (voice == nullptr) return false;
            if (!voice->effect) {
                voice->effect = std::make_unique<QSoundEffect>();
                bind_signals(*voice);
            }
        } else if (voice->src != absolute_path) {
            if (voice->effect->isPlaying()) voice->effect->stop();
        } else if (voice->effect && voice->effect->isPlaying()) {
            voice->effect->stop();  // replay: restart the same file
        }
    }

    if (voice->src != absolute_path) {
        voice->src = absolute_path;
        voice->effect->setSource(QUrl::fromLocalFile(QString::fromStdString(absolute_path)));
    }
    voice->event_id = event_id;
    voice->is_preview = false;
    voice->voice_gain = clamp_unit(voice_gain_0_1);
    voice->handle = next_handle_++;
    voice->playing = true;
    apply_mix(*voice);
    voice->effect->play();
    return true;
}

void QtSoundPlayer::stop_preview() {
    for (auto& v : voices_) {
        if (v->is_preview && v->effect && v->effect->isPlaying()) {
            v->effect->stop();
        }
        if (v->is_preview) {
            v->playing = false;
            v->event_id.clear();
        }
    }
}

void QtSoundPlayer::stop_events() {
    for (auto& v : voices_) {
        if (!v->is_preview && v->effect && v->effect->isPlaying()) v->effect->stop();
        if (!v->is_preview) {
            v->playing = false;
            v->event_id.clear();
        }
    }
}

void QtSoundPlayer::stop_event(const std::string& event_id) {
    for (auto& v : voices_) {
        if (!v->is_preview && v->event_id == event_id && v->effect
            && v->effect->isPlaying()) {
            v->effect->stop();
            v->playing = false;
        }
        if (!v->is_preview && v->event_id == event_id) v->event_id.clear();
    }
}

void QtSoundPlayer::stop_all() {
    for (auto& v : voices_) {
        if (v->effect && v->effect->isPlaying()) v->effect->stop();
        v->playing = false;
    }
}

int QtSoundPlayer::active_voice_count() const {
    // QSoundEffect::isPlaying() is the only authority: Voice::playing is a hint
    // that may lag a natural completion until playingChanged is delivered.
    int count = 0;
    for (auto& v : voices_) {
        if (v->effect && v->effect->isPlaying()) ++count;
    }
    return count;
}

int QtSoundPlayer::active_event_voice_count(const std::string& event_id) const {
    int count = 0;
    for (auto& v : voices_) {
        if (!v->is_preview && v->event_id == event_id && v->effect && v->effect->isPlaying()) {
            ++count;
        }
    }
    return count;
}

int QtSoundPlayer::active_playing_voice_count() const {
    // Non-preview isPlaying() voices only: preview capacity is separate, so a
    // preview slot that is allocated but idle never hides a live runtime voice
    // from lane accounting.
    int count = 0;
    for (auto& v : voices_) {
        if (!v->is_preview && v->effect && v->effect->isPlaying()) ++count;
    }
    return count;
}

bool QtSoundPlayer::is_playing_preview() const {
    for (auto& v : voices_) {
        if (v->is_preview && v->effect && v->effect->isPlaying()) return true;
    }
    return false;
}

int QtSoundPlayer::voice_pool_size() const {
    return static_cast<int>(voices_.size());
}

int QtSoundPlayer::preview_voice_count() const {
    int count = 0;
    for (auto& v : voices_) {
        if (v->is_preview) ++count;
    }
    return count;
}

} // namespace ptd
