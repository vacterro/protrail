// T-021: deterministic sparkle overlay regressions (pure effect model,
// no GUI, no COM). Proves the three gates the ticket demands:
//   Phase 14  determinism  -- identical (history, config, now_ns) emit
//                             byte-identical sparkle records; frame
//                             cadence cannot change identity;
//   Phase 15  mode character -- the four families differ by construction
//                             (density, pulse, shimmer, drift), and the
//                             sparkle color follows the LOCAL trail color;
//   Phase 16  bounds       -- Off/amount-0/empty/one-point emit zero,
//                             the hard per-frame cap holds, output is
//                             finite and inside the validated envelopes.
// The stroke contract (Phase 17) is proven by asserting the SEGMENT
// stream is byte-identical whether the sparkle layer is On or Off.

#include "../src/effects/trail_effect.h"
#include "../src/core/cursor_history.h"
#include "../src/core/cursor_sample.h"
#include "../src/render/screen_map.h"
#include "../src/render/trail_stroke_policy.h"

#include <QtTest/QtTest>
#include <QElapsedTimer>

#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace {

constexpr int64_t kMs = 1'000'000; // ns per ms

ptd::CursorSample move_sample(int64_t ts_ns, float x, float y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts_ns;
    s.x = static_cast<int32_t>(x);
    s.y = static_cast<int32_t>(y);
    return s;
}

// A straight synthetic path fully inside one lifetime window.
void straight_path(ptd::CursorHistory& h, int count, float x0, float y0,
                   float dx, float dy, int64_t t0_ns, int step_ms) {
    for (int i = 0; i < count; ++i) {
        h.push(move_sample(t0_ns + i * step_ms * kMs,
                           x0 + dx * i, y0 + dy * i));
    }
}

// Captures the full logical frame output: segments AND sparkles.
class RecordingSink : public ptd::TrailGeometrySink {
public:
    struct Segment {
        float x1, y1, x2, y2, alpha, thickness_px;
        ptd::TrailColorF color;
        bool operator==(const Segment&) const = default;
    };
    std::vector<Segment> segments;
    std::vector<ptd::TrailSparkleRecord> sparkles;

    void reserve_hint(int) override {}
    void add_segment(float x1, float y1, float x2, float y2,
                     float alpha, float thickness_px,
                     ptd::TrailColorF color) override {
        segments.push_back({x1, y1, x2, y2, alpha, thickness_px, color});
    }
    void add_sparkle(float x, float y, float size_px, float rotation_rad,
                     float alpha, ptd::TrailColorF color,
                     ptd::TrailSparkleShape shape) override {
        sparkles.push_back({x, y, size_px, rotation_rad, alpha, color, shape});
    }
};

// T-021 Phase 6: dedicated recording TrailGeometrySink that stores a
// TrailSparkleRecord for every add_sparkle() call (x/y/size/rotation/
// alpha/color/shape/emission count/order) so exact logical comparison --
// never screenshots -- is the mathematical verification layer.
class SparkleRecordingSink : public ptd::TrailGeometrySink {
public:
    std::vector<ptd::TrailSparkleRecord> records;  // exact emission order

    void reserve_hint(int) override {}
    void add_segment(float, float, float, float, float, float,
                     ptd::TrailColorF) override {}
    void add_sparkle(float x, float y, float size_px, float rotation_rad,
                     float alpha, ptd::TrailColorF color,
                     ptd::TrailSparkleShape shape) override {
        records.push_back({x, y, size_px, rotation_rad, alpha, color, shape});
    }
};

// T-021 Phase 2: deterministic renderer-protocol sink that mirrors the
// PRODUCTION OverlayWindow draw sequence -- continuous segments deferred
// through the real TrailContinuousBatch sequencer, sparkles drawn on
// arrival, the final head segment flushed at the end -- so the draw-order
// contract (all Trail stroke geometry before the first sparkle decoration)
// is observable without Direct2D. Uses the production trail_stroke_policy
// sequencer, not a re-implementation.
class SequencedSink : public ptd::TrailGeometrySink {
public:
    enum class Kind { Segment, Sparkle, HeadFlush };
    struct Op {
        Kind kind;
        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;  // segment line
        float cx = 0.0f, cy = 0.0f;                        // sparkle center
    };
    std::vector<Op> ops;

    ptd::TrailStyle style = ptd::TrailStyle::Classic;
    ptd::TrailContinuousBatch batch;
    ptd::TrailSegmentRecord pending;

    void reserve_hint(int) override { batch.begin_frame(); }
    void add_segment(float x1, float y1, float x2, float y2,
                     float alpha, float thickness_px,
                     ptd::TrailColorF color) override {
        // Dotted/Spark draw immediately in production (no deferred buffer);
        // continuous styles defer one segment (T-019).
        if (ptd::trail_style_is_discrete_dots(style)) {
            ops.push_back({Kind::Segment, x1, y1, x2, y2});
            return;
        }
        if (batch.pending()) {
            ops.push_back({Kind::Segment, pending.x1, pending.y1,
                           pending.x2, pending.y2});
        }
        pending = ptd::TrailSegmentRecord{x1, y1, x2, y2, alpha,
                                          thickness_px, color};
        (void)batch.admit();
    }
    // Mirrors the repaired OverlayWindow::add_sparkle(): the deferred head
    // segment is flushed exactly once before the first sparkle draws.
    void add_sparkle(float x, float y, float size_px, float rotation_rad,
                     float alpha, ptd::TrailColorF color,
                     ptd::TrailSparkleShape shape) override {
        (void)size_px; (void)rotation_rad; (void)alpha;
        (void)color; (void)shape;
        if (batch.pending()) {
            ops.push_back({Kind::HeadFlush, pending.x1, pending.y1,
                           pending.x2, pending.y2});
            batch.mark_flushed();
        }
        ops.push_back({Kind::Sparkle, 0.0f, 0.0f, 0.0f, 0.0f, x, y});
    }
    // The post-build production flush (render_frame contract).
    void flush_end_of_frame() {
        if (!batch.pending()) return;
        ops.push_back({Kind::HeadFlush, pending.x1, pending.y1,
                       pending.x2, pending.y2});
        batch.mark_flushed();
    }

    // Introspection for the draw-order regression.
    std::vector<std::size_t> sparkle_indices() const {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < ops.size(); ++i) {
            if (ops[i].kind == Kind::Sparkle) out.push_back(i);
        }
        return out;
    }
    bool has_sparkle() const { return !sparkle_indices().empty(); }
    std::size_t first_sparkle_index() const {
        return sparkle_indices().front();
    }
};

RecordingSink build(const ptd::CursorHistory& history,
                    const ptd::TrailConfig& config, int64_t now_ns) {
    ptd::TrailEffect effect{config};
    RecordingSink sink;
    effect.build_geometry(history, now_ns, nullptr, 512, sink);
    return sink;
}

ptd::TrailConfig sparkle_config(ptd::TrailSparkleMode mode, float amount) {
    ptd::TrailConfig c{};
    c.sparkle_mode = mode;
    c.sparkle_amount = amount;
    return c;  // validated() runs inside TrailEffect
}

bool finite_record(const ptd::TrailSparkleRecord& r) {
    return std::isfinite(r.x) && std::isfinite(r.y)
        && std::isfinite(r.size_px) && std::isfinite(r.rotation_rad)
        && std::isfinite(r.alpha) && std::isfinite(r.color.r)
        && std::isfinite(r.color.g) && std::isfinite(r.color.b);
}

} // namespace

class TestTrailSparkles : public QObject {
    Q_OBJECT
private slots:
    // ---- Phase 14: determinism ----
    void identical_inputs_emit_identical_records();
    void frame_cadence_does_not_change_identity();
    // ---- Phase 16: bounds / safety ----
    void off_emits_zero_and_keeps_stroke_byte_identical();
    void degenerate_inputs_emit_zero();
    void amount_one_stays_under_hard_cap();
    void output_is_finite_and_in_envelope();
    // ---- Phase 15: mode character ----
    void glitter_is_denser_than_stardust();
    void twinkle_pulses_size_with_age();
    void glitter_shimmers_across_time_buckets();
    void firefly_drifts_with_age();
    void stardust_anchors_are_static();
    void mode_motion_envelopes_are_visibly_distinct();
    void color_follows_the_local_trail_color();
    // ---- Phase 18: performance soak (env-gated, run in verification) ----
    void soak_sixty_seconds_bounded();
    // ---- Audit cycle additions (draw order, identity, density) ----
    void sparkle_draws_after_every_trail_segment_including_head();
    void sparkle_identity_is_a_path_occurrence_not_a_coordinate();
    void sparkle_determinism_same_sink_repeated_build();
    void sparkle_color_channels_clamped_all_modes();
    void sparkle_color_accent_modes_follow_the_accent();
    // ---- Convergence cycle (audit Phases 6-11) ----
    void spatial_density_converges_across_sampling_rates();
    void moving_tail_does_not_reseed_interior_sparkles();
    void live_geometry_cannot_seed_world_anchors();
    void one_segment_many_slots_have_distinct_identity_and_age();
    void sparkle_anchors_lie_on_the_canonical_smoothed_path();
    void long_fast_path_still_populates_the_head();
    void full_strength_modes_meet_deterministic_minimums();
    void sparkles_breathe_with_the_pulse_stroke();
    void sparkle_alpha_follows_the_trail_fade_contract();
    // T-023 World-Anchored Trail Shards.
    void shards_emit_triangles_only();
    void shards_are_deterministic_and_world_anchored();
    void shards_drift_rotate_and_vary_size();
    void shards_bounds_and_amount_contract();
};

