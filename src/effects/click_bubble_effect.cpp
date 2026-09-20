#include "click_bubble_effect.h"

#include <algorithm>
#include <cmath>

namespace ptd {

namespace {

// duration_ms -> ns for the progress denominator.
constexpr float kNsPerMs = 1'000'000.0f;

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

ClickBubbleEffect::ClickBubbleEffect(ClickConfig config)
    : config_(ClickConfig::validated(config)) {
    bubbles_.reserve(16);  // small steady-state allocation, no per-bubble churn
}

void ClickBubbleEffect::set_config(const ClickConfig& config) {
    const ClickConfig next = ClickConfig::validated(config);
    // T-024 config lifecycle. set_config() used to be a blind assignment,
    // which is unsafe once a gesture can be MID-FLIGHT: a transition that
    // switches the click effect or Hold FX off must cancel every
    // candidate/active hold NOW. Waiting for the Up would leave an aura
    // alive after the user disabled the feature, and would let that later Up
    // pay off a hold the settings had already cancelled.
    if (!next.enabled || !next.hold_enabled) {
        holds_.clear();
        // T-026: Hold FX OFF also drops the remaining Hold Wake immediately
        // -- the user switched the whole hold feature off, so its already-
        // born history must not keep animating as if it were still on.
        wake_.clear();
    } else if (!next.hold_wake_enabled) {
        // T-026: Motion Wake OFF stops new emissions AND clears the live
        // ones, so the setting's effect is visually immediate rather than
        // waiting out up to 2.5 s of unsnapped history. The attached aura is
        // deliberately untouched: it is a different layer.
        wake_.clear();
    }
    config_ = next;
}

float ClickBubbleEffect::ease_out_cubic(float progress) {
    const float p = clamp01(progress);
    const float inv = 1.0f - p;
    return 1.0f - inv * inv * inv;
}

float ClickBubbleEffect::apply_easing(ClickEasing easing, float p) {
    const float u = clamp01(p);
    switch (easing) {
        case ClickEasing::Smooth:  return u * u * (3.0f - 2.0f * u);
        case ClickEasing::Linear:  return u;
        case ClickEasing::EaseOut:
        default:                   return ease_out_cubic(u);
    }
}

float ClickBubbleEffect::progress_at(const Bubble& bubble, int64_t now_ns) const {
    // Clamp guards against any non-monotonic timestamp pair (e.g. a
    // synthetic test timestamp far in the past): negative elapsed means
    // "not started yet" renders as progress 0, never as negative radius.
    const float elapsed_ms =
        static_cast<float>(now_ns - bubble.start_timestamp_ns) / kNsPerMs;
    return clamp01(elapsed_ms / config_.duration_ms);
}

float ClickBubbleEffect::radius_at(const Bubble& bubble, int64_t now_ns) const {
    const float p = progress_at(bubble, now_ns);
    return config_.start_radius_px
         + (config_.end_radius_px - config_.start_radius_px) * apply_easing(config_.easing, p);
}

float ClickBubbleEffect::opacity_at(const Bubble& bubble, int64_t now_ns) const {
    const float p = progress_at(bubble, now_ns);
    return config_.base_opacity * (1.0f - p);
}

float ClickBubbleEffect::fill_alpha_at(const Bubble& bubble, int64_t now_ns) const {
    const float p = progress_at(bubble, now_ns);
    return config_.fill_opacity * (1.0f - p);
}

bool ClickBubbleEffect::on_button_down(const CursorSample& sample) {
    // C17 / Phase J: when the click effect is disabled, spawn nothing.
    if (!config_.enabled) return false;
    // MVP 04 + Phase J visual contract: exactly one bubble per
    // ButtonAction::Down transition IF that button's trigger is enabled.
    // Up transitions and movement samples spawn nothing. The position is
    // taken from the click sample itself (C3) -- never from a later
    // cursor position.
    if (sample.action != ButtonAction::Down) return false;
    bool trigger = false;
    switch (sample.button) {
        case MouseButton::Left:   trigger = config_.trigger_left;   break;
        case MouseButton::Right:  trigger = config_.trigger_right;  break;
        case MouseButton::Middle: trigger = config_.trigger_middle; break;
        default:                  trigger = false;                  break;
    }
    if (!trigger) return false;
    const float sx = static_cast<float>(sample.x);
    const float sy = static_cast<float>(sample.y);
    // T-024: a press also opens a hold CANDIDATE. A candidate is not a hold:
    // it draws nothing, charges nothing and pays off nothing. It only keeps
    // the lifecycle state needed to be promoted to an ActiveHold if the
    // button is still down kHoldActivationMs later. Release before that and
    // the candidate is discarded with no second effect of any kind, so a
    // short ordinary click stays exactly one ordinary click.
    if (config_.hold_enabled) {
        // One record per physical button. A repeated Down for the same button
        // without an Up (a lost Up, or synthetic input) REPLACES the old
        // record rather than stacking a second one.
        for (auto it = holds_.begin(); it != holds_.end(); ++it) {
            if (it->button == sample.button) {
                holds_.erase(it);
                break;
            }
        }
        if (holds_.size() < kMaxActiveHolds) {
            Hold h;
            h.button = sample.button;
            h.x = sx;
            h.y = sy;
            h.start_timestamp_ns = sample.timestamp_ns;
            h.seed = bubble_seed(sx, sy, sample.timestamp_ns);
            // T-026: wake bookkeeping starts at the press point. The
            // CANDIDATE never emits (see on_cursor_moved), but it still
            // tracks movement, so when it is promoted the first wake anchor
            // is measured from where the cursor actually is at that moment
            // rather than from a stale press coordinate.
            h.last_x = sx;
            h.last_y = sy;
            h.last_move_ns = sample.timestamp_ns;
            h.residual_px = 0.0f;
            h.wake_ordinal = 0;
            holds_.push_back(h);
        }
    }
    return spawn(sx, sy, sample.timestamp_ns);
}

bool ClickBubbleEffect::on_button_up(const CursorSample& sample) {
    if (sample.action != ButtonAction::Up) return false;
    for (auto it = holds_.begin(); it != holds_.end(); ++it) {
        if (it->button != sample.button) continue;
        const bool was_active = is_active_at(*it, sample.timestamp_ns);
        const float charge = charge_at(*it, sample.timestamp_ns);
        holds_.erase(it);
        // SHORT-CLICK CONTRACT. Releasing while the record is still a
        // CANDIDATE removes it and spawns NOTHING -- the Down bubble the
        // user already saw is the whole interaction. This is the single rule
        // that keeps a fast click from producing a phantom second effect.
        if (!was_active) return false;
        if (!config_.enabled || !config_.hold_enabled) return false;
        // The payoff fires at the RELEASE position, not the press position:
        // after a drag that is where the user is looking, and it is where
        // the aura they were watching actually was.
        const float rx = static_cast<float>(sample.x);
        const float ry = static_cast<float>(sample.y);
        if (!spawn(rx, ry, sample.timestamp_ns)) return false;
        // Charge 0 (released the instant it activated) pays off exactly like
        // the plain click it nearly was. A full charge lands at 2.2x -- big
        // enough to feel earned, bounded enough that it cannot swamp the
        // screen. Every style family honours it; see power_size_gain().
        //
        // T-027 Release Strength scales the CHARGE term only, so 1.0 is
        // exactly the accepted T-024 payoff and the preserved charge model
        // still decides the shape. The config bound (0.5..2.0) caps a full
        // charge at 3.4x, which is loud but nowhere near a nuclear release.
        bubbles_.back().power =
            1.0f + 1.2f * charge * config_.hold_release_strength;
        return true;
    }
    return false;
}

// T-026 Hold Motion Wake: convert a movement SEGMENT into world-space
// emissions. The cursor is an EMITTER, so what decides how many effects are
// shed is DISTANCE TRAVELLED, never how often Windows happened to deliver a
// mouse packet: a 60 px move delivered as one sample and the same 60 px
// delivered as ten collinear samples cross the same number of emission
// points at the same anchors.
//
// The only mutable state lives here, on the Hold, and it describes the
// INPUT (last point, residual distance, ordinal). A born emission is never
// touched again.
void ClickBubbleEffect::on_cursor_moved(const CursorSample& sample) {
    // Movement samples only: the button handlers own their own transitions.
    if (sample.action != ButtonAction::None) return;
    if (holds_.empty()) return;
    const float x = static_cast<float>(sample.x);
    const float y = static_cast<float>(sample.y);
    const int64_t ts = sample.timestamp_ns;

    for (Hold& h : holds_) {
        // Segment endpoints are captured BEFORE the aura is moved, so the walk
        // below happens along the path the cursor actually travelled.
        const float px = h.last_x;
        const float py = h.last_y;
        const int64_t prev_ns = h.last_move_ns;

        // The ATTACHED aura follows the cursor, exactly as it did in T-024.
        h.x = x;
        h.y = y;
        // The INPUT tracking advances even while this record is still only a
        // CANDIDATE: a candidate must never emit, but it must still know
        // where the cursor was so activation starts the wake from the right
        // place instead of replaying pre-activation movement.
        h.last_x = x;
        h.last_y = y;
        h.last_move_ns = ts;

        const float dx = x - px;
        const float dy = y - py;
        const float len = std::sqrt(dx * dx + dy * dy);
        // T-36 motion condition state is derived from movement vectors and
        // timestamps ONLY -- never from frame cadence. It is updated here,
        // before the wake-emission early-outs, so a gated (slow) movement
        // still drives Moving/Turning/Stopping correctly.
        const float inv_len_state = len > 0.0f ? 1.0f / len : 0.0f;
        const float dir_x = dx * inv_len_state;
        const float dir_y = dy * inv_len_state;

        if (!(len > 0.0f)) {
            // A zero-length sample is a stationary observation: if meaningful
            // movement had been in progress, this is the stop transition.
            if (h.moving && h.stop_armed) {
                if (config_.stop_accent) {
                    const float sx = x, sy = y;
                    const float px_dir = h.last_dir_x, py_dir = h.last_dir_y;
                    emit_wake(h, h.seed ^ 0x51ED2701u, sx, sy, ts, px_dir, py_dir,
                              0.0f, 0.0f, /*accent=*/true);
                }
                h.moving = false;
                h.stop_armed = false;
            }
            continue;   // stationary: no detached wake births
        }
        if (!config_.hold_wake_enabled) continue;

        // T-027 integrity repair (activation straddle): a movement segment may
        // CROSS the Candidate->ActiveHold threshold. The eligible wake segment
        // then starts at the interpolated activation point, never at the
        // pre-activation sample: no anchor, birth timestamp, residual, tangent
        // or speed may come from the part of the segment that was still only a
        // candidate. The attached aura above is deliberately unaffected.
        const int64_t activation_ns =
            h.start_timestamp_ns
            + static_cast<int64_t>(kHoldActivationMs * kNsPerMs);
        if (ts <= activation_ns) continue;  // wholly before activation

        float seg_x = px;              // start of the ELIGIBLE segment
        float seg_y = py;
        int64_t seg_prev_ns = prev_ns;
        float seg_len = len;
        if (prev_ns < activation_ns) {
            // Clip: interpolate the cursor position at activation_ns along
            // this movement and measure distance from there. The carried
            // residual is pre-activation distance and goes with it.
            const double span = static_cast<double>(ts - prev_ns);
            const double t0 = span > 0.0
                ? static_cast<double>(activation_ns - prev_ns) / span
                : 0.0;
            const float tc = static_cast<float>(
                t0 < 0.0 ? 0.0 : (t0 > 1.0 ? 1.0 : t0));
            seg_x = px + dx * tc;
            seg_y = py + dy * tc;
            seg_prev_ns = activation_ns;
            seg_len = len * (1.0f - tc);
            h.residual_px = 0.0f;
            if (!(seg_len > 0.0f)) continue;
        }

        const float inv_len = 1.0f / len;  // direction is unchanged by the clip
        const float tangent_x = dx * inv_len;
        const float tangent_y = dy * inv_len;

        // Bounded motion energy. dt is clamped so a stale timestamp can
        // neither divide by zero nor invent an infinite speed; the energy is
        // an ARTISTIC input only and never relaxes a cap. Only the ELIGIBLE
        // span contributes: a clipped segment measures both length and
        // duration from activation.
        double dt_s = static_cast<double>(ts - seg_prev_ns) / 1'000'000'000.0;
        if (!(dt_s > 1.0e-3)) dt_s = 1.0e-3;
        if (dt_s > 1.0) dt_s = 1.0;
        float speed = static_cast<float>(static_cast<double>(seg_len) / dt_s);
        if (!(speed >= 0.0f)) speed = 0.0f;
        if (speed > 20000.0f) speed = 20000.0f;

        // T-36 motion condition: meaningful movement arms the stop accent and
        // marks Moving; a turn beyond the angular threshold emits ONE bounded
        // accent after a cooldown, so jitter cannot spam it.
        const bool gate_on = config_.min_motion_speed_px_s > 0.0f;
        const bool meaningful = gate_on
            ? (speed >= config_.min_motion_speed_px_s)
            : (speed >= kMotionMeaningfulSpeedPxPerSec);
        if (meaningful) {
            if (config_.turn_accent && h.has_dir && h.moving
                && (ts - h.last_turn_accent_ns) >= kTurnAccentCooldownNs) {
                const float angle = turn_angle_rad(h.last_dir_x, h.last_dir_y,
                                                   dir_x, dir_y);
                if (angle >= kTurnAccentMinAngleRad) {
                    emit_wake(h, h.seed ^ 0x2B7E1516u, x, y, ts, tangent_x,
                              tangent_y, speed, 0.0f, /*accent=*/true);
                    h.last_turn_accent_ns = ts;
                }
            }
            h.last_dir_x = dir_x;
            h.last_dir_y = dir_y;
            h.has_dir = true;
            h.moving = true;
            h.stop_armed = true;
        } else if (h.moving && h.stop_armed) {
            // Decelerated to a near-stationary sample: at most one settling
            // accent per movement episode.
            if (config_.stop_accent) {
                emit_wake(h, h.seed ^ 0x51ED2701u, x, y, ts, tangent_x,
                          tangent_y, speed, 0.0f, /*accent=*/true);
            }
            h.moving = false;
            h.stop_armed = false;
        }

        // T-36 minimum-speed gate: below the configured threshold the
        // detached wake is suppressed, but the aura and motion state above
        // are unaffected.
        if (!wake_speed_passes_gate(speed, config_.min_motion_speed_px_s)) {
            continue;
        }

        const float motion_energy = speed_response_factor(
            speed, kWakeReferenceSpeedPxPerSec, config_.speed_response);

        const float spacing = wake_spacing_px(config_.hold_wake_density);
        const float total = h.residual_px + seg_len;
        int crossings = static_cast<int>(std::floor(total / spacing));
        if (crossings <= 0) {
            // Not far enough yet: carry the distance to the next sample. That
            // carry is what makes small input samples accumulate into the
            // same spatial pattern as one large one.
            h.residual_px = total;
            continue;
        }
        const bool over_budget = crossings > kMaxWakeBirthsPerMovement;
        const int births = over_budget ? kMaxWakeBirthsPerMovement : crossings;
        for (int i = 0; i < births; ++i) {
            float along;
            if (!over_budget) {
                // Exact: the i-th crossed spacing mark, offset by the carried
                // residual, measured along this actual segment.
                along = (static_cast<float>(i + 1) * spacing) - h.residual_px;
            } else {
                // A genuine teleport. Interpolate the segment deterministically
                // across the allowed budget instead of allocating thousands of
                // records, so a fast flick still leaves a spatially distributed
                // wake rather than one clump at the newest cursor position.
                along = seg_len * (static_cast<float>(i) + 0.5f)
                      / static_cast<float>(births);
            }
            const float t = clamp01(along / seg_len);
            // `t` is relative to the ELIGIBLE length, so it is rescaled by
            // seg_len/len before the full-segment delta is applied: a clipped
            // anchor must sit at the eligible distance along the same
            // direction, not at a fraction of the whole pre-activation path.
            // For an unclipped segment that factor is exactly 1.
            const float t_full = t * (seg_len / len);
            const float ax = seg_x + dx * t_full;
            const float ay = seg_y + dy * t_full;
            // Interpolated birth timestamps: without this, several anchors
            // generated from ONE fast sample would all carry the same age and
            // the wake would look like a rigidly simultaneous stamp.
            const int64_t birth_ns = seg_prev_ns
                + static_cast<int64_t>(static_cast<double>(ts - seg_prev_ns)
                                       * static_cast<double>(t));
            const uint32_t seed =
                h.seed ^ (h.wake_ordinal * 2654435761u) ^ 0x7F4A7C15u;
            ++h.wake_ordinal;  // advances even when the cap drops an emission
            emit_wake(h, seed, ax, ay, birth_ns, tangent_x, tangent_y, speed,
                      motion_energy);
        }
        if (!over_budget) {
            h.residual_px = total - static_cast<float>(crossings) * spacing;
        } else {
            // The budget consumed the whole segment; hold no residual or the
            // next sample would immediately re-trigger another burst.
            h.residual_px = 0.0f;
        }
    }
}

// T-026: Wake Density maps to SPATIAL emission spacing, never to particles
// per frame. Smooth, bounded and monotone decreasing in density.
float ClickBubbleEffect::wake_spacing_px(float density) {
    const float d = clamp_hold_multiplier(density,
                                          ClickConfig::kMinHoldWakeDensity,
                                          ClickConfig::kMaxHoldWakeDensity,
                                          1.0f);
    return kBaseWakeSpacingPx * std::pow(1.0f / d, 0.75f);
}

// T-36: minimum-speed gate. 0 disables the gate (any movement may emit).
bool ClickBubbleEffect::wake_speed_passes_gate(float speed_px_s,
                                               float min_speed_px_s) {
    const float gate = clamp_hold_multiplier(min_speed_px_s,
                                             ClickConfig::kMinMotionSpeedPxPerSec,
                                             ClickConfig::kMaxMotionSpeedPxPerSec,
                                             0.0f);
    if (!(gate > 0.0f)) return true;
    return speed_px_s >= gate;
}

// T-36: bounded speed-response factor. `response` 1.0 -> the accepted T-26
// energy term; 0 -> flat (no velocity amplification); 2 -> twice the read.
// Always finite and in [0, 2].
float ClickBubbleEffect::speed_response_factor(float speed_px_s,
                                               float reference_px_s,
                                               float response) {
    if (!(reference_px_s > 0.0f)) return 1.0f;
    const float r = clamp_hold_multiplier(response,
                                          ClickConfig::kMinSpeedResponse,
                                          ClickConfig::kMaxSpeedResponse, 1.0f);
    const float e = clamp01(speed_px_s / reference_px_s);
    // r == 1 -> exactly `e` (the accepted behavior); r == 0 -> 0; r == 2 -> 2e.
    return clamp01(e * r);
}

float ClickBubbleEffect::turn_angle_rad(float dir_ax, float dir_ay,
                                        float dir_bx, float dir_by) {
    const float la = std::sqrt(dir_ax * dir_ax + dir_ay * dir_ay);
    const float lb = std::sqrt(dir_bx * dir_bx + dir_by * dir_by);
    if (!(la > 0.0f) || !(lb > 0.0f)) return 0.0f;
    float c = (dir_ax * dir_bx + dir_ay * dir_by) / (la * lb);
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    return std::acos(c);
}

// T-026: record ONE immutable emission. The anchor, the timestamp and the
// whole visual snapshot are frozen here; nothing below this line ever
// follows the cursor again.
void ClickBubbleEffect::emit_wake(const Hold& hold, uint32_t seed, float x,
                                  float y, int64_t birth_ns, float tangent_x,
                                  float tangent_y, float speed,
                                  float motion_energy, bool accent) {
    if (wake_.size() >= kMaxWakeEmissions) {
        // Expired emissions prune first, so a long drag reuses its own dead
        // history before it ever has to drop something still visible.
        prune(birth_ns);
        if (wake_.size() >= kMaxWakeEmissions) {
            // Still full: discard the OLDEST emission. Active HOLD state is
            // deliberately untouched -- overflow must never cancel a gesture.
            wake_.erase(wake_.begin());
        }
    }
    HoldWakeEmission e;
    e.x = x;
    e.y = y;
    e.birth_timestamp_ns = birth_ns;
    e.seed = seed;
    e.style = config_.style;
    e.tangent_x = tangent_x;
    e.tangent_y = tangent_y;
    e.motion_speed = speed;
    e.motion_energy = clamp01(motion_energy);
    e.charge = charge_at(hold, birth_ns);
    e.color_r = static_cast<float>(config_.color_r);
    e.color_g = static_cast<float>(config_.color_g);
    e.color_b = static_cast<float>(config_.color_b);
    e.element_tint = config_.element_tint;
    e.particle_amount = static_cast<int>(config_.particle_amount);
    e.size_basis_px = config_.end_radius_px;
    e.thickness_px = config_.outline_thickness_px;
    e.intensity = config_.hold_intensity;
    e.lifetime_ms = config_.hold_wake_lifetime_ms;
    // T-36 Advanced Motion Wake snapshot.
    e.wake_strength = config_.wake_strength;
    e.wake_size = config_.wake_size;
    e.wake_spread = config_.wake_spread;
    e.accent = accent;
    wake_.push_back(e);
}

float ClickBubbleEffect::charge_at(const Hold& hold, int64_t now_ns) const {
    const float elapsed_ms =
        static_cast<float>(now_ns - hold.start_timestamp_ns) / 1'000'000.0f;
    // Charge starts at ACTIVATION, not at Down: the first kHoldActivationMs
    // are the click/hold discrimination window and must buy no charge at all,
    // or a 176 ms press would already pay off bigger than a 174 ms one.
    const float charge_ms = elapsed_ms - kHoldActivationMs;
    if (!(charge_ms > 0.0f)) return 0.0f;
    return clamp01(charge_ms / kHoldChargeMs);
}

bool ClickBubbleEffect::is_active_at(const Hold& hold, int64_t now_ns) {
    const float age_ms =
        static_cast<float>(now_ns - hold.start_timestamp_ns) / 1'000'000.0f;
    // Derived, never stored: promotion is a pure function of elapsed time, so
    // it cannot drift with frame cadence and needs no per-frame bookkeeping.
    return age_ms >= kHoldActivationMs;
}

std::size_t ClickBubbleEffect::active_hold_count(int64_t now_ns) const {
    std::size_t n = 0;
    for (const Hold& h : holds_) {
        if (is_active_at(h, now_ns)) ++n;
    }
    return n;
}

std::size_t ClickBubbleEffect::candidate_count(int64_t now_ns) const {
    return holds_.size() - active_hold_count(now_ns);
}

bool ClickBubbleEffect::has_active_hold(int64_t now_ns) const {
    for (const Hold& h : holds_) {
        if (is_active_at(h, now_ns)) return true;
    }
    return false;
}

std::size_t ClickBubbleEffect::reconcile_physical_buttons(bool left_down,
                                                          bool right_down,
                                                          bool middle_down) {
    // T-024 lost-Up recovery, replacing the draft's arbitrary 10-second
    // timeout. A record survives only while the OS agrees its button is
    // physically down. A record contradicted by the hardware is CANCELLED:
    // no payoff is invented out of nowhere, and no stale aura is left alive.
    const std::size_t before = holds_.size();
    holds_.erase(std::remove_if(holds_.begin(), holds_.end(),
                                [left_down, right_down, middle_down](const Hold& h) {
                                    switch (h.button) {
                                        case MouseButton::Left:   return !left_down;
                                        case MouseButton::Right:  return !right_down;
                                        case MouseButton::Middle: return !middle_down;
                                        default:                  return true;
                                    }
                                }),
                 holds_.end());
    return before - holds_.size();
}

bool ClickBubbleEffect::spawn(float x, float y, int64_t start_timestamp_ns) {
    // C9 overflow policy: prune completed bubbles first, then discard the
    // OLDEST active bubble. Memory and per-frame draw cost stay bounded
    // under synthetic/pathological input.
    if (bubbles_.size() >= kMaxActiveBubbles) {
        std::size_t removed = prune(start_timestamp_ns);
        if (bubbles_.size() >= kMaxActiveBubbles) {
            bubbles_.erase(bubbles_.begin());  // oldest active bubble
        }
        (void)removed;
    }
    bubbles_.push_back(Bubble{x, y, start_timestamp_ns,
                              bubble_seed(x, y, start_timestamp_ns)});
    return true;
}

std::size_t ClickBubbleEffect::prune(int64_t now_ns) {
    // Stable removal: keep bubbles with remaining life (their animation
    // is a pure function of elapsed time, so keeping them is enough -- no
    // per-frame state to advance). Order of birth is preserved. Returns
    // the number of REMOVED bubbles.
    const auto first_dead = std::stable_partition(
        bubbles_.begin(), bubbles_.end(),
        [this, now_ns](const Bubble& b) {
            return progress_at(b, now_ns) < 1.0f;
        });
    const std::size_t removed =
        static_cast<std::size_t>(bubbles_.end() - first_dead);
    bubbles_.erase(first_dead, bubbles_.end());

    // T-026: the same stable removal for detached wake emissions, using each
    // emission's OWN snapshotted lifetime. This is both the per-frame memory
    // reclaim AND the first half of the overflow policy, so a long drag frees
    // its own dead history before the cap has to drop anything visible.
    const auto wake_dead = std::stable_partition(
        wake_.begin(), wake_.end(),
        [now_ns](const HoldWakeEmission& e) {
            return wake_progress_at(e, now_ns) < 1.0f;
        });
    const std::size_t wake_removed =
        static_cast<std::size_t>(wake_.end() - wake_dead);
    wake_.erase(wake_dead, wake_.end());

    // T-024: prune NEVER touches hold records. There is deliberately no
    // maximum hold duration -- a user is allowed to keep a button down for
    // minutes, and the draft's 10-second guard would have killed the aura
    // under their finger. A genuinely lost Up is recovered by
    // reconcile_physical_buttons(), which asks the hardware instead of
    // guessing from a clock.
    return removed + wake_removed;
}

// ---- T-026 Hold Motion Wake: the detached, world-space layer ----

float ClickBubbleEffect::wake_progress_at(const HoldWakeEmission& e,
                                          int64_t now_ns) {
    // The emission's OWN snapshotted lifetime, never the current config: a
    // later Wake Life change must not retroactively stretch or shrink an
    // emission that is already fading.
    const float life_ms = e.lifetime_ms > 1.0f ? e.lifetime_ms : 1.0f;
    const float elapsed_ms =
        static_cast<float>(now_ns - e.birth_timestamp_ns) / 1'000'000.0f;
    return clamp01(elapsed_ms / life_ms);
}

std::size_t ClickBubbleEffect::live_wake_count(int64_t now_ns) const {
    std::size_t n = 0;
    for (const HoldWakeEmission& e : wake_) {
        if (wake_progress_at(e, now_ns) < 1.0f) ++n;
    }
    return n;
}

bool ClickBubbleEffect::has_live_wake(int64_t now_ns) const {
    // T-027 integrity repair: an emission snapshots its OWN lifetime at birth,
    // so birth order does NOT imply expiry order. Once Wake Life can change
    // mid-session, a NEWER emission (short life) can expire while an OLDER one
    // (long life) is still animating -- checking only wake_.back() would
    // false-idle the active-only scheduler and leave the old wake frozen.
    // The vector is hard-capped at kMaxWakeEmissions (192), so this bounded
    // scan is cheap enough for the scheduler's content gate.
    for (const HoldWakeEmission& e : wake_) {
        if (wake_progress_at(e, now_ns) < 1.0f) return true;
    }
    return false;
}

// T-026: the geometry of one emission. Every style gets a DIFFERENT
// signature -- this is deliberately not one generic dot trail in eleven
// colours, because the whole point of the milestone is that a user can tell
// which style is active from the wake alone.
//
// Pure: geometry = f(emission, elapsed). No mutable particle state, no
// physics integration, no per-frame RNG, and at most
// kMaxWakeMarksPerEmission marks however long the emission lives.
std::size_t ClickBubbleEffect::wake_marks(const HoldWakeEmission& e,
                                          int64_t now_ns, WakeMark* out,
                                          std::size_t capacity) const {
    if (out == nullptr || capacity == 0) return 0;
    const std::size_t cap = capacity < kMaxWakeMarksPerEmission
                          ? capacity : kMaxWakeMarksPerEmission;
    const float p = wake_progress_at(e, now_ns);
    if (!(p < 1.0f)) return 0;
    if (cap == 0) return 0;

    std::size_t n = 0;
    const auto push = [&out, &n, cap](WakeMarkKind kind, float x, float y,
                                      float radius, float thickness,
                                      float r, float g, float b,
                                      float alpha) {
        if (n >= cap) return;
        if (!(alpha > 0.0f) || !(radius > 0.0f)) return;
        WakeMark& m = out[n++];
        m.x = x;
        m.y = y;
        m.radius_px = radius;
        m.thickness_px = thickness;
        m.r = r;
        m.g = g;
        m.b = b;
        // The renderer contract is alpha in 0..1; the intensity multiplier
        // may push an intermediate above 1, so clamp here rather than
        // promising the sink something it cannot draw.
        m.alpha = alpha > 1.0f ? 1.0f : alpha;
        m.kind = kind;
    };

    // Bounded motion energy and intensity. Intensity scales the wake only
    // modestly and never multiplies the particle count.
    const float M = clamp01(e.motion_energy);
    const float intensity = clamp_hold_multiplier(
        e.intensity, ClickConfig::kMinHoldIntensity,
        ClickConfig::kMaxHoldIntensity, 1.0f);
    // T-36: the Advanced Motion Wake multipliers are frozen in the emission.
    // strength scales loudness (alpha), size scales spatial extent (radius /
    // thickness basis), spread scales lateral/angular scatter. All are
    // identity at 1.0, so a migrated emission is bit-identical to T-26/T-27.
    const float strength = clamp_hold_multiplier(
        e.wake_strength, ClickConfig::kMinWakeStrength,
        ClickConfig::kMaxWakeStrength, 1.0f);
    const float size = clamp_hold_multiplier(
        e.wake_size, ClickConfig::kMinWakeSize, ClickConfig::kMaxWakeSize, 1.0f);
    const float spread = clamp_hold_multiplier(
        e.wake_spread, ClickConfig::kMinWakeSpread,
        ClickConfig::kMaxWakeSpread, 1.0f);
    const float A = kWakeIntensity * intensity * (0.55f + 0.45f * M) * strength;
    const float R = (e.size_basis_px > 1.0f ? e.size_basis_px : 1.0f)
                  * (0.80f + 0.40f * M) * size;
    const float th = (e.thickness_px > 0.05f ? e.thickness_px : 0.05f) * size;
    const float fade = 1.0f - p;
    const float ease = ease_out_cubic(p);
    const float tx = e.tangent_x;
    const float ty = e.tangent_y;
    // T-36: `spread` widens LATERAL/angular scatter around the motion path.
    // It scales the per-style lateral terms below (perp curl, ember lateral,
    // droplet throw, dust distance) and is identity at 1.0, so a migrated
    // emission is bit-identical to T-26/T-27.
    const float perp_x = -ty;
    const float perp_y = tx;
    const float two_pi = 6.2831853f;
    const float amount = static_cast<float>(e.particle_amount);
    const auto h01 = [&e](uint32_t salt) { return hash01(e.seed ^ salt); };
    const ElementTintTarget base = elemental_color(
        e.style, e.color_r, e.color_g, e.color_b, e.element_tint, 0.0f);

    switch (e.style) {
        case ClickStyle::Ring:
            // One expanding pressure ring per anchor. No particles at all:
            // the echoes ARE the signature.
            push(WakeMarkKind::Ring, e.x, e.y, R * (0.40f + 1.10f * ease),
                 th * 0.7f, base.r, base.g, base.b, A * fade);
            break;

        case ClickStyle::DoubleRing:
            // A pair of resonances leaving the anchor together at two
            // different expansion rates, so the pair opens as it fades.
            push(WakeMarkKind::Ring, e.x, e.y,
                 R * (0.45f + 1.30f * std::pow(p, 0.85f)), th * 0.7f,
                 base.r, base.g, base.b, A * fade);
            push(WakeMarkKind::Ring, e.x, e.y,
                 R * (0.30f + 0.85f * std::pow(p, 1.35f)), th * 0.6f,
                 base.r, base.g, base.b, A * 0.70f * fade);
            break;

        case ClickStyle::Ripple:
            // Each anchor disturbs the surface two to three times: staggered
            // waves that are born, expand and die inside this one record.
            for (int k = 0; k < 3; ++k) {
                const float ph = (p - 0.26f * static_cast<float>(k)) / 0.74f;
                if (!(ph > 0.0f) || !(ph < 1.0f)) continue;
                push(WakeMarkKind::Ring, e.x, e.y,
                     R * (0.20f + 1.05f * ease_out_cubic(ph)), th * 0.55f,
                     base.r, base.g, base.b,
                     A * (1.0f - ph) * (1.0f - 0.15f * static_cast<float>(k)));
            }
            break;

        case ClickStyle::Burst:
        case ClickStyle::SparkBurst: {
            // Small local micro-bursts -- NEVER a replay of the full click
            // explosion. A reduced packet with deterministic angular scatter
            // and a mild backward bias from the movement tangent, so the
            // debris reads as thrown OFF a moving pointer.
            const bool spark = e.style == ClickStyle::SparkBurst;
            if (!spark) {
                push(WakeMarkKind::Ring, e.x, e.y, R * (0.35f + 0.55f * ease),
                     th * 0.5f, base.r, base.g, base.b, A * 0.28f * fade);
            }
            // Bounded packet size. Spark Burst sheds MORE than Burst -- that is
            // its whole identity -- but both stay in single digits per anchor
            // whatever particle_amount says. particle_amount 0 means particles
            // are off, exactly as it does for the click itself, so a user who
            // turned particles off is not silently given a particle wake.
            int count = 0;
            if (e.particle_amount > 0) {
                const int want = e.particle_amount / (spark ? 2 : 3) + 1;
                const int ceiling = spark ? 5 : 4;
                count = want < 2 ? 2 : (want > ceiling ? ceiling : want);
            }
            if (count > static_cast<int>(kMaxWakeMarksPerEmission)) {
                count = static_cast<int>(kMaxWakeMarksPerEmission);
            }
            // particle_amount 0 (particles off) means an EMPTY packet. The
            // angular spacing below is only meaningful for a non-empty one,
            // and two_pi / 0 would manufacture a non-finite intermediate in a
            // path whose contract is finite deterministic math. Burst keeps
            // the ring already pushed above; Spark Burst emits nothing here,
            // exactly as designed.
            if (count == 0) break;
            const float sector = two_pi / static_cast<float>(count);
            // Backward bias: how far the packet leans against the direction of
            // travel. Spark Burst leans harder.
            const float bias = (spark ? 0.50f : 0.35f) * M;
            for (int i = 0; i < count; ++i) {
                const uint32_t salt =
                    0x9E3779B9u + static_cast<uint32_t>(i) * 2654435761u;
                // Per-particle lifetime: sparks genuinely burn out at
                // different times; bursts stay a readable packet that merely
                // stops vanishing as one sheet.
                const float life = spark ? (0.50f + 0.50f * h01(salt ^ 0x5F356495u))
                                        : (0.80f + 0.20f * h01(salt ^ 0x5F356495u));
                if (p >= life) continue;
                float dir_x = std::cos(sector * static_cast<float>(i)
                             + (h01(salt ^ 0x51ED270Bu) - 0.5f) * sector
                               * (spark ? 1.00f : 0.55f));
                float dir_y = std::sin(sector * static_cast<float>(i)
                             + (h01(salt ^ 0x51ED270Bu) - 0.5f) * sector
                               * (spark ? 1.00f : 0.55f));
                // Lean the whole packet backwards along the motion tangent.
                dir_x = dir_x * (1.0f - bias) - tx * bias;
                dir_y = dir_y * (1.0f - bias) - ty * bias;
                const float dl = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (dl > 1.0e-6f) {
                    dir_x /= dl;
                    dir_y /= dl;
                }
                const float spd = spark
                    ? (0.50f + 1.20f * h01(salt ^ 0x85EBCA6Bu))
                    : (0.70f + 0.45f * h01(salt ^ 0x85EBCA6Bu));
                const float travel = R * (0.25f + 0.55f * p) * spd;
                // Occasional hot spark: rarer, faster, bigger, whiter.
                const bool hot = spark && h01(salt ^ 0x1B873593u) > 0.72f;
                const float pr = (spark ? 1.9f : 1.7f)
                               * (0.70f + 0.60f * h01(salt ^ 0xC2B2AE35u))
                               * (hot ? 1.55f : 1.0f) * (1.0f - 0.35f * p);
                const float al = A * (spark ? (0.30f + 0.70f * h01(salt ^ 0x27D4EB2Fu))
                                            : (0.55f + 0.45f * h01(salt ^ 0x27D4EB2Fu)))
                               * (1.0f - p / (life > 0.05f ? life : 0.05f));
                const float cr = hot ? 255.0f : base.r;
                const float cg = hot ? (base.g * 0.6f + 255.0f * 0.4f) : base.g;
                const float cb = hot ? (base.b * 0.5f + 255.0f * 0.5f) : base.b;
                push(WakeMarkKind::Particle, e.x + dir_x * travel,
                     e.y + dir_y * travel, pr, 0.0f, cr, cg, cb,
                     al > 0.0f ? al : 0.0f);
            }
            break;
        }

        case ClickStyle::SoftFlash:
            // Not particles pretending to be Soft Flash: small TRANSLUCENT
            // world-space glow discs that overlap into an afterimage trail.
            push(WakeMarkKind::Disc, e.x, e.y,
                 R * (0.35f + 0.75f * ease), th, base.r, base.g, base.b,
                 A * 0.55f * fade * fade);
            // A second, smaller puff drifted slightly against the motion, so
            // the afterimage has depth rather than being one disc per anchor.
            push(WakeMarkKind::Disc, e.x - tx * R * 0.20f * M,
                 e.y - ty * R * 0.20f * M, R * 0.45f * fade, th * 0.7f,
                 base.r, base.g, base.b, A * 0.30f * fade);
            break;

        case ClickStyle::DotRing:
            // Primary signature: a chain of glowing nodes left behind. Plus an
            // OCCASIONAL small ring echo -- never a spark cloud.
            push(WakeMarkKind::Particle, e.x, e.y,
                 R * 0.22f * (1.0f - 0.35f * p), 0.0f,
                 base.r, base.g, base.b, A * fade);
            if (h01(0xD07u) > 0.60f) {
                push(WakeMarkKind::Ring, e.x, e.y, R * (0.35f + 0.65f * ease),
                     th * 0.45f, base.r, base.g, base.b, A * 0.35f * fade);
            }
            break;

        case ClickStyle::Air: {
            // The pointer moves through air and disturbs it: motes lag behind
            // the motion, then curl TANGENTIALLY, and the curl keeps going
            // after the cursor is long gone -- all of it around a fixed
            // world anchor.
            const float swirl = std::sin(two_pi * p);
            for (int i = 0; i < 4; ++i) {
                const uint32_t salt = 0x3u + static_cast<uint32_t>(i) * 2246822519u;
                const float ang = two_pi * h01(salt ^ 0x11u);
                // T-36 spread widens the mote scatter around the anchor.
                const float radial = R * (0.15f + 0.30f * h01(salt ^ 0x17u))
                                   * spread;
                // Signed curl weight: half the motes swing one way, half the
                // other, so the wake reads as eddies rather than one rotation.
                const float side = h01(salt ^ 0x1Du) > 0.5f ? 1.0f : -1.0f;
                const float curl = R * (0.25f + 0.45f * h01(salt ^ 0x23u))
                                 * side * swirl * spread;
                // Lag: the mote starts slightly behind the movement and the
                // lag decays as the eddy takes over.
                const float lag = R * 0.30f * M * (1.0f - p);
                push(WakeMarkKind::Particle,
                     e.x + std::cos(ang) * radial + perp_x * curl - tx * lag,
                     e.y + std::sin(ang) * radial + perp_y * curl - ty * lag,
                     1.6f * (0.75f + 0.5f * h01(salt ^ 0x29u)) * (1.0f - 0.3f * p),
                     0.0f, base.r, base.g, base.b,
                     A * (0.45f + 0.55f * h01(salt ^ 0x2Fu)) * fade);
            }
            break;
        }

        case ClickStyle::Fire: {
            // A burning ember wake: embers born on the path, each pinned to
            // its anchor, rising and wobbling on its own while it cools.
            const float want = amount / 3.0f;
            int count = static_cast<int>(want);
            if (count < 2) count = 2;
            if (count > 4) count = 4;
            for (int i = 0; i < count; ++i) {
                const uint32_t salt = 0x5u + static_cast<uint32_t>(i) * 2654435761u;
                const float lateral = (h01(salt ^ 0x13u) - 0.5f) * R * 0.55f
                                    * spread;
                const float wobble =
                    std::sin(two_pi * (p * 1.6f + h01(salt ^ 0x17u))) * R * 0.12f
                    * spread;
                // Rise is monotone in p (y only ever decreases), which is the
                // ember signature the regressions assert.
                const float rise = R * (0.35f + 1.10f * std::pow(p, 0.7f))
                                 * (0.70f + 0.60f * h01(salt ^ 0x1Du));
                // Fast movement throws embers slightly backward before
                // buoyancy wins; the throw decays with age.
                const float throw_back = R * 0.28f * M * (1.0f - p);
                const ElementTintTarget ec = elemental_color(
                    ClickStyle::Fire, e.color_r, e.color_g, e.color_b,
                    e.element_tint, p);
                const float flicker =
                    0.55f + 0.45f * std::sin(two_pi * (p * 2.2f + h01(salt ^ 0x2Bu)));
                push(WakeMarkKind::Particle,
                     e.x + lateral + wobble - tx * throw_back,
                     e.y - rise - ty * throw_back * 0.35f,
                     2.4f * (0.70f + 0.60f * h01(salt ^ 0x31u)) * (1.0f - 0.55f * p),
                     0.0f, ec.r, ec.g, ec.b,
                     A * flicker * (1.0f - p * p));
            }
            break;
        }

        case ClickStyle::Water:
            // Dragging a wet pointer across an invisible surface: droplets
            // thrown from the path onto bounded arcs, plus a small ripple left
            // behind at the point where they came off.
            push(WakeMarkKind::Ring, e.x, e.y, R * (0.25f + 0.85f * ease),
                 th * 0.5f, base.r, base.g, base.b, A * 0.45f * fade);
            for (int i = 0; i < 2; ++i) {
                const uint32_t salt = 0x2Bu + static_cast<uint32_t>(i) * 2246822519u;
                const float ang = h01(salt ^ 0x33u) * two_pi;
                const float up = R * 0.55f * (0.75f + 0.50f * h01(salt ^ 0x37u));
                const float grav = R * 1.05f * (0.85f + 0.30f * h01(salt ^ 0x3Bu));
                const float thrown = R * 0.45f * (0.70f + 0.60f * h01(salt ^ 0x41u))
                                   * spread;
                push(WakeMarkKind::Particle,
                     e.x + std::cos(ang) * thrown * p,
                     e.y - up * p + grav * p * p,
                     1.5f * (0.75f + 0.50f * h01(salt ^ 0x47u)) * (1.0f - 0.35f * p),
                     0.0f, base.r, base.g, base.b,
                     A * (0.60f + 0.40f * h01(salt ^ 0x4Du)) * (1.0f - p * p));
            }
            break;

        case ClickStyle::Earth:
            // Heavy dust and debris, not an escorting constellation: a short
            // dust puff plus a few chunky particles that lag behind the
            // movement and SETTLE DOWNWARD instead of orbiting.
            push(WakeMarkKind::Disc, e.x, e.y + R * 0.10f * p,
                 R * (0.30f + 0.45f * p), th * 1.4f, base.r, base.g, base.b,
                 A * 0.30f * fade * fade);
            for (int i = 0; i < 3; ++i) {
                const uint32_t salt = 0x1Bu + static_cast<uint32_t>(i) * 2654435761u;
                const float ang = two_pi * h01(salt ^ 0x1Fu);
                const float dist = R * 0.30f * (0.60f + 0.60f * h01(salt ^ 0x25u)) * p
                                 * spread;
                // Lateral only along the ground plane, and always DOWNWARD in
                // time: no angular motion, no orbit.
                push(WakeMarkKind::Particle,
                     e.x + std::cos(ang) * dist
                         - tx * R * 0.25f * M * (1.0f - 0.5f * p),
                     e.y + std::sin(ang) * dist * 0.35f + R * 0.55f * p * p,
                     2.2f * (0.70f + 0.60f * h01(salt ^ 0x2Du)) * (1.0f - 0.15f * p),
                     0.0f, base.r, base.g, base.b,
                     A * (0.55f + 0.45f * h01(salt ^ 0x35u))
                       * (1.0f - p * p * p));
            }
            break;

        default:
            break;
    }
    return n;
}

void ClickBubbleEffect::draw_wake_emission(const HoldWakeEmission& e,
                                           int64_t now_ns,
                                           ClickBubbleSink& sink) const {
    // Bounded by construction: wake_marks() writes at most
    // kMaxWakeMarksPerEmission marks, on the stack, with no allocation.
    WakeMark marks[kMaxWakeMarksPerEmission];
    const std::size_t n = wake_marks(e, now_ns, marks, kMaxWakeMarksPerEmission);
    for (std::size_t i = 0; i < n; ++i) {
        const WakeMark& m = marks[i];
        switch (m.kind) {
            case WakeMarkKind::Ring:
                sink.add_bubble(m.x, m.y, m.radius_px, m.thickness_px,
                                m.r, m.g, m.b, m.alpha, 0.0f);
                break;
            case WakeMarkKind::Disc:
                sink.add_bubble(m.x, m.y, m.radius_px, m.thickness_px,
                                m.r, m.g, m.b, 0.0f, m.alpha);
                break;
            case WakeMarkKind::Particle:
            default:
                sink.add_particle(m.x, m.y, m.radius_px, m.r, m.g, m.b,
                                  m.alpha);
                break;
        }
    }
}

bool ClickBubbleEffect::has_live_content(int64_t now_ns) const {
    // T-024: a hold RECORD is live content, candidates included. Active
    // holds obviously render every frame. Candidates must count too: with a
    // short configured click duration the one-shot Down bubble can expire
    // BEFORE the activation threshold, and a sleeping scheduler would then
    // never evaluate the candidate again -- a stationary long press would
    // never activate, and the feature would appear to depend on jiggling the
    // mouse. It does not.
    if (!holds_.empty()) return true;
    // T-026: a detached wake emission is live content too. After an Up with
    // Hold FX still enabled there is no hold and possibly no bubble left, but
    // embers/ripples/dust from the drag are still animating -- if the
    // scheduler slept through that, the whole "the cursor left consequences
    // behind" promise would break the moment the button came up.
    if (has_live_wake(now_ns)) return true;
    // Vector is birth-ordered and progress is monotone in birth order
    // (equal durations), so if any bubble is live, the newest one is.
    return !bubbles_.empty()
        && progress_at(bubbles_.back(), now_ns) < 1.0f;
}

// ---- T-022 Elemental Click VFX: pure math ----

bool ClickBubbleEffect::is_elemental(ClickStyle style) {
    return is_elemental_click_style(style);  // one definition, in click_config.h
}

uint32_t ClickBubbleEffect::bubble_seed(float x, float y,
                                       int64_t start_timestamp_ns) {
    // Mix the anchor and the birth time. The coordinates are quantized to
    // whole pixels on purpose: two clicks one pixel apart are two clicks and
    // SHOULD scatter differently, but a sub-pixel difference must not be the
    // thing that decides it. The timestamp dominates anyway -- clicking the
    // exact same pixel twice still gives two different bursts.
    const uint32_t qx = static_cast<uint32_t>(static_cast<int32_t>(x));
    const uint32_t qy = static_cast<uint32_t>(static_cast<int32_t>(y));
    const uint32_t lo = static_cast<uint32_t>(start_timestamp_ns);
    const uint32_t hi = static_cast<uint32_t>(start_timestamp_ns >> 32);
    uint32_t h = qx * 2654435761u;
    h ^= qy * 2246822519u;
    h ^= lo * 3266489917u;
    h ^= hi * 668265263u;
    h ^= h >> 15; h *= 0x2c1b3c6dU; h ^= h >> 12; h *= 0x297a2d39U; h ^= h >> 15;
    return h;
}

float ClickBubbleEffect::hash01(uint32_t seed) {
    // Same integer mixer the T-017 Spark Burst jitter already uses, exposed
    // so every elemental variation stays deterministic and reproducible.
    uint32_t h = seed * 747796405u + 2891336453u;
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ClickBubbleEffect::burst_particle_life(uint32_t bubble_seed, int index,
                                             bool spark) {
    // A DEDICATED salt, mixed separately from the scatter/speed/size/alpha
    // salts: lifetime must be an independent axis of variation, or the
    // fastest particle would also always be the longest-lived and the burst
    // would still read as one stencil, just a stretched one.
    const uint32_t k = bubble_seed
                     ^ (static_cast<uint32_t>(index) * 2654435761u)
                     ^ 0x5F356495u;  // lifetime salt
    const float h = hash01(k);
    // Burst stays a readable rosette that merely stops vanishing as one flat
    // sheet; Spark Burst genuinely burns out at different times.
    return spark ? (0.55f + 0.45f * h) : (0.78f + 0.22f * h);
}

float ClickBubbleEffect::power_size_gain(float power) {
    // Exactly 1.0 at power 1.0, so an ordinary click is bit-identical to its
    // pre-T-024 appearance. A full-charge release (power 2.2) thickens every
    // mark by ~1.42x while its travel/radius scales by the full 2.2x -- the
    // burst grows mostly by spreading, not by turning into blobs.
    const float p = power > 1.0f ? power : 1.0f;
    return 1.0f + 0.35f * (p - 1.0f);
}

float ClickBubbleEffect::power_alpha(float alpha, float power) {
    // A modest lift only, hard-clamped: a charged release reads brighter but
    // can never exceed full opacity, and power 1.0 returns alpha untouched.
    const float p = power > 1.0f ? power : 1.0f;
    const float a = alpha * (1.0f + 0.15f * (p - 1.0f));
    return a > 1.0f ? 1.0f : a;
}

ElementTintTarget ClickBubbleEffect::element_target(ClickStyle style, float cool) {
    switch (style) {
        case ClickStyle::Air:
            // Pale, almost weightless blue-white.
            return {230.0f, 244.0f, 255.0f};
        case ClickStyle::Fire: {
            // Embers cool with age: bright yellow-orange -> deep red-orange.
            const float c = clamp01(cool);
            return {255.0f,
                    190.0f + (70.0f - 190.0f) * c,
                    60.0f + (20.0f - 60.0f) * c};
        }
        case ClickStyle::Water:
            return {60.0f, 150.0f, 255.0f};
        case ClickStyle::Earth:
            return {170.0f, 112.0f, 62.0f};
        default:
            return {0.0f, 0.0f, 0.0f};
    }
}

ElementTintTarget ClickBubbleEffect::elemental_color(
    ClickStyle style, float user_r, float user_g, float user_b,
    float strength, float cool) {
    if (!is_elemental(style)) return {user_r, user_g, user_b};
    const float t = clamp01(strength);
    const ElementTintTarget target = element_target(style, cool);
    return {user_r + (target.r - user_r) * t,
            user_g + (target.g - user_g) * t,
            user_b + (target.b - user_b) * t};
}

int ClickBubbleEffect::elemental_particle_count(ClickStyle style, int particle_amount) {
    // Particles off stays off for every element -- the user's 0 is absolute.
    if (particle_amount <= 0) return 0;
    switch (style) {
        case ClickStyle::Air:
        case ClickStyle::Fire:
            return particle_amount;
        case ClickStyle::Water:
            // The ripple rings carry Water; droplets are an accent only.
            return particle_amount / 3;
        case ClickStyle::Earth: {
            // Fewer but heavier chunks; never fewer than 3 while particles
            // are enabled, so the debris always reads as debris.
            const int n = (particle_amount + 1) / 2;
            return n < 3 ? 3 : n;
        }
        default:
            return particle_amount;
    }
}

ClickBubbleEffect::ElementParticle ClickBubbleEffect::elemental_particle(
    const Bubble& b, int index, int count, float progress) const {
    ElementParticle p;
    if (count <= 0) return p;

    const float n = static_cast<float>(count);
    const float i = static_cast<float>(index);
    const float prog = clamp01(progress);
    // T-024: the release payoff must be visible for the ELEMENTAL families
    // too, not only the ring-based ones. Particle geometry here is derived
    // from end_radius_px, so scaling that one value is what makes a charged
    // Air/Fire/Water/Earth discharge bigger instead of identical to a click.
    const float end_r = config_.end_radius_px * b.power;
    const float sg = power_size_gain(b.power);
    const float ring_alpha =
        power_alpha(config_.base_opacity * (1.0f - prog), b.power);
    const float two_pi = 6.2831853f;
    // T-025: fold in the bubble's own seed. Previously every jitter here
    // came from the particle index alone, so Air/Fire/Water/Earth replayed
    // the exact same arrangement on every single click -- the same defect
    // the user reported for Spark Burst. The formulas are untouched; only
    // the variation input widened, so each element keeps its motion
    // signature and its bounds.
    const uint32_t u = static_cast<uint32_t>(index) ^ b.seed;

    switch (config_.style) {
        case ClickStyle::Air: {
            // SIGNATURE: tangential swirl. The angle ADVANCES with progress
            // (a gust turning around the click) while the mote creeps only
            // moderately outward -- the motion reads sideways, not radial.
            const float swirl = 2.4f;
            const float angle = two_pi * i / n + 0.6f * hash01(u * 3u + 1u)
                              + swirl * prog;
            const float dist = end_r * (0.30f + 0.55f * prog)
                             * (0.80f + 0.40f * hash01(u * 7u + 2u));
            p.x = b.x + std::cos(angle) * dist;
            p.y = b.y + std::sin(angle) * dist;
            p.radius_px = 1.8f * sg * (1.0f - 0.45f * prog);
            p.alpha = ring_alpha * std::pow(1.0f - prog, 1.6f)
                    * (0.60f + 0.40f * hash01(u * 11u + 3u));
            break;
        }
        case ClickStyle::Fire: {
            // SIGNATURE: buoyancy. Every ember RISES monotonically (y only
            // decreases) with a lateral wobble, shrinks, flickers, and its
            // tint cools with age.
            const float lateral = (hash01(u * 5u + 1u) - 0.5f) * end_r * 0.50f;
            const float wobble = std::sin(two_pi * (prog + hash01(u * 13u + 2u)))
                               * end_r * 0.12f;
            const float rise = end_r * 1.15f * std::pow(prog, 0.75f);
            p.x = b.x + lateral + wobble;
            p.y = b.y - rise;
            p.radius_px = 3.2f * sg * (1.0f - 0.65f * prog)
                        * (0.70f + 0.60f * hash01(u * 17u + 3u));
            const float flicker = 0.55f + 0.45f
                * std::sin(two_pi * (prog * 2.0f + hash01(u * 19u + 4u)));
            p.alpha = ring_alpha * std::pow(1.0f - prog, 1.2f) * flicker;
            p.cool = prog;
            break;
        }
        case ClickStyle::Water: {
            // Accent droplets: thrown out low and pulled back down, so they
            // arc under the rings instead of flying away.
            // T-025: Water was the purest form of the defect the user
            // reported -- not one hash in the whole droplet, so every Water
            // click threw the identical droplets along the identical arcs
            // forever. The arc SIGNATURE is untouched (thrown out low, then
            // pulled back down under the rings); only the per-droplet
            // variation is real now.
            const float angle = two_pi * i / n
                              + (hash01(u * 37u + 1u) - 0.5f) * (two_pi / n) * 0.7f;
            const float up = end_r * 0.55f * (0.75f + 0.50f * hash01(u * 41u + 2u));
            const float gravity = end_r * 1.10f * (0.85f + 0.30f * hash01(u * 43u + 3u));
            const float throw_px = end_r * 0.45f
                                 * (0.70f + 0.60f * hash01(u * 47u + 4u));
            p.x = b.x + std::cos(angle) * throw_px * prog;
            p.y = b.y - up * prog + gravity * prog * prog;
            p.radius_px = 1.8f * sg * (1.0f - 0.40f * prog)
                        * (0.75f + 0.50f * hash01(u * 53u + 5u));
            p.alpha = ring_alpha * (0.70f + 0.30f * hash01(u * 59u + 6u));
            break;
        }
        case ClickStyle::Earth: {
            // SIGNATURE: weight. Chunky debris is thrown outward at a
            // per-chunk speed and then FALLS: by expiry every chunk sits
            // below the anchor. Size barely shrinks (rocks, not sparks) and
            // the alpha holds before dropping sharply.
            const float angle = two_pi * i / n + 0.5f * hash01(u * 23u + 1u);
            const float speed = end_r * (0.75f + 0.50f * hash01(u * 29u + 2u));
            const float dist = speed * prog;
            const float up = end_r * 0.45f;
            const float gravity = end_r * 1.30f;
            p.x = b.x + std::cos(angle) * dist;
            p.y = b.y + std::sin(angle) * dist * 0.35f
                - up * prog + gravity * prog * prog;
            p.radius_px = 3.4f * sg * (0.75f + 0.60f * hash01(u * 31u + 3u))
                        * (1.0f - 0.20f * prog);
            p.alpha = power_alpha(
                config_.base_opacity * (1.0f - std::pow(prog, 2.5f)), b.power);
            break;
        }
        default:
            break;
    }
    return p;
}

ClickBubbleEffect::RippleRing ClickBubbleEffect::water_ripple(
    const Bubble& b, int ring_index, float progress) const {
    // The rings are concentric on the anchor the caller already has; the
    // bubble is still needed for its T-024 release power.
    RippleRing r;
    if (ring_index < 0 || ring_index >= kWaterRipples) return r;

    // Staggered births: ring 0 leaves at the click, rings 1 and 2 follow 26%
    // and 52% of the life later. A ring that has not been born yet, or has
    // already completed its own life, is not live.
    const float phase = clamp01(progress) - 0.26f * static_cast<float>(ring_index);
    if (phase <= 0.0f || phase >= 1.0f) return r;

    const float k = static_cast<float>(ring_index);
    const float eased = apply_easing(config_.easing, phase);
    const float span = config_.end_radius_px - config_.start_radius_px;
    // T-024: a charged Water release throws visibly wider rings.
    r.radius_px = (config_.start_radius_px + span * eased)
                * (1.0f - 0.10f * k) * b.power;
    r.thickness_px = config_.outline_thickness_px * 0.55f
                   * power_size_gain(b.power);
    // Each ring owns its life, so a young inner ring is still bright while
    // the leading ring is already fading out.
    r.alpha = power_alpha(
        config_.base_opacity * (1.0f - phase) * (1.0f - 0.18f * k), b.power);
    r.live = true;
    return r;
}

void ClickBubbleEffect::draw(int64_t now_ns, ClickBubbleSink& sink) const {
    // Only live bubbles emit render state; expired ones stay silent (the
    // Application prunes them separately). One reserve + one emit per
    // bubble: no per-frame allocation in the sink path (C7). The ring and
    // fill alphas are both computed HERE (Phase K) -- the renderer only
    // draws what it receives.
    int live = 0;
    for (const Bubble& b : bubbles_) {
        if (progress_at(b, now_ns) < 1.0f) ++live;
    }
    // T-026: the detached wake is part of the same frame budget, so it is
    // counted in the single reserve hint (no per-emission sink churn).
    const std::size_t live_wake = live_wake_count(now_ns);
    sink.reserve_bubbles_hint(live + static_cast<int>(live_wake));

    // T-026 draw ORDER: the detached wake is HISTORY, so it goes down first.
    // The live release payoff and the attached aura then draw on top of the
    // path that produced them -- the three layers cooperate without any of
    // them owning another's lifecycle.
    for (const HoldWakeEmission& e : wake_) {
        if (wake_progress_at(e, now_ns) >= 1.0f) continue;
        draw_wake_emission(e, now_ns, sink);
    }

    const float cr = static_cast<float>(config_.color_r);
    const float cg = static_cast<float>(config_.color_g);
    const float cb = static_cast<float>(config_.color_b);

    for (const Bubble& b : bubbles_) {
        const float progress = progress_at(b, now_ns);
        if (progress >= 1.0f) continue;
        // T-024: a release payoff draws bigger, thicker and slightly
        // brighter than the click it replaces. Every one of these three is
        // EXACTLY the identity at power 1.0, so an ordinary click is
        // bit-identical to its pre-T-024 appearance.
        const float size_gain = power_size_gain(b.power);
        const float radius = radius_at(b, now_ns) * b.power;
        const float ring_alpha = power_alpha(opacity_at(b, now_ns), b.power);
        const float fill_alpha = power_alpha(fill_alpha_at(b, now_ns), b.power);
        const float thickness = config_.outline_thickness_px * size_gain;

        switch (config_.style) {
            case ClickStyle::DoubleRing:
                // Main ring + inner secondary ring at 62% radius.
                sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                ring_alpha, 0.0f);
                sink.add_bubble(b.x, b.y, radius * 0.62f, thickness, cr, cg, cb,
                                ring_alpha * 0.7f, 0.0f);
                break;

            case ClickStyle::Ripple:
                // Thin pure ring, no fill.
                sink.add_bubble(b.x, b.y, radius, thickness * 0.6f,
                                cr, cg, cb, ring_alpha, 0.0f);
                break;

            case ClickStyle::Burst:
            case ClickStyle::SparkBurst: {
                // T-017 Phase 5 repair: particles must travel MONOTONICALLY
                // OUTWARD for their whole visible lifetime. The previous
                // formula combined the expanding ring radius (grows with
                // progress) with a multiplier that DECREASED toward expiry:
                //     travel = radius(p) * (a + b * (1 - p))
                // which grows, peaks mid-life, then collapses inward -- the
                // opposite of "flying outward".
                //
                // Contract here: base excursion = a fixed base + a start-
                // anchored outward fly distance, all scaled by the ring's
                // END radius (so the burst scales with the configured ring
                // size) and modulated ONLY by per-index jitter for Spark
                // Burst. For a given particle i, distance from the anchor
                // is monotone non-decreasing in progress (jitter is
                // constant per particle and progress advances 0 -> 1).
                //
                // T-025: everything below used to be a stencil. The angle
                // was exactly 2*pi*i/n and the only variation,
                // `spark_scale(i)`, was a function of the particle INDEX --
                // so every Spark Burst click in the product's life rendered
                // the identical rosette at the identical angles, and plain
                // Burst had no variation at all. Per-bubble seeding fixes
                // that without touching the purity contract above: `b.seed`
                // is fixed at spawn, so each particle's scatter, speed, size
                // and alpha remain pure functions of (bubble, elapsed) and
                // still replay bit-identically at any frame cadence.
                const bool spark = config_.style == ClickStyle::SparkBurst;
                if (!spark) {
                    sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                    ring_alpha * 0.5f, 0.0f);
                }
                const int n = config_.particle_amount;
                const float sector = 6.2831853f
                                   / static_cast<float>(n > 0 ? n : 1);
                // T-024: the burst families derive ALL their geometry from
                // end_radius_px, so without scaling it here a fully charged
                // release would have looked identical to an ordinary click --
                // exactly the defect the draft had. Spark Burst therefore
                // discharges visibly harder after a real hold.
                const float burst_r = config_.end_radius_px * b.power;
                // Whole-burst rotation: without it every burst still lands
                // its first particle on the +x axis, which reads as "the
                // same shape again" even once the spokes are scattered.
                const float burst_rotation =
                    hash01(b.seed ^ 0x2545F491u) * 6.2831853f;
                // How far a spoke may wander inside its own sector. Spark
                // Burst scatters hard (chaotic sparks); Burst stays a
                // readable rosette that merely stops being a stencil.
                const float scatter = spark ? 0.85f : 0.45f;
                for (int i = 0; i < n; ++i) {
                    // T-025 per-particle LIFETIME. Each particle owns a
                    // deterministic visible-life fraction of the parent
                    // bubble; past it the particle simply stops being
                    // emitted. It is never dragged back inward -- travel
                    // below stays monotone in progress for the particle's
                    // whole visible life, which is the T-017 Phase 5
                    // contract the regressions still assert. Particle COUNT
                    // is untouched: this can only remove marks, never add.
                    if (progress >= burst_particle_life(b.seed, i, spark)) {
                        continue;
                    }
                    const uint32_t pk =
                        b.seed ^ (static_cast<uint32_t>(i) * 2654435761u);
                    const float angle =
                        burst_rotation + sector * static_cast<float>(i)
                        + (hash01(pk ^ 0x9E3779B9u) - 0.5f) * sector * scatter;
                    // Per-particle speed, CONSTANT for the particle's whole
                    // life -- that constancy is what keeps travel monotone
                    // in progress (the T-017 Phase 5 contract asserted by
                    // the regressions).
                    const float speed =
                        spark ? (0.55f + 0.90f * hash01(pk ^ 0x85EBCA6Bu))
                              : (0.80f + 0.45f * hash01(pk ^ 0x85EBCA6Bu));
                    // Base excursion: where the particle sits at birth,
                    // already slightly outside the start radius so it does
                    // not overlap the ring fill. End radius anchors the
                    // farthest travel at expiry (monotone growth).
                    const float base_px = burst_r * 0.45f;
                    const float fly_px = burst_r * (spark ? 0.95f : 0.70f);
                    const float travel = (base_px + fly_px * progress) * speed;
                    const float px = b.x + std::cos(angle) * travel;
                    const float py = b.y + std::sin(angle) * travel;
                    const float size_j = 0.60f + 0.85f * hash01(pk ^ 0xC2B2AE35u);
                    const float pr = (spark ? 3.0f : 2.5f) * size_gain * size_j
                                   * (1.0f - progress * 0.5f);
                    const float alpha_j = 0.55f + 0.45f * hash01(pk ^ 0x27D4EB2Fu);
                    sink.add_particle(px, py, pr, cr, cg, cb,
                                      ring_alpha * alpha_j);
                }
                break;
            }

            case ClickStyle::SoftFlash:
                // Soft filled flash disc, no ring.
                sink.add_bubble(b.x, b.y,
                                radius * 0.35f, thickness,
                                cr, cg, cb, 0.0f,
                                ring_alpha * 0.9f);
                break;

            case ClickStyle::DotRing:
                // Central dot + full ring with its subtle fill. The dot
                // grows with the charge too (T-024): the core IS the style.
                sink.add_particle(b.x, b.y, 3.0f * size_gain * b.power,
                                  cr, cg, cb, ring_alpha);
                sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                ring_alpha, fill_alpha);
                break;

            // T-022 Elemental Click VFX. Rings/discs are emitted first so
            // the particles always land ON TOP of their own element's base
            // shape (same draw-order discipline the T-021 audit imposed on
            // the trail sparkles). Every colour goes through
            // elemental_color(), so the user's Click color is always the
            // base and element_tint only biases it.
            case ClickStyle::Air:
            case ClickStyle::Fire:
            case ClickStyle::Water:
            case ClickStyle::Earth: {
                const float tint = config_.element_tint;
                const ElementTintTarget base_col =
                    elemental_color(config_.style, cr, cg, cb, tint, 0.0f);

                switch (config_.style) {
                    case ClickStyle::Air:
                        // A single fast, wide, very thin ring: the gust
                        // front. No fill -- air is not a disc.
                        sink.add_bubble(b.x, b.y, radius * 1.25f, thickness * 0.5f,
                                        base_col.r, base_col.g, base_col.b,
                                        ring_alpha * std::pow(1.0f - progress, 1.8f),
                                        0.0f);
                        break;

                    case ClickStyle::Fire:
                        // No ring at all: just a small hot base flash the
                        // embers rise out of.
                        sink.add_bubble(b.x, b.y + config_.end_radius_px * 0.05f,
                                        radius * 0.30f, thickness,
                                        base_col.r, base_col.g, base_col.b,
                                        0.0f,
                                        ring_alpha * 0.75f
                                            * std::pow(1.0f - progress, 1.5f));
                        break;

                    case ClickStyle::Water:
                        // Up to three concentric ripples with staggered
                        // births -- the repeating wave IS the signature.
                        for (int k = 0; k < kWaterRipples; ++k) {
                            const RippleRing rr = water_ripple(b, k, progress);
                            if (!rr.live) continue;
                            sink.add_bubble(b.x, b.y, rr.radius_px, rr.thickness_px,
                                            base_col.r, base_col.g, base_col.b,
                                            rr.alpha, 0.0f);
                        }
                        break;

                    case ClickStyle::Earth:
                        // Low, thick dust ring that never travels far.
                        sink.add_bubble(b.x, b.y, radius * 0.60f, thickness * 1.6f,
                                        base_col.r, base_col.g, base_col.b,
                                        ring_alpha * std::pow(1.0f - progress, 2.2f),
                                        0.0f);
                        break;

                    default:
                        break;
                }

                const int n = elemental_particle_count(config_.style,
                                                       config_.particle_amount);
                for (int i = 0; i < n; ++i) {
                    const ElementParticle ep = elemental_particle(b, i, n, progress);
                    if (ep.alpha <= 0.0f || ep.radius_px <= 0.0f) continue;
                    // Fire re-tints per particle (embers cool with age); the
                    // other elements pass cool = 0 and get the base colour.
                    const ElementTintTarget pc =
                        elemental_color(config_.style, cr, cg, cb, tint, ep.cool);
                    sink.add_particle(ep.x, ep.y, ep.radius_px,
                                      pc.r, pc.g, pc.b, ep.alpha);
                }
                break;
            }

            case ClickStyle::Ring:
            default:
                sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                ring_alpha, fill_alpha);
                break;
        }
    }

    // T-024: the hold aura renders AFTER the bubbles, so a release payoff
    // spawned on this same frame draws over the aura it came from.
    for (const Hold& h : holds_) {
        draw_hold(h, now_ns, sink);
    }
}

