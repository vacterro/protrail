#include "trail_effect.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ptd {

namespace {

constexpr float kNsPerMs = 1'000'000.0f;

// Segment subdivision count for the Catmull-Rom -> Bezier evaluation
// (B5). 12 subdivisions make a segment visually indistinguishable from an
// exact Bezier at trail thickness while keeping per-frame cost trivial.
//
// PERF-003: 12 is now the QUALITY CEILING, not a universal constant. The
// production walker adaptively subdivides each source span by geometric
// complexity (see adaptive_curve_subdivisions), so a dense but nearly-straight
// high-polling path collapses to one piece per span while a genuinely curved
// span may still spend the full budget. The fixed value is retained as the
// documented ceiling and as the reference oracle the equivalence regressions
// compare against.
constexpr int kCurveSubdivisions = 12;

// PERF-003 adaptive bounds. A span shorter than kAdaptiveMaxSegmentPx that is
// also nearly flat (control-polygon excess below kAdaptiveFlatnessTolPx)
// becomes a single piece; curvature and length raise the count, which is
// always clamped to [1, kCurveSubdivisions].
constexpr float kAdaptiveMaxSegmentPx = 24.0f;
constexpr float kAdaptiveFlatnessTolPx = 0.5f;
// Fade/width/color are sampled ONCE per piece from the piece's newer window
// position, so the parametric (t) extent of a span must also bound the step:
// a span that covers a large slice of the lifetime would otherwise flatten
// the tail->head fade onto a single alpha. This is the old fixed policy's
// worst-case sampling density (a full-lifetime span still wants 12 pieces).
constexpr float kAdaptiveMaxTStep = 1.0f / static_cast<float>(kCurveSubdivisions);

inline float point_distance(float ax, float ay, float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    return std::sqrt(dx * dx + dy * dy);
}

// PERF-003 deterministic adaptive tessellation for one Catmull-Rom span,
// expressed as the cubic Bezier (P0, C1, C2, P1) the span evaluates to.
// The count depends only on the geometry and the span's window extent, so
// identical (history, config, now_ns) always yields identical pieces.
//
//   - the control-polygon length Lc approximates the curve; the chord L is
//     what one straight piece would draw, so `excess = Lc - L` is a cheap,
//     monotone flatness proxy (0 for a perfectly straight span);
//   - short and flat           -> 1 piece;
//   - long or curved           -> 2..12 pieces, bounded by the ceiling;
//   - a span covering a large slice of the lifetime keeps enough pieces that
//     the per-piece window step stays at the old worst-case 1/12, preserving
//     the exact monotone tail->head fade granularity;
//   - never 0, never above the ceiling, never NaN.
inline int adaptive_curve_subdivisions(float x0, float y0,
                                       float c1x, float c1y,
                                       float c2x, float c2y,
                                       float x1, float y1,
                                       float t_span) {
    const float chord = point_distance(x0, y0, x1, y1);
    const float control = point_distance(x0, y0, c1x, c1y)
                        + point_distance(c1x, c1y, c2x, c2y)
                        + point_distance(c2x, c2y, x1, y1);
    if (!std::isfinite(chord) || !std::isfinite(control)
        || !std::isfinite(t_span)) {
        return 1;
    }

    const float excess = control > chord ? control - chord : 0.0f;
    const float by_flatness = std::sqrt(excess / kAdaptiveFlatnessTolPx);
    const float by_length = chord / kAdaptiveMaxSegmentPx;
    const float by_fade = (t_span > 0.0f ? t_span : 0.0f) / kAdaptiveMaxTStep;

    float want = by_flatness > by_length ? by_flatness : by_length;
    if (by_fade > want) want = by_fade;

    int n = static_cast<int>(std::ceil(want));
    if (n < 1) n = 1;
    if (n > kCurveSubdivisions) n = kCurveSubdivisions;
    return n;
}

// A source occurrence begins at the first real sample in each fixed time
// bucket. Its start sample and all earlier finalized spans are immutable.
// Grouping short raw spans avoids sparse modes vanishing at high input rate.
constexpr int64_t kSparkleOccurrenceNs = 80'000'000LL;

// Quantization grid for duplicate-position collapse (B3): 0.25 px.
constexpr float kQuantum = 0.25f;

inline int quantize(float v) {
    return static_cast<int>(v / kQuantum);
}

// Centripetal Catmull-Rom control points for the segment (P1 -> P2) with
// neighbors (P0, P3) (B5), scaled by the smoothing contract (T-008A).
//
// T-008A defect: smoothing was previously binary (<= 0 -> polyline, > 0 ->
// one fixed centripetal curve), so intermediate config values such as 0.2
// or 0.5 had no effect and a future Settings slider would have been
// dishonest. Fix: the cubic Bezier control points are blended linearly
// between the straight-line (polyline) control points of the same segment
// and the full centripetal CR control points:
//
//     effective_control = linear_control
//                       + smoothing * (catmull_control - linear_control)
//
// so tangential influence -- and therefore curve strength and overshoot
// energy -- grows continuously with smoothing:
//   0.0  -> raw polyline (the caller branches before reaching this),
//   0.25 -> light smoothing, path stays close to the raw movement,
//   0.5  -> moderate,
//   0.75 -> strong continuous curve (T-019 default),
//   1.0  -> exactly the previously proven centripetal curve (strongest).
// Endpoint interpolation is untouched, and the centripetal parameterization
// (alpha = 0.5) keeps EVERY intermediate value cusp- and overshoot-
// resistant, unlike the uniform variant. Missing neighbors fall back to
// the segment endpoints (clamped ends).
inline void centripetal_cp(float smoothing, float x0, float y0, float x1, float y1,
                           float x2, float y2, float x3, float y3,
                           float& c1x, float& c1y, float& c2x, float& c2y) {
    auto dist = [](float ax, float ay, float bx, float by) {
        const float dx = bx - ax;
        const float dy = by - ay;
        const float d2 = dx * dx + dy * dy;
        return d2 > 1e-12f ? std::sqrt(d2) : 1e-6f;
    };

    const float d01 = std::pow(dist(x0, y0, x1, y1), 0.5f);
    const float d12 = std::pow(dist(x1, y1, x2, y2), 0.5f);
    const float d23 = std::pow(dist(x2, y2, x3, y3), 0.5f);

    // Guard against collinear/duplicate edge cases producing non-finite
    // control points.
    float sum_l = d01 + d12;
    float sum_r = d12 + d23;
    if (!(sum_l > 1e-9f)) sum_l = 1.0f;
    if (!(sum_r > 1e-9f)) sum_r = 1.0f;

    const float t1x = x1 - (x0 - x1) * (d12 / sum_l);
    const float t1y = y1 - (y0 - y1) * (d12 / sum_l);
    const float t2x = x2 - (x3 - x2) * (d12 / sum_r);
    const float t2y = y2 - (y3 - y2) * (d12 / sum_r);

    // Convert to cubic Bezier control points for D2D (full centripetal CR).
    const float cr_c1x = x1 + (x2 - t1x) / 3.0f;
    const float cr_c1y = y1 + (y2 - t1y) / 3.0f;
    const float cr_c2x = x2 - (t2x - x1) / 3.0f;
    const float cr_c2y = y2 - (t2y - y1) / 3.0f;

    // Straight-line (polyline) Bezier control points of the same segment:
    // at 1/3 and 2/3 along the chord, so the curve degenerates to the raw
    // polyline when their influence is fully removed.
    const float l1x = x1 + (x2 - x1) / 3.0f;
    const float l1y = y1 + (y2 - y1) / 3.0f;
    const float l2x = x2 - (x2 - x1) / 3.0f;
    const float l2y = y2 - (y2 - y1) / 3.0f;

    // T-008A contract blend: continuous tangent influence. smoothing == 1
    // reduces EXACTLY to the previously proven centripetal curve; smoothing
    // -> 0 reduces to the polyline.
    c1x = l1x + smoothing * (cr_c1x - l1x);
    c1y = l1y + smoothing * (cr_c1y - l1y);
    c2x = l2x + smoothing * (cr_c2x - l2x);
    c2y = l2y + smoothing * (cr_c2y - l2y);
}

// Cubic Bezier point at parameter u.
inline void bezier_point(float x0, float y0, float c1x, float c1y,
                         float c2x, float c2y, float x1, float y1,
                         float u, float& px, float& py) {
    const float v = 1.0f - u;
    const float a = v * v * v;
    const float b = 3.0f * v * v * u;
    const float c = 3.0f * v * u * u;
    const float d = u * u * u;
    px = a * x0 + b * c1x + c * c2x + d * x1;
    py = a * y0 + b * c1y + c * c2y + d * y1;
}

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline bool is_finite(float v) {
    return std::isfinite(v);
}

// T-021: deterministic [0,1) hash draw. Pure integer math, no std::rand,
// no global state: identical seeds give identical draws on every frame,
// every cadence, every machine.
inline float hash01(uint32_t h) {
    h = TrailEffect::style_hash(h);
    return static_cast<float>(h & 0x00FFFFFFu)
         / static_cast<float>(0x01000000u);
}

// T-021: pure config-parameterized fade/color arithmetic. The member
// functions delegate to these so the static sparkle model and the stroke
// share ONE copy of the arithmetic (a second divergent copy is exactly
// how the sparkle color could drift from the trail color).
float fade_for(const TrailConfig& c, float age_progress) {
    // Phase G contract, AGE-progress domain: 0 = newest/head, 1 = oldest
    // (lifetime boundary). fade_start holds full opacity up to the start,
    // the remainder performs the selected FadeCurve, alpha always reaches
    // exactly 0 at the boundary. Pure function of elapsed time (B6).
    const float age = clamp01(age_progress);
    const float start = clamp01(c.fade_start);
    if (age <= start) return 1.0f;
    const float span = 1.0f - start;
    const float fade_progress = span > 0.0f ? (age - start) / span : 1.0f;
    return 1.0f - TrailEffect::apply_fade_curve(c.fade_curve, fade_progress);
}

float alpha_for(const TrailConfig& c, float t) {
    // t in [0,1]: 0 = oldest visible (lifetime boundary), 1 = newest.
    const float u = clamp01(t);
    return c.base_opacity * fade_for(c, 1.0f - u);
}

TrailColorF color_for(const TrailConfig& c, float t) {
    const float u = clamp01(t);

    const float sr = static_cast<float>(c.start_color_r) / 255.0f;
    const float sg = static_cast<float>(c.start_color_g) / 255.0f;
    const float sb = static_cast<float>(c.start_color_b) / 255.0f;

    const float fr = static_cast<float>(c.fade_color_r) / 255.0f;
    const float fg = static_cast<float>(c.fade_color_g) / 255.0f;
    const float fb = static_cast<float>(c.fade_color_b) / 255.0f;

    auto lerp_color = [sr, sg, sb, fr, fg, fb](float factor) -> TrailColorF {
        factor = clamp01(factor);
        return TrailColorF{
            fr + (sr - fr) * factor,
            fg + (sg - fg) * factor,
            fb + (sb - fb) * factor
        };
    };

    switch (c.color_mode) {
        case TrailColorMode::Full:
            return TrailColorF{sr, sg, sb};

        case TrailColorMode::Gradient:
            return lerp_color(u);

        case TrailColorMode::StartAccent: {
            // For t <= 0.75, color = Fade. For 0.75 < t <= 1.0,
            // interpolate Fade -> Start.
            const float accent_start = 1.0f - TrailEffect::kColorAccentSpan;
            if (u <= accent_start) {
                return TrailColorF{fr, fg, fb};
            }
            const float factor = (u - accent_start) / TrailEffect::kColorAccentSpan;
            return lerp_color(factor);
        }

        case TrailColorMode::FadeAccent: {
            // For t >= 0.25, color = Start. For 0 <= t < 0.25,
            // interpolate Fade -> Start.
            if (u >= TrailEffect::kColorAccentSpan) {
                return TrailColorF{sr, sg, sb};
            }
            const float factor = u / TrailEffect::kColorAccentSpan;
            return lerp_color(factor);
        }

        default:
            return TrailColorF{sr, sg, sb};
    }
}

// T-016 spacing repair: true arc-length dot sampler for Dotted/Spark.
//
// The previous implementation accumulated each input chord into a gap
// accumulator and emitted AT MOST ONE dot per chord, then reset the
// accumulator to zero. That is not arc-length spacing: a long sparse
// 500 px chord with 10 px spacing produced exactly one dot (at its end)
// and threw away the phase, so dot placement depended on input sampling
// density and boundaries restarted the count.
//
// Contract implemented here:
//  - dots are placed at exact arc-length multiples of `spacing` measured
//    from the start of the consumed path (no dot at arc position 0);
//  - MULTIPLE dots are emitted inside one source chord when required;
//  - the residual distance from the last emitted dot to the chord end is
//    preserved in `carry` and consumed by the NEXT chord, so spacing is
//    continuous across segment boundaries and curve spans;
//  - placement depends only on the geometric path and the spacing, so
//    changing input sampling density for the same geometric path does not
//    change dot positions (a 500 px straight segment sampled as one 500 px
//    chord, 5x100 px chords or 50x10 px chords yields the same dots);
//  - a dot whose arc position lands exactly on a chord boundary is
//    emitted at the boundary (exact-multiple chord decomposition emits
//    the same dots as the fused chord, up to float precision).
//  Endpoint policy (documented): the very start of the path (arc 0) has
//  no dot; the first dot appears one `spacing` in.
// Arc-length dot lattice for the Dotted/Spark styles.
//
// HEAD-ANCHORED (repair): the lattice phase is derived from the TOTAL
// visible arc length, so dot k sits at head-relative arc (k + 0.5) *
// spacing. The pre-repair sampler started its residual at the tail, so
// every frame the retracting lifetime boundary shifted the phase of the
// whole lattice and every dot crawled backwards along the stroke; the
// running dot_index renumbered at the same time, which re-seeded the
// Spark flicker of every dot on the path. Anchoring at the head makes an
// expiring tail dot remove exactly that dot and touch nothing else -- the
// same invariant the sparkle sampler holds.
struct DotSampler {
    float spacing;
    int budget;            // hard per-build emission cap (boundedness, B10)
    float total_arc;       // whole visible path length (measured first)
    float arc = 0.0f;      // absolute arc walked so far, from the tail
    float carry = 0.0f;    // arc distance since the last emitted dot
    int emitted_count = 0; // bounded-output counter (never a seed)

