#pragma once

#include "../core/cursor_history.h"
#include "trail_config.h"

#include <cstdint>
#include <vector>

namespace ptd {

// TrailEffect (T-008 B1, extended MVP 05 Phases F/G): owns ALL trail-
// specific simulation, geometry, WIDTH and FADE decisions. It consumes
// CursorHistory and emits antialiasing-ready line segments with per-
// segment alpha and thickness; it never touches Direct2D, windows, or
// input (rendering ownership stays with OverlayWindow, input with
// MouseInput).
//
// Coordinate space: virtual-screen physical pixels in, virtual-screen
// physical pixels out. The overlay-local transform (B4) is applied by the
// renderer sink, not here (MVP 08 will replace it with per-monitor math).
//
// All time math uses the monotonic nanosecond clock from Phase A. Fade is
// a pure function of elapsed time, never of frame count (B6).

// One trail point in virtual-screen physical pixels.
struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
    // Normalized window position 0..1: 0 = oldest visible (lifetime
    // boundary, alpha 0), 1 = newest visible (now, full base opacity).
    float t = 0.0f;
};

// Normalized float RGB color for a trail segment (T-015 Phase 4).
struct TrailColorF {
    float r = 1.0f;
    float g = 1.0f;
    float b = 0.0f;

    bool operator==(const TrailColorF&) const = default;
};

// Output interface for TrailEffect::build_geometry. Implemented by the
// renderer (OverlayWindow) and by tests. Segments arrive oldest -> newest
// with monotonically non-decreasing alpha. Thickness and color are per
// segment (T-015 Phase 4: the effect owns color selection math, the
// renderer only strokes what it is given).
class TrailGeometrySink {
public:
    virtual ~TrailGeometrySink() = default;

    // Called once before the first add_segment with an upper bound on the
    // segment count, so sinks can preallocate once and never churn per
    // frame (B10).
    virtual void reserve_hint(int segment_count) = 0;

    // One antialiased stroke segment. alpha is in [0, base_opacity];
    // thickness_px is the stroke width for this segment; color is the
    // normalized Direct2D RGB color.
    virtual void add_segment(float x1, float y1, float x2, float y2,
                             float alpha, float thickness_px,
                             TrailColorF color) = 0;
};

class TrailEffect {
public:
    static constexpr float kColorAccentSpan = 0.25f;

    explicit TrailEffect(TrailConfig config = {});

    void set_config(const TrailConfig& config) { config_ = TrailConfig::validated(config); }
    const TrailConfig& config() const { return config_; }

    // ---- Pure math (MVP 05 Phases F/G; testable without Direct2D) ----

    // Fade equation (B6 + Phase G). Input is AGE progress: 0 = newest
    // (head), 1 = lifetime boundary (oldest). fade_start holds full base
    // opacity for age_progress <= fade_start; the remainder performs the
    // selected FadeCurve; alpha always reaches exactly 0 at the boundary.
    // Pure function of elapsed time -> frame-rate independent.
    float fade(float age_progress) const;

    // FadeCurve application: curve f: [0,1] -> [0,1], f(0)=0, f(1)=1.
    //   Linear  -> p
    //   Smooth  -> 3p^2 - 2p^3 (smoothstep)
    //   EaseOut -> 1 - (1-p)^3
    static float apply_fade_curve(FadeCurve curve, float p);

    // Alpha of a sample at window position t in [0,1] (t=0 -> 0, t=1 ->
    // base_opacity). Clamped; never negative or out of range.
    float alpha_at(float t) const;

    // Width contract (Phase F), t = window position (0=tail, 1=head):
    //   target(t)      = lerp(tail_thickness_px, head_thickness_px, t)
    //   final(t)       = lerp(head_thickness_px, target(t), taper_strength)
    // taper_strength 0 -> constant head width (approved baseline);
    // taper_strength 1 -> full tail-to-head interpolation. Result is
    // clamped to [kMinThicknessPx, kMaxThicknessPx]; never NaN/negative.
    float width_at(float t) const;

