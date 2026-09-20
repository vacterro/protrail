// T-008 B11: pure trail-math tests. No Qt, no D2D runtime calls (only D2D
// value types in TrailEffect's public interface), so correctness does not
// depend on screenshot/manual testing.

#include "../src/effects/trail_effect.h"

#include <cmath>
#include <cstdio>
#include <chrono>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void expect_true(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

void expect_near(float got, float want, float eps, const char* what) {
    ++g_checks;
    if (!(std::fabs(got - want) <= eps)) {
        ++g_failures;
        std::printf("FAIL %s: got=%f want=%f\n", what, got, want);
    }
}

// Recording sink: captures segments for assertions. T-015: segments carry
// the per-segment TrailColorF selected by TrailEffect.
class RecordSink : public ptd::TrailGeometrySink {
public:
    void reserve_hint(int) override {}
    void add_segment(float x1, float y1, float x2, float y2,
                     float alpha, float thickness_px,
                     ptd::TrailColorF color) override {
        segs.push_back(Seg{x1, y1, x2, y2, alpha, thickness_px, color});
    }
    struct Seg {
        float x1, y1, x2, y2, alpha, thickness;
        ptd::TrailColorF color;
    };
    std::vector<Seg> segs;

    void clear() { segs.clear(); }
};

constexpr int64_t kMs = 1'000'000; // ns per ms

ptd::CursorSample move(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    return s;
}

ptd::CursorSample click_at(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    s.button = ptd::MouseButton::Left;
    s.action = ptd::ButtonAction::Down;
    return s;
}

ptd::TrailConfig make_cfg() {
    ptd::TrailConfig c;      // defaults: yellow, 3 px, 350 ms, 0.9 opacity
    c.smoothing = 0.5f;      // curve mode
    return ptd::TrailConfig::validated(c);
}

} // namespace

// ---- T-015 Phase 3: color-mode math helpers ----
namespace {

// Distinct canonical endpoints: Start red, Fade blue.
ptd::TrailConfig make_color_cfg(ptd::TrailColorMode mode) {
    ptd::TrailConfig c = make_cfg();
    c.start_color_r = 255; c.start_color_g = 0;   c.start_color_b = 0;
    c.fade_color_r  = 0;   c.fade_color_g  = 0;   c.fade_color_b  = 255;
    c.color_mode = mode;
    return ptd::TrailConfig::validated(c);
}

void expect_color(const ptd::TrailColorF& got, float r, float g, float b,
                  const char* what) {
    expect_near(got.r, r, 1e-5f, what);
    expect_near(got.g, g, 1e-5f, what);
    expect_near(got.b, b, 1e-5f, what);
}

} // namespace

