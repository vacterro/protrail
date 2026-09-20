#pragma once

#include "../core/cursor_history.h"
#include "trail_config.h"

#include <cmath>
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

// T-021: shape of one sparkle decoration. The renderer draws each shape
// from cached Direct2D resources only (FillEllipse / cached-style lines);
// no per-sparkle geometry or brush allocation.
enum class TrailSparkleShape : uint8_t {
    Dot = 0,
    Cross = 1,
    Diamond = 2,
    Triangle = 3,
};

// One deterministic sparkle record in virtual-screen physical pixels.
// Pure data with value equality: identical (history, config, now_ns) must
// produce identical records (T-021 Phase 14), so tests compare records
// byte-wise. There is deliberately no identity beyond these fields -- no
// persistent particle state exists anywhere.
struct TrailSparkleRecord {
    float x = 0.0f;
    float y = 0.0f;
    float size_px = 0.0f;
    float rotation_rad = 0.0f;
    float alpha = 0.0f;
    TrailColorF color{};
    TrailSparkleShape shape = TrailSparkleShape::Dot;

    bool operator==(const TrailSparkleRecord&) const = default;
};

// Output interface for TrailEffect::build_geometry. Implemented by the
// renderer (OverlayWindow) and by tests. Segments arrive oldest -> newest
// in path order. Pulse intentionally varies alpha along the path.
// Thickness and color are per
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

    // T-021: optional sparkle decoration primitive. Default no-op so every
    // existing sink (and every pre-T-021 test) keeps compiling unchanged;
    // OverlayWindow overrides it with the cached-resource renderer. A
    // sparkle arrives fully resolved: position (virtual-screen physical
    // px), size, rotation, alpha, color (derived from the LOCAL trail
    // color) and shape. Rotation is in radians.
    virtual void add_sparkle(float x, float y, float size_px,
                             float rotation_rad, float alpha,
                             TrailColorF color,
                             TrailSparkleShape shape) {
        (void)x; (void)y; (void)size_px; (void)rotation_rad;
        (void)alpha; (void)color; (void)shape;
    }
};

class TrailEffect {
    struct BuildPoint {
        float x;
        float y;
        float t;       // window position 0..1 (0 = lifetime boundary)
        int64_t ts;    // stable sample timestamp (ns) owning this point
        // A real point starts with zero offset. A synthetic tail carries
        // its distance from the stale predecessor; such spans never seed.
        float occ_arc0 = 0.0f;
        // True only for a live-head point APPENDED beyond the newest real
        // sample. Its position is re-read from the OS every frame, so no
        // sparkle may be anchored to the piece it terminates.
        bool live = false;
        bool synthetic = false; // moving lifetime-boundary tail, never an anchor
    };

    // One piece of the canonical stroke path: exactly the geometry the
    // renderer strokes (a polyline chord, or one Catmull-Rom subdivision).
    struct CanonicalPiece {
        float x1, y1, t1;
        float x2, y2, t2;
        int64_t occ_ts;   // stable identity of the owning source occurrence
        float occ_arc;    // arc from the occurrence start to (x1, y1)
        bool live;        // unfinalized span or moving synthetic-tail context
    };

public:
    static constexpr float kColorAccentSpan = 0.25f;

    explicit TrailEffect(TrailConfig config = {});

    void set_config(const TrailConfig& config) { config_ = TrailConfig::validated(config); }
    const TrailConfig& config() const { return config_; }

    // PERF-003 quality ceiling: the fixed subdivision count the adaptive
    // walker may never exceed, and the reference tessellation the equivalence
    // regressions compare against.
    static constexpr int kCurveSubdivisionCeiling = 12;

    // PERF-003 TEST-ONLY reference oracle. When enabled, the canonical curve
    // walkers use the previous fixed kCurveSubdivisionCeiling pieces per span
    // instead of adaptive tessellation. Production never enables it; the
    // equivalence/geometric-error regressions use it as the reference.
    void set_reference_tessellation_for_tests(bool on) {
        reference_tessellation_ = on;
    }
    bool reference_tessellation_for_tests() const {
        return reference_tessellation_;
    }

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

    // Period of the traveling Pulse wave.
    static constexpr int kPulsePeriodMs = 900;

    // Dots (Dotted/Spark) are emitted as short stubs so the existing
    // round-cap stroke renders them as dots; no new renderer primitive.
    static constexpr float kDotStubPx = 1.5f;

    // Compatibility sample of the Pulse wave at the head.
    float pulse_multiplier(int64_t now_ns) const;

    // Same math without an instance, so the static sparkle model can
    // breathe with the Pulse stroke it decorates (a decoration that stayed
    // at constant brightness over a pulsing trail read as a separate,
    // detached layer).
    static float style_pulse_multiplier(TrailStyle style, float t, int64_t now_ns);

    // Pure spatial/time style profiles. Classic and unrelated styles retain
    // the Phase F baseline; Comet is head-heavy, Pulse carries a traveling
    // wave, and Ribbon has broad/narrow twist lobes.
    // Always within [kMinThicknessPx, kMaxThicknessPx].
    float style_width_at(float t, int64_t now_ns = 0) const;
    float style_alpha_multiplier(float t, int64_t now_ns) const;