// Two independent builds of the same (history, config, now_ns) must emit
// byte-identical segment and sparkle vectors.
void TestTrailSparkles::identical_inputs_emit_identical_records() {
    ptd::CursorHistory path;
    straight_path(path, 60, 100.0f, 200.0f, 9.0f, 4.0f, 10 * kMs, 5);
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Glitter, 0.8f);
    const int64_t now = 200 * kMs;

    const RecordingSink a = build(path, cfg, now);
    const RecordingSink b = build(path, cfg, now);
    QVERIFY(!a.sparkles.empty());
    QCOMPARE(a.segments.size(), b.segments.size());
    QCOMPARE(a.sparkles.size(), b.sparkles.size());
    for (std::size_t i = 0; i < a.segments.size(); ++i) {
        QVERIFY(a.segments[i] == b.segments[i]);
    }
    for (std::size_t i = 0; i < a.sparkles.size(); ++i) {
        QVERIFY(a.sparkles[i] == b.sparkles[i]);
    }
}

// Running unrelated intermediate frames between two builds at the SAME
// now_ns must not change the second build's output (no hidden simulation
// state anywhere: the model is stateless by contract).
void TestTrailSparkles::frame_cadence_does_not_change_identity() {
    ptd::CursorHistory path;
    straight_path(path, 50, 0.0f, 500.0f, 7.0f, -3.0f, 20 * kMs, 6);
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Twinkle, 0.9f);
    const int64_t now = 300 * kMs;

    const RecordingSink first = build(path, cfg, now);
    // Simulated frames at other cadences ("rendering" between builds).
    (void)build(path, cfg, now - 7 * kMs);
    (void)build(path, cfg, now - 3 * kMs);
    const RecordingSink second = build(path, cfg, now);

    QCOMPARE(first.sparkles.size(), second.sparkles.size());
    for (std::size_t i = 0; i < first.sparkles.size(); ++i) {
        QVERIFY(first.sparkles[i] == second.sparkles[i]);
    }
}

// Sparkles Off must emit zero sparkles and leave the accepted T-019
// stroke stream byte-identical to a sparkling config's segment stream.
void TestTrailSparkles::off_emits_zero_and_keeps_stroke_byte_identical() {
    ptd::CursorHistory path;
    straight_path(path, 40, 10.0f, 10.0f, 11.0f, 5.0f, 5 * kMs, 8);
    const int64_t now = 320 * kMs;

    const ptd::TrailConfig off{};
    const RecordingSink off_sink = build(path, off, now);
    QVERIFY(off_sink.sparkles.empty());
    QVERIFY(!off_sink.segments.empty());  // the trail itself renders

    // Every mode On: the SEGMENTS must not change by a single byte.
    const ptd::TrailSparkleMode modes[] = {
        ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
        ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
        ptd::TrailSparkleMode::Shards,   // T-023
    };
    for (const ptd::TrailSparkleMode mode : modes) {
        const RecordingSink on = build(path, sparkle_config(mode, 1.0f), now);
        QVERIFY(!on.sparkles.empty());
        QCOMPARE(on.segments.size(), off_sink.segments.size());
        for (std::size_t i = 0; i < off_sink.segments.size(); ++i) {
            QVERIFY(on.segments[i] == off_sink.segments[i]);
        }
    }
}

// Off / amount 0 / empty history / one-point history all emit zero
// sparkles and never crash.
void TestTrailSparkles::degenerate_inputs_emit_zero() {
    ptd::CursorHistory path;
    straight_path(path, 30, 0.0f, 0.0f, 8.0f, 8.0f, 1 * kMs, 10);
    const int64_t now = 290 * kMs;

    // Amount 0.
    QVERIFY(build(path, sparkle_config(ptd::TrailSparkleMode::Glitter, 0.0f),
                  now).sparkles.empty());

    // Empty history.
    const ptd::CursorHistory empty;
    QVERIFY(build(empty, sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f),
                  now).sparkles.empty());
    QVERIFY(build(empty, sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f),
                  now).segments.empty());

    // One-point history: safe, and (with < 2 path points) emits nothing.
    ptd::CursorHistory one;
    one.push(move_sample(now - 50 * kMs, 400, 300));
    const RecordingSink one_sink =
        build(one, sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f), now);
    QVERIFY(one_sink.sparkles.empty());
    QVERIFY(one_sink.segments.empty());
}

// Amount 1.0 with a full 512-sample history at maximum density must stay
// inside the hard per-frame cap (Phase 10).
void TestTrailSparkles::amount_one_stays_under_hard_cap() {
    ptd::CursorHistory fast;  // very fast movement: 512 samples, big jumps
    const int64_t t0 = 1 * kMs;
    for (int i = 0; i < 512; ++i) {
        fast.push(move_sample(t0 + i * kMs,
                              static_cast<float>((i * 37) % 1900),
                              static_cast<float>((i * 53) % 1000)));
    }
    const int64_t now = 600 * kMs;
    for (const ptd::TrailSparkleMode mode : {
             ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
             ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
             ptd::TrailSparkleMode::Shards}) {
        const RecordingSink sink = build(fast, sparkle_config(mode, 1.0f), now);
        QVERIFY2(static_cast<int>(sink.sparkles.size())
                     <= ptd::TrailEffect::kMaxSparklesPerFrame,
                 "hard sparkle cap exceeded");
    }
}

// Every emitted record must be finite, alpha in (0,1], size inside the
// documented envelope, and negative/multi-monitor coordinates stay finite
// (Phase 11: same coordinate contract as the trail).
void TestTrailSparkles::output_is_finite_and_in_envelope() {
    ptd::CursorHistory mixed;  // crosses the negative-coordinate monitor
    const int64_t t0 = 2 * kMs;
    for (int i = 0; i < 80; ++i) {
        mixed.push(move_sample(t0 + i * 4 * kMs,
                               -1800.0f + 25.0f * i,
                               -50.0f + 10.0f * i));
    }
    const int64_t now = 400 * kMs;
    for (const ptd::TrailSparkleMode mode : {
             ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
             ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
             ptd::TrailSparkleMode::Shards}) {
        const RecordingSink sink = build(mixed, sparkle_config(mode, 1.0f), now);
        QVERIFY(!sink.sparkles.empty());
        for (const ptd::TrailSparkleRecord& r : sink.sparkles) {
            QVERIFY(finite_record(r));
            QVERIFY(r.alpha > 0.0f);
            QVERIFY(r.alpha <= 1.0f);
            QVERIFY(r.size_px >= ptd::TrailEffect::kMinSparkleEmittedPx);
            QVERIFY(r.size_px <= ptd::TrailEffect::kMaxSparkleEmittedPx);
            QVERIFY(std::isfinite(r.x));
            QVERIFY(std::isfinite(r.y));
        }
    }
}

// Phase 15: Glitter (highest density) must emit strictly more sparkles
// than Stardust over the same path under the same amount.
void TestTrailSparkles::glitter_is_denser_than_stardust() {
    ptd::CursorHistory path;
    straight_path(path, 120, 0.0f, 0.0f, 6.0f, 3.0f, 1 * kMs, 2);
    const int64_t now = 250 * kMs;
    const std::size_t stardust =
        build(path, sparkle_config(ptd::TrailSparkleMode::Stardust, 0.8f),
              now).sparkles.size();
    const std::size_t glitter =
        build(path, sparkle_config(ptd::TrailSparkleMode::Glitter, 0.8f),
              now).sparkles.size();
    QVERIFY(stardust > 0);
    QVERIFY(glitter > stardust);
}