    // Head-anchored lattice: dots land on arc == total_arc - (k + 0.5) *
    // spacing, i.e. on offset + k * spacing measured from the tail.
    static DotSampler head_anchored(float spacing_px, int budget_dots,
                                    float total_arc_px) {
        DotSampler d{spacing_px, budget_dots, total_arc_px};
        if (!(spacing_px > 0.0f) || !(total_arc_px > 0.0f)) return d;
        float offset = std::fmod(total_arc_px - spacing_px * 0.5f, spacing_px);
        if (offset < 0.0f) offset += spacing_px;
        // advance() emits its first dot at arc == spacing - carry.
        d.carry = spacing_px - offset;
        return d;
    }

    // Walks one chord (x1,y1,t1) -> (x2,y2,t2) and emits every dot that
    // falls inside it through emit_dot(x, y, t, dir_x, dir_y, index).
    // (dir_x, dir_y) is the normalized local chord direction, used for
    // the round-capped dot stub orientation. `index` is the HEAD-relative
    // dot ordinal, stable while the tail retracts, so the deterministic
    // Spark flicker stays attached to its own dot.
    template <typename EmitDot>
    void advance(float x1, float y1, float x2, float y2,
                 float t1, float t2, EmitDot&& emit_dot) {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float chord = std::sqrt(dx * dx + dy * dy);
        if (!(chord > 0.0f) || !(spacing > 0.0f)) {  // degenerate
            return;
        }
        const float chord_start = arc;
        arc += chord;
        const float inv_chord = 1.0f / chord;
        const float dir_x = dx * inv_chord;
        const float dir_y = dy * inv_chord;

        float next = spacing - carry;  // arc offset of the next dot
        bool emitted = false;
        while (next <= chord) {
            if (emitted_count >= budget) return;  // bounded-output cap (B10)
            const float f = next * inv_chord;
            const float t = t1 + (t2 - t1) * f;
            const float abs_arc = chord_start + next;
            int index = static_cast<int>(
                (total_arc - abs_arc) / spacing + 0.5f);
            if (index < 0) index = 0;
            emit_dot(x1 + dx * f, y1 + dy * f, t, dir_x, dir_y, index);
            ++emitted_count;
            emitted = true;
            next += spacing;
        }
        // Preserve the residual: distance from the last emitted dot (or
        // the chord start when this chord emitted nothing) to the end.
        carry = emitted ? (chord - (next - spacing)) : (carry + chord);
    }
};

} // namespace