    // Pure color selection math (T-015 Phase 3). Normalized RGB suitable
    // for Direct2D, with t clamped to [0,1].
    //   Full        -> Start color for all t
    //   Gradient    -> lerp(Fade, Start, t)
    //   StartAccent -> Fade for t <= 0.75, lerp(Fade, Start) for 0.75 < t <= 1.0
    //   FadeAccent  -> Start for t >= 0.25, lerp(Fade, Start) for 0 <= t < 0.25
    TrailColorF color_at(float t) const;

    // ---- Style math (T-016 / Post-MVP V2; pure, testable) ----

    // Pulse style period. The alpha multiplier is a pure function of
    // `now_ns`: 1.0 for every non-Pulse style, 0.6..1.0 cosine pulse for
    // Pulse.
    static constexpr int kPulsePeriodMs = 900;

    // Dots (Dotted/Spark) are emitted as short stubs so the existing
    // round-cap stroke renders them as dots; no new renderer primitive.
    static constexpr float kDotStubPx = 1.5f;

    // Alpha multiplier for the style at absolute time `now_ns`.
    float pulse_multiplier(int64_t now_ns) const;

    // Width of the style at window position t. Classic/SoftGlow/Neon/
    // Dotted/Pulse/Spark keep the Phase F baseline; Comet applies a strong
    // quadratic tail taper; Ribbon forces a full 25%-head-to-head profile.
    // Always within [kMinThicknessPx, kMaxThicknessPx].
    float style_width_at(float t) const;

    // Deterministic Spark per-dot flicker in [0.35, 1.0]: a pure function
    // of the dot index and the 100 ms time bucket of `now_ns` (same inputs
    // -> same alpha, any frame cadence; integer hash, no std::sin cost).
    static float spark_flicker(int dot_index, int64_t now_ns);
    static uint32_t style_hash(uint32_t x);

    // Builds the visible trail geometry for `now_ns`.
    //
    // B3 rules implemented here:
    //  - only samples inside the active lifetime window [now-lifetime, now]
    //    are consumed; stale samples are excluded (never rendered forever);
    //  - button-transition samples contribute their coordinates like any
    //    other sample (button identity is ignored for the line);
    //  - consecutive identical positions (0.25 px quantization grid) are
    //    collapsed for geometry;
    //  - the tail boundary point is interpolated at exactly
    //    now-lifetime from its two surrounding samples, so the tail
    //    retracts smoothly and frame-time independently;
    //  - `live_head` (optional): the current GetCursorPos position sampled
    //    at render time; a straight head segment pins the trail tip to the
    //    real pointer (no visible cursor lag, B5/B12).
    //
    // `history_count_bound` is the CursorHistory capacity; it bounds the
    // scratch buffer and the sink reserve hint. Single-threaded (GUI
    // thread), like CursorHistory.
    void build_geometry(const CursorHistory& history,
                        int64_t now_ns,
                        const TrailPoint* live_head,
                        int history_count_bound,
                        TrailGeometrySink& sink) const;

    // Maximum segments one build can emit for a given history capacity
    // (used by sinks to size fixed storage; B10 boundedness contract).
    static int max_segments_for(int history_count_bound);

private:
    struct BuildPoint {
        float x;
        float y;
        float t;       // window position 0..1 (0 = lifetime boundary)
        int64_t ts;    // exact sample timestamp (ns); 0 for synthetic points
    };

    void emit_polyline(const std::vector<BuildPoint>& pts,
                       float base_alpha, int64_t now_ns,
                       TrailGeometrySink& sink) const;
    void emit_catmull_rom(const std::vector<BuildPoint>& pts,
                          float base_alpha, int64_t now_ns,
                          TrailGeometrySink& sink) const;

    TrailConfig config_;

    // Reused scratch buffer (single-threaded): no per-frame heap churn.
    mutable std::vector<BuildPoint> scratch_;
};

} // namespace ptd