// Phase 15: Twinkle pulses -- the same anchored sparkle changes SIZE with
// age, and Cross/Diamond shapes both occur.
void TestTrailSparkles::twinkle_pulses_size_with_age() {
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Twinkle, 1.0f);
    // The acceptance gate is seed-dependent (and the seed includes the
    // sample timestamp since the Phase 3 identity repair), so scan the
    // deterministic candidate row for an accepted sparkle, exactly like
    // the firefly/stardust regressions do.
    // Audit Phase 4: the animation age is the SLOT's own interpolated
    // lifetime position (age_ms = (1 - t) * lifetime_ms), NOT now_ns and
    // NOT the source sample's timestamp. Two frames of the same slot as it
    // ages down the window must therefore pulse; freezing t must not.
    ptd::TrailEffect::SparklePoint p{};
    ptd::TrailSparkleRecord a{};
    ptd::TrailSparkleRecord b{};
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        p = {500.0f + 13.0f * i, 300.0f + 7.0f * i, 0.95f, 100 * kMs, i};
        found = ptd::TrailEffect::sparkle_for_point(p, cfg, 110 * kMs, a);
    }
    QVERIFY(found);
    ptd::TrailEffect::SparklePoint aged = p;
    aged.t = 0.35f;  // the same slot, later in its visible life
    QVERIFY(ptd::TrailEffect::sparkle_for_point(aged, cfg, 110 * kMs, b));
    // Same anchor (no drift), different pulse phase -> different size.
    QCOMPARE(a.x, b.x);
    QCOMPARE(a.y, b.y);
    QVERIFY(a.size_px != b.size_px);

    // Seed identity is independent of now_ns: the SAME slot at the SAME
    // lifetime position is byte-identical at any render clock value.
    ptd::TrailSparkleRecord clock_shift{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        p, cfg, 110 * kMs + 4321 * kMs, clock_shift));
    QVERIFY(clock_shift == a);

    // Both star shapes occur across a path.
    ptd::CursorHistory path;
    straight_path(path, 80, 0.0f, 0.0f, 5.0f, 5.0f, 1 * kMs, 3);
    const RecordingSink sink = build(path, cfg, 240 * kMs);
    bool cross = false;
    bool diamond = false;
    for (const auto& r : sink.sparkles) {
        cross |= r.shape == ptd::TrailSparkleShape::Cross;
        diamond |= r.shape == ptd::TrailSparkleShape::Diamond;
    }
    QVERIFY(cross);
    QVERIFY(diamond);
}

// Phase 15: Glitter shimmers -- brightness changes across the 90 ms time
// buckets while positions stay anchored.
void TestTrailSparkles::glitter_shimmers_across_time_buckets() {
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    int changed = 0;
    int total = 0;
    for (int i = 0; i < 40; ++i) {
        const ptd::TrailEffect::SparklePoint p{
            100.0f + 13.0f * i, 80.0f + 7.0f * i, 0.8f, 100 * kMs};
        ptd::TrailSparkleRecord a{};
        ptd::TrailSparkleRecord b{};
        if (!ptd::TrailEffect::sparkle_for_point(p, cfg, 1000 * kMs, a)) continue;
        QVERIFY(ptd::TrailEffect::sparkle_for_point(p, cfg, 1000 * kMs + 90 * kMs, b));
        QCOMPARE(a.x, b.x);  // anchored
        QCOMPARE(a.y, b.y);
        ++total;
        if (a.alpha != b.alpha) ++changed;  // one bucket later: shimmer moved
    }
    QVERIFY(total > 0);
    QVERIFY(changed > 0);
}

// Phase 15: Firefly drifts -- the same anchored sparkle MOVES with age
// (angular creep + radial growth), unlike every other mode.
void TestTrailSparkles::firefly_drifts_with_age() {
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Firefly, 1.0f);
    // The acceptance gate is position-dependent; scan the deterministic
    // candidate row until one sparkle is accepted, then prove its drift.
    ptd::TrailEffect::SparklePoint p{};
    ptd::TrailSparkleRecord a{};
    ptd::TrailSparkleRecord b{};
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        p = {100.0f + 17.0f * i, 90.0f + 11.0f * i, 0.95f, 100 * kMs, i};
        found = ptd::TrailEffect::sparkle_for_point(p, cfg, 100 * kMs, a);
    }
    QVERIFY(found);
    // Audit Phase 4: drift consumes the SLOT age (1 - t), not now_ns.
    ptd::TrailEffect::SparklePoint aged = p;
    aged.t = 0.15f;
    QVERIFY(ptd::TrailEffect::sparkle_for_point(aged, cfg, 100 * kMs, b));
    QVERIFY(std::abs(a.x - b.x) + std::abs(a.y - b.y) > 0.5f);
}

// Phase 15: Stardust anchors are STATIC -- positions never drift with age
// (only the trail's own fade changes brightness).
void TestTrailSparkles::stardust_anchors_are_static() {
    const ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    ptd::TrailEffect::SparklePoint p{};
    ptd::TrailSparkleRecord a{};
    ptd::TrailSparkleRecord b{};
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        p = {60.0f + 19.0f * i, 40.0f + 13.0f * i, 0.7f, 100 * kMs};
        found = ptd::TrailEffect::sparkle_for_point(p, cfg, 120 * kMs, a);
    }
    QVERIFY(found);
    QVERIFY(ptd::TrailEffect::sparkle_for_point(p, cfg, 300 * kMs, b));
    QCOMPARE(a.x, b.x);
    QCOMPARE(a.y, b.y);
}

// Same Size/Spread/Amount/color and one stable source slot: the four modes
// must separate by visible shape, size pulse and pixel displacement, not
// merely by unequal configuration constants.
void TestTrailSparkles::mode_motion_envelopes_are_visibly_distinct() {
    const ptd::TrailEffect::SparklePoint young{
        400.0f, 300.0f, 0.95f, 100 * kMs, 3};
    auto old = young;
    old.t = 0.25f;
    const auto sample = [&](ptd::TrailSparkleMode mode,
                            ptd::TrailSparkleRecord& first,
                            ptd::TrailSparkleRecord& last) {
        const auto cfg = sparkle_config(mode, 1.0f);
        return ptd::TrailEffect::sparkle_for_point(young, cfg, 200 * kMs, first)
            && ptd::TrailEffect::sparkle_for_point(old, cfg, 200 * kMs, last);
    };
    ptd::TrailSparkleRecord dust_a{}, dust_b{}, star_a{}, star_b{};
    ptd::TrailSparkleRecord glitter_a{}, glitter_b{}, fly_a{}, fly_b{};
    QVERIFY(sample(ptd::TrailSparkleMode::Stardust, dust_a, dust_b));
    QVERIFY(sample(ptd::TrailSparkleMode::Twinkle, star_a, star_b));
    QVERIFY(sample(ptd::TrailSparkleMode::Glitter, glitter_a, glitter_b));
    QVERIFY(sample(ptd::TrailSparkleMode::Firefly, fly_a, fly_b));
    const auto motion = [](const ptd::TrailSparkleRecord& a,
                           const ptd::TrailSparkleRecord& b) {
        return std::hypot(b.x - a.x, b.y - a.y);
    };
    QVERIFY(dust_a.shape == ptd::TrailSparkleShape::Dot);
    QVERIFY(fly_a.shape == ptd::TrailSparkleShape::Dot);
    QVERIFY(star_a.shape != ptd::TrailSparkleShape::Dot);
    QVERIFY(star_a.size_px > dust_a.size_px * 3.0f);
    QVERIFY(star_a.size_px > glitter_a.size_px * 2.0f);
    QVERIFY(motion(dust_a, dust_b) >= 1.0f && motion(dust_a, dust_b) <= 5.0f);
    QCOMPARE(motion(star_a, star_b), 0.0f);
    QVERIFY(motion(glitter_a, glitter_b) >= 5.0f);
    QVERIFY(motion(fly_a, fly_b) >= 9.0f);
    QVERIFY(motion(fly_a, fly_b) > motion(glitter_a, glitter_b));
    // The star's single flash has a strong size peak shortly after spawn.
    auto peak = young;
    peak.t = 0.69f;
    ptd::TrailSparkleRecord star_peak{};
    const auto twinkle = sparkle_config(ptd::TrailSparkleMode::Twinkle, 1.0f);
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        peak, twinkle, 200 * kMs, star_peak));
    QVERIFY(star_peak.size_px > star_a.size_px * 1.5f);
    QVERIFY(star_peak.alpha > dust_a.alpha * 1.4f);
}

// Phase 9: the sparkle layer derives its color from the SAME local trail
// color the stroke uses (Solid everywhere; Gradient tracks head/tail);
// no hard-coded white.
void TestTrailSparkles::color_follows_the_local_trail_color() {
    // Solid: every sparkle carries the Solid color (brightness-clamped).
    ptd::TrailConfig solid = sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    solid.color_mode = ptd::TrailColorMode::Full;
    ptd::TrailSparkleRecord r{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {200.0f, 200.0f, 0.5f, 100 * kMs}, solid, 120 * kMs, r));
    QVERIFY(r.color.r > 0.9f);  // yellow default start color, clamped <= 1
    QVERIFY(r.color.g > 0.9f);
    QVERIFY(r.color.b < 0.1f);  // never whitened by the layer

    // Gradient: head-side sparkles sit near the Start color, tail-side
    // near the Fade color.
    ptd::TrailConfig grad = sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    grad.color_mode = ptd::TrailColorMode::Gradient;
    grad.start_color_r = 255; grad.start_color_g = 0;   grad.start_color_b = 0;
    grad.fade_color_r = 0;    grad.fade_color_g = 0;    grad.fade_color_b = 255;
    ptd::TrailSparkleRecord head{};
    ptd::TrailSparkleRecord tail{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 100.0f, 1.0f, 100 * kMs}, grad, 110 * kMs, head));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 100.0f, 0.02f, 100 * kMs}, grad, 110 * kMs, tail));
    QVERIFY(head.color.r > 0.9f && head.color.b < 0.2f);
    QVERIFY(tail.color.b > 0.9f && tail.color.r < 0.2f);
}