TrailEffect::TrailEffect(TrailConfig config)
    : config_(TrailConfig::validated(config)) {}

float TrailEffect::apply_fade_curve(FadeCurve curve, float p) {
    const float u = clamp01(p);
    switch (curve) {
        case FadeCurve::Smooth:  return u * u * (3.0f - 2.0f * u);
        case FadeCurve::EaseOut: return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
        case FadeCurve::Linear:
        default:                 return u;
    }
}

float TrailEffect::fade(float age_progress) const {
    // Delegates to the shared pure arithmetic (see fade_for above).
    return fade_for(config_, age_progress);
}

float TrailEffect::alpha_at(float t) const {
    // Delegates to the shared pure arithmetic (see alpha_for above).
    return alpha_for(config_, t);
}

float TrailEffect::width_at(float t) const {
    // Phase F width contract, t = window position (0=tail, 1=head):
    //   target = lerp(tail, head, t)
    //   final  = lerp(head, target, taper_strength)
    // taper_strength 0 -> constant head width (the approved baseline
    // look); 1 -> full tail-to-head interpolation. Clamped to the
    // validated thickness bounds; never NaN or negative.
    const float u = clamp01(t);
    const float head = config_.head_thickness_px;
    const float tail = config_.tail_thickness_px;
    const float target = tail + (head - tail) * u;
    const float w = head + (target - head) * clamp01(config_.taper_strength);
    const float clamped = w < TrailConfig::kMinThicknessPx ? TrailConfig::kMinThicknessPx
                       : (w > TrailConfig::kMaxThicknessPx ? TrailConfig::kMaxThicknessPx : w);
    return clamped;
}

TrailColorF TrailEffect::color_at(float t) const {
    // Delegates to the shared pure arithmetic (see color_for above).
    return color_for(config_, t);
}

int TrailEffect::max_segments_for(int history_count_bound) {
    if (history_count_bound < 2) history_count_bound = 2;
    // Each consumed sample point can contribute at most one polyline span
    // plus (curve mode) kCurveSubdivisions segments; +1 for the optional
    // live-head segment; +1 slack for the interpolated tail split.
    const int spans = history_count_bound + 1;
    return spans * (kCurveSubdivisions + 1) + 2;
}

// ---- T-016 style math ----

uint32_t TrailEffect::style_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float TrailEffect::spark_flicker(int dot_index, int64_t now_ns) {
    // 100 ms buckets: the flicker animates, yet every evaluation within a
    // bucket (and every frame cadence) sees identical values.
    const uint32_t bucket = static_cast<uint32_t>(now_ns / 100'000'000LL);
    uint32_t h = style_hash(static_cast<uint32_t>(dot_index) * 747796405u
                            ^ (bucket * 2654435761u));
    h = style_hash(h);
    const float f = static_cast<float>(h & 0x00FFFFFFu)
                  / static_cast<float>(0x01000000u);
    return 0.35f + 0.65f * f;
}

float TrailEffect::style_pulse_multiplier(TrailStyle style, float t, int64_t now_ns) {
    if (style != TrailStyle::Pulse) return 1.0f;
    constexpr int64_t kPeriodNs =
        static_cast<int64_t>(kPulsePeriodMs) * 1'000'000LL;
    const float phase = static_cast<float>(now_ns % kPeriodNs)
                      / static_cast<float>(kPeriodNs);
    // Three spatial crests travel toward the head over one cycle. A broad
    // cosine keeps the energy moving without a hard edge or disappearance.
    const float wave = 0.5f + 0.5f * std::cos(6.2831853f *
                                             (3.0f * clamp01(t) - phase));
    return 0.55f + 0.45f * wave;
}

float TrailEffect::pulse_multiplier(int64_t now_ns) const {
    return style_pulse_multiplier(config_.style, 1.0f, now_ns);
}

// ---- T-021 sparkle decoration overlay ----

