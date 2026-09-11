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
    return spawn(static_cast<float>(sample.x), static_cast<float>(sample.y),
                 sample.timestamp_ns);
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
    bubbles_.push_back(Bubble{x, y, start_timestamp_ns});
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
    return removed;
}

bool ClickBubbleEffect::has_live_content(int64_t now_ns) const {
    // Vector is birth-ordered and progress is monotone in birth order
    // (equal durations), so if any bubble is live, the newest one is.
    return !bubbles_.empty()
        && progress_at(bubbles_.back(), now_ns) < 1.0f;
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
    sink.reserve_bubbles_hint(live);
    if (live == 0) return;

    const float cr = static_cast<float>(config_.color_r);
    const float cg = static_cast<float>(config_.color_g);
    const float cb = static_cast<float>(config_.color_b);

    // Particle count helper (deterministic per-index jitter for SparkBurst).
    auto spark_scale = [](int index) {
        uint32_t h = static_cast<uint32_t>(index) * 747796405u + 2891336453u;
        h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
        const float f = static_cast<float>(h & 0x00FFFFFFu)
                      / static_cast<float>(0x01000000u);
        return 0.6f + 0.8f * f;  // 0.6..1.4
    };

    for (const Bubble& b : bubbles_) {
        const float progress = progress_at(b, now_ns);
        if (progress >= 1.0f) continue;
        const float radius = radius_at(b, now_ns);
        const float ring_alpha = opacity_at(b, now_ns);
        const float fill_alpha = fill_alpha_at(b, now_ns);
        const float thickness = config_.outline_thickness_px;

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
                const bool spark = config_.style == ClickStyle::SparkBurst;
                if (!spark) {
                    sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                    ring_alpha * 0.5f, 0.0f);
                }
                const int n = config_.particle_amount;
                for (int i = 0; i < n; ++i) {
                    const float angle = 6.2831853f * static_cast<float>(i)
                                      / static_cast<float>(n > 0 ? n : 1);
                    const float jitter = spark ? spark_scale(i) : 1.0f;
                    // Base excursion: where the particle sits at birth,
                    // already slightly outside the start radius so it does
                    // not overlap the ring fill. End radius anchors the
                    // farthest travel at expiry (monotone growth).
                    const float base_px = config_.end_radius_px * 0.45f;
                    const float fly_px = config_.end_radius_px * (spark ? 0.95f : 0.70f);
                    const float travel = (base_px + fly_px * progress) * jitter;
                    const float px = b.x + std::cos(angle) * travel;
                    const float py = b.y + std::sin(angle) * travel;
                    const float pr = (spark ? 3.0f : 2.5f) * (1.0f - progress * 0.5f);
                    sink.add_particle(px, py, pr, cr, cg, cb,
                                      ring_alpha * (spark ? jitter * 0.7f : 1.0f));
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
                // Central dot + full ring with its subtle fill.
                sink.add_particle(b.x, b.y, 3.0f, cr, cg, cb, ring_alpha);
                sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                ring_alpha, fill_alpha);
                break;

            case ClickStyle::Ring:
            default:
                sink.add_bubble(b.x, b.y, radius, thickness, cr, cg, cb,
                                ring_alpha, fill_alpha);
                break;
        }
    }
}

} // namespace ptd