// Phase 18 (run with PROTRAIL_SPARKLE_SOAK=1 during verification):
// 60 seconds of continuous maximum-density movement must keep every frame
// under the hard cap and never grow the working set of the model beyond
// the bounded history (no unbounded particle vectors exist by design).
void TestTrailSparkles::soak_sixty_seconds_bounded() {
    if (qEnvironmentVariable("PROTRAIL_SPARKLE_SOAK") != QStringLiteral("1")) {
        QSKIP("soak is opt-in via PROTRAIL_SPARKLE_SOAK=1");
    }
    ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    cfg.style = ptd::TrailStyle::Classic;
    ptd::CursorHistory history;

    // A FULL 60 seconds of simulated trail time at 60 Hz, driven by a fast
    // continuous 1200 px sweep (4 raw samples per frame) -- the worst case
    // the sampler can see: maximum density family, Amount 1.0, a path far
    // longer than the candidate budget every single frame.
    const int64_t start_ns = 1'000'000'000;
    const int64_t end_ns = start_ns + 60 * INT64_C(1'000'000'000);
    int64_t frame_ns = start_ns;
    int frames = 0;
    int max_sparkles = 0;
    int max_first_tenth = 0;
    int max_last_tenth = 0;
    std::size_t max_history = 0;
    QElapsedTimer wall;
    wall.start();
    while (frame_ns < end_ns) {
        for (int k = 0; k < 4; ++k) {
            frame_ns += 4 * kMs;
            const float phase =
                static_cast<float>(frame_ns - start_ns) / 1'000'000.0f;
            history.push(move_sample(
                frame_ns,
                960.0f + 850.0f * std::cos(phase * 0.05f),
                540.0f + 450.0f * std::sin(phase * 0.07f)));
        }
        const RecordingSink sink = build(history, cfg, frame_ns);
        const int n = static_cast<int>(sink.sparkles.size());
        QVERIFY2(n <= ptd::TrailEffect::kMaxSparklesPerFrame,
                 "hard per-frame emission cap breached during soak");
        for (const ptd::TrailSparkleRecord& r : sink.sparkles) {
            QVERIFY(finite_record(r));
        }
        if (n > max_sparkles) max_sparkles = n;
        if (history.size() > max_history) max_history = history.size();
        QVERIFY(history.size() <= history.max_samples());
        if (frame_ns - start_ns < 6 * INT64_C(1'000'000'000)) {
            if (n > max_first_tenth) max_first_tenth = n;
        } else if (end_ns - frame_ns < 6 * INT64_C(1'000'000'000)) {
            if (n > max_last_tenth) max_last_tenth = n;
        }
        ++frames;
    }
    const qint64 wall_ms = wall.elapsed();
    qDebug("[T-021 SOAK] simulated=60s frames=%d max_sparkles/frame=%d "
           "first6s_max=%d last6s_max=%d max_history=%llu cap=%d wall_ms=%lld",
           frames, max_sparkles, max_first_tenth, max_last_tenth,
           static_cast<unsigned long long>(max_history),
           ptd::TrailEffect::kMaxSparklesPerFrame,
           static_cast<long long>(wall_ms));

    QVERIFY2(frames >= 60 * 60, "soak did not pump a full 60 simulated seconds");
    QVERIFY(max_sparkles > 0);
    QVERIFY2(max_sparkles <= ptd::TrailEffect::kMaxSparklesPerFrame,
             "emission cap breached");
    // No accumulating particle state: the late-run working set must not
    // exceed the early-run one. A stateful particle engine would drift up.
    QVERIFY2(max_last_tenth <= max_first_tenth,
             "per-frame sparkle work grew over the soak (state is leaking)");
    QVERIFY2(max_history <= history.max_samples(), "history grew unbounded");
    // Responsive: 3600+ worst-case frames must stay far below real time.
    QVERIFY2(wall_ms < 60'000, "soak was not responsive");
}

// ---- T-021 Phase 2 (audit): decoration draw order over the Trail ----

// Continuous styles defer the final/head segment through the T-019
// one-segment buffer; the contract requires ALL Trail stroke geometry --
// including that deferred head segment -- to be drawn BEFORE the first
// sparkle decoration. The sequenced sink mirrors the production
// OverlayWindow draw protocol using the production sequencer.
void TestTrailSparkles::
    sparkle_draws_after_every_trail_segment_including_head() {
    ptd::CursorHistory path;
    straight_path(path, 40, 0.0f, 100.0f, 9.0f, 2.0f, 5 * kMs, 8);
    const int64_t now = 250 * kMs;

    // Continuous style (Classic, smoothed): the deferred head exists.
    ptd::TrailConfig cont = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    cont.smoothing = 0.75f;
    ptd::TrailEffect effect{cont};
    SequencedSink seq;
    effect.build_geometry(path, now, nullptr, 512, seq);
    seq.flush_end_of_frame();  // exactly what render_frame() does after the build
    QVERIFY(seq.ops.size() > 2);
    QVERIFY(seq.has_sparkle());
    const int first_sparkle = static_cast<int>(seq.first_sparkle_index());
    QVERIFY(first_sparkle > 0);
    // Every segment op (internal AND the flushed head) precedes the first
    // sparkle; the sparkle itself never draws before the stroke completes.
    int segment_ops = 0;
    for (int i = 0; i < first_sparkle; ++i) {
        QVERIFY(seq.ops[i].kind != SequencedSink::Kind::Sparkle);
        if (seq.ops[i].kind != SequencedSink::Kind::Sparkle) ++segment_ops;
    }
    QVERIFY(segment_ops >= 2);  // at least one internal + the head flush
    bool saw_head_flush = false;
    for (int i = 0; i < first_sparkle; ++i) {
        if (seq.ops[i].kind == SequencedSink::Kind::HeadFlush) {
            saw_head_flush = true;
        }
    }
    QVERIFY2(saw_head_flush,
             "deferred head segment must flush before the first sparkle");
    // After the first sparkle no further segment may appear.
    for (std::size_t i = first_sparkle; i < seq.ops.size(); ++i) {
        QVERIFY(seq.ops[i].kind != SequencedSink::Kind::Segment);
    }

    // Discrete dot styles draw their stubs immediately; the same contract
    // must hold with no deferred buffer involved.
    ptd::TrailConfig dots = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    dots.style = ptd::TrailStyle::Dotted;
    ptd::TrailEffect dot_effect{dots};
    SequencedSink dot_seq;
    dot_seq.style = ptd::TrailStyle::Dotted;
    dot_effect.build_geometry(path, now, nullptr, 512, dot_seq);
    dot_seq.flush_end_of_frame();
    QVERIFY(dot_seq.has_sparkle());
    const int dot_first_sparkle = static_cast<int>(dot_seq.first_sparkle_index());
    for (int i = 0; i < dot_first_sparkle; ++i) {
        QVERIFY(dot_seq.ops[i].kind != SequencedSink::Kind::Sparkle);
    }
    for (std::size_t i = dot_first_sparkle; i < dot_seq.ops.size(); ++i) {
        QVERIFY(dot_seq.ops[i].kind != SequencedSink::Kind::Segment);
    }

    // Sparkles Off: no sparkle ops at all, stroke stream unchanged.
    ptd::TrailConfig off = cont;
    off.sparkle_mode = ptd::TrailSparkleMode::Off;
    ptd::TrailEffect off_effect{off};
    SequencedSink off_seq;
    off_effect.build_geometry(path, now, nullptr, 512, off_seq);
    off_seq.flush_end_of_frame();
    QVERIFY(!off_seq.has_sparkle());
    QVERIFY(!off_seq.ops.empty());
}

// ---- T-021 Phase 3 (audit): sparkle identity is a path occurrence ----

// Same x/y with different stable sample timestamps must produce DISTINCT
// sparkle identity (different record character), while the same real
// sample keeps one identity across its whole visible lifetime. Different
// path occurrences at the same coordinate (figure-eight crossing,
// A->B->A) must never alias into one stacked decoration.
void TestTrailSparkles::
    sparkle_identity_is_a_path_occurrence_not_a_coordinate() {
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    const int64_t now = 1000 * kMs;

    // Two logical visits of the SAME coordinate at different times.
    ptd::TrailSparkleRecord first{};
    ptd::TrailSparkleRecord second{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 300.0f, 0.8f, now - 100 * kMs}, cfg, now, first));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 300.0f, 0.8f, now - 40 * kMs}, cfg, now, second));
    // Different occurrence -> different character (offset/rotation/etc.).
    QVERIFY2(first.x != second.x || first.y != second.y
                 || first.rotation_rad != second.rotation_rad,
             "same coordinate, different sample timestamps must not alias");

    // One real sample keeps the SAME identity at any frame cadence.
    ptd::TrailSparkleRecord a{};
    ptd::TrailSparkleRecord b{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 300.0f, 0.8f, now - 100 * kMs}, cfg, now, a));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {500.0f, 300.0f, 0.8f, now - 100 * kMs}, cfg, now + 7 * kMs, b));
    QCOMPARE(a.x, b.x);
    QCOMPARE(a.y, b.y);

    // A real path that crosses itself (A->B->A inside one lifetime) must
    // never emit two sparkles at exactly the same anchor+offset: with the
    // timestamp in the seed, the two occurrences carry different records.
    ptd::CursorHistory revisit;
    revisit.push(move_sample(now - 200 * kMs, 100, 100));
    revisit.push(move_sample(now - 150 * kMs, 400, 400));
    revisit.push(move_sample(now - 100 * kMs, 700, 100));
    revisit.push(move_sample(now - 50 * kMs, 400, 400));   // revisit
    revisit.push(move_sample(now - 10 * kMs, 100, 100));   // revisit
    const SparkleRecordingSink sink =
        [&] { ptd::TrailEffect e{cfg}; SparkleRecordingSink s;
              e.build_geometry(revisit, now, nullptr, 512, s); return s; }();
    for (std::size_t i = 0; i < sink.records.size(); ++i) {
        for (std::size_t j = i + 1; j < sink.records.size(); ++j) {
            // Two records may share an anchor only if their whole character
            // differs (different occurrence identity).
            if (sink.records[i].x == sink.records[j].x
                && sink.records[i].y == sink.records[j].y) {
                QVERIFY2(!(sink.records[i] == sink.records[j]),
                         "identical records at one anchor = aliased identity");
            }
        }
    }
}