TrailEffect::SparkleModeParams TrailEffect::sparkle_params(TrailSparkleMode mode) {
    switch (mode) {
        case TrailSparkleMode::Stardust:
            // Numerous small dots with a restrained outward drift.
            // T-023 rework: size 0.42 x the 3 px default emitted 1.3 px
            // dots -- literally sub-stroke dust. Stardust stays the
            // SMALLEST and DIMMEST family (its alpha scale is deliberately
            // unchanged: the Twinkle-peak-outshines-Stardust contract is a
            // regression here), so its visibility is bought with size and
            // with an envelope that no longer collapses mid-trail.
            return {0.72f, 0.42f, 0.71f, 0.52f, 1.45f,
                    0.0f, 0.0f, 0.0f};
        case TrailSparkleMode::Twinkle:
            // Star flashes: sparse, larger, pulsing size with a short
            // bright peak, slowly rotating Cross/Diamond shapes.
            // density 0.30 -> ~60 px slot spacing: sparse but ALWAYS
            // present at Amount 1 on a normal trail (audit Phase 11).
            // T-023 rework: bigger flash + a brightness lift, so the
            // star peak is an event you notice, not a hint.
            return {0.30f, 0.65f, 4.00f, 1.00f, 1.80f,
                    350.0f, 0.0f, 0.65f};
        case TrailSparkleMode::Glitter:
            // Dense sharp shimmer: highest density, tiny Dot/Diamond mix,
            // short deterministic brightness buckets.
            // T-023 rework: Glitter keeps the densest/tiniest character
            // relative to the others, but 0.48 x 3 px was 1.4 px of
            // shimmer that the eye merged into the stroke.
            return {1.25f, 0.65f, 0.82f, 1.00f, 1.90f,
                    0.0f, 90.0f, 0.0f};
        case TrailSparkleMode::Firefly:
            // Sparse luminous drifters: wide spread, slow angular drift
            // and outward creep with age, long soft fade.
            // density 0.14 -> ~129 px slot spacing (kSparkleBaseSpacingPx
            // 18 / 0.14): Firefly is INTENTIONALLY the sparsest family --
            // the fewest, largest, slowest-drifting lights of the four.
            // 0.14 is production truth; the previous "0.22" note was stale.
            // The perceived on-screen density remains subject to user
            // visual verification (T-021R1).
            // T-023 rework: the sparsest family must earn its slots --
            // each Firefly is now a genuinely large, bright drifter.
            return {0.14f, 1.55f, 2.64f, 1.00f, 1.70f,
                    0.0f, 0.0f, 0.0f};
        case TrailSparkleMode::Shards:
            // Detached fragments: medium density, larger than Glitter,
            // moderate scatter, translucent alpha and a small local color
            // brightness lift.
            // T-023 rework: fragments read as debris only if you can see
            // the triangle; 0.82 alpha over the trail's own fade was the
            // main reason Shards looked like a smudge.
            return {0.48f, 1.00f, 2.64f, 1.00f, 1.55f,
                    0.0f, 0.0f, 0.0f};
        case TrailSparkleMode::Off:
        default:
            return {0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                    0.0f, 0.0f, 0.0f};
    }
}

TrailSparkleShape TrailEffect::sparkle_shape(TrailSparkleMode mode, uint32_t seed) {
    switch (mode) {
        case TrailSparkleMode::Twinkle:
            // Alternate the two star shapes deterministically per seed.
            return ((seed >> 8) & 1u) ? TrailSparkleShape::Cross
                                      : TrailSparkleShape::Diamond;
        case TrailSparkleMode::Glitter:
            // Tiny sharp mix: dots and diamonds.
            return ((seed >> 8) & 1u) ? TrailSparkleShape::Dot
                                      : TrailSparkleShape::Diamond;
        case TrailSparkleMode::Shards:
            return TrailSparkleShape::Triangle;
        case TrailSparkleMode::Stardust:
        case TrailSparkleMode::Firefly:
        case TrailSparkleMode::Off:
        default:
            return TrailSparkleShape::Dot;
    }
}