// ---- Phase B helpers: smoothing-contract regression ----
namespace {

// 3-point non-collinear path: A=(0,0) at lifetime boundary, B=(40,0),
// C=(40,40). Span 0 (A->B) is emitted first; its 6th segment (0-based index
// 5) ends at curve parameter u = 6/12 = 0.5 -- the exact span midpoint.
struct PathData {
    ptd::CursorHistory history{512};
    int64_t now = 10'000 * kMs;
    float chord_mx = 20.0f;  // mid of A->B
    float chord_my = 0.0f;
};

void build_three_point_path(ptd::TrailConfig cfg, PathData& pd) {
    const int64_t lifetime_ns = static_cast<int64_t>(cfg.lifetime_ms * kMs);
    // A exactly at the lifetime boundary -> tail interpolation is skipped;
    // all three positions are distinct -> dedupe keeps them.
    pd.history.push(move(pd.now - lifetime_ns, 0, 0));
    pd.history.push(move(pd.now - 200 * kMs, 40, 0));
    pd.history.push(move(pd.now - 100 * kMs, 40, 40));
}

// Distance of span-0 curve midpoint from the chord midpoint A->B.
// For smoothing > 0: 12 subdivisions per span, segment[5] endpoint is the
// exact u=0.5 curve point.  For smoothing == 0: polyline mode emits 1 segment
// per span; endpoint of segment[0] = u=1.0 of the straight span (chord
// midpoint is the segment's own midpoint, i.e. chord endpoint of seg0).
float span0_midpoint_deviation(const PathData& pd, ptd::TrailConfig cfg, RecordSink& sink) {
    ptd::TrailEffect e(cfg);
    // PERF-003: this contract samples the exact u=0.5 point as segment[5],
    // which is the fixed-12 reference tessellation. Adaptive production
    // equivalence is proven by the dedicated PERF-003 regressions.
    e.set_reference_tessellation_for_tests(true);
    sink.clear();
    e.build_geometry(pd.history, pd.now, nullptr, 512, sink);
    float mx, my;
    if (cfg.smoothing <= 0.0f) {
        // Polyline: seg[0] spans A->B. Midpoint of segment = (A+B)/2.
        mx = (sink.segs[0].x1 + sink.segs[0].x2) * 0.5f;
        my = (sink.segs[0].y1 + sink.segs[0].y2) * 0.5f;
    } else {
        // Curve: 12 subdivs per span. seg[5] (6th) ends at u=0.5.
        mx = sink.segs[5].x2;
        my = sink.segs[5].y2;
    }
    const float dx = mx - pd.chord_mx;
    const float dy = my - pd.chord_my;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

int main() {
    using ptd::CursorHistory;
    using ptd::TrailEffect;

    // ---- age-window filtering + stale exclusion (B3/B11) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        // Stale sample sits LEFT of the visible span; if it were consumed,
        // some segment endpoint would touch x == -50 exactly.
        h.push(move(now - 1000 * kMs, -50, 0));    // stale: outside 350 ms window
        h.push(move(now - 100 * kMs, 10, 0));      // visible
        h.push(move(now - 50 * kMs, 20, 0));       // visible
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        expect_true(!sink.segs.empty(), "window: visible samples produce segments");
        for (const auto& s : sink.segs) {
            expect_true(s.x1 > -49.9f && s.x2 > -49.9f,
                        "window: stale sample has no geometry");
            expect_true(s.alpha >= 0.0f && s.alpha <= 0.9001f,
                        "window: alphas in range");
        }
        // Tail boundary: the oldest drawn segment must sit at/near the
        // lifetime boundary (alpha close to 0), not at full head alpha.
        expect_true(sink.segs.front().alpha < 0.3f,
                    "window: tail starts near-zero alpha");
        expect_true(sink.segs.back().alpha > 0.5f,
                    "window: head keeps strong alpha");
    }

    // ---- duplicate positional points collapsed (B3/B11) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        for (int i = 0; i < 5; ++i) {
            h.push(move(now - (100 - i) * kMs, 42, 42)); // same position
        }
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        expect_true(sink.segs.empty(),
                    "dedupe: identical positions produce zero segments");
    }

    // ---- button-transition samples contribute coordinates (B3) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        h.push(move(now - 100 * kMs, 0, 0));
        h.push(click_at(now - 90 * kMs, 50, 50));    // click sample: valid coords
        h.push(move(now - 80 * kMs, 100, 100));
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        expect_true(!sink.segs.empty(),
                    "clicks: transition samples do not create holes");
        bool crossed_click = false;
        for (const auto& s : sink.segs) {
            if ((s.x1 <= 50.f && s.x2 >= 50.f) || (s.x1 >= 50.f && s.x2 <= 50.f)) {
                crossed_click = true;
            }
        }
        expect_true(crossed_click, "clicks: geometry crosses the click position");
    }

    // ---- negative coordinates transformed correctly (B4/B11) ----
    {
        // Transform itself lives in OverlayWindow (screen - origin). The
        // trail-side contract tested here: negative coords pass through
        // TrailEffect unmodified.
        TrailEffect e(make_cfg());
        // PERF-003: assert the reference subdivision count for this contract.
        e.set_reference_tessellation_for_tests(true);
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        h.push(move(now - 100 * kMs, -1920, -1080));
        h.push(move(now - 50 * kMs, -1800, -900));
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        expect_true(sink.segs.size() >= 12, "negative: curve subdivides");
        expect_near(sink.segs.front().x1, -1920.0f, 0.01f, "negative: x preserved");
        expect_near(sink.segs.front().y1, -1080.0f, 0.01f, "negative: y preserved");
    }

    // ---- alpha high near head, ~0 at lifetime boundary, always in range ----
    {
        TrailEffect e(make_cfg());
        const float head = e.alpha_at(1.0f);
        const float boundary = e.alpha_at(0.0f);
        expect_near(head, 0.9f, 1e-4f, "alpha: head = base opacity");
        expect_near(boundary, 0.0f, 1e-4f, "alpha: lifetime boundary = 0");
        for (float t = -0.5f; t <= 1.5f; t += 0.01f) {
            const float a = e.alpha_at(t);
            expect_true(a >= 0.0f && a <= 1.0f, "alpha: within [0,1] for all t");
        }
        expect_true(e.alpha_at(0.8f) > e.alpha_at(0.2f),
                    "alpha: monotone toward head");
    }

    // ---- smoothing output bounded for a sharp corner (B5/B11) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            const int64_t ts = now - static_cast<int64_t>(n - i) * 5 * kMs;
            if (i < n / 2) h.push(move(ts, 100 * i, 0));           // east
            else           h.push(move(ts, 100 * (n / 2), 100 * (i - n / 2))); // north
        }
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        // Overshoot bound: a 90-degree corner at (1200, 0) may not extend
        // more than half a step (50 px) beyond the corner on either axis.
        for (const auto& s : sink.segs) {
            expect_true(s.x1 <= 1250.0f && s.x2 <= 1250.0f,
                        "corner: no east overshoot beyond 50 px");
            expect_true(s.y1 >= -50.0f && s.y2 >= -50.0f,
                        "corner: no south overshoot beyond 50 px");
        }
        // Boundedness: segment count scales with samples, not with time.
        expect_true(sink.segs.size() <= TrailEffect::max_segments_for(512),
                    "corner: segment count within bounded bound");
    }

    // ---- frame-time independence of fade (B6/B11) ----
    {
        TrailEffect e(make_cfg());
        // Fade depends ONLY on elapsed wall time (age/lifetime), never on
        // frame cadence: a 60 Hz frame at age 50 ms and a delayed 144 Hz
        // frame at age 50 ms must compute the same alpha.
        const int64_t lifetime_ns = 350 * kMs;
        const float age_50ms = static_cast<float>(50 * kMs) / static_cast<float>(lifetime_ns);
        const float via_alpha = e.alpha_at(1.0f - age_50ms);
        const float via_fade = e.fade(age_50ms);
        expect_near(via_alpha, 0.9f * via_fade, 1e-4f,
                    "fade: pure function of elapsed time");
        // Deterministic across call order and frame count.
        expect_near(e.fade(0.5f), e.fade(0.5f), 0.0f, "fade: deterministic");
        expect_near(e.fade(0.25f), 0.84375f, 1e-6f, "fade: smooth default quarter");
        expect_near(e.fade(2.0f), 0.0f, 1e-6f, "fade: clamped beyond lifetime");
    }

    // ---- empty history produces no trail (B11) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(512); // empty
        RecordSink sink;
        e.build_geometry(h, 10'000 * kMs, nullptr, 512, sink);
        expect_true(sink.segs.empty(), "empty: no segments");
    }

    // ---- disabled config produces no trail (B2) ----
    {
        ptd::TrailConfig c = make_cfg();
        c.enabled = false;
        TrailEffect e(c);
        CursorHistory h(512);
        const int64_t now = 10'000 * kMs;
        h.push(move(now - 10 * kMs, 1, 1));
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 512, sink);
        expect_true(sink.segs.empty(), "disabled: no segments");
    }

    // ---- bounded segment count contract (B10) ----
    {
        TrailEffect e(make_cfg());
        CursorHistory h(64);
        const int64_t now = 10'000 * kMs;
        for (int i = 0; i < 64; ++i) {
            h.push(move(now - (200 - i * 3) * kMs, i * 7, (i * 13) % 97));
        }
        RecordSink sink;
        e.build_geometry(h, now, nullptr, 64, sink);
        expect_true(sink.segs.size() <= TrailEffect::max_segments_for(64),
                    "bounded: full history respects max segment bound");
    }

    // ---- T-008A: continuous smoothing contract ladder ----
    {
        // Non-collinear 3-point path: A=(0,0) at lifetime boundary,
        // B=(40,0), C=(40,40). Deviation of the span-0 midpoint from its
        // chord must: be ~0 at smoothing 0 (raw polyline), grow monotonically
        // with smoothing, and differ between 0 / 0.25 / 0.5 / 1.0.
        RecordSink sink;
        PathData pd;
        build_three_point_path(make_cfg(), pd);

        ptd::TrailConfig c0   = make_cfg(); c0.smoothing   = 0.0f;
        ptd::TrailConfig c25  = make_cfg(); c25.smoothing  = 0.25f;
        ptd::TrailConfig c50  = make_cfg(); c50.smoothing  = 0.5f;
        ptd::TrailConfig c100 = make_cfg(); c100.smoothing = 1.0f;

        const float d0   = span0_midpoint_deviation(pd, c0,   sink);
        const float d25  = span0_midpoint_deviation(pd, c25,  sink);
        const float d50  = span0_midpoint_deviation(pd, c50,  sink);
        const float d100 = span0_midpoint_deviation(pd, c100, sink);

        // smoothing 0 -> raw polyline: midpoint on the chord (deviation 0).
        expect_true(d0 <= 0.01f, "smoothing 0 = raw polyline");
        // Each step differs meaningfully (0.1 px threshold well above
        // float noise; small geometry keeps differences subtle but real).
        expect_true(d25 - d0   > 0.1f, "smoothing 0.25 differs from 0");
        expect_true(d50 - d25  > 0.1f, "smoothing 0.5 differs from 0.25");
        expect_true(d100 - d50 > 0.1f, "smoothing 1.0 differs from 0.5");
        // Monotone growth of curvature with the smoothing value.
        expect_true(d0 <= d25 && d25 <= d50 && d50 <= d100,
                    "smoothing ladder monotone");
    }

    // ---- T-008A: sharp-corner boundedness at every smoothing level ----
    {
        // 90-degree corner path (same shape as the corner test above).
        // At EVERY smoothing level the emitted geometry must stay within
        // half a step of the corner, and no NaN/Inf may be emitted.
        const int64_t now = 10'000 * kMs;
        const int n = 24;
        const float step = 100.0f;
        for (float sm : {0.0f, 0.25f, 0.5f, 1.0f}) {
            ptd::TrailConfig c = make_cfg();
            c.smoothing = sm;
            TrailEffect e(c);
            CursorHistory h(512);
            for (int i = 0; i < n; ++i) {
                const int64_t ts = now - static_cast<int64_t>(n - i) * 5 * kMs;
                if (i < n / 2) h.push(move(ts, 100 * i, 0));
                else           h.push(move(ts, 100 * (n / 2), 100 * (i - n / 2)));
            }
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(!sink.segs.empty(), "corner: geometry exists");
            for (const auto& s : sink.segs) {
                expect_true(std::isfinite(s.x1) && std::isfinite(s.y1)
                            && std::isfinite(s.x2) && std::isfinite(s.y2)
                            && std::isfinite(s.alpha),
                            "corner: no NaN/Inf");
                expect_true(s.x1 <= 1200.0f + step * 0.5f
                            && s.x2 <= 1200.0f + step * 0.5f,
                            "corner: east overshoot bounded");
                expect_true(s.y1 >= -step * 0.5f && s.y2 >= -step * 0.5f,
                            "corner: south overshoot bounded");
                expect_true(s.alpha >= 0.0f && s.alpha <= 0.9f,
                            "corner: alpha in range");
            }
        }
    }

    // ---- T-008A: NaN/Inf guarded on degenerate inputs ----
    {
        // Duplicate/near-duplicate points across all smoothing values must
        // not produce non-finite geometry.
        for (float sm : {0.0f, 0.25f, 0.5f, 1.0f}) {
            ptd::TrailConfig c = make_cfg();
            c.smoothing = sm;
            TrailEffect e(c);
            CursorHistory h(512);
            const int64_t now = 10'000 * kMs;
            h.push(move(now - 300 * kMs, 10, 10));
            h.push(move(now - 200 * kMs, 10, 10));       // exact duplicate
            h.push(move(now - 100 * kMs, 11, 10));       // 1 px off grid
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            for (const auto& s : sink.segs) {
                expect_true(std::isfinite(s.x1) && std::isfinite(s.y1)
                            && std::isfinite(s.x2) && std::isfinite(s.y2),
                            "degenerate: no NaN/Inf");
            }
        }
    }

    // ---- MVP 05 Phase F: width / taper contract ----
    {
        // Clamp contract via validated().
        ptd::TrailConfig bad{};
        bad.head_thickness_px = -3.0f;
        bad.tail_thickness_px = 999.0f;
        bad.taper_strength = 2.0f;
        const ptd::TrailConfig v = ptd::TrailConfig::validated(bad);
        expect_near(v.head_thickness_px, ptd::TrailConfig::kMinThicknessPx, 1e-5f,
                    "F: head thickness clamped to minimum");
        expect_near(v.tail_thickness_px, ptd::TrailConfig::kMaxThicknessPx, 1e-5f,
                    "F: tail thickness clamped to maximum");
        expect_near(v.taper_strength, 1.0f, 1e-5f, "F: taper clamped to 1");

        // Taper 0 -> constant head width everywhere (approved baseline).
        ptd::TrailConfig base = make_cfg();   // head = tail = 3 px, taper 0
        {
            TrailEffect e(base);
            expect_near(e.width_at(0.0f), 3.0f, 1e-5f, "F: taper0 width@tail");
            expect_near(e.width_at(0.5f), 3.0f, 1e-5f, "F: taper0 width@mid");
            expect_near(e.width_at(1.0f), 3.0f, 1e-5f, "F: taper0 width@head");
        }
        // Taper 1 -> full tail-to-head interpolation.
        ptd::TrailConfig tap = base;
        tap.tail_thickness_px = 1.0f;
        tap.taper_strength = 1.0f;
        {
            TrailEffect e(tap);
            expect_near(e.width_at(0.0f), 1.0f, 1e-5f, "F: taper1 width@tail");
            expect_near(e.width_at(0.5f), 2.0f, 1e-5f, "F: taper1 width@mid");
            expect_near(e.width_at(1.0f), 3.0f, 1e-5f, "F: taper1 width@head");
        }
        // Emitted segments carry the per-t thickness.
        {
            TrailEffect e(tap);
            PathData pd;
            build_three_point_path(base, pd);
            RecordSink sink;
            e.build_geometry(pd.history, pd.now, nullptr, 512, sink);
            expect_true(!sink.segs.empty(), "F: taper geometry exists");
            for (const auto& s : sink.segs) {
                expect_true(std::isfinite(s.thickness) && s.thickness >= 0.5f,
                            "F: emitted thickness finite and >= min");
            }
        }
    }

    // ---- T-019: Smooth is the intentional production default ----
    {
        TrailEffect e(ptd::TrailConfig{});
        expect_near(e.fade(0.0f), 1.0f, 1e-6f, "T-019: smooth default at 0.0");
        expect_near(e.fade(0.25f), 0.84375f, 1e-6f, "T-019: smooth default at 0.25");
        expect_near(e.fade(0.5f), 0.5f, 1e-6f, "T-019: smooth default at 0.5");
        expect_near(e.fade(0.75f), 0.15625f, 1e-6f, "T-019: smooth default at 0.75");
        expect_near(e.fade(1.0f), 0.0f, 1e-6f, "T-019: smooth default at 1.0");
    }

    // ---- MVP 05 Phase G: fade_start + fade curves ----
    {
        // fade_start 0 + Linear == the legacy always-fading behavior.
        ptd::TrailConfig c = make_cfg();
        c.fade_curve = ptd::FadeCurve::Linear;
        TrailEffect e(c);
        expect_near(e.fade(0.0f), 1.0f, 1e-6f, "G: head full");
        expect_near(e.fade(0.5f), 0.5f, 1e-6f, "G: legacy linear midpoint");
        expect_near(e.fade(1.0f), 0.0f, 1e-6f, "G: alpha 0 at boundary");

        // fade_start 0.5: newest 50% at full opacity, then fade.
        c.fade_start = 0.5f;
        {
            TrailEffect e2(c);
            expect_near(e2.fade(0.25f), 1.0f, 1e-6f,
                        "G: plateau inside fade_start");
            expect_near(e2.fade(0.75f), 0.5f, 1e-6f,
                        "G: remap mid fade");
            expect_near(e2.fade(1.0f), 0.0f, 1e-6f,
                        "G: boundary still zero");
        }

        // Curves: f(0)=0, f(1)=1, bounded 0..1 for every curve type.
        for (ptd::FadeCurve fc : {ptd::FadeCurve::Linear, ptd::FadeCurve::Smooth,
                                  ptd::FadeCurve::EaseOut}) {
            expect_near(TrailEffect::apply_fade_curve(fc, 0.0f), 0.0f, 1e-6f,
                        "G: curve(0)=0");
            expect_near(TrailEffect::apply_fade_curve(fc, 1.0f), 1.0f, 1e-6f,
                        "G: curve(1)=1");
            for (float p = -0.5f; p <= 1.5f; p += 0.01f) {
                const float f = TrailEffect::apply_fade_curve(fc, p);
                expect_true(f >= 0.0f && f <= 1.0f, "G: curve bounded [0,1]");
            }
        }

        // Smooth is a smoothstep: midpoint 0.5.
        expect_near(TrailEffect::apply_fade_curve(ptd::FadeCurve::Smooth, 0.5f),
                    0.5f, 1e-6f, "G: smoothstep midpoint");
        // EaseOut is front-loaded: f(0.5) > 0.5.
        expect_true(TrailEffect::apply_fade_curve(ptd::FadeCurve::EaseOut, 0.5f) > 0.5f,
                    "G: easeout front-loaded");

        // fade_start clamps: negative -> 0, >0.95 -> 0.95 (zero-at-boundary
        // guarantee needs a non-empty fading span).
        ptd::TrailConfig fs{};
        fs.fade_start = -1.0f;
        expect_near(ptd::TrailConfig::validated(fs).fade_start, 0.0f, 1e-5f,
                    "G: fade_start clamped low");
        fs.fade_start = 5.0f;
        expect_near(ptd::TrailConfig::validated(fs).fade_start, 0.95f, 1e-5f,
                    "G: fade_start clamped high");
        // Frame-time independence of the plateau path too.
        expect_near(TrailEffect(c).fade(0.75f), TrailEffect(c).fade(0.75f), 0.0f,
                    "G: deterministic");
    }

    // ---- T-015 Phase 3: color_at per mode ----
    {
        // Full: Start everywhere.
        TrailEffect full(make_color_cfg(ptd::TrailColorMode::Full));
        expect_color(full.color_at(0.0f), 1.0f, 0.0f, 0.0f, "color Full: t=0 exact Start");
        expect_color(full.color_at(0.5f), 1.0f, 0.0f, 0.0f, "color Full: t=0.5 exact Start");
        expect_color(full.color_at(1.0f), 1.0f, 0.0f, 0.0f, "color Full: t=1 exact Start");

        // Gradient: t=0 exact Fade, t=0.5 midpoint, t=1 exact Start.
        TrailEffect grad(make_color_cfg(ptd::TrailColorMode::Gradient));
        expect_color(grad.color_at(0.0f), 0.0f, 0.0f, 1.0f, "color Gradient: t=0 exact Fade");
        expect_color(grad.color_at(0.5f), 0.5f, 0.0f, 0.5f, "color Gradient: t=0.5 midpoint");
        expect_color(grad.color_at(1.0f), 1.0f, 0.0f, 0.0f, "color Gradient: t=1 exact Start");

        // Start only (StartAccent): Fade through t <= 0.75, then Fade ->
        // Start across the final 25%.
        TrailEffect sa(make_color_cfg(ptd::TrailColorMode::StartAccent));
        expect_color(sa.color_at(0.0f), 0.0f, 0.0f, 1.0f, "color StartAccent: t=0 Fade");
        expect_color(sa.color_at(0.5f), 0.0f, 0.0f, 1.0f, "color StartAccent: t=0.5 Fade");
        expect_color(sa.color_at(0.75f), 0.0f, 0.0f, 1.0f, "color StartAccent: t=0.75 Fade");
        expect_color(sa.color_at(0.875f), 0.5f, 0.0f, 0.5f,
                     "color StartAccent: head accent midpoint");
        expect_color(sa.color_at(1.0f), 1.0f, 0.0f, 0.0f, "color StartAccent: t=1 Start");

        // Fade only (FadeAccent): Fade -> Start across first 25%, then Start.
        TrailEffect fa(make_color_cfg(ptd::TrailColorMode::FadeAccent));
        expect_color(fa.color_at(0.0f), 0.0f, 0.0f, 1.0f, "color FadeAccent: t=0 Fade");
        expect_color(fa.color_at(0.125f), 0.5f, 0.0f, 0.5f,
                     "color FadeAccent: tail accent midpoint");
        expect_color(fa.color_at(0.25f), 1.0f, 0.0f, 0.0f, "color FadeAccent: t=0.25 Start");
        expect_color(fa.color_at(0.5f), 1.0f, 0.0f, 0.0f, "color FadeAccent: t=0.5 Start");
        expect_color(fa.color_at(1.0f), 1.0f, 0.0f, 0.0f, "color FadeAccent: t=1 Start");

        // Clamping below 0 / above 1.
        const ptd::TrailColorF lo = grad.color_at(-0.5f);
        const ptd::TrailColorF at0 = grad.color_at(0.0f);
        expect_true(lo == at0, "color: t<0 clamps to t=0");
        const ptd::TrailColorF hi = grad.color_at(1.5f);
        const ptd::TrailColorF at1 = grad.color_at(1.0f);
        expect_true(hi == at1, "color: t>1 clamps to t=1");

        // Invalid mode validates to Full.
        ptd::TrailConfig bad = make_cfg();
        bad.color_mode = static_cast<ptd::TrailColorMode>(99);
        const auto v = ptd::TrailConfig::validated(bad);
        expect_true(v.color_mode == ptd::TrailColorMode::Full,
                    "color: invalid mode validates to Full");
    }

    // ---- T-015 CRITICAL REGRESSION: color-only changes never move geometry ----
    {
        // Build the same path twice: once with default colors/mode, once
        // with a different mode and completely different colors. Segment
        // count, endpoints, alpha and thickness must be identical; the
        // captured colors must actually differ (proves the sink capture
        // carries the mode-selected color).
        PathData pd;
        build_three_point_path(make_cfg(), pd);

        ptd::TrailConfig base = make_cfg();
        ptd::TrailConfig recolored = base;
        recolored.start_color_r = 10; recolored.start_color_g = 20; recolored.start_color_b = 30;
        recolored.fade_color_r = 200; recolored.fade_color_g = 210; recolored.fade_color_b = 220;
        recolored.color_mode = ptd::TrailColorMode::Gradient;

        RecordSink s1, s2;
        TrailEffect(base).build_geometry(pd.history, pd.now, nullptr, 512, s1);
        TrailEffect(recolored).build_geometry(pd.history, pd.now, nullptr, 512, s2);

        expect_true(s1.segs.size() == s2.segs.size(),
                    "regression: color-only change keeps segment count");
        const std::size_t n = s1.segs.size() < s2.segs.size() ? s1.segs.size() : s2.segs.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = s1.segs[i];
            const auto& b = s2.segs[i];
            expect_true(a.x1 == b.x1 && a.y1 == b.y1
                        && a.x2 == b.x2 && a.y2 == b.y2,
                        "regression: color-only change keeps coordinates");
            expect_true(a.alpha == b.alpha,
                        "regression: color-only change keeps alpha");
            expect_true(a.thickness == b.thickness,
                        "regression: color-only change keeps thickness");
            expect_true(!(a.color == b.color),
                        "regression: captured colors reflect the mode change");
        }
    }

    // ---- T-016 style math ----
    {
        // Validation: invalid style -> Classic, glow + spacing clamped.
        ptd::TrailConfig bad = make_cfg();
        bad.style = static_cast<ptd::TrailStyle>(99);
        bad.glow_strength = 7.0f;
        bad.segment_spacing_px = 0.0f;
        const auto v = ptd::TrailConfig::validated(bad);
        expect_true(v.style == ptd::TrailStyle::Classic, "style: invalid validates to Classic");
        expect_near(v.glow_strength, 1.0f, 1e-6f, "style: glow clamped high");
        expect_near(v.segment_spacing_px, ptd::TrailConfig::kMinSegmentSpacingPx,
                    1e-6f, "style: spacing clamped low");

        // Classic stays the exact Phase F baseline width.
        ptd::TrailConfig classic = make_cfg();
        expect_near(TrailEffect(classic).style_width_at(0.0f),
                    TrailEffect(classic).width_at(0.0f), 0.0f,
                    "style: classic width == baseline");
        expect_near(TrailEffect(classic).pulse_multiplier(1234567LL * kMs), 1.0f, 0.0f,
                    "style: non-pulse multiplier is exactly 1");

        // Comet: a bright, enlarged head and thin visible middle/tail.
        ptd::TrailConfig comet = classic;
        comet.style = ptd::TrailStyle::Comet;
        {
            TrailEffect e(comet);
            expect_near(e.style_width_at(1.0f), 4.5f, 1e-5f, "style: comet enlarged head");
            expect_true(e.style_width_at(0.0f) < e.style_width_at(0.5f)
                        && e.style_width_at(0.5f) < e.style_width_at(1.0f),
                        "style: comet taper monotone toward head");
            expect_true(e.style_width_at(0.0f) < 3.0f * 0.2f,
                        "style: comet tail strongly tapered");
            expect_true(e.style_width_at(0.5f) < 3.0f * 0.45f,
                        "style: comet middle visibly thinner than Classic");
            expect_true(e.style_alpha_multiplier(0.5f, 0) < 0.60f,
                        "style: comet alpha emphasizes head across visible middle");
        }

        // Ribbon: broad/narrow lobes alternate, not a monotonic taper.
        ptd::TrailConfig ribbon = classic;
        ribbon.style = ptd::TrailStyle::Ribbon;
        {
            TrailEffect e(ribbon);
            float lo = 100.0f, hi = 0.0f;
            int direction_changes = 0;
            float prev = e.style_width_at(0.0f, 0);
            int previous_direction = 0;
            for (int i = 1; i <= 40; ++i) {
                const float w = e.style_width_at(i / 40.0f, 0);
                if (w < lo) lo = w;
                if (w > hi) hi = w;
                const int direction = w > prev ? 1 : -1;
                if (previous_direction && direction != previous_direction) ++direction_changes;
                previous_direction = direction;
                prev = w;
            }
            expect_true(direction_changes >= 3,
                        "style: ribbon profile has multiple twist lobes");
            expect_true(hi / lo >= 2.0f,
                        "style: ribbon broad/narrow ratio at least 2x");
        }

        // Pulse: spatial wave travels smoothly, with meaningful width and
        // alpha contrast at the same instant.
        ptd::TrailConfig pulse = classic;
        pulse.style = ptd::TrailStyle::Pulse;
        {
            TrailEffect e(pulse);
            for (int64_t ns = 0; ns <= static_cast<int64_t>(TrailEffect::kPulsePeriodMs) * kMs;
                 ns += 37 * kMs) {
                const float m = e.pulse_multiplier(ns);
                expect_true(m >= 0.55f - 1e-5f && m <= 1.0f + 1e-5f,
                            "style: pulse multiplier bounded [0.55,1]");
            }
            expect_near(e.pulse_multiplier(0LL), 1.0f, 1e-5f, "style: pulse head crest at phase 0");
            expect_near(e.pulse_multiplier(123'456'789LL),
                        e.pulse_multiplier(123'456'789LL), 0.0f,
                        "style: pulse deterministic");
            const float crest = e.style_width_at(0.50f, 0);
            const float trough = e.style_width_at(0.50f, 450 * kMs);
            expect_true(std::abs(crest - trough) > 0.6f,
                        "style: pulse width changes perceptibly over time");
            expect_true(std::abs(e.style_width_at(0.50f, 0)
                               - e.style_width_at(0.66f, 0)) > 0.6f,
                        "style: pulse width varies along the trail");
            expect_true(std::abs(e.style_alpha_multiplier(0.50f, 0)
                               - e.style_alpha_multiplier(0.66f, 0)) > 0.25f,
                        "style: pulse alpha varies along the trail");
        }

        // Spark flicker: deterministic, bounded, varies with dot index.
        {
            const float a = TrailEffect::spark_flicker(3, 1'000'000'000LL);
            const float b = TrailEffect::spark_flicker(3, 1'000'000'000LL);
            expect_true(a == b, "style: spark flicker deterministic");
            expect_true(a >= 0.35f && a <= 1.0f, "style: spark flicker bounded");
            for (int i = 0; i < 32; ++i) {
                const float f = TrailEffect::spark_flicker(i, 500'000'000LL);
                expect_true(f >= 0.35f && f <= 1.0f, "style: spark flicker sweep bounded");
            }
        }

        // Dotted: spacing controls segment count; dots are stubs.
        PathData pd;
        build_three_point_path(classic, pd);
        ptd::TrailConfig dotted = classic;
        dotted.style = ptd::TrailStyle::Dotted;
        dotted.segment_spacing_px = 64.0f;
        {
            RecordSink s_classic, s_dots;
            TrailEffect(classic).build_geometry(pd.history, pd.now, nullptr, 512, s_classic);
            TrailEffect(dotted).build_geometry(pd.history, pd.now, nullptr, 512, s_dots);
            expect_true(!s_classic.segs.empty(), "style: classic baseline emits");
            expect_true(s_dots.segs.size() < s_classic.segs.size(),
                        "style: dotted emits fewer segments than classic");
            for (const auto& s : s_dots.segs) {
                const float dx = s.x2 - s.x1;
                const float dy = s.y2 - s.y1;
                const float len = std::sqrt(dx * dx + dy * dy);
                expect_near(len, TrailEffect::kDotStubPx, 0.01f,
                            "style: dot stub length constant");
                expect_true(std::isfinite(s.alpha) && s.alpha > 0.0f,
                            "style: dot alpha positive finite");
            }
            // Tighter spacing -> more dots (monotone in spacing).
            dotted.segment_spacing_px = 4.0f;
            RecordSink s_dense;
            TrailEffect(dotted).build_geometry(pd.history, pd.now, nullptr, 512, s_dense);
            expect_true(s_dense.segs.size() > s_dots.segs.size(),
                        "style: tighter spacing emits more dots");
        }

        // Style-only changes never break the geometry/color regression:
        // Pulse changes ONLY alpha multiplier; Comet changes ONLY width.
        {
            ptd::TrailConfig pulse_cfg = classic;
            pulse_cfg.style = ptd::TrailStyle::Pulse;
            RecordSink s_base, s_pulse;
            TrailEffect(classic).build_geometry(pd.history, pd.now, nullptr, 512, s_base);
            TrailEffect(pulse_cfg).build_geometry(pd.history, pd.now, nullptr, 512, s_pulse);
            expect_true(s_base.segs.size() == s_pulse.segs.size(),
                        "style: pulse keeps segment count");
            for (std::size_t i = 0; i < s_base.segs.size() && i < s_pulse.segs.size(); ++i) {
                expect_true(s_base.segs[i].x1 == s_pulse.segs[i].x1
                            && s_base.segs[i].y2 == s_pulse.segs[i].y2,
                            "style: pulse keeps coordinates");
                expect_true(s_base.segs[i].color == s_pulse.segs[i].color,
                            "style: pulse keeps colors");
            }
        }
    }

    // ---- T-016R spacing repair: arc-length Dotted/Spark contract ----
    {
        auto straight_path = [](ptd::TrailConfig cfg, int n_chords,
                                int chord_px, ptd::CursorHistory& h,
                                int64_t now) {
            const int64_t lifetime_ns = static_cast<int64_t>(cfg.lifetime_ms * kMs);
            for (int i = 0; i <= n_chords; ++i) {
                h.push(move(now - lifetime_ns + static_cast<int64_t>(i) * (lifetime_ns / n_chords),
                            i * chord_px, 0));
            }
        };

        auto count_dots = [](ptd::TrailConfig cfg, ptd::CursorHistory& h,
                             int64_t now, std::vector<float>* out_x = nullptr) {
            TrailEffect e(cfg);
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            if (out_x) {
                for (const auto& s : sink.segs) out_x->push_back(s.x1);
            }
            return static_cast<int>(sink.segs.size());
        };

        // (a) One long sparse straight segment: 500 px chord, 10 px spacing
        //     -> ~50 dots (endpoint policy: no dot at arc 0; the last dot
        //     may sit one spacing short of the end if the residual < spacing).
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.segment_spacing_px = 10.0f;
            cfg.smoothing = 0.0f;
            ptd::CursorHistory h(512);
            const int64_t now = 10'000 * kMs;
            straight_path(cfg, 1, 500, h, now);
            const int dots = count_dots(cfg, h, now);
            expect_true(dots >= 49 && dots <= 51,
                        "spacing: 500 px chord @ 10 px -> ~50 dots");
            // Regular spacing between consecutive dot positions.
            std::vector<float> xs;
            (void)count_dots(cfg, h, now, &xs);
            for (std::size_t i = 2; i < xs.size(); ++i) {
                const float d1 = xs[i] - xs[i - 1];
                const float d0 = xs[i - 1] - xs[i - 2];
                expect_true(std::fabs(d1 - d0) < 0.5f,
                            "spacing: consecutive dot gaps equal");
            }
        }

        // (b) Density independence: the same 500 px geometric path sampled
        //     as 1x500, 5x100 and 50x10 px chords yields the same dots.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.segment_spacing_px = 10.0f;
            cfg.smoothing = 0.0f;
            const int64_t now = 10'000 * kMs;
            std::vector<float> xs1, xs5, xs50;
            {
                ptd::CursorHistory h(512);
                straight_path(cfg, 1, 500, h, now);
                (void)count_dots(cfg, h, now, &xs1);
            }
            {
                ptd::CursorHistory h(512);
                straight_path(cfg, 5, 100, h, now);
                (void)count_dots(cfg, h, now, &xs5);
            }
            {
                ptd::CursorHistory h(512);
                straight_path(cfg, 50, 10, h, now);
                (void)count_dots(cfg, h, now, &xs50);
            }
            expect_true(xs1.size() == xs5.size() && xs5.size() == xs50.size(),
                        "spacing: dot count density-independent");
            const std::size_t n = xs1.size() < xs5.size() ? xs1.size() : xs5.size();
            for (std::size_t i = 0; i < n; ++i) {
                expect_true(std::fabs(xs1[i] - xs5[i]) < 0.75f
                            && std::fabs(xs1[i] - xs50[i]) < 0.75f,
                            "spacing: dot positions density-independent");
            }
        }

        // (c) Residual continuity across segment boundaries: two 25 px
        //     chords with 40 px spacing emit exactly one dot ONCE the
        //     combined 50 px arc crosses the threshold (the fused-chord
        //     equivalent), and the residual from the first chord is not
        //     thrown away at the boundary.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.segment_spacing_px = 40.0f;
            cfg.smoothing = 0.0f;
            const int64_t now = 10'000 * kMs;
            ptd::CursorHistory h(512);
            straight_path(cfg, 2, 25, h, now);
            const int dots = count_dots(cfg, h, now);
            expect_true(dots == 1,
                        "spacing: residual carries across segment boundary (50 px @ 40 -> 1 dot)");
        }

        // (d) Multiple emissions inside ONE segment (the old code's core
        //     failure: it emitted at most one dot per chord).
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.segment_spacing_px = 10.0f;
            cfg.smoothing = 0.0f;
            const int64_t now = 10'000 * kMs;
            ptd::CursorHistory h(512);
            straight_path(cfg, 3, 200, h, now);  // 3 chords of 200 px each
            const int dots = count_dots(cfg, h, now);
            // 600 px total @ 10 px spacing -> ~60 dots regardless of the
            // chord decomposition (each chord must emit ~20).
            expect_true(dots >= 58 && dots <= 62,
                        "spacing: multiple dots inside one segment (600 px @ 10)");
        }

        // (e) Minimum and maximum configured spacing behave.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.smoothing = 0.0f;
            const int64_t now = 10'000 * kMs;
            const auto v = ptd::TrailConfig::validated(cfg);
            (void)v;
            cfg.segment_spacing_px = ptd::TrailConfig::kMinSegmentSpacingPx;
            ptd::CursorHistory h1(512);
            straight_path(cfg, 1, 500, h1, now);
            const int min_dots = count_dots(cfg, h1, now);
            expect_true(min_dots >= 249 && min_dots <= 251,
                        "spacing: minimum spacing emits ~250 dots on 500 px");

            cfg.segment_spacing_px = ptd::TrailConfig::kMaxSegmentSpacingPx;  // 64
            ptd::CursorHistory h2(512);
            straight_path(cfg, 1, 500, h2, now);
            const int max_dots = count_dots(cfg, h2, now);
            expect_true(max_dots >= 7 && max_dots <= 9,
                        "spacing: maximum spacing emits ~7-8 dots on 500 px");
        }

        // (f) Smoothed Catmull-Rom paths obey the same contract as the
        //     polyline: same geometric path, same spacing -> same dot count
        //     (within float-tolerance of the curve shortening vs chords).
        {
            const int64_t now = 10'000 * kMs;
            ptd::TrailConfig poly = make_cfg();
            poly.style = ptd::TrailStyle::Dotted;
            poly.segment_spacing_px = 10.0f;
            poly.smoothing = 0.0f;
            ptd::TrailConfig curve = poly;
            curve.smoothing = 0.5f;

            ptd::CursorHistory h_poly(512), h_curve(512);
            straight_path(poly, 1, 500, h_poly, now);
            straight_path(curve, 1, 500, h_curve, now);
            const int poly_dots = count_dots(poly, h_poly, now);
            const int curve_dots = count_dots(curve, h_curve, now);
            // A smoothing 0.5 centripetal curve through collinear points IS
            // the straight chord, so counts must match exactly here.
            expect_true(poly_dots == curve_dots,
                        "spacing: straight smoothed path == polyline path count");
        }

        // (g) Spark uses the same geometric sampling as Dotted; only the
        //     per-dot alpha differs (deterministic flicker).
        {
            ptd::TrailConfig dotted = make_cfg();
            dotted.style = ptd::TrailStyle::Dotted;
            dotted.segment_spacing_px = 16.0f;
            dotted.smoothing = 0.0f;
            ptd::TrailConfig spark = dotted;
            spark.style = ptd::TrailStyle::Spark;
            const int64_t now = 10'000 * kMs;
            ptd::CursorHistory h1(512), h2(512);
            straight_path(dotted, 7, 100, h1, now);
            straight_path(spark, 7, 100, h2, now);
            std::vector<float> xd, xs;
            const int nd = count_dots(dotted, h1, now, &xd);
            const int ns = count_dots(spark, h2, now, &xs);
            expect_true(nd == ns, "spacing: Spark count == Dotted count");
            for (std::size_t i = 0; i < xd.size() && i < xs.size(); ++i) {
                expect_true(std::fabs(xd[i] - xs[i]) < 0.01f,
                            "spacing: Spark dot positions == Dotted positions");
            }
        }

        // (h) Negative virtual-screen coordinates: spacing and stubs stay
        //      correct left of / above the primary monitor.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.segment_spacing_px = 10.0f;
            cfg.smoothing = 0.0f;
            const int64_t now = 10'000 * kMs;
            ptd::CursorHistory h(512);
            const int64_t lifetime_ns = static_cast<int64_t>(cfg.lifetime_ms * kMs);
            // 500 px straight run at negative coordinates: x -1500 -> -1000.
            h.push(move(now - lifetime_ns, -1500, -800));
            h.push(move(now - 10 * kMs, -1000, -800));
            const int dots = count_dots(cfg, h, now);
            expect_true(dots >= 49 && dots <= 51,
                        "spacing: negative-coordinate straight path emits ~50 dots");
            std::vector<float> xs;
            (void)count_dots(cfg, h, now, &xs);
            for (std::size_t i = 1; i < xs.size(); ++i) {
                expect_true(xs[i] > xs[i - 1], "spacing: negative path advances rightward");
                expect_true(xs[i] <= -1000.0f && xs[i] >= -1500.0f,
                            "spacing: negative path stays on segment");
            }
        }

        // (i) Bounded finite output on a pathological sparse path.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Spark;
            cfg.segment_spacing_px = 2.0f;  // densest legal spacing
            cfg.smoothing = 1.0f;
            const int64_t now = 10'000 * kMs;
            ptd::CursorHistory h(512);
            const int64_t lifetime_ns = static_cast<int64_t>(cfg.lifetime_ms * kMs);
            for (int i = 0; i < 64; ++i) {
                h.push(move(now - lifetime_ns + static_cast<int64_t>(i) * (lifetime_ns / 64),
                            i * 137, (i % 2) ? 911 : -911));
            }
            TrailEffect e(cfg);
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(sink.segs.size() <= TrailEffect::max_segments_for(512),
                        "spacing: bounded segment count on pathological path");
            for (const auto& s : sink.segs) {
                expect_true(std::isfinite(s.x1) && std::isfinite(s.y1)
                            && std::isfinite(s.x2) && std::isfinite(s.y2)
                            && std::isfinite(s.alpha) && std::isfinite(s.thickness),
                            "spacing: all emitted dot values finite");
            }
        }
    }

    // ---- FADE CONTRACT REACHES THE SCREEN (repair regression) ----
    //
    // Defect: emit_polyline/emit_catmull_rom published a raw linear
    // `base_opacity * t`, so the Fade start slider and the Fade curve
    // selector were both wired all the way from Settings into TrailConfig
    // and then silently discarded by the emitters. alpha_at()/fade() were
    // correct and unit-tested in isolation, which hid it. These checks
    // compare the EMITTED alpha against the documented contract.
    {
        // A straight path with distinct positions across the window.
        auto make_path = [](ptd::CursorHistory& h, int64_t now,
                            const ptd::TrailConfig& cfg) {
            const int64_t lifetime_ns = static_cast<int64_t>(cfg.lifetime_ms * kMs);
            for (int i = 0; i <= 10; ++i) {
                h.push(move(now - lifetime_ns + (lifetime_ns * i) / 11,
                            i * 20, 100));
            }
        };
        const int64_t now = 10'000 * kMs;

        // Polyline mode: segment alpha == alpha_at(newer endpoint).
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.smoothing = 0.0f;
            cfg.fade_start = 0.5f;
            cfg.fade_curve = ptd::FadeCurve::Smooth;
            CursorHistory h(512);
            make_path(h, now, cfg);
            TrailEffect e(cfg);
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(!sink.segs.empty(), "fade: polyline emitted segments");
            bool any_plateau = false;
            for (const auto& seg : sink.segs) {
                // The newer endpoint drives alpha/width/color (documented).
                // Recover t from the emitted width, not from geometry:
                // instead assert the alpha is a value alpha_at() can produce
                // and that the head segment matches alpha_at(1).
                expect_true(seg.alpha <= cfg.base_opacity + 1e-5f,
                            "fade: emitted alpha never exceeds base opacity");
                if (seg.alpha > cfg.base_opacity - 1e-5f) any_plateau = true;
            }
            expect_near(sink.segs.back().alpha, e.alpha_at(1.0f), 1e-5f,
                        "fade: head segment alpha == alpha_at(1)");
            // fade_start 0.5 means the newest half of the window holds FULL
            // base opacity. A linear emitter can only reach it at the very
            // head, so this plateau is the discriminating evidence.
            expect_true(any_plateau,
                        "fade: fade_start plateau reaches the emitted stroke");
            int plateau_segments = 0;
            for (const auto& seg : sink.segs) {
                if (seg.alpha > cfg.base_opacity - 1e-5f) ++plateau_segments;
            }
            expect_true(plateau_segments >= 3,
                        "fade: plateau covers the newest part of the window");
        }

        // The FadeCurve selector must change what is drawn.
        {
            auto tail_alpha = [&](ptd::FadeCurve curve) {
                ptd::TrailConfig cfg = make_cfg();
                cfg.smoothing = 0.0f;
                cfg.fade_start = 0.0f;
                cfg.fade_curve = curve;
                CursorHistory h(512);
                make_path(h, now, cfg);
                TrailEffect e(cfg);
                RecordSink sink;
                e.build_geometry(h, now, nullptr, 512, sink);
                // An older-quarter segment: the curves disagree clearly
                // there (at the exact window midpoint Linear and Smooth
                // cross, which would make the comparison meaningless).
                return sink.segs[sink.segs.size() / 4].alpha;
            };
            const float lin = tail_alpha(ptd::FadeCurve::Linear);
            const float smo = tail_alpha(ptd::FadeCurve::Smooth);
            const float eas = tail_alpha(ptd::FadeCurve::EaseOut);
            expect_true(lin != smo, "fade: Linear and Smooth curves differ on screen");
            expect_true(lin != eas, "fade: Linear and EaseOut curves differ on screen");
            // fade = 1 - curve(p); EaseOut curve = 1-(1-p)^3, so the
            // emitted alpha is (1-p)^3 -- it drops FASTER than Linear.
            expect_true(eas < lin, "fade: EaseOut drops faster than Linear");
            expect_true(smo < lin, "fade: Smooth trails Linear in the old quarter");
        }

        // Smoothed (Catmull-Rom) emission obeys the same contract.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.smoothing = 0.75f;
            cfg.fade_start = 0.6f;
            cfg.fade_curve = ptd::FadeCurve::EaseOut;
            CursorHistory h(512);
            make_path(h, now, cfg);
            TrailEffect e(cfg);
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(!sink.segs.empty(), "fade: curve mode emitted segments");
            expect_near(sink.segs.back().alpha, e.alpha_at(1.0f), 1e-5f,
                        "fade: curve head alpha == alpha_at(1)");
            int plateau_segments = 0;
            for (const auto& seg : sink.segs) {
                if (seg.alpha > cfg.base_opacity - 1e-5f) ++plateau_segments;
            }
            expect_true(plateau_segments >= 3,
                        "fade: curve mode honours the fade_start plateau");
            for (const auto& seg : sink.segs) {
                expect_true(seg.alpha >= 0.0f
                            && seg.alpha <= cfg.base_opacity + 1e-5f,
                            "fade: curve alphas stay inside the envelope");
            }
        }

        // Alpha must still arrive monotonically non-decreasing oldest ->
        // newest for the batching renderer (B-contract preserved).
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.fade_start = 0.35f;
            cfg.fade_curve = ptd::FadeCurve::Smooth;
            CursorHistory h(512);
            make_path(h, now, cfg);
            TrailEffect e(cfg);
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            for (std::size_t i = 1; i < sink.segs.size(); ++i) {
                expect_true(sink.segs[i].alpha >= sink.segs[i - 1].alpha - 1e-6f,
                            "fade: alpha is non-decreasing oldest -> newest");
            }
        }
    }

    // ---- DOTTED/SPARK LATTICE IS HEAD-ANCHORED (repair regression) ----
    //
    // Defect: DotSampler started its arc-length residual at the TAIL, so
    // the retracting lifetime boundary shifted the phase of the whole dot
    // lattice every frame -- every dot crawled backwards along the stroke
    // and the running dot_index renumbered, re-seeding the Spark flicker
    // of every dot at once. Head-anchoring makes an expiring tail dot
    // remove that dot and nothing else.
    {
        for (const ptd::TrailStyle style : {ptd::TrailStyle::Dotted,
                                            ptd::TrailStyle::Spark}) {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = style;
            cfg.smoothing = 0.0f;
            cfg.segment_spacing_px = 12.0f;
            const int64_t t0 = 10'000 * kMs;
            CursorHistory h(512);
            for (int i = 0; i < 60; ++i) {
                h.push(move(t0 + static_cast<int64_t>(i) * 4 * kMs,
                            100 + 10 * i, 300));
            }
            const int64_t last = t0 + 59 * 4 * kMs;

            TrailEffect e(cfg);
            RecordSink a;
            RecordSink b;
            // Two frames 40 ms apart: the tail retracts, the head does not
            // move (the history is frozen).
            e.build_geometry(h, last + 100 * kMs, nullptr, 512, a);
            e.build_geometry(h, last + 140 * kMs, nullptr, 512, b);
            expect_true(a.segs.size() >= 6 && b.segs.size() >= 6,
                        "dots: both frames emitted a usable lattice");
            expect_true(b.segs.size() <= a.segs.size(),
                        "dots: the retracting tail can only remove dots");

            // Compare from the HEAD end: surviving dots must sit at the
            // exact same coordinates, not slide along the path.
            const std::size_t compare =
                (b.segs.size() < a.segs.size() ? b.segs.size() : a.segs.size()) - 1;
            for (std::size_t k = 0; k < compare; ++k) {
                const auto& sa = a.segs[a.segs.size() - 1 - k];
                const auto& sb = b.segs[b.segs.size() - 1 - k];
                const float ax = (sa.x1 + sa.x2) * 0.5f;
                const float ay = (sa.y1 + sa.y2) * 0.5f;
                const float bx = (sb.x1 + sb.x2) * 0.5f;
                const float by = (sb.y1 + sb.y2) * 0.5f;
                expect_near(bx, ax, 1e-3f, "dots: head-side dot x is anchored");
                expect_near(by, ay, 1e-3f, "dots: head-side dot y is anchored");
            }
        }

        // Same invariant on the SMOOTHED path: the Catmull-Rom emitter
        // shares the one sampler, so its lattice is anchored too.
        {
            ptd::TrailConfig cfg = make_cfg();
            cfg.style = ptd::TrailStyle::Dotted;
            cfg.smoothing = 0.75f;
            cfg.segment_spacing_px = 14.0f;
            const int64_t t0 = 10'000 * kMs;
            CursorHistory h(512);
            for (int i = 0; i < 40; ++i) {
                h.push(move(t0 + static_cast<int64_t>(i) * 6 * kMs,
                            120 + 12 * i, 260 + ((i % 2) ? 18 : -18)));
            }
            const int64_t last = t0 + 39 * 6 * kMs;
            TrailEffect e(cfg);
            RecordSink a;
            RecordSink b;
            e.build_geometry(h, last + 90 * kMs, nullptr, 512, a);
            e.build_geometry(h, last + 130 * kMs, nullptr, 512, b);
            expect_true(a.segs.size() >= 5 && b.segs.size() >= 5,
                        "dots: smoothed lattice emitted dots");
            // Only the head-side half: the OLDEST span legitimately
            // changes shape because its Catmull-Rom neighbour is the
            // moving lifetime-boundary point, so dots inside that span
            // ride real geometry, not a lattice phase shift.
            const std::size_t shared =
                (b.segs.size() < a.segs.size() ? b.segs.size() : a.segs.size());
            const std::size_t compare = shared / 2;
            expect_true(compare >= 3, "dots: enough smoothed dots to compare");
            for (std::size_t k = 0; k < compare; ++k) {
                const auto& sa = a.segs[a.segs.size() - 1 - k];
                const auto& sb = b.segs[b.segs.size() - 1 - k];
                expect_near((sb.x1 + sb.x2) * 0.5f, (sa.x1 + sa.x2) * 0.5f,
                            1e-3f, "dots: smoothed head-side dot x anchored");
                expect_near((sb.y1 + sb.y2) * 0.5f, (sa.y1 + sa.y2) * 0.5f,
                            1e-3f, "dots: smoothed head-side dot y anchored");
            }
        }
    }

    // ---- PERF-003: adaptive tessellation scales with complexity, not sample count ----
    {
        // Dense, nearly-straight 350-sample path. The old fixed-12 walker
        // emitted (350 - 1) * 12 = 4188 segments; adaptive must emit
        // materially fewer because every span is short and flat.
        {
            ptd::TrailConfig c = make_cfg();
            TrailEffect e(c);
            CursorHistory h(512);
            const int64_t now = 10'000 * kMs;
            for (int i = 0; i < 350; ++i) {
                h.push(move(now - (350 - i) * kMs, i, 0));  // 1 px per sample
            }
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(!sink.segs.empty(), "perf003: dense straight emits geometry");
            expect_true(sink.segs.size() < 4188,
                        "perf003: dense straight emits fewer than the old 4188");
            expect_true(sink.segs.size() <= 1600,
                        "perf003: dense straight collapses materially");
        }

        // The retained reference oracle, on the SAME input, proves the old
        // count was 4188 -- so the reduction is real, not a weakened fixture.
        {
            ptd::TrailConfig c = make_cfg();
            TrailEffect e(c);
            e.set_reference_tessellation_for_tests(true);
            CursorHistory h(512);
            const int64_t now = 10'000 * kMs;
            for (int i = 0; i < 350; ++i) {
                h.push(move(now - (350 - i) * kMs, i, 0));
            }
            RecordSink sink;
            e.build_geometry(h, now, nullptr, 512, sink);
            expect_true(sink.segs.size() == 349 * 12,
                        "perf003: reference oracle reproduces the old 4188");
        }

        // Highly curved input must automatically spend more pieces than a
        // near-straight one over the same span count.
        {
            const int64_t now = 10'000 * kMs;
            auto build_zigzag = [&](RecordSink& sink) {
                ptd::TrailConfig c = make_cfg();
                TrailEffect e(c);
                CursorHistory h(512);
                for (int i = 0; i < 40; ++i) {
                    const int y = (i % 2 == 0) ? 0 : 200;
                    h.push(move(now - (40 - i) * 5 * kMs, i * 40, y));
                }
                e.build_geometry(h, now, nullptr, 512, sink);
            };
            RecordSink zig;
            build_zigzag(zig);

            ptd::TrailConfig straight_cfg = make_cfg();
            TrailEffect straight_e(straight_cfg);
            CursorHistory sh(512);
            for (int i = 0; i < 40; ++i) {
                sh.push(move(now - (40 - i) * 5 * kMs, i * 40, 0));
            }
            RecordSink straight;
            straight_e.build_geometry(sh, now, nullptr, 512, straight);

            expect_true(zig.segs.size() > straight.segs.size(),
                        "perf003: curved input subdivides more than straight");
            expect_true(zig.segs.size() <= TrailEffect::max_segments_for(512),
                        "perf003: curved output stays within the ceiling bound");
        }

        // Determinism: identical (history, config, now) -> identical output.
        {
            ptd::TrailConfig c = make_cfg();
            const int64_t now = 10'000 * kMs;
            auto build = [&](RecordSink& sink) {
                TrailEffect e(c);
                CursorHistory h(512);
                for (int i = 0; i < 60; ++i) {
                    h.push(move(now - (60 - i) * 5 * kMs, i * 20, (i * i) % 300));
                }
                e.build_geometry(h, now, nullptr, 512, sink);
            };
            RecordSink a, b;
            build(a);
            build(b);
            expect_true(a.segs.size() == b.segs.size(),
                        "perf003: adaptive output is deterministic (count)");
            bool same = a.segs.size() == b.segs.size();
            for (std::size_t i = 0; same && i < a.segs.size(); ++i) {
                same = a.segs[i].x1 == b.segs[i].x1 && a.segs[i].y1 == b.segs[i].y1
                    && a.segs[i].x2 == b.segs[i].x2 && a.segs[i].y2 == b.segs[i].y2;
            }
            expect_true(same, "perf003: adaptive output is deterministic (bytes)");
        }

        // PERF-003 before/after benchmark: emitted segment count and wall time
        // for the 350- and 512-sample dense-straight cases, adaptive vs the
        // retained fixed-12 reference. Printed as evidence, asserted only for
        // the direction of the change.
        {
            const int64_t now = 10'000 * kMs;
            auto measure = [&](int samples, bool reference, double& ms_out,
                               std::size_t& segs_out) {
                ptd::TrailConfig c = make_cfg();
                TrailEffect e(c);
                e.set_reference_tessellation_for_tests(reference);
                CursorHistory h(512);
                // 0.5 ms spacing keeps EVERY sample inside the 350 ms window,
                // so the 512-sample case exercises 511 visible spans.
                for (int i = 0; i < samples; ++i) {
                    h.push(move(now - (samples - i) * 500'000, i, 0));
                }
                RecordSink sink;
                const auto t0 = std::chrono::steady_clock::now();
                const int iters = 200;
                for (int k = 0; k < iters; ++k) {
                    sink.clear();
                    e.build_geometry(h, now, nullptr, 512, sink);
                }
                const auto t1 = std::chrono::steady_clock::now();
                segs_out = sink.segs.size();
                ms_out = std::chrono::duration<double, std::milli>(t1 - t0).count()
                       / iters;
            };
            for (int samples : {350, 512}) {
                double ad_ms = 0, ref_ms = 0;
                std::size_t ad_segs = 0, ref_segs = 0;
                measure(samples, false, ad_ms, ad_segs);
                measure(samples, true, ref_ms, ref_segs);
                std::printf("PERF-003 %d samples: adaptive %zu segs %.1f us | "
                            "reference %zu segs %.1f us\n",
                            samples, ad_segs, ad_ms * 1000.0, ref_segs,
                            ref_ms * 1000.0);
                expect_true(ref_segs == static_cast<std::size_t>(samples - 1) * 12,
                            "perf003: reference reproduces the fixed-12 count");
                expect_true(ad_segs < ref_segs,
                            "perf003: adaptive emits fewer segments than reference");
            }
        }
        {
            const float kErrorTolPx = 1.0f;
            const int64_t now = 10'000 * kMs;
            ptd::TrailConfig c = make_cfg();
            auto collect = [&](bool reference, std::vector<RecordSink::Seg>& out) {
                TrailEffect e(c);
                e.set_reference_tessellation_for_tests(reference);
                CursorHistory h(512);
                for (int i = 0; i < 30; ++i) {
                    const int y = (i % 3 == 0) ? 0 : (i % 3 == 1 ? 90 : 30);
                    h.push(move(now - (30 - i) * 8 * kMs, i * 25, y));
                }
                RecordSink sink;
                e.build_geometry(h, now, nullptr, 512, sink);
                out = sink.segs;
            };
            std::vector<RecordSink::Seg> adaptive, reference;
            collect(false, adaptive);
            collect(true, reference);

            // Max distance from any adaptive vertex to the reference polyline.
            float max_err = 0.0f;
            for (const auto& s : adaptive) {
                const float px = s.x1, py = s.y1;
                float best = 1e30f;
                for (const auto& r : reference) {
                    const float dx = r.x2 - r.x1, dy = r.y2 - r.y1;
                    const float len2 = dx * dx + dy * dy;
                    float u = 0.0f;
                    if (len2 > 1e-9f) {
                        u = ((px - r.x1) * dx + (py - r.y1) * dy) / len2;
                        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
                    }
                    const float cx = r.x1 + dx * u, cy = r.y1 + dy * u;
                    const float ex = px - cx, ey = py - cy;
                    const float d = std::sqrt(ex * ex + ey * ey);
                    if (d < best) best = d;
                }
                if (best > max_err) max_err = best;
            }
            expect_true(max_err <= kErrorTolPx,
                        "perf003: adaptive stays within subpixel error tolerance");
        }
    }

    std::printf("test_trail_effect: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