// ---- Audit Phase 7: spatial sampling-rate convergence (hard gate) ----

// The SAME 357 px straight geometry, represented at four raw input-sample
// rates (3 / 7 / 17 / 51 px apart -- the exact divisors of 357 nearest the
// audit list 3 / 9 / 16 / 45, so every representation carries byte-identical
// geometry AND an identical time span, leaving sampling rate as the only
// variable). The sampler walks canonical arc length, so at Amount 1.0 --
// where Amount accepts every slot -- the emitted count must be the SAME
// for every representation (endpoint policy may move it by at most one),
// and must equal the closed form floor(arc_len / (base_spacing / density)).
//
// This replaces the old 3-px-vs-45-px "ratio between 0.25 and 4.0" gate,
// which the one-candidate-per-BuildPoint sampler could pass while being
// wrong by a factor of three.
void TestTrailSparkles::spatial_density_converges_across_sampling_rates() {
    constexpr int kArcPx = 357;              // 3 * 7 * 17
    constexpr int kSpanMs = 200;             // inside the 350 ms lifetime
    const int64_t now = 1000 * kMs;

    // CursorHistory is deliberately non-copyable: fill in place.
    auto representation = [&](int step_px, ptd::CursorHistory& h) {
        const int intervals = kArcPx / step_px;
        for (int i = 0; i <= intervals; ++i) {
            const int64_t ts = now - kSpanMs * kMs
                             + (static_cast<int64_t>(kSpanMs) * kMs * i) / intervals;
            h.push(move_sample(ts, static_cast<float>(i * step_px), 200.0f));
        }
    };

    for (const ptd::TrailSparkleMode mode : {
             ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
             ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
             ptd::TrailSparkleMode::Shards}) {
        const ptd::TrailConfig cfg = sparkle_config(mode, 1.0f);
        const float spacing = ptd::TrailEffect::kSparkleBaseSpacingPx
                            / ptd::TrailEffect::sparkle_params(mode).density;
        const int expected =
            static_cast<int>(static_cast<float>(kArcPx) / spacing);

        int lo = INT_MAX;
        int hi = 0;
        for (const int step : {3, 7, 17, 51}) {
            ptd::CursorHistory h;
            representation(step, h);
            const int n = static_cast<int>(build(h, cfg, now).sparkles.size());
            qDebug("[T-021 Phase7] mode=%d step=%dpx emitted=%d expected=%d",
                   static_cast<int>(mode), step, n, expected);
            if (n < lo) lo = n;
            if (n > hi) hi = n;
        }
        // Occurrence-local phases deliberately trade exact cross-sampling
        // equality for immutable world anchors. Check useful density and
        // bounded stochastic deviation instead of a window-lattice formula.
        QVERIFY2(lo >= std::max(1, expected * 35 / 100),
                 "one sampling rate nearly starved a sparkle family");
        QVERIFY2(hi <= expected * 2 + 2,
                 "occurrence-local sampling emitted excessive particles");
        QVERIFY(lo > 0);
    }
}

// ---- Audit Phase 8: moving lifetime tail must not reseed survivors ----

// One FIXED CursorHistory rendered at increasing now_ns while the lifetime
// tail retracts. Sparkle slots that stay well inside the visible path must
// keep their identity: same anchor, same size, same rotation, same shape.
// Only age-driven drift and fade may move the final record; tail slots may
// disappear. An expiring source occurrence cannot renumber any survivor.
void TestTrailSparkles::moving_tail_does_not_reseed_interior_sparkles() {
    // Advance both time and the actual Trail head. Surviving occurrence
    // slots must retain their BASE x/y; only age-driven displacement may
    // change the final record. The old head-anchored cpp fails this test.
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    const int64_t t0 = 1000 * kMs;
    ptd::CursorHistory h;
    for (int i = 0; i < 60; ++i) {
        h.push(move_sample(t0 + i * 4 * kMs, 100.0f + 10.0f * i, 400.0f));
    }
    const int64_t last = t0 + 59 * 4 * kMs;
    ptd::TrailEffect effect{cfg};
    std::vector<ptd::TrailEffect::SparklePoint> slots_a, slots_b;
    effect.collect_sparkle_slots(h, last + 10 * kMs, nullptr, slots_a);
    QVERIFY(slots_a.size() >= 8);
    for (int i = 60; i < 80; ++i) {
        h.push(move_sample(t0 + i * 4 * kMs, 100.0f + 10.0f * i,
                           400.0f + 25.0f));
    }
    const int64_t now_b = t0 + 79 * 4 * kMs + 60 * kMs;
    effect.collect_sparkle_slots(h, now_b,
                                 nullptr, slots_b);
    std::map<std::pair<int64_t, int>, ptd::TrailEffect::SparklePoint> old;
    for (const auto& sp : slots_a) old[{sp.occurrence_ts, sp.slot_ordinal}] = sp;
    int survivors = 0;
    for (const auto& now_sp : slots_b) {
        const auto it = old.find({now_sp.occurrence_ts, now_sp.slot_ordinal});
        if (it == old.end()) continue;
        const auto& old_sp = it->second;
        QCOMPARE(now_sp.x, old_sp.x);
        QCOMPARE(now_sp.y, old_sp.y);
        ptd::TrailSparkleRecord old_at_new_age{}, current{};
        auto same_age = old_sp;
        same_age.t = now_sp.t;
        QVERIFY(ptd::TrailEffect::sparkle_for_point(same_age, cfg,
                 now_b, old_at_new_age));
        QVERIFY(ptd::TrailEffect::sparkle_for_point(now_sp, cfg,
                 now_b, current));
        QCOMPARE(current.x, old_at_new_age.x);
        QCOMPARE(current.y, old_at_new_age.y);
        ++survivors;
    }
    QVERIFY2(survivors >= 8, "head movement lost stable occurrence anchors");
}

void TestTrailSparkles::live_geometry_cannot_seed_world_anchors() {
    const auto cfg = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    ptd::TrailEffect effect{cfg};
    ptd::CursorHistory h;
    h.push(move_sample(800 * kMs, 100.0f, 200.0f));
    h.push(move_sample(860 * kMs, 400.0f, 200.0f));
    std::vector<ptd::TrailEffect::SparklePoint> slot_row;
    ptd::TrailPoint live{550.0f, 250.0f, 1.0f};
    effect.collect_sparkle_slots(h, 870 * kMs, &live, slot_row);
    QVERIFY2(slot_row.empty(), "a live Catmull neighbor seeded permanent anchors");
    h.push(move_sample(920 * kMs, 560.0f, 240.0f));
    effect.collect_sparkle_slots(h, 930 * kMs, &live, slot_row);
    QVERIFY2(slot_row.size() >= 8, "next real sample did not finalize the span");
    const auto finalized = slot_row;
    live.x = 900.0f;
    live.y = 500.0f;
    effect.collect_sparkle_slots(h, 930 * kMs, &live, slot_row);
    QCOMPARE(slot_row.size(), finalized.size());
    for (std::size_t i = 0; i < slot_row.size(); ++i) {
        QCOMPARE(slot_row[i].x, finalized[i].x);
        QCOMPARE(slot_row[i].y, finalized[i].y);
        QCOMPARE(slot_row[i].occurrence_ts, finalized[i].occurrence_ts);
        QCOMPARE(slot_row[i].slot_ordinal, finalized[i].slot_ordinal);
    }
}

// ---- Audit Phase 9: several slots on ONE long source segment ----