// Deterministic, stateless sparkle slot model (Phases 2-6 spatial rewrite).
//
// Every quantity is a pure function of the slot position (x/y/t interpolated
// along the canonical geometry), the occurrence identity (occurrence_ts = the
// source BuildPoint's stable timestamp, NOT now_ns), the slot ordinal, the
// validated config and now_ns. The sparkle SEED derives from occurrence_ts +
// slot_ordinal + mode, so:
//   - the same spatial slot keeps its identity for its whole visible lifetime;
//   - distinct slots on the same source segment get distinct seeds;
//   - distinct path occurrences at the same coordinate get distinct seeds
//     (different occurrence_ts).
// Time (now_ns) drives only the animated quantities (pulse/shimmer/drift);
// it never redefines which sparkle exists.
bool TrailEffect::sparkle_for_point(const SparklePoint& point,
                                    const TrailConfig& config,
                                    int64_t now_ns,
                                    TrailSparkleRecord& out) {
    if (config.sparkle_mode == TrailSparkleMode::Off) return false;
    const SparkleModeParams P = sparkle_params(config.sparkle_mode);
    const float amount = clamp01(config.sparkle_amount);
    if (!(amount > 0.0f)) return false;  // Amount 0 emits nothing.

    // Stable slot identity: occurrence timestamp + slot ordinal + mode.
    // Using occurrence_ts (NOT now_ns) ensures the sparkle's existence is
    // tied to the stable path occurrence, never to the render clock.
    const uint32_t seed =
        style_hash(static_cast<uint32_t>(point.occurrence_ts) * 2654435761u
                   ^ static_cast<uint32_t>(point.occurrence_ts >> 32) * 2246822519u
                   ^ static_cast<uint32_t>(point.slot_ordinal) * 747796405u
                   ^ 0x9E3779B9u * static_cast<uint32_t>(config.sparkle_mode));

    // Deterministic acceptance gate: Amount controls acceptance. Mode density
    // is already applied exactly once through spatial slot spacing.
    const float accept = amount;
    if (!(accept > 0.0f)) return false;
    if (hash01(seed ^ 0x68BC21EBu) >= accept) return false;

    const float spread_px = config.sparkle_spread_px * P.spread_scale;
    const bool pulses = P.pulse_period_ms > 0.0f;
    const bool shimmers = P.shimmer_bucket_ms > 0.0f;

    // Animation age follows the slot's normalized lifetime position. Stable
    // occurrence identity affects only deterministic seed selection.
    const float age01 = 1.0f - clamp01(point.t);
    const float age_ms = age01 * config.lifetime_ms;
    const float age_s = age_ms / 1000.0f;

    // The path slot is an immutable world-space spawn anchor. All movement
    // is a deterministic function of age measured FROM that anchor; no
    // current trail arc/head/window coordinate enters this calculation.
    const float ang = hash01(seed ^ 0x85EBCA6Bu) * 6.2831853f;
    const float dx = std::cos(ang), dy = std::sin(ang);
    const float radius = spread_px * (0.25f + 0.75f * hash01(seed ^ 0xC2B2AE35u));
    float drift = 0.0f;
    float sideways = 0.0f;
    switch (config.sparkle_mode) {
        case TrailSparkleMode::Stardust:
            drift = config.sparkle_spread_px * 0.32f * age01;
            break;
        case TrailSparkleMode::Glitter:
            drift = config.sparkle_spread_px * 1.15f * std::sqrt(age01);
            break;
        case TrailSparkleMode::Firefly:
            drift = config.sparkle_spread_px * 1.65f * age01;
            sideways = config.sparkle_spread_px * 0.55f
                     * std::sin(3.14159265f * age01);
            break;
        case TrailSparkleMode::Shards:
            // T-023: detached fragments creep away from their immutable
            // world anchor with age only. At the normal Spread this gives
            // approximately 4..18 px of deterministic separation over the
            // visible lifetime. No trail head/tail/window coordinate is
            // read here, so advancing the head cannot drag a live shard.
            drift = config.sparkle_spread_px
                  * (0.40f + 1.40f * hash01(seed ^ 0xD1B54A35u)) * age01;
            break;
        case TrailSparkleMode::Twinkle:
        case TrailSparkleMode::Off:
            break;
    }
    out.x = point.x + dx * (radius + drift) - dy * sideways;
    out.y = point.y + dy * (radius + drift) + dx * sideways;

    // Size: mode scale + deterministic variance, plus the Twinkle pulse
    // (a pure function of age). Always inside the documented envelope.
    const float size_variation = config.sparkle_mode == TrailSparkleMode::Shards
        ? (0.65f + 0.80f * hash01(seed ^ 0x27D4EB2Fu))
        : (0.75f + 0.5f * hash01(seed ^ 0x27D4EB2Fu));
    float size = config.sparkle_size_px * P.size_scale * size_variation;
    float pulse = 1.0f;
    if (pulses) {
        const float flash_age = clamp01(age01 / 0.62f);
        pulse = std::sin(3.14159265f * flash_age);
        size *= 0.45f + 1.35f * pulse;
    }
    out.size_px = size < kMinSparkleEmittedPx ? kMinSparkleEmittedPx
                : (size > kMaxSparkleEmittedPx ? kMaxSparkleEmittedPx : size);

    // Brightness envelope on top of the trail's own fade.
    //
    // T-023 rework -- THE main reason the layer read as "barely there".
    // out.alpha below is alpha_for(t) * alpha_scale * env, and alpha_for()
    // ALREADY performs the whole lifetime fade (base_opacity + fade_start +
    // FadeCurve). The old envelopes then faded a SECOND time along the same
    // axis -- Glitter squared it ((1-age01)^2 == t^2, so a mid-trail glitter
    // kept a quarter of an already-faded alpha) and the others took a square
    // root of it. Two fades stacked on one axis is how a decoration becomes
    // invisible while every unit test still passes.
    //
    // Every envelope below keeps its family's CHARACTER (Twinkle pulses,
    // Glitter flickers, Firefly breathes, Shards fades in behind the head)
    // but now MODULATES around a floor instead of driving toward zero. The
    // fade itself stays where it belongs: in alpha_for().
    float env = 1.0f;
    if (pulses) {
        env = 0.45f + 0.55f * pulse;
    } else if (shimmers) {
        // Deterministic high-frequency flicker that cannot alter position
        // or identity, plus a mild head bias (no longer a squared fade).
        const uint32_t bucket = static_cast<uint32_t>(
            now_ns / static_cast<int64_t>(P.shimmer_bucket_ms * 1'000'000.0f));
        env = (0.72f + 0.28f * hash01(seed ^ (bucket * 2654435761u)))
            * (0.40f + 0.60f * (1.0f - age01));
    } else if (config.sparkle_mode == TrailSparkleMode::Firefly) {
        env = 0.55f + 0.45f * std::sqrt(1.0f - age01);
    } else if (config.sparkle_mode == TrailSparkleMode::Shards) {
        // Quick fade-in, visible middle, smooth fade-out. age01 is newest
        // to oldest, so this envelope is frame-cadence independent. The
        // fade-in is what detaches a fragment from the head and is kept
        // exactly; the fade-out now bottoms out at 0.35 instead of 0 so a
        // mid-life shard is a solid fragment, not a ghost.
        const float fade_in = clamp01(age01 / 0.12f);
        const float fade_out = clamp01((age01 - 0.30f) / 0.70f);
        const float smooth = fade_out * fade_out * (3.0f - 2.0f * fade_out);
        env = fade_in * (1.0f - 0.65f * smooth);
    } else {
        env = 0.55f + 0.35f * std::sqrt(1.0f - age01);
    }

    // The decoration shares the stroke's own alpha authority: the same
    // fade contract (fade_start + FadeCurve + base_opacity) AND the same
    // Pulse breathing, so the layer can never drift away from the trail
    // it decorates.
    out.alpha = clamp01(alpha_for(config, point.t) * P.alpha_scale * env
                        * style_pulse_multiplier(config.style, point.t, now_ns));
    if (!(out.alpha > 0.0f)) return false;

    // Color: the SAME local trail color the stroke uses at this path
    // position (Solid/Head/Tail/Gradient), with a clamped brightness
    // multiplier. Never a hard-coded white.
    //
    // T-023 rework note: `brightness` is a channel GAIN and is therefore a
    // no-op on a saturated color -- the shipped yellow (255,255,0) already
    // has r=g=1.0, so any gain returns it unchanged. A highlight lift
    // toward white was tried here and REJECTED: it makes a yellow trail
    // emit bluish sparkles, which breaks the contract below (and the
    // color_follows_the_local_trail_color / accent regressions that guard
    // it). Visibility is bought with SIZE and ALPHA, never with a color the
    // user did not choose. The gain stays because it does real work on a
    // dark user color.
    TrailColorF c = color_for(config, point.t);
    c.r = clamp01(c.r * P.brightness);
    c.g = clamp01(c.g * P.brightness);
    c.b = clamp01(c.b * P.brightness);
    out.color = c;

    // Rotation: deterministic base angle; Twinkle shapes spin slowly with
    // age (pure function of elapsed time).
    out.rotation_rad = hash01(seed ^ 0x165667B1u) * 3.14159265f;
    if (pulses) out.rotation_rad += P.spin_rad_per_s * age_s;
    if (config.sparkle_mode == TrailSparkleMode::Shards) {
        const float direction = hash01(seed ^ 0xA24BAED5u) < 0.5f ? -1.0f : 1.0f;
        const float angular_velocity = direction
            * (1.4f + 1.6f * hash01(seed ^ 0x94D049BBu));
        out.rotation_rad += angular_velocity * age_s;
    }

    out.shape = sparkle_shape(config.sparkle_mode, seed);
    return true;
}

// PERF-003: one policy for every canonical walker. Adaptive by default; the
// fixed ceiling when the test-only reference oracle is enabled.
int TrailEffect::subdivisions_for_span(float x0, float y0,
                                       float c1x, float c1y,
                                       float c2x, float c2y,
                                       float x1, float y1,
                                       float t_span) const {
    if (reference_tessellation_) return kCurveSubdivisionCeiling;
    return adaptive_curve_subdivisions(x0, y0, c1x, c1y, c2x, c2y, x1, y1,
                                       t_span);
}

void TrailEffect::emit_sparkles_arc(const std::vector<BuildPoint>& pts,
                                    int64_t now_ns,
                                    TrailGeometrySink& sink) const {
    for_each_sparkle_slot(pts, [&](const SparklePoint& sp) {
        TrailSparkleRecord rec;
        if (sparkle_for_point(sp, config_, now_ns, rec)) {
            sink.add_sparkle(rec.x, rec.y, rec.size_px, rec.rotation_rad,
                             rec.alpha, rec.color, rec.shape);
        }
    });
}