    // Deterministic Spark per-dot flicker in [0.35, 1.0]: a pure function
    // of the dot index and the 100 ms time bucket of `now_ns` (same inputs
    // -> same alpha, any frame cadence; integer hash, no std::sin cost).
    static float spark_flicker(int dot_index, int64_t now_ns);
    static uint32_t style_hash(uint32_t x);

    // ---- T-021 sparkle decoration overlay ----

    // Hard per-frame emission cap (Phase 10), enforced BEFORE any sink
    // call inside build_geometry. No configuration, history length, cursor
    // speed or window geometry can produce more than this many sparkles
    // per frame.
    static constexpr int kMaxSparklesPerFrame = 128;

    // T-021 spatial sparkle sampler: base arc-length spacing between sparkle
    // candidate slots. Each mode scales it ONCE via SparkleModeParams:
    //     effective_spacing = kSparkleBaseSpacingPx / mode_density
    // so a denser mode places more slots per unit length. Amount is the
    // only acceptance authority: at Amount 1.0 every slot emits, below it
    // slots are deterministically hash-rejected.
    //
    // Finalized source spans are grouped into fixed 80 ms occurrences,
    // each owned by its first real sample. A path may yield slightly
    // different counts at different raw input rates, but no slot moves
    // when the visible trail window advances.
    static constexpr float kSparkleBaseSpacingPx = 18.0f;

    // Emitted sparkle size bounds after all mode scaling (Phase 16 test
    // contract: no emitted size outside the validated envelope).
    static constexpr float kMinSparkleEmittedPx = 1.0f;
    // T-023 rework: with the widened 16 px config ceiling and the louder
    // mode size scales, the old 16 px emitted cap clipped Twinkle's flash
    // and Firefly's drifters back to the size the user called invisible.
    // 48 px is still a hard, testable envelope -- it just no longer IS the
    // limit at ordinary settings.
    static constexpr float kMaxSparkleEmittedPx = 48.0f;

    // Per-mode character (Phase 8). Exposed so tests prove the four
    // families differ by CONSTRUCTION (density/size/pulse/shimmer/drift),
    // not by screenshot.
    //
    // DENSITY IS APPLIED EXACTLY ONCE (audit Phase 5): `density` sets the
    // spatial slot frequency and nothing else --
    //     effective_spacing = kSparkleBaseSpacingPx / density
    // -- while the user's Amount is the ONLY acceptance authority
    //     accept = sparkle_amount
    // so at Amount 1.0 every generated spatial slot becomes a sparkle and
    // the emitted count is deterministic for a given immutable history.
    // pulse/shimmer/drift are pure functions of the slot age and now_ns,
    // keeping the model stateless.
    struct SparkleModeParams {
        float density;           // spatial slot frequency (>0)
        float spread_scale;      // x config sparkle_spread_px
        float size_scale;        // x config sparkle_size_px
        float alpha_scale;       // x local trail alpha
        float brightness;        // color multiplier, clamped to 1
        float pulse_period_ms;   // 0 = no size pulse (Twinkle)
        float shimmer_bucket_ms; // 0 = no alpha shimmer (Glitter)
        float spin_rad_per_s;    // Twinkle rotation
    };
    static SparkleModeParams sparkle_params(TrailSparkleMode mode);

    // Deterministic shape selection for a sparkle seed (Phase 8/14).
    static TrailSparkleShape sparkle_shape(TrailSparkleMode mode, uint32_t seed);

    // One spatial sparkle slot: the pure sparkle model's whole input.
    // x/y/t are interpolated along the actual geometry at the slot
    // position. occurrence_ts is the first real sample timestamp in its
    // fixed source-time occurrence (never now_ns or the window boundary).
    // slot_ordinal is the lattice index k WITHIN that occurrence, counted
    // from the occurrence's own real start point -- so it does not change
    // when the lifetime tail eats into the segment, when the trail slides,
    // or when older slots expire.
    struct SparklePoint {
        float x = 0.0f;
        float y = 0.0f;
        float t = 0.0f;           // window position 0..1 (0=tail, 1=head)
        int64_t occurrence_ts = 0; // stable source occurrence identity
        int slot_ordinal = 0;      // slot index within occurrence
    };

    // T-021 pure deterministic sparkle model: resolves ONE spatial slot
    // under (config, now_ns). Returns false when the slot carries no
    // sparkle (mode Off, acceptance gate rejected, or alpha 0). Exposed
    // for determinism/mode-character tests; emit_sparkles_arc() drives it
    // per slot and owns the per-frame emission cap.
    static bool sparkle_for_point(const SparklePoint& point,
                                  const TrailConfig& config,
                                  int64_t now_ns,
                                  TrailSparkleRecord& out);