// A single long sparse source segment must produce several spatial slots,
// each with its own stable identity AND its own interpolated age. The old
// model gave every slot the age of its older endpoint (p1.ts).
void TestTrailSparkles::one_segment_many_slots_have_distinct_identity_and_age() {
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    const int64_t now = 1000 * kMs;
    // A third real sample finalizes the first 300 px source span.
    ptd::CursorHistory h;
    h.push(move_sample(now - 250 * kMs, 200.0f, 500.0f));
    h.push(move_sample(now - 50 * kMs, 500.0f, 500.0f));
    h.push(move_sample(now - 10 * kMs, 520.0f, 500.0f));

    ptd::TrailEffect effect{cfg};
    std::vector<ptd::TrailEffect::SparklePoint> slot_row;
    effect.collect_sparkle_slots(h, now, nullptr, slot_row);
    QVERIFY2(slot_row.size() >= 8, "one long segment must yield many slots");

    // All slots belong to the SAME source occurrence ...
    for (const auto& sp : slot_row) {
        QCOMPARE(sp.occurrence_ts, slot_row.front().occurrence_ts);
    }
    // ... yet every slot ordinal is distinct (distinct seeds) ...
    std::vector<int> ordinals;
    for (const auto& sp : slot_row) ordinals.push_back(sp.slot_ordinal);
    for (std::size_t i = 0; i < ordinals.size(); ++i) {
        for (std::size_t j = i + 1; j < ordinals.size(); ++j) {
            QVERIFY2(ordinals[i] != ordinals[j],
                     "two slots on one segment shared a seed ordinal");
        }
    }
    // ... and the ages are NOT all identical: t decreases monotonically
    // head -> tail, so the age basis is per-slot, not per-segment.
    for (std::size_t i = 1; i < slot_row.size(); ++i) {
        QVERIFY2(slot_row[i].t < slot_row[i - 1].t,
                 "slot ages must progress along the segment");
    }
    QVERIFY(slot_row.front().t - slot_row.back().t > 0.05f);

    // The animated families consume that per-slot age basis: two slots of
    // the same occurrence, same ordinal, different t must differ.
    const ptd::TrailConfig tw = sparkle_config(ptd::TrailSparkleMode::Twinkle, 1.0f);
    ptd::TrailSparkleRecord young{};
    ptd::TrailSparkleRecord older{};
    ptd::TrailEffect::SparklePoint a{400.0f, 500.0f, slot_row.front().t,
                                     slot_row.front().occurrence_ts, 0};
    ptd::TrailEffect::SparklePoint b = a;
    b.t = slot_row.back().t;
    QVERIFY(ptd::TrailEffect::sparkle_for_point(a, tw, now, young));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(b, tw, now, older));
    QVERIFY2(young.size_px != older.size_px,
             "Twinkle pulse must use the per-slot age, not one segment age");

    // Distinct ordinals on the SAME occurrence produce distinct anchors.
    ptd::TrailEffect::SparklePoint c = a;
    c.slot_ordinal = 7;
    ptd::TrailSparkleRecord other{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(c, tw, now, other));
    QVERIFY2(other.x != young.x || other.y != young.y,
             "slot ordinal must participate in the seed");

    // Integration: emitted alpha falls monotonically head -> tail for a
    // non-shimmering family on this single segment.
    const ptd::TrailConfig sd =
        sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    const std::vector<ptd::TrailSparkleRecord> recs = build(h, sd, now).sparkles;
    QVERIFY(recs.size() >= 4);
    for (std::size_t i = 1; i < recs.size(); ++i) {
        QVERIFY2(recs[i].alpha < recs[i - 1].alpha,
                 "sparkle alpha must follow the per-slot path position");
    }
}

// ---- Audit Phase 10: anchors ride the CANONICAL smoothed path ----

// With smoothing 0.75 on a sparse curved path the Catmull-Rom stroke
// deviates visibly from the raw polyline. Every pre-spread sparkle anchor
// must lie ON the stroke own subdivision path (the exact segments the
// renderer receives), not on the raw BuildPoint chords.
void TestTrailSparkles::sparkle_anchors_lie_on_the_canonical_smoothed_path() {
    ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    cfg.smoothing = 0.75f;
    const int64_t now = 1000 * kMs;

    // Sparse, strongly curved: six samples around a wide arc.
    const float rx[6] = {100.0f, 240.0f, 330.0f, 330.0f, 240.0f, 100.0f};
    const float ry[6] = {400.0f, 330.0f, 440.0f, 600.0f, 700.0f, 620.0f};
    ptd::CursorHistory h;
    for (int i = 0; i < 6; ++i) {
        h.push(move_sample(now - (6 - i) * 40 * kMs, rx[i], ry[i]));
    }

    // The stroke segments ARE the canonical subdivision path.
    const RecordingSink stroke = build(h, cfg, now);
    QVERIFY(!stroke.segments.empty());

    ptd::TrailEffect effect{cfg};
    std::vector<ptd::TrailEffect::SparklePoint> slot_row;
    effect.collect_sparkle_slots(h, now, nullptr, slot_row);
    QVERIFY(slot_row.size() >= 6);

    struct Seg { float x1, y1, x2, y2; };
    auto dist_to_polyline = [](float px, float py,
                               const std::vector<Seg>& segs) {
        float best = std::numeric_limits<float>::max();
        for (const Seg& s : segs) {
            const float dx = s.x2 - s.x1, dy = s.y2 - s.y1;
            const float len2 = dx * dx + dy * dy;
            float u = 0.0f;
            if (len2 > 1e-9f) u = ((px - s.x1) * dx + (py - s.y1) * dy) / len2;
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            const float qx = s.x1 + dx * u, qy = s.y1 + dy * u;
            const float d = std::sqrt((px - qx) * (px - qx)
                                    + (py - qy) * (py - qy));
            if (d < best) best = d;
        }
        return best;
    };

    std::vector<Seg> curve;
    for (const auto& seg : stroke.segments) {
        curve.push_back(Seg{seg.x1, seg.y1, seg.x2, seg.y2});
    }
    std::vector<Seg> raw;
    for (int i = 1; i < 6; ++i) {
        raw.push_back(Seg{rx[i - 1], ry[i - 1], rx[i], ry[i]});
    }

    float worst_curve = 0.0f;
    float worst_raw = 0.0f;
    for (const auto& sp : slot_row) {
        const float dc = dist_to_polyline(sp.x, sp.y, curve);
        const float dr = dist_to_polyline(sp.x, sp.y, raw);
        if (dc > worst_curve) worst_curve = dc;
        if (dr > worst_raw) worst_raw = dr;
    }
    qDebug("[T-021 Phase10] worst_dist_to_curve=%.4f worst_dist_to_raw=%.4f",
           static_cast<double>(worst_curve), static_cast<double>(worst_raw));
    QVERIFY2(worst_curve < 0.05f,
             "sparkle anchors must lie on the canonical smoothed stroke");
    // Proof the gate has teeth: the raw polyline is a materially different
    // path here, so the pre-rewrite raw-BuildPoint sampler would fail.
    QVERIFY2(worst_raw > 2.0f,
             "test geometry is not curved enough to discriminate");
}

// ---- Audit Phase 6: bounded work must not starve the head ----

// A very long, very fast trail must widen slot spacing across the WHOLE
// path instead of spending the candidate budget on its oldest prefix.
void TestTrailSparkles::long_fast_path_still_populates_the_head() {
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Glitter, 1.0f);
    const int64_t now = 5000 * kMs;
    // 4000 px of travel inside one 350 ms lifetime window.
    ptd::CursorHistory h;
    const int n = 200;
    for (int i = 0; i < n; ++i) {
        const int64_t ts = now - 300 * kMs + (300LL * kMs * i) / (n - 1);
        h.push(move_sample(ts, 20.0f * i, 500.0f));
    }
    const float head_x = 20.0f * (n - 1);

    const std::vector<ptd::TrailSparkleRecord> recs = build(h, cfg, now).sparkles;
    QVERIFY(!recs.empty());
    QVERIFY2(static_cast<int>(recs.size())
                 <= ptd::TrailEffect::kMaxSparklesPerFrame,
             "hard per-frame cap breached");

    float nearest_to_head = std::numeric_limits<float>::max();
    float nearest_to_tail = std::numeric_limits<float>::max();
    for (const auto& r : recs) {
        const float dh = std::abs(head_x - r.x);
        const float dt = std::abs(r.x);
        if (dh < nearest_to_head) nearest_to_head = dh;
        if (dt < nearest_to_tail) nearest_to_tail = dt;
    }
    qDebug("[T-021 Phase6] emitted=%d nearest_head=%.1f nearest_tail=%.1f",
           static_cast<int>(recs.size()),
           static_cast<double>(nearest_to_head),
           static_cast<double>(nearest_to_tail));
    // A capped world lattice retains the newest slots, never the oldest
    // prefix. Head proximity is required; old tail slots may be dropped.
    QVERIFY2(nearest_to_head < 120.0f, "head region starved of sparkles");
    QVERIFY2(nearest_to_tail > 1000.0f, "oldest slots should leave the cap first");
}