void TrailEffect::collect_sparkle_slots(const CursorHistory& history,
                                        int64_t now_ns,
                                        const TrailPoint* live_head,
                                        std::vector<SparklePoint>& out) const {
    out.clear();
    if (!prepare_points(history, now_ns, live_head)) return;
    for_each_sparkle_slot(scratch_,
                          [&](const SparklePoint& sp) { out.push_back(sp); });
}

template<typename Fn>
void TrailEffect::for_each_canonical_piece(const std::vector<BuildPoint>& pts,
                                            Fn&& fn) const {
    const std::size_t n = pts.size();
    if (n < 2) return;
    if (config_.smoothing <= 0.0f) {
        int64_t group_key = -1, group_start_ts = 0;
        float group_arc = 0.0f;
        bool group_unstable = false;
        for (std::size_t i = 1; i < n; ++i) {
            const BuildPoint& a = pts[i - 1];
            const BuildPoint& b = pts[i];
            const int64_t key = a.ts / kSparkleOccurrenceNs;
            if (key != group_key) {
                group_key = key;
                group_start_ts = a.ts;
                group_arc = a.occ_arc0;
                group_unstable = a.synthetic;
            }
            // The newest source span remains unfinalized until another real
            // sample arrives. Synthetic tail and live-head spans never seed.
            const bool unfinalized = group_unstable || a.synthetic || b.live || b.synthetic
                || i + 1 >= n || pts[i + 1].live;
            fn(CanonicalPiece{a.x, a.y, a.t, b.x, b.y, b.t,
                              group_start_ts, group_arc, unfinalized});
            const float dx = b.x - a.x, dy = b.y - a.y;
            group_arc += std::sqrt(dx * dx + dy * dy);
        }
        return;
    }
    int64_t group_key = -1, group_start_ts = 0;
    float group_arc = 0.0f;
    bool group_unstable = false;
    for (std::size_t i = 1; i < n; ++i) {
        const BuildPoint& p1 = pts[i - 1];
        const BuildPoint& p2 = pts[i];
        const BuildPoint& p0 = (i >= 2) ? pts[i - 2] : p1;
        const BuildPoint& p3 = (i + 1 < n) ? pts[i + 1] : p2;
        const int64_t key = p1.ts / kSparkleOccurrenceNs;
        if (key != group_key) {
            group_key = key;
            group_start_ts = p1.ts;
            group_arc = p1.occ_arc0;
            group_unstable = p1.synthetic;
        }
        const bool unfinalized = group_unstable || p0.synthetic || p1.synthetic
            || p2.synthetic || p2.live || p3.live || i + 1 >= n;
        float c1x, c1y, c2x, c2y;
        centripetal_cp(config_.smoothing, p0.x, p0.y, p1.x, p1.y,
                       p2.x, p2.y, p3.x, p3.y, c1x, c1y, c2x, c2y);
        // PERF-003: the SAME adaptive walker the stroke uses, so sparkle
        // anchors and stroke geometry can never approximate the curve
        // differently.
        const int subdivisions = subdivisions_for_span(
            p1.x, p1.y, c1x, c1y, c2x, c2y, p2.x, p2.y, p2.t - p1.t);
        float px = p1.x, py = p1.y, prev_t = p1.t;
        float occ_arc = group_arc;
        for (int s = 1; s <= subdivisions; ++s) {
            const float u = static_cast<float>(s)
                          / static_cast<float>(subdivisions);
            float qx, qy;
            bezier_point(p1.x, p1.y, c1x, c1y, c2x, c2y,
                         p2.x, p2.y, u, qx, qy);
            const float qt = p1.t + (p2.t - p1.t) * u;
            fn(CanonicalPiece{px, py, prev_t, qx, qy, qt,
                              group_start_ts, occ_arc, unfinalized});
            const float dx = qx - px, dy = qy - py;
            occ_arc += std::sqrt(dx * dx + dy * dy);
            px = qx;
            py = qy;
            prev_t = qt;
        }
        group_arc = occ_arc;
    }
}

// Stable per-occurrence sparkle lattice. Only source spans with immutable
// Catmull-Rom neighbor context can emit. A fixed phase and ordinal in each
// source occurrence determine every anchor; the current head, tail and
// visible total arc never enter the position or seed.
template<typename Fn>
void TrailEffect::for_each_sparkle_slot(const std::vector<BuildPoint>& pts,
                                        Fn&& fn) const {
    if (config_.sparkle_mode == TrailSparkleMode::Off || pts.size() < 3) return;
    const SparkleModeParams params = sparkle_params(config_.sparkle_mode);
    const float spacing = std::max(2.0f, kSparkleBaseSpacingPx / params.density);

    auto slot_range = [&](const CanonicalPiece& piece) {
        const float dx = piece.x2 - piece.x1, dy = piece.y2 - piece.y1;
        const float chord = std::sqrt(dx * dx + dy * dy);
        if (piece.live || !(chord > 0.0f)) return std::pair<int, int>{0, 0};
        const uint32_t id = static_cast<uint32_t>(piece.occ_ts)
                          ^ style_hash(static_cast<uint32_t>(piece.occ_ts >> 32));
        const float phase = spacing * (0.05f + 0.90f * hash01(id ^ 0xA27D4EB2u));
        const int first = std::max(0, static_cast<int>(std::ceil(
            (piece.occ_arc - phase) / spacing)));
        const int after = std::max(first, static_cast<int>(std::ceil(
            (piece.occ_arc + chord - phase) / spacing)));
        return std::pair<int, int>{first, after};
    };

    // Count algebraically, without visiting every candidate on a huge path.
    // The second pass resolves only the newest 128 candidates; older slots
    // expire from the bounded output without renumbering surviving ones.
    int64_t total = 0;
    for_each_canonical_piece(pts, [&](const CanonicalPiece& piece) {
        const auto [first, after] = slot_range(piece);
        total += static_cast<int64_t>(after) - first;
    });
    const int64_t discard = std::max<int64_t>(0, total - kMaxSparklesPerFrame);
    SparklePoint slots[kMaxSparklesPerFrame]{};
    int used = 0;
    int64_t seen = 0;
    for_each_canonical_piece(pts, [&](const CanonicalPiece& piece) {
        const auto [begin, after] = slot_range(piece);
        const int64_t count = static_cast<int64_t>(after) - begin;
        if (seen + count <= discard) {
            seen += count;
            return;
        }
        int first = begin;
        if (seen < discard) first += static_cast<int>(discard - seen);
        seen += count;
        if (first >= after) return;
        const float dx = piece.x2 - piece.x1, dy = piece.y2 - piece.y1;
        const float chord = std::sqrt(dx * dx + dy * dy);
        const uint32_t id = static_cast<uint32_t>(piece.occ_ts)
                          ^ style_hash(static_cast<uint32_t>(piece.occ_ts >> 32));
        const float phase = spacing * (0.05f + 0.90f * hash01(id ^ 0xA27D4EB2u));
        for (int ord = first; ord < after && used < kMaxSparklesPerFrame; ++ord) {
            const float local_arc = phase + static_cast<float>(ord) * spacing;
            const float u = (local_arc - piece.occ_arc) / chord;
            SparklePoint& sp = slots[used++];
            sp.x = piece.x1 + dx * u;
            sp.y = piece.y1 + dy * u;
            sp.t = piece.t1 + (piece.t2 - piece.t1) * u;
            sp.occurrence_ts = piece.occ_ts;
            sp.slot_ordinal = ord;
        }
    });
    for (int i = used - 1; i >= 0; --i) fn(slots[i]);
}