    // T-021 PURE TEST SEAM (Phase 10 curve-alignment regression).
    //
    // Collects the spatial sparkle SLOTS the current config would generate
    // for (history, now_ns, live_head) BEFORE the deterministic acceptance
    // gate and BEFORE the per-sparkle random spread offset, in emission
    // order (head -> tail). Tests assert the raw anchors lie on the
    // canonical stroke path; production never calls this.
    void collect_sparkle_slots(const CursorHistory& history,
                               int64_t now_ns,
                               const TrailPoint* live_head,
                               std::vector<SparklePoint>& out) const;

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
    // Fills scratch_ with the visible BuildPoint path for `now_ns`
    // (window clipping, duplicate collapse, synthetic lifetime-boundary
    // tail, live head). Returns false when fewer than two points remain.
    // Shared by build_geometry and the pure slot test seam so both always
    // observe byte-identical geometry.
    bool prepare_points(const CursorHistory& history,
                        int64_t now_ns,
                        const TrailPoint* live_head) const;

    // PERF-003: subdivisions for one span's cubic Bezier. Adaptive by default;
    // fixed kCurveSubdivisionCeiling when the test-only reference oracle is
    // enabled. Both canonical walkers call this so stroke and sparkles can
    // never diverge.
    int subdivisions_for_span(float x0, float y0, float c1x, float c1y,
                              float c2x, float c2y, float x1, float y1,
                              float t_span) const;

    // T-021 spatial sparkle sampler -- WORLD-ANCHORED lattice.
    //
    // Slots are placed in the coordinate frame of the SOURCE OCCURRENCE
    // that owns them: inside the occurrence, slot k sits at arc distance
    // phase(occurrence) + k * spacing from the occurrence's real start
    // point, where phase is a deterministic hash of the occurrence's
    // timestamp. Both the start point and the timestamp are frozen history
    // samples, so a slot's world position and its seed are fixed for the
    // slot's whole life: the glitter is generated where the cursor passed
    // and STAYS there, scattering and fading in place, instead of being
    // dragged along behind the pointer.
    //
    // Earlier revisions anchored the lattice to the window (tail-relative,
    // then head-relative). Both are window frames that translate as the
    // trail slides, so every sparkle crawled along the stroke. The
    // occurrence frame is the only one that does not move.
    //
    // Cost of the change, recorded honestly: the lattice phase restarts at
    // every 80 ms source occurrence, so different raw sampling rates can
    // change count slightly. The source occurrence start remains fixed.
    // The newest span and any span with a live or moving-tail neighbor are
    // unfinalized and cannot create permanent anchors.
    //
    // Boundedness: two piece passes count candidates algebraically, then
    // resolve only the newest kMaxSparklesPerFrame in a fixed stack array.
    // An over-long path drops its oldest slots without renumbering survivors.
    //
    // for_each_canonical_piece walks the exact geometry pieces consumed by
    // the visible stroke (polyline chords, or the Catmull-Rom Bezier
    // subdivision segments emit_catmull_rom generates), oldest -> newest,
    // handing each to fn as a CanonicalPiece.
    template<typename Fn>
    void for_each_canonical_piece(const std::vector<BuildPoint>& pts,
                                  Fn&& fn) const;

    // World-anchored bounded slot lattice over the whole canonical path.
    // Calls fn(const SparklePoint&) head -> tail, at most
    // kMaxSparklesPerFrame times. No acceptance, no spread: pure geometry
    // plus stable identity.
    template<typename Fn>
    void for_each_sparkle_slot(const std::vector<BuildPoint>& pts,
                               Fn&& fn) const;

    // Total length of the canonical stroke path -- the same geometry
    // pieces the renderer receives. Measured before dot emission so the
    // Dotted/Spark lattice can be head-anchored instead of crawling with
    // the retracting tail.
    float canonical_arc_length(const std::vector<BuildPoint>& pts) const;

    // Both emitters take the segment alpha from alpha_at()/fade() -- the
    // documented fade contract, including fade_start and FadeCurve --
    // never from a raw linear window position.
    void emit_polyline(const std::vector<BuildPoint>& pts,
                       int64_t now_ns,
                       TrailGeometrySink& sink) const;
    void emit_catmull_rom(const std::vector<BuildPoint>& pts,
                          int64_t now_ns,
                          TrailGeometrySink& sink) const;
    // T-021 spatial arc-length sparkle emission. Walks the same canonical
    // geometry segments used by the visible Trail (polyline chords or
    // Catmull-Rom Bezier subdivisions). Emits sparkle candidates at fixed
    // arc-length intervals in stable source occurrences; density is geometry-driven,
    // not per-raw-input-point. Hard-capped at kMaxSparklesPerFrame.
    // smoothing == 0: walks polyline chords (pts[i-1] -> pts[i]).
    // smoothing  > 0: walks the same Catmull-Rom subdivision segments that
    //                 emit_catmull_rom() generated for the stroke.
    void emit_sparkles_arc(const std::vector<BuildPoint>& pts,
                           int64_t now_ns,
                           TrailGeometrySink& sink) const;


    TrailConfig config_;

    // PERF-003 test-only reference-tessellation switch (never set in
    // production; default false -> adaptive).
    bool reference_tessellation_ = false;

    // Reused scratch buffer (single-threaded): no per-frame heap churn.
    mutable std::vector<BuildPoint> scratch_;
};

} // namespace ptd