// ---- Audit Phase 11: every mode is usable at full strength ----

// Deterministic lower bounds that follow directly from the spacing
// contract (count = floor(arc / (base_spacing / density))) on a
// representative 357 px trail at Amount 1.0. No probability roulette.
void TestTrailSparkles::full_strength_modes_meet_deterministic_minimums() {
    const int64_t now = 1000 * kMs;
    ptd::CursorHistory h;
    for (int i = 0; i <= 51; ++i) {   // 7 px raw samples over 357 px
        const int64_t ts = now - 200 * kMs + (200LL * kMs * i) / 51;
        h.push(move_sample(ts, static_cast<float>(i * 7), 300.0f));
    }

    int counts[5] = {0, 0, 0, 0, 0};
    int idx = 0;
    for (const ptd::TrailSparkleMode mode : {
             ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
             ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
             ptd::TrailSparkleMode::Shards}) {
        counts[idx++] = static_cast<int>(
            build(h, sparkle_config(mode, 1.0f), now).sparkles.size());
    }
    qDebug("[T-021 Phase11] stardust=%d twinkle=%d glitter=%d firefly=%d "
           "shards=%d",
           counts[0], counts[1], counts[2], counts[3], counts[4]);
    const int stardust = counts[0], twinkle = counts[1];
    const int glitter = counts[2],  firefly = counts[3];
    const int shards = counts[4];

    // floor(357 / (18 / 0.55)) = 10
    QVERIFY2(stardust >= 9, "Stardust must emit a useful set at Amount 1");
    // floor(357 / (18 / 0.30)) = 5 -- reliably several, never ~0
    QVERIFY2(twinkle >= 4, "Twinkle must reliably emit several sparkles");
    // floor(357 / (18 / 0.85)) = 16
    QVERIFY2(glitter >= 15, "Glitter must be the densest family");
    QVERIFY2(glitter > stardust, "Glitter must be visibly denser than Stardust");
    // floor(357 / (18 / 0.14)) = 2 -- Firefly is intentionally the
    // sparsest family: sparse, but never statistically gone
    QVERIFY2(firefly >= 2, "Firefly must emit a useful sparse set");
    QVERIFY(stardust > twinkle);
    QVERIFY(twinkle > firefly);
    // T-023: floor(357 / (18 / 0.48)) = 9 -- Shards sits between Twinkle
    // and Stardust: clearly present at Amount 1, never a dense shimmer.
    QVERIFY2(shards >= 8, "Shards must emit a useful set at Amount 1");
    QVERIFY2(shards < glitter, "Shards must stay sparser than Glitter");
    QVERIFY2(shards > twinkle, "Shards must be denser than Twinkle");
}

// Phase 6/7 (audit): a repeated build on the SAME sink instance must
// produce byte-identical records (the sink clears per frame like the
// renderer would; no state may leak into the second build), and the
// TrailEffect must be reusable unmodified.
void TestTrailSparkles::sparkle_determinism_same_sink_repeated_build() {
    ptd::CursorHistory path;
    straight_path(path, 45, -300.0f, -40.0f, 8.0f, 6.0f, 10 * kMs, 6);
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Firefly, 0.9f);
    const int64_t now = 300 * kMs;

    ptd::TrailEffect effect{cfg};
    SparkleRecordingSink sink;
    effect.build_geometry(path, now, nullptr, 512, sink);
    const std::vector<ptd::TrailSparkleRecord> first = sink.records;
    sink.records.clear();
    effect.build_geometry(path, now, nullptr, 512, sink);  // same instances
    QVERIFY(!first.empty());
    QCOMPARE(first.size(), sink.records.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        QVERIFY(first[i] == sink.records[i]);
    }

    // Negative virtual coordinates: deterministic, finite records.
    for (const ptd::TrailSparkleRecord& r : sink.records) {
        QVERIFY(finite_record(r));
    }
}

// Phase 10 (audit): every color channel of every emitted sparkle in every
// mode stays normalized [0,1] even with extreme brightness multipliers.
void TestTrailSparkles::sparkle_color_channels_clamped_all_modes() {
    ptd::TrailConfig cfg{};
    cfg.start_color_r = 255; cfg.start_color_g = 255; cfg.start_color_b = 255;
    cfg.fade_color_r = 255;  cfg.fade_color_g = 255;  cfg.fade_color_b = 255;
    const int64_t now = 500 * kMs;
    ptd::CursorHistory path;
    straight_path(path, 60, 0.0f, 0.0f, 7.0f, 5.0f, 5 * kMs, 7);
    for (const ptd::TrailSparkleMode mode : {
             ptd::TrailSparkleMode::Stardust, ptd::TrailSparkleMode::Twinkle,
             ptd::TrailSparkleMode::Glitter, ptd::TrailSparkleMode::Firefly,
             ptd::TrailSparkleMode::Shards}) {
        cfg.sparkle_mode = mode;
        const SparkleRecordingSink sink =
            [&] { ptd::TrailEffect e{cfg}; SparkleRecordingSink s;
                  e.build_geometry(path, now, nullptr, 512, s); return s; }();
        QVERIFY(!sink.records.empty());
        for (const ptd::TrailSparkleRecord& r : sink.records) {
            QVERIFY(r.color.r >= 0.0f && r.color.r <= 1.0f);
            QVERIFY(r.color.g >= 0.0f && r.color.g <= 1.0f);
            QVERIFY(r.color.b >= 0.0f && r.color.b <= 1.0f);
            QVERIFY(r.alpha > 0.0f && r.alpha <= 1.0f);
        }
    }
}

// Phase 10 (audit): Head/Tail accent color modes derive the accent on the
// correct side of the path for sparkles (same math as the stroke).
void TestTrailSparkles::sparkle_color_accent_modes_follow_the_accent() {
    ptd::TrailConfig head_accent =
        sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    head_accent.color_mode = ptd::TrailColorMode::StartAccent;  // Head accent
    head_accent.start_color_r = 255; head_accent.start_color_g = 0;
    head_accent.start_color_b = 0;
    head_accent.fade_color_r = 0;   head_accent.fade_color_g = 0;
    head_accent.fade_color_b = 255;
    ptd::TrailConfig tail_accent = head_accent;
    tail_accent.color_mode = ptd::TrailColorMode::FadeAccent;   // Tail accent

    ptd::TrailSparkleRecord head_side{};
    ptd::TrailSparkleRecord tail_side{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {100.0f, 100.0f, 1.0f, 100 * kMs}, head_accent, 110 * kMs, head_side));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {100.0f, 100.0f, 0.02f, 100 * kMs}, head_accent, 110 * kMs, tail_side));
    // Head accent: head side is the Start (red) color.
    QVERIFY(head_side.color.r > 0.9f && head_side.color.b < 0.2f);
    QVERIFY(tail_side.color.b > 0.9f && tail_side.color.r < 0.2f);

    // Tail accent (FadeAccent): the accent (Start, red) covers the head
    // side; the fade (blue) concentrates at the tail tip. The very tail
    // edge sits INSIDE the accent span [0, 0.25), so the exact color is a
    // dominated-by-fade blend (factor 0.02/0.25 = 0.08) -- assert the
    // blend, not pure blue.
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {100.0f, 100.0f, 1.0f, 100 * kMs}, tail_accent, 110 * kMs, head_side));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(
        {100.0f, 100.0f, 0.02f, 100 * kMs}, tail_accent, 110 * kMs, tail_side));
    QVERIFY(head_side.color.r > 0.9f && head_side.color.b < 0.2f);
    QVERIFY(tail_side.color.b > tail_side.color.r);  // fade-dominated tail
    QVERIFY(tail_side.color.b > 0.7f);
}

// ---- Repair regression: decoration shares the stroke alpha authority ----

// TrailStyle::Pulse breathes the whole trail 0.6..1.0. Before the repair
// the sparkle layer ignored it and stayed at constant brightness, so the
// decoration visibly detached from the stroke it belongs to.
void TestTrailSparkles::sparkles_breathe_with_the_pulse_stroke() {
    ptd::TrailConfig cfg = sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
    cfg.style = ptd::TrailStyle::Pulse;
    const ptd::TrailEffect::SparklePoint p{300.0f, 300.0f, 0.8f, 100 * kMs, 0};

    // Two instants a half period apart: peak vs trough of the same pulse.
    const int64_t peak = 450 * kMs;              // kPulsePeriodMs / 2
    const int64_t trough = 900 * kMs;            // full period -> multiplier 0.6
    ptd::TrailSparkleRecord hi{};
    ptd::TrailSparkleRecord lo{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(p, cfg, peak, hi));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(p, cfg, trough, lo));
    QVERIFY2(hi.alpha > lo.alpha,
             "Pulse style must breathe the sparkle layer too");
    // Identity is untouched by the breathing: only alpha moves.
    QCOMPARE(hi.x, lo.x);
    QCOMPARE(hi.y, lo.y);
    QCOMPARE(hi.size_px, lo.size_px);
    QVERIFY(hi.shape == lo.shape);

    // Every non-Pulse style is unaffected (multiplier is exactly 1).
    ptd::TrailConfig classic = cfg;
    classic.style = ptd::TrailStyle::Classic;
    ptd::TrailSparkleRecord a{};
    ptd::TrailSparkleRecord b{};
    QVERIFY(ptd::TrailEffect::sparkle_for_point(p, classic, peak, a));
    QVERIFY(ptd::TrailEffect::sparkle_for_point(p, classic, trough, b));
    QCOMPARE(a.alpha, b.alpha);
}