float TrailEffect::style_width_at(float t, int64_t now_ns) const {
    const float u = clamp01(t);
    const float baseline = width_at(u);
    float multiplier = 1.0f;
    switch (config_.style) {
        case TrailStyle::Comet:
            // A bright 1.5x head, with a genuinely thin middle and tail.
            multiplier = 0.15f + 1.35f * u * u * u;
            break;
        case TrailStyle::Pulse: {
            const float phase = static_cast<float>(
                now_ns % (static_cast<int64_t>(kPulsePeriodMs) * 1'000'000LL))
                / static_cast<float>(kPulsePeriodMs * 1'000'000LL);
            const float wave = 0.5f + 0.5f * std::cos(
                6.2831853f * (3.0f * u - phase));
            multiplier = 0.72f + 0.73f * wave;
            break;
        }
        case TrailStyle::Ribbon: {
            const float phase = static_cast<float>(now_ns % 2'400'000'000LL)
                              / 2'400'000'000.0f;
            const float wave = 0.5f + 0.5f * std::cos(
                6.2831853f * (2.5f * u - phase));
            multiplier = 0.43f + 0.82f * wave;
            break;
        }
        default:
            break;
    }
    const float result = baseline * multiplier;
    return std::clamp(result, TrailConfig::kMinThicknessPx,
                      TrailConfig::kMaxThicknessPx);
}

float TrailEffect::style_alpha_multiplier(float t, int64_t now_ns) const {
    if (config_.style == TrailStyle::Comet) {
        const float u = clamp01(t);
        return 0.28f + 0.72f * std::sqrt(u) * u;
    }
    return style_pulse_multiplier(config_.style, t, now_ns);
}


// Visible BuildPoint path for `now_ns`. Shared by build_geometry and the
// pure slot test seam so both always observe byte-identical geometry.
bool TrailEffect::prepare_points(const CursorHistory& history,
                                 int64_t now_ns,
                                 const TrailPoint* live_head) const {
    scratch_.clear();
    if (!config_.enabled) return false;

    const int64_t lifetime_ns = static_cast<int64_t>(config_.lifetime_ms * kNsPerMs);
    const int64_t window_start = now_ns - lifetime_ns;
    const float lifetime_ms_f = config_.lifetime_ms;

    // B3: consume only samples inside the active window, preserving time
    // order. Button-transition samples contribute their coordinates like
    // movement samples; button identity is ignored for the visual line.
    // history.samples() is ordered oldest -> newest.
    for (const CursorSample& s : history.samples()) {
        if (s.timestamp_ns < window_start) continue;   // stale: excluded
        if (s.timestamp_ns > now_ns) continue;         // future-clamped guard
        const float age_ms = static_cast<float>(now_ns - s.timestamp_ns) / kNsPerMs;
        scratch_.push_back(BuildPoint{
            static_cast<float>(s.x), static_cast<float>(s.y),
            clamp01(1.0f - age_ms / lifetime_ms_f), s.timestamp_ns});
    }

    // B3: collapse consecutive duplicate positions on a 0.25 px grid so a
    // stationary cursor does not accumulate zero-length geometry. The
    // first occurrence is kept; duplicates carry identical coordinates and
    // equivalent fade state, so nothing visual is lost.
    {
        std::size_t w = 0;
        for (std::size_t i = 0; i < scratch_.size(); ++i) {
            if (w == 0 || quantize(scratch_[i].x) != quantize(scratch_[w - 1].x)
                       || quantize(scratch_[i].y) != quantize(scratch_[w - 1].y)) {
                scratch_[w++] = scratch_[i];
            }
        }
        scratch_.resize(w);
    }

    // At capacity the true first sample of the oldest time occurrence may
    // already have been evicted. Never reconstruct anchors in that group
    // from a shifted start; later complete groups remain eligible.
    if (!scratch_.empty() && history.size() >= history.max_samples()) {
        scratch_.front().synthetic = true;
    }

    // Tail boundary interpolation (B6/B12): the visible tail must end at
    // exactly window_start. If the oldest visible sample is younger than
    // that boundary, prepend a synthetic point ON the boundary, linearly
    // interpolated (in time) between the last stale sample and that
    // visible sample. The tail then retracts smoothly in equal wall-time
    // steps regardless of frame pacing, instead of jumping
    // sample-to-sample. When no stale predecessor exists (young trail),
    // the tail simply starts at the oldest visible sample.
    if (!scratch_.empty() && scratch_.front().ts > window_start) {
        const auto& samples = history.samples();
        const CursorSample* prev = nullptr;
        for (const CursorSample& s : samples) {
            if (s.timestamp_ns < window_start
                && (prev == nullptr || s.timestamp_ns >= prev->timestamp_ns)) {
                prev = &s;
            }
        }
        if (prev != nullptr) {
            const BuildPoint& a = scratch_.front();
            const int64_t span = a.ts - prev->timestamp_ns;
            float f = 0.0f;
            if (span > 0) {
                f = static_cast<float>(window_start - prev->timestamp_ns)
                    / static_cast<float>(span);
            }
            f = clamp01(f);
            BuildPoint tail{};
            tail.x = static_cast<float>(prev->x) + (a.x - static_cast<float>(prev->x)) * f;
            tail.y = static_cast<float>(prev->y) + (a.y - static_cast<float>(prev->y)) * f;
            tail.t = 0.0f;
            // Preserve source-occurrence identity across moving tail windows.
            tail.ts = prev->timestamp_ns;
            const float from_x = tail.x - static_cast<float>(prev->x);
            const float from_y = tail.y - static_cast<float>(prev->y);
            tail.occ_arc0 = std::sqrt(from_x * from_x + from_y * from_y);
            tail.synthetic = true;
            if (!(quantize(tail.x) == quantize(a.x)
                  && quantize(tail.y) == quantize(a.y))) {
                scratch_.insert(scratch_.begin(), tail);
            } else {
                // Even when quantization suppresses an inserted point, the
                // oldest span has lost its original curve neighbor.
                scratch_.front().synthetic = true;
            }
        }
    }

    // Live head (B5/B12): pin the trail tip to the real pointer so the
    // trail never appears to lag the cursor between raw-input packets.
    // This frame-local endpoint is never eligible as a sparkle anchor or
    // the future neighbor of a supposedly finalized Catmull-Rom span.
    int64_t head_ts = now_ns;
    if (!history.samples().empty()) {
        head_ts = history.samples().back().timestamp_ns;
    }
    if (live_head != nullptr && is_finite(live_head->x) && is_finite(live_head->y)) {
        BuildPoint head{live_head->x, live_head->y, 1.0f, head_ts};
        head.live = true;
        if (scratch_.empty()
            || quantize(scratch_.back().x) != quantize(head.x)
            || quantize(scratch_.back().y) != quantize(head.y)) {
            scratch_.push_back(head);
        }
    }

    // Nothing visible: no geometry at all (B11 "empty history" case).
    return scratch_.size() >= 2;
}

void TrailEffect::build_geometry(const CursorHistory& history,
                                 int64_t now_ns,
                                 const TrailPoint* live_head,
                                 int history_count_bound,
                                 TrailGeometrySink& sink) const {
    (void)history_count_bound;  // scratch_ is self-bounding (see header)
    if (!prepare_points(history, now_ns, live_head)) {
        sink.reserve_hint(0);
        return;
    }

    // Straight mode (smoothing == 0): raw polyline, still age-faded.
    if (config_.smoothing <= 0.0f) {
        emit_polyline(scratch_, now_ns, sink);
        emit_sparkles_arc(scratch_, now_ns, sink);
        return;
    }

    emit_catmull_rom(scratch_, now_ns, sink);
    // T-021: the sparkle layer is a decoration pass OVER the accepted
    // stroke path; the segment stream above is byte-identical to the
    // T-019 output whether sparkles are On or Off.
    emit_sparkles_arc(scratch_, now_ns, sink);
}

float TrailEffect::canonical_arc_length(const std::vector<BuildPoint>& pts) const {
    float total = 0.0f;
    for_each_canonical_piece(pts,
        [&](const CanonicalPiece& piece) {
            const float dx = piece.x2 - piece.x1;
            const float dy = piece.y2 - piece.y1;
            total += std::sqrt(dx * dx + dy * dy);
        });
    return total;
}

// Segment alpha comes from alpha_for() -- base_opacity * fade_for(1 - t) --
// so fade_start and the selected FadeCurve actually reach the screen. The
// pre-repair emitters used a raw linear `base_opacity * t`, which silently
// made both Settings controls decorative.
void TrailEffect::emit_polyline(const std::vector<BuildPoint>& pts,
                                int64_t now_ns,
                                TrailGeometrySink& sink) const {
    sink.reserve_hint(static_cast<int>(pts.size()) - 1);
    const bool dots = config_.style == TrailStyle::Dotted
                   || config_.style == TrailStyle::Spark;

    if (dots) {
        const int max_dot_budget = max_segments_for(512);
        DotSampler sampler = DotSampler::head_anchored(
            config_.segment_spacing_px, max_dot_budget,
            canonical_arc_length(pts));
        auto emit_dot = [&](float x, float y, float t,
                            float dir_x, float dir_y, int index) {
            const float half = kDotStubPx * 0.5f;
            float alpha = alpha_for(config_, t)
                        * style_alpha_multiplier(t, now_ns);
            if (config_.style == TrailStyle::Spark) {
                alpha *= spark_flicker(index, now_ns);
            }
            sink.add_segment(x - dir_x * half, y - dir_y * half,
                             x + dir_x * half, y + dir_y * half,
                             alpha, style_width_at(t, now_ns), color_at(t));
        };
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const BuildPoint& a = pts[i - 1];
            const BuildPoint& b = pts[i];
            sampler.advance(a.x, a.y, b.x, b.y, a.t, b.t, emit_dot);
        }
        return;
    }

    for (std::size_t i = 1; i < pts.size(); ++i) {
        const BuildPoint& a = pts[i - 1];
        const BuildPoint& b = pts[i];
        // Segment alpha from the NEWER end (head side), oldest -> newest
        // order guarantees non-decreasing alpha for the batch renderer.
        // Width and color from the same newer-end window position.
        sink.add_segment(a.x, a.y, b.x, b.y,
                         alpha_for(config_, b.t) * style_alpha_multiplier(b.t, now_ns),
                         style_width_at(b.t, now_ns), color_at(b.t));
    }
}

