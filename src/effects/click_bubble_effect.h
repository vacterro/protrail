#pragma once

#include "../core/cursor_sample.h"
#include "click_config.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptd {

// ClickBubbleEffect (T-009 / MVP 04): owns ALL click-bubble lifecycle,
// animation-state and geometry decisions. It consumes normalized input
// events (CursorSample button transitions) and emits per-bubble render
// state; it never touches Direct2D, windows, or the raw-input API
// (rendering ownership stays with OverlayWindow, input with MouseInput --
// same split as TrailEffect, ROADMAP/ARCHITECTURE.md).
//
// Event flow (C3): MouseInput -> CursorSample -> Application/controller ->
// on_button_down() -> spawn(). The spawn position is the position stored
// IN the click CursorSample (the authoritative GetCursorPos coordinate of
// the down packet); the bubble stays anchored there even if the mouse
// moves immediately afterward.
//
// Time model (C5): every animation value is a pure function of
// elapsed = now_ns - start_timestamp_ns (monotonic QPC clock). No
// per-frame state mutation, no pixels-per-frame integration, so results
// are identical at any frame cadence.
//
//   progress = clamp(elapsed / duration, 0..1)
//   ease     = ease_out_cubic(progress) = 1 - (1 - progress)^3
//   radius   = start_radius + (end_radius - start_radius) * ease
//   opacity  = base_opacity * (1 - progress)
//
// A bubble is live while progress < 1. Expired bubbles are removed by
// prune(). Memory is bounded: fixed vector + kMaxActiveBubbles cap (C9);
// on overflow completed bubbles are pruned first, then the OLDEST active
// bubble is discarded. Single-threaded (GUI thread), like CursorHistory.

// Output interface for ClickBubbleEffect drawing (C7 + MVP 05 Phase K),
// mirroring TrailGeometrySink: the effect describes render state (center,
// radii, opacity, outline, fill), the renderer issues the Direct2D calls.
// Coordinates arrive in virtual-screen physical pixels; the overlay-local
// transform is applied by the renderer sink, not here (same contract as
// the trail, C8).
class ClickBubbleSink {
public:
    virtual ~ClickBubbleSink() = default;

    // Called once before the first add_bubble with the number of bubbles
    // about to be drawn (sinks preallocate once; no per-frame churn).
    // Named distinctly from TrailGeometrySink::reserve_hint so a renderer
    // implementing both interfaces keeps the two contracts separate.
    virtual void reserve_bubbles_hint(int bubble_count) = 0;

    // One bubble: centered at (cx, cy), radius_px outer radius,
    // outline_thickness_px stroke width, color channels 0..255, ring
    // alpha in [0, base_opacity], fill_alpha in [0, fill_opacity] -- the
    // EFFECT computes both alphas (Phase K); the renderer must not
    // re-derive the fill from the ring.
    virtual void add_bubble(float cx, float cy, float radius_px,
                            float outline_thickness_px,
                            float r, float g, float b,
                            float ring_alpha, float fill_alpha) = 0;

    // T-017: one particle dot (Burst / Spark Burst / Dot Ring center).
    // Default empty body keeps existing sink implementations (test
    // doubles) compiling; styles that never emit particles pay nothing.
    virtual void add_particle(float cx, float cy, float radius_px,
                              float r, float g, float b, float alpha) {
        (void)cx; (void)cy; (void)radius_px;
        (void)r; (void)g; (void)b; (void)alpha;
    }
};

class ClickBubbleEffect {
public:
    // Defensive active-bubble cap (C9). 128 covers any realistic clicking
    // cadence for the 250 ms bubble life (including synthetic/stress
    // input) while bounding per-frame draw cost and memory.
    static constexpr std::size_t kMaxActiveBubbles = 128;

    // One bubble's bounded state (C4): anchor + birth timestamp. Derived
    // values (progress/radius/opacity) are computed on demand from elapsed
    // time; no frame-by-frame history is stored.
    struct Bubble {
        float x = 0.0f;             // virtual-screen physical pixels
        float y = 0.0f;             // virtual-screen physical pixels
        int64_t start_timestamp_ns = 0;  // monotonic clock
    };

    explicit ClickBubbleEffect(ClickConfig config = {});

    void set_config(const ClickConfig& config) { config_ = ClickConfig::validated(config); }
    const ClickConfig& config() const { return config_; }

    // C3/C17: normalized input entry point. Spawns exactly one bubble for
    // a ButtonAction::Down transition when the sample's button trigger is
    // enabled in the config (trigger_left/right/middle, Phase J) AND the
    // effect is enabled. Up transitions and movement samples spawn
    // nothing. Returns true when a bubble was spawned.
    bool on_button_down(const CursorSample& sample);

    // Direct spawn primitive (used by on_button_down; exposed for tests).
    // Anchors the bubble at (x, y) with birth time start_timestamp_ns.
    bool spawn(float x, float y, int64_t start_timestamp_ns);

    // C9: remove completed bubbles (age >= duration). Safe to call every
    // frame. Returns the number of bubbles removed.
    std::size_t prune(int64_t now_ns);

    // C5/C10: whether any bubble is still animating at `now_ns`. Drives
    // the shared active-only render scheduler together with the trail.
    bool has_live_content(int64_t now_ns) const;

    // Number of currently active (unexpired) bubbles.
    std::size_t active_count() const { return bubbles_.size(); }
    bool empty() const { return bubbles_.empty(); }
    void clear() { bubbles_.clear(); }

    const std::vector<Bubble>& bubbles() const { return bubbles_; }

    // ---- Pure animation math (C12 + MVP 05 Phase J: testable without
    // Direct2D) ----

    // Easing application (Phase J). f: [0,1] -> [0,1], f(0)=0, f(1)=1:
    //   Linear  -> p                     (constant expansion rate)
    //   Smooth  -> 3p^2 - 2p^3            (gentle start and end)
    //   EaseOut -> 1 - (1-p)^3           (the approved MVP 04 curve)
    static float apply_easing(ClickEasing easing, float p);

    // Kept for regression compatibility: the approved MVP 04 curve.
    static float ease_out_cubic(float progress);

    // progress = clamp((now - start) / duration, 0..1); pure function of
    // elapsed monotonic time and the config duration.
    float progress_at(const Bubble& bubble, int64_t now_ns) const;

    // radius = start + (end - start) * apply_easing(easing, progress).
    // Monotone non-decreasing in progress for every easing; exactly
    // start_radius at birth and exactly end_radius at expiry.
    float radius_at(const Bubble& bubble, int64_t now_ns) const;

    // opacity = base_opacity * (1 - progress). Exactly base_opacity at
    // birth, exactly 0 at/after expiry.
    float opacity_at(const Bubble& bubble, int64_t now_ns) const;

    // Fill alpha (Phase K): fill_opacity * (1 - progress) -- the subtle
    // inner disc fades with the same time contract as the ring. Exactly
    // fill_opacity at birth, exactly 0 at/after expiry.
    float fill_alpha_at(const Bubble& bubble, int64_t now_ns) const;

    // Emits the render state of every live bubble into `sink` for the
    // frame at `now_ns` (C7). Pure: never mutates state; expired bubbles
    // emit nothing (alpha 0). Call prune() separately to reclaim them.
    void draw(int64_t now_ns, ClickBubbleSink& sink) const;

private:
    ClickConfig config_;
    std::vector<Bubble> bubbles_;  // bounded by kMaxActiveBubbles
};

} // namespace ptd
