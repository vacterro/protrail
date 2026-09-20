// T-019: continuous-stroke cap-policy regressions. Pure tests (no Qt, no
// D2D runtime): the cap policy and the one-segment deferred sequencer in
// trail_stroke_policy.h are exercised directly, and a recording harness
// sink drives real TrailEffect::build_geometry output through the exact
// production contract OverlayWindow::add_segment()/flush implement:
//   - internal continuous segments are drawn flat-flat;
//   - the final continuous segment is drawn flat-round;
//   - a single-segment frame may use round-round;
//   - Dotted/Spark keep round-round dot stubs;
//   - empty/disabled trails draw nothing;
//   - no internal continuous segment ever uses round-round caps.

#include "../src/render/trail_stroke_policy.h"
#include "../src/effects/trail_effect.h"

#include <cmath>
#include <cstdio>
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

void expect_policy(ptd::TrailCapPolicy got, ptd::TrailCapPolicy want,
                   const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL %s: got=%d want=%d\n", what,
                    static_cast<int>(got), static_cast<int>(want));
    }
}

constexpr int64_t kMs = 1'000'000; // ns per ms

ptd::CursorSample move(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    return s;
}

// Recording harness: mirrors the production sink contract in
// OverlayWindow::add_segment()/flush_pending_trail_segment() (T-019) on
// top of the pure sequencer, so the emitted DrawLine-equivalent cap
// decisions are observable without Direct2D.
class HarnessSink : public ptd::TrailGeometrySink {
public:
    struct Op {
        ptd::TrailCapPolicy caps;
        float x1, y1, x2, y2, alpha, thickness;
    };

    ptd::TrailStyle style = ptd::TrailStyle::Classic;
    ptd::TrailContinuousBatch batch;
    ptd::TrailSegmentRecord pending;
    std::vector<Op> ops;

    void reserve_hint(int) override { batch.begin_frame(); }

    void add_segment(float x1, float y1, float x2, float y2,
                     float alpha, float thickness_px,
                     ptd::TrailColorF color) override {
        if (!(alpha > 0.0f)) return;
        if (ptd::trail_style_is_discrete_dots(style)) {
            record(ptd::TrailSegmentRecord{x1, y1, x2, y2, alpha,
                                           thickness_px, color},
                   ptd::TrailCapPolicy::RoundRound);
            return;
        }
        if (batch.pending()) {
            record(pending, ptd::trail_cap_policy(style,
                                                  ptd::TrailSegmentRole::Internal));
        }
        pending = ptd::TrailSegmentRecord{x1, y1, x2, y2, alpha,
                                          thickness_px, color};
        (void)batch.admit();
    }

    void flush() {
        if (!batch.pending()) return;
        record(pending, ptd::trail_cap_policy(style, batch.flush_role()));
        batch.mark_flushed();
    }

    void record(const ptd::TrailSegmentRecord& r, ptd::TrailCapPolicy caps) {
        ops.push_back(Op{caps, r.x1, r.y1, r.x2, r.y2, r.alpha, r.thickness_px});
    }

    void reset(ptd::TrailStyle s) {
        style = s;
        batch.begin_frame();
        pending = ptd::TrailSegmentRecord{};
        ops.clear();
    }
};

// Runs one build for `style` through the harness and flushes, exactly as
// render_frame() would. `ages_ms` are sample ages before `now` (oldest
// first); all must be < lifetime so every emitted segment has alpha > 0.
std::vector<HarnessSink::Op> build(ptd::TrailStyle style,
                                   const std::vector<std::pair<int, int>>& pts,
                                   const std::vector<int64_t>& ages_ms,
                                   float smoothing = 0.0f,
                                   float lifetime_ms = 350.0f,
                                   float spacing_px = 12.0f,
                                   bool enabled = true,
                                   bool reference_tessellation = true) {
    ptd::TrailConfig cfg;
    cfg.style = style;
    cfg.smoothing = smoothing;
    cfg.lifetime_ms = lifetime_ms;
    cfg.segment_spacing_px = spacing_px;
    cfg.enabled = enabled;

    ptd::CursorHistory history{512};
    const int64_t now = 10'000 * kMs;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        history.push(move(now - ages_ms[i] * kMs, pts[i].first, pts[i].second));
    }

    HarnessSink sink;
    sink.reset(style);
    ptd::TrailEffect effect(cfg);
    // PERF-003: this suite asserts the FIXED-12 reference geometry/cap
    // contract, so it drives the retained reference oracle rather than the
    // adaptive production path (adaptive equivalence is proven separately).
    effect.set_reference_tessellation_for_tests(reference_tessellation);
    effect.build_geometry(history, now, nullptr, 512, sink);
    sink.flush();
    return sink.ops;
}