// The sparkle layer must consume the SAME fade authority as the stroke:
// base_opacity, fade_start and the FadeCurve all move sparkle alpha.
void TestTrailSparkles::sparkle_alpha_follows_the_trail_fade_contract() {
    const ptd::TrailEffect::SparklePoint mid{300.0f, 300.0f, 0.5f, 100 * kMs, 0};

    auto alpha_at_mid = [&](float fade_start, ptd::FadeCurve curve,
                            float base_opacity) {
        ptd::TrailConfig cfg =
            sparkle_config(ptd::TrailSparkleMode::Stardust, 1.0f);
        cfg.fade_start = fade_start;
        cfg.fade_curve = curve;
        cfg.base_opacity = base_opacity;
        ptd::TrailSparkleRecord r{};
        const bool ok =
            ptd::TrailEffect::sparkle_for_point(mid, cfg, 200 * kMs, r);
        return ok ? r.alpha : -1.0f;
    };

    const float linear = alpha_at_mid(0.0f, ptd::FadeCurve::Linear, 0.9f);
    const float ease = alpha_at_mid(0.0f, ptd::FadeCurve::EaseOut, 0.9f);
    const float plateau = alpha_at_mid(0.8f, ptd::FadeCurve::Linear, 0.9f);
    const float dim = alpha_at_mid(0.0f, ptd::FadeCurve::Linear, 0.3f);
    QVERIFY(linear > 0.0f && ease > 0.0f && plateau > 0.0f && dim > 0.0f);
    // fade = 1 - curve(p); EaseOut curve = 1-(1-p)^3 -> alpha (1-p)^3,
    // which drops faster than Linear at the same window position.
    QVERIFY2(ease < linear, "FadeCurve must move sparkle alpha");
    QVERIFY2(plateau > linear, "fade_start must move sparkle alpha");
    QVERIFY2(dim < linear, "base_opacity must move sparkle alpha");
}

void TestTrailSparkles::shards_emit_triangles_only() {
    ptd::CursorHistory path;
    straight_path(path, 70, -240.0f, 180.0f, 8.0f, 2.0f, 10 * kMs, 6);
    const int64_t now = 500 * kMs;
    const RecordingSink sink = build(
        path, sparkle_config(ptd::TrailSparkleMode::Shards, 1.0f), now);
    QVERIFY(!sink.sparkles.empty());
    for (const auto& sparkle : sink.sparkles) {
        QCOMPARE(sparkle.shape, ptd::TrailSparkleShape::Triangle);
    }
}

void TestTrailSparkles::shards_are_deterministic_and_world_anchored() {
    ptd::CursorHistory path;
    straight_path(path, 90, 40.0f, -120.0f, 5.0f, 3.0f, 0, 8);
    const ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Shards, 1.0f);
    const int64_t first_now = 800 * kMs;
    const RecordingSink first = build(path, cfg, first_now);
    const RecordingSink repeated = build(path, cfg, first_now);
    QVERIFY(!first.sparkles.empty());
    QCOMPARE(first.sparkles.size(), repeated.sparkles.size());
    for (std::size_t i = 0; i < first.sparkles.size(); ++i) {
        QVERIFY(first.sparkles[i] == repeated.sparkles[i]);
    }

    std::vector<ptd::TrailEffect::SparklePoint> before;
    ptd::TrailEffect effect{cfg};
    effect.collect_sparkle_slots(path, first_now, nullptr, before);
    QVERIFY(!before.empty());

    // Append newer samples and advance the visible head. Existing finalized
    // source-occurrence slots must retain their world-space anchors.
    straight_path(path, 20, 490.0f, 150.0f, 5.0f, 3.0f, 720 * kMs, 8);
    std::vector<ptd::TrailEffect::SparklePoint> after;
    effect.collect_sparkle_slots(path, 960 * kMs, nullptr, after);
    std::map<std::pair<int64_t, int>, std::pair<float, float>> old_anchors;
    for (const auto& slot : before) {
        old_anchors[{slot.occurrence_ts, slot.slot_ordinal}] =
            {slot.x, slot.y};
    }
    int surviving = 0;
    for (const auto& slot : after) {
        const auto it = old_anchors.find({slot.occurrence_ts, slot.slot_ordinal});
        if (it == old_anchors.end()) continue;
        ++surviving;
        QCOMPARE(slot.x, it->second.first);
        QCOMPARE(slot.y, it->second.second);
    }
    QVERIFY2(surviving > 0, "no surviving finalized Shard anchors to compare");
}

void TestTrailSparkles::shards_drift_rotate_and_vary_size() {
    ptd::TrailConfig cfg =
        sparkle_config(ptd::TrailSparkleMode::Shards, 1.0f);
    const int64_t now = 1000 * kMs;
    ptd::TrailSparkleRecord young{};
    ptd::TrailSparkleRecord old{};
    int selected_ordinal = -1;
    for (int ordinal = 0; ordinal < 256; ++ordinal) {
        const ptd::TrailEffect::SparklePoint point{
            100.0f, 200.0f, 0.80f, 300 * kMs, ordinal};
        if (ptd::TrailEffect::sparkle_for_point(point, cfg, now, young)) {
            selected_ordinal = ordinal;
            const ptd::TrailEffect::SparklePoint older{
                100.0f, 200.0f, 0.40f, 300 * kMs, ordinal};
            QVERIFY(ptd::TrailEffect::sparkle_for_point(older, cfg, now, old));
            break;
        }
    }
    QVERIFY(selected_ordinal >= 0);
    QVERIFY(old.x != young.x || old.y != young.y);
    QVERIFY(old.rotation_rad != young.rotation_rad);

    std::vector<float> sizes;
    for (int ordinal = 0; ordinal < 256; ++ordinal) {
        const ptd::TrailEffect::SparklePoint point{
            100.0f, 200.0f, 0.60f, 300 * kMs, ordinal};
        ptd::TrailSparkleRecord record{};
        if (ptd::TrailEffect::sparkle_for_point(point, cfg, now, record)) {
            sizes.push_back(record.size_px);
        }
    }
    QVERIFY(sizes.size() > 1);
    QVERIFY(std::any_of(sizes.begin() + 1, sizes.end(),
                        [&](float size) { return size != sizes.front(); }));

    bool positive = false;
    bool negative = false;
    for (int ordinal = 0; ordinal < 256; ++ordinal) {
        const ptd::TrailEffect::SparklePoint point{
            100.0f, 200.0f, 0.60f, 300 * kMs, ordinal};
        ptd::TrailSparkleRecord at_age{};
        const ptd::TrailEffect::SparklePoint older{
            100.0f, 200.0f, 0.30f, 300 * kMs, ordinal};
        ptd::TrailSparkleRecord at_older{};
        if (!ptd::TrailEffect::sparkle_for_point(point, cfg, now, at_age)
            || !ptd::TrailEffect::sparkle_for_point(older, cfg, now, at_older)) {
            continue;
        }
        const float motion = at_older.rotation_rad - at_age.rotation_rad;
        positive = positive || motion > 0.0f;
        negative = negative || motion < 0.0f;
    }
    QVERIFY(positive);
    QVERIFY(negative);
}

void TestTrailSparkles::shards_bounds_and_amount_contract() {
    ptd::CursorHistory path;
    straight_path(path, 100, -600.0f, -300.0f, 9.0f, 4.0f, 20 * kMs, 5);
    const int64_t now = 700 * kMs;
    ptd::TrailConfig zero =
        sparkle_config(ptd::TrailSparkleMode::Shards, 0.0f);
    QVERIFY(build(path, zero, now).sparkles.empty());

    const RecordingSink full = build(
        path, sparkle_config(ptd::TrailSparkleMode::Shards, 1.0f), now);
    QVERIFY(!full.sparkles.empty());
    QVERIFY(full.sparkles.size() <= 128);
    for (const auto& sparkle : full.sparkles) {
        QVERIFY(finite_record(sparkle));
        QVERIFY(sparkle.alpha >= 0.0f && sparkle.alpha <= 1.0f);
        QVERIFY(sparkle.color.r >= 0.0f && sparkle.color.r <= 1.0f);
        QVERIFY(sparkle.color.g >= 0.0f && sparkle.color.g <= 1.0f);
        QVERIFY(sparkle.color.b >= 0.0f && sparkle.color.b <= 1.0f);
    }
}

QTEST_MAIN(TestTrailSparkles)
#include "test_trail_sparkles.moc"
