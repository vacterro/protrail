#include "trail_effect.h"

#include <cmath>

namespace ptd {

namespace {

constexpr float kNsPerMs = 1'000'000.0f;

// Segment subdivision count for the Catmull-Rom -> Bezier evaluation
// (B5). 12 subdivisions make a segment visually indistinguishable from an
// exact Bezier at trail thickness while keeping per-frame cost trivial.
constexpr int kCurveSubdivisions = 12;

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
//   0.5  -> moderate (default),
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
struct DotSampler {
    float spacing;
    int budget;          // hard per-build emission cap (boundedness, B10)
    float carry = 0.0f;  // arc distance since the last emitted dot
    int dot_index = 0;   // deterministic Spark flicker input

    // Walks one chord (x1,y1,t1) -> (x2,y2,t2) and emits every dot that
    // falls inside it through emit_dot(x, y, t, dir_x, dir_y, index).
    // (dir_x, dir_y) is the normalized local chord direction, used for
    // the round-capped dot stub orientation.
    template <typename EmitDot>
    void advance(float x1, float y1, float x2, float y2,
                 float t1, float t2, EmitDot&& emit_dot) {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float chord = std::sqrt(dx * dx + dy * dy);
        if (!(chord > 0.0f) || !(spacing > 0.0f)) return;  // degenerate
        const float inv_chord = 1.0f / chord;
        const float dir_x = dx * inv_chord;
        const float dir_y = dy * inv_chord;

        float next = spacing - carry;  // arc offset of the next dot
        bool emitted = false;
        while (next <= chord) {
            if (dot_index >= budget) return;  // bounded-output cap (B10)
            const float f = next * inv_chord;
            const float t = t1 + (t2 - t1) * f;
            emit_dot(x1 + dx * f, y1 + dy * f, t, dir_x, dir_y, dot_index);
            ++dot_index;
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
    // Phase G contract, AGE-progress domain: 0 = newest/head, 1 = oldest
    // (lifetime boundary).
    //   age_progress <= fade_start -> full opacity (fade_progress 0)
    //   otherwise                 -> remap the remaining span to [0,1] and
    //                                apply the selected FadeCurve.
    // fade_start 0 therefore preserves the previous always-fading
    // behavior (Linear curve), and every curve reaches exactly 0 at the
    // boundary. Pure function of elapsed time (B6 frame-rate freedom).
    const float age = clamp01(age_progress);
    const float start = clamp01(config_.fade_start);
    if (age <= start) return 1.0f;
    const float span = 1.0f - start;
    const float fade_progress = span > 0.0f ? (age - start) / span : 1.0f;
    return 1.0f - apply_fade_curve(config_.fade_curve, fade_progress);
}

float TrailEffect::alpha_at(float t) const {
    // t in [0,1]: 0 = oldest visible (lifetime boundary), 1 = newest.
    const float u = clamp01(t);
    return config_.base_opacity * fade(1.0f - u);
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
    const float u = clamp01(t);

    const float sr = static_cast<float>(config_.start_color_r) / 255.0f;
    const float sg = static_cast<float>(config_.start_color_g) / 255.0f;
    const float sb = static_cast<float>(config_.start_color_b) / 255.0f;

    const float fr = static_cast<float>(config_.fade_color_r) / 255.0f;
    const float fg = static_cast<float>(config_.fade_color_g) / 255.0f;
    const float fb = static_cast<float>(config_.fade_color_b) / 255.0f;

    auto lerp_color = [sr, sg, sb, fr, fg, fb](float factor) -> TrailColorF {
        factor = clamp01(factor);
        return TrailColorF{
            fr + (sr - fr) * factor,
            fg + (sg - fg) * factor,
            fb + (sb - fb) * factor
        };
    };

    switch (config_.color_mode) {
        case TrailColorMode::Full:
            return TrailColorF{sr, sg, sb};

        case TrailColorMode::Gradient:
            return lerp_color(u);

        case TrailColorMode::StartAccent: {
            // For t <= 0.75, color = Fade. For 0.75 < t <= 1.0, interpolate Fade -> Start.
            const float accent_start = 1.0f - kColorAccentSpan;
            if (u <= accent_start) {
                return TrailColorF{fr, fg, fb};
            }
            const float factor = (u - accent_start) / kColorAccentSpan;
            return lerp_color(factor);
        }

        case TrailColorMode::FadeAccent: {
            // For t >= 0.25, color = Start. For 0 <= t < 0.25, interpolate Fade -> Start.
            if (u >= kColorAccentSpan) {
                return TrailColorF{sr, sg, sb};
            }
            const float factor = u / kColorAccentSpan;
            return lerp_color(factor);
        }

        default:
            return TrailColorF{sr, sg, sb};
    }
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

float TrailEffect::pulse_multiplier(int64_t now_ns) const {
    if (config_.style != TrailStyle::Pulse) return 1.0f;
    constexpr int64_t kPeriodNs =
        static_cast<int64_t>(kPulsePeriodMs) * 1'000'000LL;
    const float phase = static_cast<float>(now_ns % kPeriodNs)
                      / static_cast<float>(kPeriodNs);
    // Cosine pulse 0.6..1.0: breathing trail, never fully invisible.
    return 0.6f + 0.4f * (0.5f - 0.5f * std::cos(phase * 6.2831853f));
}

float TrailEffect::style_width_at(float t) const {
    const float w = width_at(t);
    switch (config_.style) {
        case TrailStyle::Comet: {
            // Strong quadratic taper: full head width, ~0.15 near the tail.
            const float u = clamp01(t);
            return w * (0.15f + 0.85f * u * u);
        }
        case TrailStyle::Ribbon: {
            // Forced ribbon profile: 25% head width at the tail growing
            // linearly to the head, independent of the user taper sliders.
            const float u = clamp01(t);
            const float head = config_.head_thickness_px;
            const float rw = head * (0.25f + 0.75f * u);
            return rw < TrailConfig::kMinThicknessPx ? TrailConfig::kMinThicknessPx
                 : (rw > TrailConfig::kMaxThicknessPx ? TrailConfig::kMaxThicknessPx : rw);
        }
        case TrailStyle::Classic:
        case TrailStyle::SoftGlow:
        case TrailStyle::Neon:
        case TrailStyle::Dotted:
        case TrailStyle::Pulse:
        case TrailStyle::Spark:
        default:
            return w;
    }
}

void TrailEffect::build_geometry(const CursorHistory& history,
                                 int64_t now_ns,
                                 const TrailPoint* live_head,
                                 int history_count_bound,
                                 TrailGeometrySink& sink) const {
    (void)history_count_bound;  // scratch_ is self-bounding (see header)
    if (!config_.enabled) {
        sink.reserve_hint(0);
        return;
    }

    const int64_t lifetime_ns = static_cast<int64_t>(config_.lifetime_ms * kNsPerMs);
    const int64_t window_start = now_ns - lifetime_ns;
    const float lifetime_ms_f = config_.lifetime_ms;

    scratch_.clear();

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
            tail.ts = window_start;
            if (!(quantize(tail.x) == quantize(a.x)
                  && quantize(tail.y) == quantize(a.y))) {
                scratch_.insert(scratch_.begin(), tail);
            }
        }
    }

    // Live head (B5/B12): pin the trail tip to the real pointer so the
    // trail never appears to lag the cursor between raw-input packets.
    if (live_head != nullptr && is_finite(live_head->x) && is_finite(live_head->y)) {
        const BuildPoint head{live_head->x, live_head->y, 1.0f, now_ns};
        if (scratch_.empty()
            || quantize(scratch_.back().x) != quantize(head.x)
            || quantize(scratch_.back().y) != quantize(head.y)) {
            scratch_.push_back(head);
        } else {
            scratch_.back() = head; // refresh newest point to live position
        }
    }

    // Nothing visible: no segments at all (B11 "empty history" case).
    if (scratch_.size() < 2) {
        sink.reserve_hint(0);
        return;
    }

    // Straight mode (smoothing == 0): raw polyline, still age-faded.
    if (config_.smoothing <= 0.0f) {
        emit_polyline(scratch_, config_.base_opacity, now_ns, sink);
        return;
    }

    emit_catmull_rom(scratch_, config_.base_opacity, now_ns, sink);
}

void TrailEffect::emit_polyline(const std::vector<BuildPoint>& pts,
                                float base_alpha, int64_t now_ns,
                                TrailGeometrySink& sink) const {
    sink.reserve_hint(static_cast<int>(pts.size()) - 1);
    const bool dots = config_.style == TrailStyle::Dotted
                   || config_.style == TrailStyle::Spark;
    const float pulse = pulse_multiplier(now_ns);

    if (dots) {
        const int max_dot_budget = max_segments_for(512);
        DotSampler sampler{config_.segment_spacing_px, max_dot_budget};
        auto emit_dot = [&](float x, float y, float t,
                            float dir_x, float dir_y, int index) {
            const float half = kDotStubPx * 0.5f;
            float alpha = base_alpha * t * pulse;
            if (config_.style == TrailStyle::Spark) {
                alpha *= spark_flicker(index, now_ns);
            }
            sink.add_segment(x - dir_x * half, y - dir_y * half,
                             x + dir_x * half, y + dir_y * half,
                             alpha, style_width_at(t), color_at(t));
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
        sink.add_segment(a.x, a.y, b.x, b.y, base_alpha * b.t * pulse,
                         style_width_at(b.t), color_at(b.t));
    }
}

void TrailEffect::emit_catmull_rom(const std::vector<BuildPoint>& pts,
                                   float base_alpha, int64_t now_ns,
                                   TrailGeometrySink& sink) const {
    const std::size_t n = pts.size();
    const int spans = static_cast<int>(n) - 1;
    sink.reserve_hint(spans * kCurveSubdivisions);

    const bool dots = config_.style == TrailStyle::Dotted
                   || config_.style == TrailStyle::Spark;
    const float pulse = pulse_multiplier(now_ns);

    // T-016 spacing repair: the SAME DotSampler drives polyline and curve
    // emission, so smoothed paths obey the identical arc-length spacing
    // contract (residual carried across spans; density-independent).
    const int max_dot_budget = max_segments_for(512);
    DotSampler sampler{config_.segment_spacing_px, max_dot_budget};
    auto emit_dot = [&](float x, float y, float t,
                        float dir_x, float dir_y, int index) {
        const float half = kDotStubPx * 0.5f;
        float alpha = base_alpha * t * pulse;
        if (config_.style == TrailStyle::Spark) {
            alpha *= spark_flicker(index, now_ns);
        }
        sink.add_segment(x - dir_x * half, y - dir_y * half,
                         x + dir_x * half, y + dir_y * half,
                         alpha, style_width_at(t), color_at(t));
    };

    for (std::size_t i = 1; i < n; ++i) {
        const BuildPoint& p1 = pts[i - 1];
        const BuildPoint& p2 = pts[i];
        const BuildPoint& p0 = (i >= 2) ? pts[i - 2] : p1;
        const BuildPoint& p3 = (i + 1 < n) ? pts[i + 1] : p2;

        float c1x, c1y, c2x, c2y;
        centripetal_cp(config_.smoothing, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y,
                       p3.x, p3.y, c1x, c1y, c2x, c2y);

        // Per-subdivision alpha: linear blend of endpoint window positions
        // -> smooth fade along the curve, monotone within the segment.
        // Width and color from the same interpolated window position.
        float px = p1.x, py = p1.y, prev_t = p1.t;
        for (int s = 1; s <= kCurveSubdivisions; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(kCurveSubdivisions);
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
            sink.add_segment(px, py, qx, qy, base_alpha * t * pulse,
                             style_width_at(t), color_at(t));
            px = qx;
            py = qy;
            prev_t = t;
        }
    }
}

} // namespace ptd