// The continuous-style invariant: every op is FlatFlat except the last,
// which is FlatRound for a multi-segment frame (and RoundRound only when
// exactly one segment exists).
void expect_continuous_ops(const std::vector<HarnessSink::Op>& ops,
                           const char* what) {
    ++g_checks;
    if (ops.empty()) {
        ++g_failures;
        std::printf("FAIL %s: no ops\n", what);
        return;
    }
    if (ops.size() == 1) {
        expect_policy(ops[0].caps, ptd::TrailCapPolicy::RoundRound,
                      "single continuous segment is round-round");
        return;
    }
    for (std::size_t i = 0; i + 1 < ops.size(); ++i) {
        expect_policy(ops[i].caps, ptd::TrailCapPolicy::FlatFlat,
                      "internal continuous segment is flat-flat");
    }
    expect_policy(ops.back().caps, ptd::TrailCapPolicy::FlatRound,
                  "final continuous segment is flat-round");
}

} // namespace

int main() {
    using ptd::TrailCapPolicy;
    using ptd::TrailSegmentRole;
    using ptd::TrailStyle;

    // ---- Phase 8: cap policy matrix (pure classification) ----
    const TrailStyle continuous[] = {TrailStyle::Classic, TrailStyle::SoftGlow,
                                     TrailStyle::Comet,  TrailStyle::Neon,
                                     TrailStyle::Pulse,  TrailStyle::Ribbon};
    for (TrailStyle s : continuous) {
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Internal),
                      TrailCapPolicy::FlatFlat, "continuous internal flat-flat");
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Final),
                      TrailCapPolicy::FlatRound, "continuous final flat-round");
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Only),
                      TrailCapPolicy::RoundRound, "continuous only round-round");
        expect_true(!ptd::trail_style_is_discrete_dots(s),
                    "continuous style classified continuous");
    }
    const TrailStyle dots[] = {TrailStyle::Dotted, TrailStyle::Spark};
    for (TrailStyle s : dots) {
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Internal),
                      TrailCapPolicy::RoundRound, "dot stub internal round-round");
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Final),
                      TrailCapPolicy::RoundRound, "dot stub final round-round");
        expect_policy(ptd::trail_cap_policy(s, TrailSegmentRole::Only),
                      TrailCapPolicy::RoundRound, "dot stub only round-round");
        expect_true(ptd::trail_style_is_discrete_dots(s),
                    "dot style classified discrete");
    }
    expect_policy(ptd::trail_cap_policy(TrailStyle::Classic, TrailSegmentRole::None),
                  TrailCapPolicy::RoundRound, "no segment no stroke");

    // SoftGlow/Neon outer pass shares the ONE policy authority with the
    // core (Phase 3): the renderer resolves caps per segment via
    // trail_cap_policy() and applies the same result to every pass, so
    // the matrix above IS the outer-pass contract.

    // ---- Phase 8: sequencer unit contract ----
    {
        ptd::TrailContinuousBatch b;
        expect_true(!b.pending() && b.continuous_seen() == 0,
                    "fresh batch has no pending segment");
        expect_true(b.flush_role() == TrailSegmentRole::Only,
                    "empty batch flush role is Only");
        expect_true(b.admit() == TrailSegmentRole::None,
                    "first admit displaces nothing");
        expect_true(b.pending() && b.continuous_seen() == 1,
                    "first segment pending");
        expect_true(b.flush_role() == TrailSegmentRole::Only,
                    "one segment flush role Only");
        expect_true(b.admit() == TrailSegmentRole::Internal,
                    "second admit displaces Internal");
        expect_true(b.flush_role() == TrailSegmentRole::Final,
                    "two segments flush role Final");
        b.mark_flushed();
        expect_true(!b.pending(), "flush clears pending");
        b.begin_frame();
        expect_true(!b.pending() && b.continuous_seen() == 0,
                    "begin_frame resets the batch");
        for (int i = 0; i < 10; ++i) {
            const TrailSegmentRole displaced = b.admit();
            expect_true(displaced == (i == 0 ? TrailSegmentRole::None
                                             : TrailSegmentRole::Internal),
                        "admit displacement role");
        }
    }

    // ---- Phase 8: Classic, 1 segment -> round-round ----
    {
        // smoothing 0: polyline, 2 samples -> exactly 1 segment.
        const auto ops = build(TrailStyle::Classic, {{0, 0}, {40, 0}}, {100, 50});
        expect_true(ops.size() == 1, "classic 1-segment frame emits one op");
        expect_continuous_ops(ops, "classic 1 segment");
    }

    // ---- Phase 8: Classic, 2+ segments -> flat-flat internals + flat-round head ----
    {
        const auto ops = build(TrailStyle::Classic, {{0, 0}, {40, 0}, {40, 40}},
                               {300, 200, 100});
        expect_true(ops.size() == 2, "classic polyline 3 points emits two ops");
        expect_continuous_ops(ops, "classic 2 segments");

        // Curve mode: 3 points x 12 subdivisions = 24 segments.
        const auto curve = build(TrailStyle::Classic, {{0, 0}, {40, 0}, {40, 40}},
                                 {300, 200, 100}, 0.75f);
        expect_true(curve.size() == 24, "curve frame emits 24 subdivisions");
        expect_continuous_ops(curve, "classic curve multi-segment");
        for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
            expect_true(curve[i].caps != TrailCapPolicy::RoundRound,
                        "no internal continuous segment uses round-round");
        }
    }

    // ---- Phase 8: Soft Glow / Neon (core + outer share the policy) ----
    for (TrailStyle s : {TrailStyle::SoftGlow, TrailStyle::Neon}) {
        const auto ops = build(s, {{0, 0}, {40, 0}, {40, 40}}, {300, 200, 100});
        expect_true(ops.size() == 2, "glow style emits two ops");
        expect_continuous_ops(ops, "glow style cap matrix");
        const auto curve = build(s, {{0, 0}, {40, 0}, {40, 40}},
                                 {300, 200, 100}, 0.75f);
        expect_true(curve.size() == 24, "glow curve emits 24 subdivisions");
        expect_continuous_ops(curve, "glow curve cap matrix");
    }

    // ---- Phase 8: Comet / Pulse / Ribbon follow the continuous policy ----
    for (TrailStyle s : {TrailStyle::Comet, TrailStyle::Pulse, TrailStyle::Ribbon}) {
        const auto ops = build(s, {{0, 0}, {40, 0}, {40, 40}}, {300, 200, 100});
        expect_true(ops.size() == 2, "style emits two ops");
        expect_continuous_ops(ops, "style continuous policy");
    }

    // ---- Phase 8: Dotted / Spark keep round-round stubs ----
    for (TrailStyle s : {TrailStyle::Dotted, TrailStyle::Spark}) {
        // 200 px chord, 12 px spacing -> many dots along one segment.
        const auto ops = build(s, {{0, 0}, {200, 0}}, {100, 50},
                               0.0f, 350.0f, 12.0f);
        expect_true(ops.size() >= 2, "dot style emits several stubs");
        for (std::size_t i = 0; i < ops.size(); ++i) {
            expect_policy(ops[i].caps, TrailCapPolicy::RoundRound,
                          "dot stub is round-round");
            const float dx = ops[i].x2 - ops[i].x1;
            const float dy = ops[i].y2 - ops[i].y1;
            expect_true(std::sqrt(dx * dx + dy * dy) < 4.0f,
                        "dot stub stays a short stub");
        }
    }

    // ---- Phase 8: empty / disabled trail -> no pending, no flush draw ----
    {
        const auto disabled = build(TrailStyle::Classic, {{0, 0}, {40, 0}},
                                    {100, 50}, 0.0f, 350.0f, 12.0f, false);
        expect_true(disabled.empty(), "disabled trail draws nothing");

        // No samples inside the lifetime window: nothing visible.
        const auto empty = build(TrailStyle::Classic, {{0, 0}, {40, 0}},
                                 {10'000, 9'950});
        expect_true(empty.empty(), "empty trail draws nothing");

        // Disabled trail: no pending segment either (reserve_hint(0) ran).
        ptd::TrailConfig off;
        off.enabled = false;
        ptd::CursorHistory history{512};
        history.push(move(10'000 * kMs - 100 * kMs, 0, 0));
        HarnessSink sink;
        sink.reset(TrailStyle::Classic);
        ptd::TrailEffect effect(off);
        effect.build_geometry(history, 10'000 * kMs, nullptr, 512, sink);
        expect_true(!sink.batch.pending(), "disabled trail leaves nothing pending");
        sink.flush();
        expect_true(sink.ops.empty(), "disabled trail flush draws nothing");
    }

    // ---- Phase 6/7: fresh-default visual profile ----
    {
        const ptd::TrailConfig d{};
        expect_true(d.style == TrailStyle::Classic, "default style Classic");
        expect_true(d.smoothing == 0.75f, "default smoothing 0.75");
        expect_true(d.fade_curve == ptd::FadeCurve::Smooth,
                    "default fade curve Smooth");
        expect_true(d.head_thickness_px == 3.0f && d.tail_thickness_px == 3.0f,
                    "default thickness 3 px head and tail");
        expect_true(d.taper_strength == 0.0f, "default taper 0");
        expect_true(d.lifetime_ms == 350.0f, "default lifetime 350 ms");
        expect_true(d.base_opacity == 0.9f, "default base opacity 0.9");
        expect_true(d.fade_start == 0.0f, "default fade start 0");
        expect_true(d.color_mode == ptd::TrailColorMode::Full,
                    "default color mode Full");
        expect_true(d.start_color_r == 255 && d.start_color_g == 255
                        && d.start_color_b == 0,
                    "default Start color yellow");
        expect_true(d.fade_color_r == 255 && d.fade_color_g == 255
                        && d.fade_color_b == 0,
                    "default Fade color yellow");
        expect_true(ptd::TrailConfig::validated(d) == d,
                    "defaults stable under validation");
        expect_true(d.glow_strength == 0.5f, "default glow unchanged");
    }

    std::printf("trail stroke policy: %d checks, %d failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