void TrailEffect::emit_catmull_rom(const std::vector<BuildPoint>& pts,
                                   int64_t now_ns,
                                   TrailGeometrySink& sink) const {
    const std::size_t n = pts.size();
    const int spans = static_cast<int>(n) - 1;
    sink.reserve_hint(spans * kCurveSubdivisions);

    const bool dots = config_.style == TrailStyle::Dotted
                   || config_.style == TrailStyle::Spark;

    // T-016 spacing repair: the SAME DotSampler drives polyline and curve
    // emission, so smoothed paths obey the identical arc-length spacing
    // contract (residual carried across spans; density-independent).
    const int max_dot_budget = max_segments_for(512);
    DotSampler sampler = DotSampler::head_anchored(
        config_.segment_spacing_px, max_dot_budget,
        canonical_arc_length(pts));
    auto emit_dot = [&](float x, float y, float t,
                        float dir_x, float dir_y, int index) {
        const float half = kDotStubPx * 0.5f;
        float alpha = alpha_for(config_, t)
                    * style_alpha_multiplier(t, now_ns);
        if (config_.style == TrailStyle::Spark) {
            alpha *= spark_flicker(index, now_ns);
        }
        sink.add_segment(x - dir_x * half, y - dir_y * half,
                         x + dir_x * half, y + dir_y * half,
                         alpha, style_width_at(t, now_ns), color_at(t));
    };

    for (std::size_t i = 1; i < n; ++i) {
        const BuildPoint& p1 = pts[i - 1];
        const BuildPoint& p2 = pts[i];
        const BuildPoint& p0 = (i >= 2) ? pts[i - 2] : p1;
        const BuildPoint& p3 = (i + 1 < n) ? pts[i + 1] : p2;

        float c1x, c1y, c2x, c2y;
        centripetal_cp(config_.smoothing, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y,
                       p3.x, p3.y, c1x, c1y, c2x, c2y);

        // PERF-003: adaptive, deterministic subdivision for this span -- the
        // byte-identical policy the canonical piece walker uses, so the stroke
        // and the sparkle anchors share ONE curve approximation.
        const int subdivisions = subdivisions_for_span(
            p1.x, p1.y, c1x, c1y, c2x, c2y, p2.x, p2.y, p2.t - p1.t);

        // Per-subdivision alpha: linear blend of endpoint window positions
        // -> smooth fade along the curve, monotone within the segment.
        // Width and color from the same interpolated window position.
        float px = p1.x, py = p1.y, prev_t = p1.t;
        for (int s = 1; s <= subdivisions; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(subdivisions);
            float qx, qy;
            bezier_point(p1.x, p1.y, c1x, c1y, c2x, c2y, p2.x, p2.y, u, qx, qy);
            const float t = p1.t + (p2.t - p1.t) * u;
            if (dots) {
                sampler.advance(px, py, qx, qy, prev_t, t, emit_dot);
                px = qx;
                py = qy;
                prev_t = t;
                continue;
            }
            sink.add_segment(px, py, qx, qy,
                             alpha_for(config_, t) * style_alpha_multiplier(t, now_ns),
                             style_width_at(t, now_ns), color_at(t));
            px = qx;
            py = qy;
            prev_t = t;
        }
    }
}

} // namespace ptd