// T-024 press-and-hold aura.
//
// Every family below is expressed with the EXISTING sink contract -- rings
// (add_bubble) and dots (add_particle) -- which is why this feature needed
// no renderer change at all. Each style's hold is the same idea as its
// click, held in tension instead of fired: whatever the click DOES on
// release, the hold spends the charge time visibly getting ready to do it.
//
// Everything is a pure function of (hold, elapsed). Nothing accumulates per
// frame, so the aura is identical at any frame cadence, exactly like the
// bubbles.
void ClickBubbleEffect::draw_hold(const Hold& hold, int64_t now_ns,
                                  ClickBubbleSink& sink) const {
    // CANDIDATE GATE. Before the activation threshold this record draws
    // NOTHING -- that single early return is what stops a fast ordinary
    // click from flashing an aura on its way past. There is deliberately no
    // upper bound here either: a hold may last as long as the user wants.
    const float age_ms =
        static_cast<float>(now_ns - hold.start_timestamp_ns) / 1'000'000.0f;
    if (!(age_ms >= kHoldActivationMs)) return;

    const float charge = charge_at(hold, now_ns);
    // Time since ACTIVATION drives every cyclic family, so each animation
    // starts from zero the moment the aura appears instead of jumping into
    // the middle of a cycle.
    const float held_s = (age_ms - kHoldActivationMs) / 1000.0f;
    const float R = config_.end_radius_px;
    const float thickness = config_.outline_thickness_px;
    const float x = hold.x;
    const float y = hold.y;
    const float two_pi = 6.2831853f;

    // The aura still fades UP over its first 90 ms after activation, so the
    // transition from click to hold reads as a build rather than a pop.
    //
    // T-027: Hold Intensity scales the ATTACHED aura through the same named
    // baseline it always used. At 1.0 the expression is arithmetically
    // identical to the accepted T-024 appearance -- the multiplier is an
    // identity at its default, exactly like the other three Hold controls.
    const float onset = clamp01(held_s * 1000.0f / 90.0f);
    float A = config_.base_opacity * kHoldIntensity
            * config_.hold_intensity * onset
            * (0.35f + 0.65f * charge);
    // Hard clamp: at the maximum Hold Intensity the aura is louder, never
    // brighter than opaque. The renderer contract stays "alpha in 0..1".
    if (A > 1.0f) A = 1.0f;
    if (!(A > 0.0f)) return;

    const float cr = static_cast<float>(config_.color_r);
    const float cg = static_cast<float>(config_.color_g);
    const float cb = static_cast<float>(config_.color_b);
    const float tint = config_.element_tint;
    const ElementTintTarget base =
        elemental_color(config_.style, cr, cg, cb, tint, 0.0f);

    // Deterministic per-hold, per-index variation (T-025's seed, same idea).
    const auto hh = [&hold](int i, uint32_t salt) {
        return hash01(hold.seed ^ (static_cast<uint32_t>(i) * 2654435761u) ^ salt);
    };
    // Repeating 0..1 phase for the cyclic families.
    const auto frac = [](float v) { return v - std::floor(v); };

    const int amount = config_.particle_amount;

    switch (config_.style) {
        case ClickStyle::DoubleRing: {
            // Two rings closing on each other: the charge IS the gap.
            const float outer = R * (1.70f - 1.05f * charge);
            const float inner = R * (0.25f + 0.55f * charge);
            sink.add_bubble(x, y, outer, thickness, base.r, base.g, base.b, A, 0.0f);
            sink.add_bubble(x, y, inner, thickness, base.r, base.g, base.b,
                            A * 0.75f, 0.0f);
            break;
        }
        case ClickStyle::Ripple: {
            // Rings marching outward on a cadence that ACCELERATES with
            // charge -- the tell is the rate, not the size.
            const float rate = 0.9f + 1.8f * charge;
            for (int k = 0; k < 3; ++k) {
                const float ph = frac(held_s * rate + static_cast<float>(k) / 3.0f);
                sink.add_bubble(x, y, R * (0.25f + 1.25f * ph), thickness * 0.6f,
                                base.r, base.g, base.b, A * (1.0f - ph), 0.0f);
            }
            break;
        }
        case ClickStyle::Burst: {
            // Particles gathering INWARD -- the exact reverse of what the
            // release then does with them.
            const int n = amount > 4 ? amount : 4;
            const float orbit = R * (1.55f - 0.95f * charge);
            for (int i = 0; i < n; ++i) {
                const float ang = two_pi * static_cast<float>(i) / static_cast<float>(n)
                                + 0.35f * held_s + hh(i, 0x9E3779B9u) * 0.4f;
                const float d = orbit * (0.85f + 0.30f * hh(i, 0x85EBCA6Bu));
                sink.add_particle(x + std::cos(ang) * d, y + std::sin(ang) * d,
                                  2.2f * (0.70f + 0.60f * hh(i, 0xC2B2AE35u)),
                                  base.r, base.g, base.b,
                                  A * (0.55f + 0.45f * hh(i, 0x27D4EB2Fu)));
            }
            break;
        }
        case ClickStyle::SparkBurst: {
            // Crackle. Scattered radii and a flicker whose RATE climbs with
            // charge, so a nearly-full Spark Burst hold visibly buzzes.
            const int n = amount > 4 ? amount : 4;
            const float rate = 8.0f + 14.0f * charge;
            const uint32_t bucket = static_cast<uint32_t>(held_s * rate);
            for (int i = 0; i < n; ++i) {
                const float ang = two_pi * hh(i, 0x165667B1u)
                                + 1.1f * held_s * (0.5f + hh(i, 0xD1B54A35u));
                const float d = R * (0.50f + 1.10f * hh(i, 0x94D049BBu));
                const float flick = hash01(hold.seed
                                           ^ (static_cast<uint32_t>(i) * 2246822519u)
                                           ^ (bucket * 2654435761u));
                sink.add_particle(x + std::cos(ang) * d, y + std::sin(ang) * d,
                                  2.6f * (0.60f + 0.70f * hh(i, 0xA24BAED5u)),
                                  base.r, base.g, base.b,
                                  A * (0.25f + 0.75f * flick));
            }
            break;
        }
        case ClickStyle::SoftFlash: {
            // A disc drawing breath in.
            const float breathe = 1.0f + 0.05f * std::sin(two_pi * held_s * 2.2f);
            sink.add_bubble(x, y, R * (0.40f + 0.35f * charge) * breathe, thickness,
                            base.r, base.g, base.b, 0.0f, A);
            break;
        }
        case ClickStyle::DotRing: {
            // The dot swells while the ring closes on it.
            sink.add_bubble(x, y, R * (1.50f - 0.85f * charge), thickness,
                            base.r, base.g, base.b, A * 0.85f, 0.0f);
            sink.add_particle(x, y, 2.5f + 5.5f * charge,
                              base.r, base.g, base.b, A);
            break;
        }
        case ClickStyle::Air: {
            // Vortex spin-up: the motes barely creep outward, but their
            // angular speed climbs hard with charge.
            const int n = amount > 4 ? amount : 4;
            const float omega = 1.2f + 5.0f * charge;
            for (int i = 0; i < n; ++i) {
                const float ang = two_pi * static_cast<float>(i) / static_cast<float>(n)
                                + omega * held_s + hh(i, 0x3u) * 0.6f;
                const float d = R * (0.45f + 0.55f * hh(i, 0x7u));
                sink.add_particle(x + std::cos(ang) * d, y + std::sin(ang) * d,
                                  1.8f * (0.8f + 0.5f * hh(i, 0xBu)),
                                  base.r, base.g, base.b,
                                  A * (0.55f + 0.45f * hh(i, 0x11u)));
            }
            break;
        }
        case ClickStyle::Fire: {
            // A flame that GROWS: more embers as the charge builds, each
            // rising on its own loop and cooling as it goes.
            const int n_full = amount > 3 ? amount : 3;
            int n = static_cast<int>(static_cast<float>(n_full)
                                     * (0.35f + 0.65f * charge));
            if (n < 1) n = 1;
            for (int i = 0; i < n; ++i) {
                const float ph = frac(held_s * (0.8f + 0.5f * hh(i, 0x5u))
                                      + hh(i, 0xDu));
                const float lateral = (hh(i, 0x13u) - 0.5f) * R * 0.55f;
                const float wobble = std::sin(two_pi * (ph + hh(i, 0x17u)))
                                   * R * 0.10f;
                const ElementTintTarget ec =
                    elemental_color(ClickStyle::Fire, cr, cg, cb, tint, ph);
                sink.add_particle(x + lateral + wobble, y - R * 1.25f * ph,
                                  3.0f * (1.0f - 0.60f * ph)
                                       * (0.70f + 0.60f * hh(i, 0x1Du)),
                                  ec.r, ec.g, ec.b, A * (1.0f - ph));
            }
            break;
        }
        case ClickStyle::Water: {
            // Drips into a pool: rings leaving the anchor on a steady
            // cadence, the pool filling faster as the charge builds. This
            // repeating pool/ripple identity is the Water signature and is
            // kept exactly as the draft had it.
            const float rate = 0.8f + 1.0f * charge;
            for (int k = 0; k < 3; ++k) {
                const float ph = frac(held_s * rate + static_cast<float>(k) / 3.0f);
                const float fade = (1.0f - ph) * (1.0f - ph);
                sink.add_bubble(x, y, R * (0.15f + 1.15f * ph), thickness * 0.8f,
                                base.r, base.g, base.b, A * fade, 0.0f);
            }
            // T-024 K: rings ALONE read as just another Ripple. A small
            // bounded set of droplets hopping in and out of the pool on their
            // own cyclic phase is what makes this read as Water. Count is
            // bounded by the configured particle amount; nothing escapes the
            // ring field.
            const int n_drop_half = amount / 3;
            const int n_drop = n_drop_half > 2 ? n_drop_half : 2;
            for (int i = 0; i < n_drop; ++i) {
                const float ph = frac(held_s * (0.9f + 0.6f * hh(i, 0x2Bu))
                                      + hh(i, 0x2Fu));
                // One parabolic hop per cycle: up out of the pool, then back
                // down into it. Peaks at ph = 0.5, zero at both ends.
                const float hop = 4.0f * ph * (1.0f - ph);
                const float ang = two_pi * hh(i, 0x33u);
                const float d = R * (0.20f + 0.45f * hh(i, 0x37u));
                sink.add_particle(x + std::cos(ang) * d,
                                  y - R * 0.55f * hop + std::sin(ang) * d * 0.25f,
                                  2.0f * (0.75f + 0.55f * hh(i, 0x3Bu)),
                                  base.r, base.g, base.b,
                                  A * (0.45f + 0.55f * hop));
            }
            break;
        }
        case ClickStyle::Earth: {
            // WEIGHT, not orbit. The draft circled its chunks around the
            // anchor, which read as satellites in space rather than as
            // rubble on the ground. Now: a thick low dust ring pulsing on a
            // slow quake rhythm, and heavy chunks that sit STILL near the
            // anchor and quiver in place. No angular motion at all.
            const float quake = std::sin(two_pi * held_s * 2.7f);
            sink.add_bubble(x, y + R * 0.18f,
                            R * (1.35f - 0.55f * charge) * (1.0f + 0.03f * quake),
                            thickness * (1.4f + 0.6f * charge),
                            base.r, base.g, base.b, A * 0.70f, 0.0f);
            const int n_half = amount / 2;
            const int n = n_half > 3 ? n_half : 3;
            for (int i = 0; i < n; ++i) {
                // Fixed resting place per chunk: low, close, and constant in
                // angle for the whole hold.
                const float ang = two_pi * hh(i, 0x17u);
                const float d = R * (0.25f + 0.45f * hh(i, 0x1Bu))
                              * (0.85f + 0.30f * charge);
                // Tremble: a small per-chunk displacement on its own phase,
                // amplitude bounded to a few percent of the ring radius, so
                // the chunks shake rather than travel.
                const float tr = std::sin(two_pi
                    * (held_s * (2.2f + 1.6f * hh(i, 0x1Fu)) + hh(i, 0x23u)));
                const float amp = R * 0.05f * (0.5f + 0.5f * charge);
                sink.add_particle(x + std::cos(ang) * d + tr * amp,
                                  y + R * 0.18f + std::sin(ang) * d * 0.40f
                                      + std::fabs(tr) * amp * 0.6f,
                                  3.4f * (0.80f + 0.55f * hh(i, 0x25u)),
                                  base.r, base.g, base.b,
                                  A * (0.65f + 0.35f * hh(i, 0x29u)));
            }
            break;
        }
        case ClickStyle::Ring:
        default: {
            // The baseline: one ring closing in and brightening, breathing
            // once it is full so a held-forever button still reads as alive.
            float ring = R * (1.70f - 1.05f * charge);
            if (charge >= 1.0f) {
                ring *= 1.0f + 0.06f * std::sin(two_pi * held_s * 1.6f);
            }
            sink.add_bubble(x, y, ring, thickness * (0.8f + 0.9f * charge),
                            base.r, base.g, base.b, A, 0.0f);
            break;
        }
    }
}

} // namespace ptd
