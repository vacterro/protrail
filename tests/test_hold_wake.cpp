// T-026 Hold Motion Wake / T-027 Hold Controls: pure structural and
// deterministic tests. No Qt, no Direct2D runtime calls -- every assertion
// runs against ClickBubbleEffect's own math, which is why the style coverage
// is STRUCTURAL ("Ring emits ring geometry and no particle cloud") instead of
// screenshot-based.
//
// The producer/consumer contract is the sink: a recording sink captures
// exactly what draw() hands the renderer, so these tests observe the real
// frame path, not a private helper.

#include "../src/effects/click_bubble_effect.h"
#include "../src/config/app_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
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

void expect_finite(float v, const char* what) {
    ++g_checks;
    if (!std::isfinite(v)) {
        ++g_failures;
        std::printf("FAIL %s: non-finite value %f\n", what, v);
    }
}

constexpr int64_t kMs = 1'000'000;  // ns per ms

ptd::CursorSample down(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    s.button = ptd::MouseButton::Left;
    s.action = ptd::ButtonAction::Down;
    return s;
}

ptd::CursorSample up(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    s.button = ptd::MouseButton::Left;
    s.action = ptd::ButtonAction::Up;
    return s;
}

ptd::CursorSample move(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    s.button = ptd::MouseButton::None;
    s.action = ptd::ButtonAction::None;
    return s;
}

using ptd::ClickBubbleEffect;
using ptd::ClickStyle;
using ptd::HoldWakeEmission;
using ptd::WakeMark;
using ptd::WakeMarkKind;

// Wake-ready click config: the hold gesture and the motion wake both on, with
// the documented baseline multipliers and the base 12 px emission spacing.
ptd::ClickConfig wake_cfg() {
    ptd::ClickConfig c;
    c.style = ClickStyle::Ring;
    c.hold_enabled = true;
    c.hold_wake_enabled = true;
    c.hold_intensity = 1.0f;
    c.hold_wake_density = 1.0f;
    c.hold_wake_lifetime_ms = ptd::ClickConfig::kDefaultHoldWakeLifetimeMs;
    c.hold_release_strength = 1.0f;
    c.end_radius_px = 26.0f;
    c.particle_amount = 8;
    return c;
}

// Records exactly what the renderer would be asked to draw.
struct RecordSink : ptd::ClickBubbleSink {
    struct Ring {
        float x = 0.0f, y = 0.0f, radius = 0.0f, thickness = 0.0f;
        float ring_alpha = 0.0f, fill_alpha = 0.0f;
    };
    struct Dot {
        float x = 0.0f, y = 0.0f, radius = 0.0f, alpha = 0.0f;
    };
    int reserved = 0;
    std::vector<Ring> rings;
    std::vector<Dot> dots;

    void reserve_bubbles_hint(int bubble_count) override { reserved = bubble_count; }
    void add_bubble(float cx, float cy, float radius_px, float outline_thickness_px,
                    float, float, float, float ring_alpha,
                    float fill_alpha) override {
        rings.push_back(Ring{cx, cy, radius_px, outline_thickness_px, ring_alpha, fill_alpha});
    }
    void add_particle(float cx, float cy, float radius_px, float, float, float,
                      float alpha) override {
        dots.push_back(Dot{cx, cy, radius_px, alpha});
    }
};

// Drive an ActiveHold to (x, y) along a straight path in `steps` samples,
// returning the number of emissions created. The activation move is a
// zero-length sample so it never contributes a wake birth of its own.
void activate(ClickBubbleEffect& fx, int x, int y, int64_t at_ns) {
    fx.on_button_down(down(0, x, y));
    fx.on_cursor_moved(move(at_ns, x, y));
}

// One emission for the given style, born at (x, y) with the given energy.
HoldWakeEmission make_emission(ClickStyle style, float x, float y,
                              uint32_t seed, float energy) {
    HoldWakeEmission e;
    e.x = x;
    e.y = y;
    e.birth_timestamp_ns = 0;
    e.seed = seed;
    e.style = style;
    e.tangent_x = 1.0f;  // moving along +x
    e.tangent_y = 0.0f;
    e.motion_speed = energy * ClickBubbleEffect::kWakeReferenceSpeedPxPerSec;
    e.motion_energy = energy;
    e.charge = 0.5f;
    e.color_r = 0.0f;
    e.color_g = 200.0f;
    e.color_b = 255.0f;
    e.element_tint = 0.65f;
    e.particle_amount = 8;
    e.size_basis_px = 26.0f;
    e.thickness_px = 2.5f;
    e.intensity = 1.0f;
    e.lifetime_ms = 800.0f;
    return e;
}

// Marks of one emission at a normalized progress `p`.
std::size_t marks_at(const ClickBubbleEffect& fx, const HoldWakeEmission& e,
                     float p, WakeMark* out) {
    // lifetime_ms -> ns is 1e6, so `p` really is the emission's progress.
    const int64_t now = e.birth_timestamp_ns
        + static_cast<int64_t>(static_cast<double>(e.lifetime_ms) * 1'000'000.0
                               * static_cast<double>(p));
    return fx.wake_marks(e, now, out, ClickBubbleEffect::kMaxWakeMarksPerEmission);
}

int count_kind(const WakeMark* marks, std::size_t n, WakeMarkKind kind) {
    int c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (marks[i].kind == kind) ++c;
    }
    return c;
}

bool same_emission(const HoldWakeEmission& a, const HoldWakeEmission& b) {
    return a.x == b.x && a.y == b.y
        && a.birth_timestamp_ns == b.birth_timestamp_ns && a.seed == b.seed
        && a.style == b.style && a.tangent_x == b.tangent_x
        && a.tangent_y == b.tangent_y && a.motion_speed == b.motion_speed
        && a.motion_energy == b.motion_energy && a.charge == b.charge
        && a.color_r == b.color_r && a.color_g == b.color_g
        && a.color_b == b.color_b && a.element_tint == b.element_tint
        && a.particle_amount == b.particle_amount
        && a.size_basis_px == b.size_basis_px && a.thickness_px == b.thickness_px
        && a.intensity == b.intensity && a.lifetime_ms == b.lifetime_ms;
}

// ---------------------------------------------------------------- determinism

void test_input_stream_determinism() {
    const float ox = 500.0f;
    const float oy = 400.0f;

    // (A) the same 60 px in ONE movement sample.
    ClickBubbleEffect one(wake_cfg());
    activate(one, 500, 400, 200 * kMs);
    one.on_cursor_moved(move(300 * kMs, 560, 400));

    // (B) the same 60 px delivered as ten collinear 6 px samples over the
    // same wall-clock span.
    ClickBubbleEffect many(wake_cfg());
    activate(many, 500, 400, 200 * kMs);
    for (int i = 1; i <= 10; ++i) {
        many.on_cursor_moved(move(200 * kMs + i * 10 * kMs, 500 + i * 6, 400));
    }

    expect_true(one.wake_count() >= 4,
                "T-026D: one 60 px sample crosses several emission spacings");
    expect_true(one.wake_count() == many.wake_count(),
                "T-026D: emission count is distance-based, not event-based");

    const std::size_t n = std::min(one.wake_count(), many.wake_count());
    float max_dx = 0.0f;
    int64_t max_dt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const HoldWakeEmission& a = one.wake_emissions()[i];
        const HoldWakeEmission& b = many.wake_emissions()[i];
        max_dx = std::max(max_dx, std::fabs(a.x - b.x));
        max_dx = std::max(max_dx, std::fabs(a.y - b.y));
        max_dt = std::max(max_dt, a.birth_timestamp_ns > b.birth_timestamp_ns
                                      ? a.birth_timestamp_ns - b.birth_timestamp_ns
                                      : b.birth_timestamp_ns - a.birth_timestamp_ns);
        // Same spatial pattern also means the same tangents and energies.
        expect_near(a.tangent_x, b.tangent_x, 1.0e-3f,
                    "T-026D: equivalent paths yield equivalent tangents");
        expect_near(a.motion_energy, b.motion_energy, 1.0e-3f,
                    "T-026D: equivalent paths yield equivalent motion energy");
    }
    expect_true(max_dx <= 1.0f,
                "T-026D: equivalent paths yield equivalent anchor positions");
    // Sub-microsecond agreement: the two deliveries reach the same anchor at
    // the same instant up to float rounding of the interpolation parameter,
    // which over an 800 ms wake life is a 1e-9 relative difference and never a
    // visible age difference between two anchors of one segment.
    expect_true(max_dt <= 1000,
                "T-026D: equivalent paths yield equivalent interpolated births");

    // Spatial distribution, not a clump at the newest cursor coordinate.
    const auto& e = one.wake_emissions();
    expect_true(e.front().y == oy, "T-026D: anchors stay on the travelled path");
    expect_true(e.front().x >= ox, "T-026D: first anchor is after the origin");
    expect_true(e.back().x > e.front().x,
                "T-026D: anchors are spread along the segment, not stacked");

    // Interpolated birth timestamps: a fast movement must not produce a
    // spatially distributed wake whose parts are all the same age.
    expect_true(e.size() >= 2 && e.front().birth_timestamp_ns
                                     < e.back().birth_timestamp_ns,
                "T-026D: birth timestamps are interpolated along the segment");
    for (std::size_t i = 0; i < e.size(); ++i) {
        expect_true(e[i].birth_timestamp_ns >= 200 * kMs
                        && e[i].birth_timestamp_ns <= 300 * kMs,
                    "T-026D: interpolated births stay inside the segment window");
        if (i > 0) {
            expect_true(e[i - 1].birth_timestamp_ns <= e[i].birth_timestamp_ns,
                        "T-026D: interpolated births are non-decreasing");
        }
    }
}

// ------------------------------------------------------------------ lifecycle

void test_lifecycle() {
    // No wake before the activation threshold.
    {
        ClickBubbleEffect fx(wake_cfg());
        fx.on_button_down(down(0, 100, 100));
        fx.on_cursor_moved(move(40 * kMs, 140, 100));
        fx.on_cursor_moved(move(120 * kMs, 200, 100));
        expect_true(fx.candidate_count(120 * kMs) == 1,
                    "T-026L: the record is still a candidate");
        expect_true(fx.wake_count() == 0,
                    "T-026L: a candidate never emits wake");
    }

    // A stationary active hold creates no detached wake births, however long
    // it is held: the attached aura is what provides stationary life.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 300, 300, 200 * kMs);
        for (int i = 0; i < 40; ++i) {
            fx.on_cursor_moved(move(300 * kMs + i * 25 * kMs, 300, 300));
        }
        expect_true(fx.active_hold_count(1300 * kMs) == 1,
                    "T-026L: the hold is active while held still");
        expect_true(fx.wake_count() == 0,
                    "T-026L: a stationary hold never spawns world-space wake");
    }

    // Movement after activation creates wake, and every anchor is immutable
    // once born: later cursor motion must not move it.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 40, 0));
        expect_true(fx.wake_count() > 0, "T-026L: movement after activation emits");
        const HoldWakeEmission before = fx.wake_emissions().front();
        const std::size_t before_n = fx.wake_count();

        fx.on_cursor_moved(move(300 * kMs, 4000, 3000));
        fx.on_cursor_moved(move(350 * kMs, -2000, 500));
        expect_true(fx.wake_count() >= before_n,
                    "T-026L: later movement only adds emissions");
        expect_true(same_emission(before, fx.wake_emissions().front()),
                    "T-026L: an already-born anchor is immutable");
    }

    // Up stops NEW births; already-born wake finishes on its own and only
    // then does the scheduler go idle.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 40, 0));
        const std::size_t born = fx.wake_count();
        expect_true(born > 0, "T-026L: pre-Up travel emitted wake");

        fx.on_button_up(up(260 * kMs, 40, 0));
        expect_true(fx.hold_record_count() == 0, "T-026L: Up ends the hold");
        fx.on_cursor_moved(move(400 * kMs, 900, 900));
        fx.on_cursor_moved(move(500 * kMs, 1200, 1200));
        expect_true(fx.wake_count() == born,
                    "T-026L: Up stops new births, no hold means no emission");

        const int64_t after_release = 700 * kMs;
        expect_true(fx.has_live_wake(after_release),
                    "T-026L: old wake is still alive after the release");
        expect_true(fx.has_live_content(after_release),
                    "T-026L: wake alone keeps the scheduler awake");
        expect_true(!fx.has_live_content(9000 * kMs),
                    "T-026L: once the wake expires the effect is idle");
        expect_true(fx.live_wake_count(9000 * kMs) == 0,
                    "T-026L: expired wake is not live");
        fx.prune(9000 * kMs);
        expect_true(fx.wake_count() == 0, "T-026L: prune reclaims expired wake");
    }

    // Lost-Up reconciliation: no new births, no invented release payoff, and
    // the already-born history is allowed to finish.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        const std::size_t born = fx.wake_count();
        const std::size_t bubbles = fx.active_count();
        fx.on_cursor_moved(move(400 * kMs, 200, 0));
        const std::size_t born_after = fx.wake_count();
        expect_true(born_after > born, "T-026L: the drag is still emitting");

        const std::size_t cancelled = fx.reconcile_physical_buttons(false, true, true);
        expect_true(cancelled == 1, "T-026L: the lost Up is reconciled");
        expect_true(fx.active_count() == bubbles,
                    "T-026L: a cancelled hold invents no release payoff");
        expect_true(fx.wake_count() == born_after,
                    "T-026L: a cancelled hold erases no born wake");
        fx.on_cursor_moved(move(500 * kMs, 800, 800));
        expect_true(fx.wake_count() == born_after,
                    "T-026L: a cancelled hold stops future wake generation");
        expect_true(fx.has_live_wake(600 * kMs),
                    "T-026L: cancelled hold leaves its history to finish");
    }

    // Master OFF / explicit clear semantics.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        expect_true(fx.wake_count() > 0, "T-026L: precondition, wake exists");
        fx.clear();
        expect_true(fx.wake_count() == 0, "T-026L: explicit clear drops wake");
        expect_true(fx.hold_record_count() == 0,
                    "T-026L: explicit clear drops holds");
    }

    // Hold FX OFF drops the wake with the gesture.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        expect_true(fx.wake_count() > 0, "T-026L: precondition, wake exists");
        ptd::ClickConfig off = wake_cfg();
        off.hold_enabled = false;
        fx.set_config(off);
        expect_true(fx.wake_count() == 0,
                    "T-026L: Hold FX OFF clears the remaining wake");
        // And a hold can no longer be opened at all.
        fx.on_button_down(down(300 * kMs, 10, 10));
        fx.on_cursor_moved(move(600 * kMs, 300, 300));
        expect_true(fx.wake_count() == 0, "T-026L: Hold FX OFF emits nothing");
    }

    // Motion Wake OFF stops creation and clears what is alive, immediately.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        expect_true(fx.wake_count() > 0, "T-026L: precondition, wake exists");
        ptd::ClickConfig off = wake_cfg();
        off.hold_wake_enabled = false;
        fx.set_config(off);
        expect_true(fx.wake_count() == 0,
                    "T-026L: Motion Wake OFF clears live wake at once");
        expect_true(fx.hold_record_count() == 1,
                    "T-026L: Motion Wake OFF keeps the attached hold aura");
        fx.on_cursor_moved(move(400 * kMs, 400, 0));
        expect_true(fx.wake_count() == 0,
                    "T-026L: Motion Wake OFF stops new emissions");
    }

    // Click FX OFF (the whole effect) clears holds AND wake.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        ptd::ClickConfig off = wake_cfg();
        off.enabled = false;
        fx.set_config(off);
        expect_true(fx.wake_count() == 0 && fx.hold_record_count() == 0,
                    "T-026L: Click FX OFF clears holds and wake");
    }
}

// -------------------------------------------------- T-027 integrity repairs

// Variable lifetimes. Each emission snapshots its lifetime at birth, so birth
// order is NOT expiry order: a long-lived emission born FIRST must keep the
// scheduler awake after a newer short-lived one has already expired, and only
// its own expiry may idle the content gate.
void test_variable_lifetimes() {
    // Long first, short second.
    {
        ptd::ClickConfig long_life = wake_cfg();
        long_life.hold_wake_lifetime_ms = 2500.0f;
        ClickBubbleEffect fx(long_life);
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));   // A: long-life emissions
        const std::size_t a_count = fx.wake_count();
        expect_true(a_count > 0, "T-027V: long-life emission A exists");
        expect_near(fx.wake_emissions().front().lifetime_ms, 2500.0f, 0.0f,
                    "T-027V: A snapshots the long Wake Life");

        ptd::ClickConfig short_life = long_life;
        short_life.hold_wake_lifetime_ms = 150.0f;
        fx.set_config(short_life);
        // A Wake Life change never erases already-born wake.
        expect_true(fx.wake_count() == a_count,
                    "T-027V: a Wake Life change clears nothing");
        fx.on_cursor_moved(move(300 * kMs, 120, 0));  // B: short-life emissions
        expect_true(fx.wake_count() > a_count, "T-027V: newer emission B exists");
        expect_near(fx.wake_emissions().back().lifetime_ms, 150.0f, 0.0f,
                    "T-027V: B snapshots the short Wake Life");
        // Up ends the gesture: from here only the detached wake histories
        // decide the content gate.
        fx.on_button_up(up(320 * kMs, 120, 0));
        expect_true(fx.hold_record_count() == 0, "T-027V: Up ends the hold");
        expect_true(fx.wake_progress_at(fx.wake_emissions().back(), 800 * kMs)
                        >= 1.0f,
                    "T-027V: B (newest) has expired by 800 ms");
        expect_true(fx.wake_progress_at(fx.wake_emissions().front(), 800 * kMs)
                        < 1.0f,
                    "T-027V: A (oldest) is still alive at 800 ms");
        expect_true(fx.has_live_wake(800 * kMs),
                    "T-027V: A keeps has_live_wake true after B expires");
        expect_true(fx.has_live_content(800 * kMs),
                    "T-027V: A keeps the scheduler content gate awake");

        // Only A's own expiry idles the gate.
        expect_true(!fx.has_live_wake(3000 * kMs),
                    "T-027V: the gate idles only after A expires");
        expect_true(!fx.has_live_content(3000 * kMs),
                    "T-027V: no live content once every lifetime ended");
        expect_true(fx.live_wake_count(3000 * kMs) == 0,
                    "T-027V: expired variable-lifetime wake is not live");
        fx.prune(3000 * kMs);
        expect_true(fx.wake_count() == 0,
                    "T-027V: prune reclaims every expired lifetime");
    }

    // Inverse: short first, long second -- normal expiry order is unchanged.
    {
        ptd::ClickConfig short_life = wake_cfg();
        short_life.hold_wake_lifetime_ms = 150.0f;
        ClickBubbleEffect fx(short_life);
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));   // old short-life
        ptd::ClickConfig long_life = short_life;
        long_life.hold_wake_lifetime_ms = 2500.0f;
        fx.set_config(long_life);
        fx.on_cursor_moved(move(300 * kMs, 120, 0));  // new long-life
        fx.on_button_up(up(320 * kMs, 120, 0));
        expect_true(fx.hold_record_count() == 0, "T-027V: Up ends the hold");
        expect_true(fx.wake_progress_at(fx.wake_emissions().front(), 800 * kMs)
                        >= 1.0f,
                    "T-027V: the old short-life emission has expired");
        expect_true(fx.has_live_wake(800 * kMs),
                    "T-027V: the new long-life emission keeps the gate awake");
        expect_true(fx.has_live_content(800 * kMs),
                    "T-027V: live content while the new emission animates");
        expect_true(!fx.has_live_wake(3000 * kMs),
                    "T-027V: the gate idles when the new emission expires");
    }
}

// Activation straddle: a movement segment may cross the Candidate->ActiveHold
// threshold. Only the post-activation part of the segment may emit, and the
// eligible part must start at the interpolated activation point.
void test_activation_straddle() {
    constexpr int64_t kActivation = 175 * kMs;  // kHoldActivationMs

    // One sample crossing activation: 160 ms / x=0 -> 200 ms / x=80.
    ClickBubbleEffect one(wake_cfg());
    one.on_button_down(down(0, 0, 0));
    one.on_cursor_moved(move(160 * kMs, 0, 0));
    one.on_cursor_moved(move(200 * kMs, 80, 0));
    expect_true(one.wake_count() > 0,
                "T-027S: travel crossing activation emits after it");
    for (const HoldWakeEmission& e : one.wake_emissions()) {
        expect_true(e.birth_timestamp_ns >= kActivation,
                    "T-027S: no emission is born before activation");
        expect_true(e.x >= 30.0f - 0.1f,
                    "T-027S: no anchor comes from pre-activation travel");
    }
    // Eligible distance is 80 - 30 = 50 px at the base 12 px spacing -> four
    // anchors (the whole 80 px segment would have produced six).
    expect_true(one.wake_count() == 4,
                "T-027S: only post-activation distance contributes");

    // The same movement split at the activation point must deliver the same
    // post-activation wake within the deterministic interpolation rules.
    ClickBubbleEffect many(wake_cfg());
    many.on_button_down(down(0, 0, 0));
    many.on_cursor_moved(move(160 * kMs, 0, 0));
    many.on_cursor_moved(move(kActivation, 30, 0));
    many.on_cursor_moved(move(200 * kMs, 80, 0));
    expect_true(many.wake_count() == one.wake_count(),
                "T-027S: segmented delivery matches the single crossing sample");
    const std::size_t n = std::min(one.wake_count(), many.wake_count());
    for (std::size_t i = 0; i < n; ++i) {
        const HoldWakeEmission& a = one.wake_emissions()[i];
        const HoldWakeEmission& b = many.wake_emissions()[i];
        expect_near(b.x, a.x, 1.0e-3f, "T-027S: equivalent anchors");
        expect_near(b.motion_speed, a.motion_speed, 1.0e-3f,
                    "T-027S: equivalent speeds");
        expect_true(b.birth_timestamp_ns == a.birth_timestamp_ns,
                    "T-027S: equivalent interpolated births");
    }

    // Movement wholly before activation still emits nothing...
    {
        ClickBubbleEffect before(wake_cfg());
        before.on_button_down(down(0, 0, 0));
        before.on_cursor_moved(move(120 * kMs, 300, 0));
        before.on_cursor_moved(move(170 * kMs, 360, 0));
        expect_true(before.wake_count() == 0,
                    "T-027S: wholly pre-activation travel emits nothing");
    }

    // ...and a stationary activation spawns no detached wake.
    {
        ClickBubbleEffect still(wake_cfg());
        activate(still, 50, 50, 200 * kMs);
        still.on_cursor_moved(move(400 * kMs, 50, 50));
        expect_true(still.wake_count() == 0,
                    "T-027S: stationary activation emits no detached wake");
    }
}

void test_bounds() {
    // The global cap is a hard limit, and no gesture may breach it.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        for (int i = 1; i <= 400; ++i) {
            fx.on_cursor_moved(move(200 * kMs + i * 4 * kMs, i * 30, 0));
            expect_true(fx.wake_count() <= ClickBubbleEffect::kMaxWakeEmissions,
                        "T-026B: the global wake cap is never exceeded");
        }
        expect_true(fx.wake_count() == ClickBubbleEffect::kMaxWakeEmissions,
                    "T-026B: a long sweep actually fills the cap");
        expect_true(fx.has_active_hold(4000 * kMs),
                    "T-026B: wake overflow never cancels the active hold");
        expect_true(fx.active_hold_count(4000 * kMs) == 1,
                    "T-026B: exactly one hold survives the overflow");
    }

    // A teleport cannot produce an allocation storm: per-sample births are
    // capped and the segment is sampled across the allowed budget.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 100000, 0));
        expect_true(fx.wake_count() <=
                        static_cast<std::size_t>(
                            ClickBubbleEffect::kMaxWakeBirthsPerMovement),
                    "T-026B: a huge movement respects the per-sample birth cap");
        expect_true(fx.wake_count() >= 2,
                    "T-026B: a teleport still leaves a distributed wake");
        const auto& e = fx.wake_emissions();
        expect_true(e.front().x > 0.0f && e.back().x > e.front().x
                        && e.back().x <= 100000.0f,
                    "T-026B: budgeted births are sampled along the segment");
    }

    // Multi-monitor virtual coordinates: negative (left-of-primary) space is
    // as valid as any other, and everything stays finite.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, -1920, -300, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, -1860, -300));
        expect_true(fx.wake_count() > 0,
                    "T-026B: negative virtual coordinates emit normally");
        for (const HoldWakeEmission& e : fx.wake_emissions()) {
            expect_finite(e.x, "T-026B: virtual x is finite");
            expect_finite(e.y, "T-026B: virtual y is finite");
            expect_true(e.x <= -1800.0f, "T-026B: the anchor keeps its quadrant");
        }
    }

    // No NaN/Inf anywhere, from any style, at any progress.
    {
        ClickBubbleEffect fx(wake_cfg());
        const ClickStyle styles[] = {
            ClickStyle::Ring, ClickStyle::DoubleRing, ClickStyle::Ripple,
            ClickStyle::Burst, ClickStyle::SparkBurst, ClickStyle::SoftFlash,
            ClickStyle::DotRing, ClickStyle::Air, ClickStyle::Fire,
            ClickStyle::Water, ClickStyle::Earth};
        const float energies[] = {0.0f, 0.5f, 1.0f};
        for (ClickStyle style : styles) {
            for (float energy : energies) {
                for (uint32_t seed = 1; seed <= 4; ++seed) {
                    const HoldWakeEmission e =
                        make_emission(style, 10.0f, 20.0f, seed * 2654435761u, energy);
                    for (int step = 0; step <= 20; ++step) {
                        WakeMark marks[ClickBubbleEffect::kMaxWakeMarksPerEmission];
                        const std::size_t n = marks_at(fx, e,
                            static_cast<float>(step) / 20.0f, marks);
                        expect_true(n <= ClickBubbleEffect::kMaxWakeMarksPerEmission,
                                    "T-026B: the per-emission mark budget holds");
                        for (std::size_t i = 0; i < n; ++i) {
                            expect_finite(marks[i].x, "T-026B: mark x is finite");
                            expect_finite(marks[i].y, "T-026B: mark y is finite");
                            expect_finite(marks[i].radius_px,
                                          "T-026B: mark radius is finite");
                            expect_finite(marks[i].alpha, "T-026B: mark alpha is finite");
                            expect_true(marks[i].alpha >= 0.0f && marks[i].alpha <= 1.0f,
                                        "T-026B: mark alpha stays inside 0..1");
                            expect_true(marks[i].radius_px >= 0.0f,
                                        "T-026B: mark radius is non-negative");
                        }
                    }
                }
            }
        }
    }

    // Zero-particle wake math stays finite. With particles off the burst
    // packet is EMPTY; its angular spacing must never be computed, Burst
    // keeps its ring, and Spark Burst emits no marks at all.
    {
        ClickBubbleEffect fx(wake_cfg());
        const ClickStyle styles[] = {ClickStyle::Burst, ClickStyle::SparkBurst};
        for (ClickStyle style : styles) {
            HoldWakeEmission e = make_emission(style, 0, 0, 0x5EEDu, 0.5f);
            e.particle_amount = 0;
            for (int step = 0; step < 20; ++step) {
                WakeMark marks[ClickBubbleEffect::kMaxWakeMarksPerEmission];
                const std::size_t n = marks_at(fx, e,
                    static_cast<float>(step) / 20.0f, marks);
                for (std::size_t i = 0; i < n; ++i) {
                    expect_finite(marks[i].x, "T-027Z: zero-particle mark x finite");
                    expect_finite(marks[i].y, "T-027Z: zero-particle mark y finite");
                    expect_finite(marks[i].radius_px,
                                  "T-027Z: zero-particle mark radius finite");
                    expect_finite(marks[i].alpha, "T-027Z: zero-particle mark alpha finite");
                }
                expect_true(count_kind(marks, n, WakeMarkKind::Particle) == 0,
                            "T-027Z: particles off emits no particle marks");
                if (style == ClickStyle::Burst) {
                    expect_true(count_kind(marks, n, WakeMarkKind::Ring) == 1,
                                "T-027Z: Burst keeps its ring with particles off");
                } else {
                    expect_true(n == 0,
                                "T-027Z: SparkBurst with particles off is empty");
                }
            }
        }
    }
}

// -------------------------------------------------------------- style grammar

void test_style_structure() {
    ClickBubbleEffect fx(wake_cfg());
    WakeMark marks[ClickBubbleEffect::kMaxWakeMarksPerEmission];
    // Ring: expanding ring stamps, never a generic particle cloud.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Ring, 0, 0, 11u, 0.4f);
        const std::size_t n = marks_at(fx, e, 0.5f, marks);
        expect_true(n == 1, "T-026S: Ring emits exactly one mark");
        expect_true(count_kind(marks, n, WakeMarkKind::Ring) == 1,
                    "T-026S: Ring emits ring geometry");
        expect_true(count_kind(marks, n, WakeMarkKind::Particle) == 0,
                    "T-026S: Ring emits no particle cloud");
        // Expanding, not shrinking.
        WakeMark a[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t na = marks_at(fx, e, 0.2f, a);
        expect_true(na == 1 && a[0].radius_px < marks[0].radius_px,
                    "T-026S: a Ring wake stamp keeps expanding after birth");
    }

    // Double Ring: a PAIR of concentric echoes at different rates.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::DoubleRing, 0, 0, 12u, 0.4f);
        const std::size_t n = marks_at(fx, e, 0.5f, marks);
        expect_true(n == 2 && count_kind(marks, n, WakeMarkKind::Ring) == 2,
                    "T-026S: Double Ring emits paired rings");
        expect_true(marks[0].radius_px > marks[1].radius_px,
                    "T-026S: the pair is concentric at two different radii");
        WakeMark a[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t na = marks_at(fx, e, 0.8f, a);
        const float gap_early = (marks[0].radius_px - marks[1].radius_px)
                              / (marks[0].radius_px > 0 ? marks[0].radius_px : 1.0f);
        const float gap_late = (a[0].radius_px - a[1].radius_px)
                             / (a[0].radius_px > 0 ? a[0].radius_px : 1.0f);
        expect_true(na == 2 && gap_late != gap_early,
                    "T-026S: the two rings expand at different rates");
    }

    // Ripple: PHASED waves -- one wave first, more as the record ages.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Ripple, 0, 0, 13u, 0.4f);
        const std::size_t early = marks_at(fx, e, 0.10f, marks);
        const std::size_t late = marks_at(fx, e, 0.60f, marks);
        expect_true(early == 1, "T-026S: Ripple starts with a single wave");
        expect_true(late >= 2, "T-026S: Ripple staggers further waves later");
        expect_true(count_kind(marks, late, WakeMarkKind::Particle) == 0,
                    "T-026S: Ripple emits ripple geometry, not particles");
    }

    // Burst: a REDUCED radial packet, not the full click explosion.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Burst, 0, 0, 14u, 0.4f);
        const std::size_t n = marks_at(fx, e, 0.4f, marks);
        const int particles = count_kind(marks, n, WakeMarkKind::Particle);
        expect_true(particles >= 2, "T-026S: Burst sheds a particle packet");
        expect_true(particles <= 4, "T-026S: the Burst packet is reduced");
        expect_true(n <= ClickBubbleEffect::kMaxWakeMarksPerEmission,
                    "T-026S: one Burst emission stays inside the mark budget");
        // Backward bias from the movement tangent: with high energy the packet
        // leans against the direction of travel. Measured as the mean
        // displacement ALONG the tangent, normalized by the packet radius, so
        // the comparison is not polluted by the packet simply getting bigger.
        const HoldWakeEmission slow = make_emission(ClickStyle::Burst, 0, 0, 14u, 0.0f);
        const HoldWakeEmission fast = make_emission(ClickStyle::Burst, 0, 0, 14u, 1.0f);
        WakeMark ms[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark mf[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ns = marks_at(fx, slow, 0.4f, ms);
        const std::size_t nf = marks_at(fx, fast, 0.4f, mf);
        const auto mean_along = [](const WakeMark* m, std::size_t n,
                                   const HoldWakeEmission& e) {
            float sum = 0.0f;
            int count = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (m[i].kind != WakeMarkKind::Particle) continue;
                sum += (m[i].x - e.x) * e.tangent_x + (m[i].y - e.y) * e.tangent_y;
                ++count;
            }
            return count > 0 ? sum / static_cast<float>(count) : 0.0f;
        };
        const float along_slow = mean_along(ms, ns, slow);
        const float along_fast = mean_along(mf, nf, fast);
        expect_true(along_fast < along_slow,
                    "T-026S: fast movement throws the Burst packet backwards");
    }

    // Spark Burst: burstier than Burst, with deterministic per-particle
    // variation -- the same record replays identically, a different one does
    // not.
    {
        const HoldWakeEmission b = make_emission(ClickStyle::Burst, 0, 0, 15u, 0.5f);
        const HoldWakeEmission s = make_emission(ClickStyle::SparkBurst, 0, 0, 15u, 0.5f);
        WakeMark mb[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark ms[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t nb = marks_at(fx, b, 0.35f, mb);
        const std::size_t ns = marks_at(fx, s, 0.35f, ms);
        const int pb = count_kind(mb, nb, WakeMarkKind::Particle);
        const int ps = count_kind(ms, ns, WakeMarkKind::Particle);
        expect_true(ps > pb, "T-026S: Spark Burst sheds more than Burst");
        float amin = 1.0f;
        float amax = 0.0f;
        for (std::size_t i = 0; i < ns; ++i) {
            if (ms[i].kind != WakeMarkKind::Particle) continue;
            amin = std::min(amin, ms[i].alpha);
            amax = std::max(amax, ms[i].alpha);
        }
        expect_true(amax > amin, "T-026S: Spark Burst varies per particle");
        // Determinism: same record, same elapsed, same geometry.
        WakeMark again[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t na = marks_at(fx, s, 0.35f, again);
        expect_true(na == ns, "T-026S: Spark Burst mark count is reproducible");
        bool identical = true;
        for (std::size_t i = 0; i < ns; ++i) {
            identical = identical && again[i].x == ms[i].x && again[i].y == ms[i].y
                     && again[i].alpha == ms[i].alpha;
        }
        expect_true(identical, "T-026S: a Spark Burst record replays identically");
        // A different record lays out differently.
        const HoldWakeEmission s2 = make_emission(ClickStyle::SparkBurst, 0, 0, 999u, 0.5f);
        WakeMark mo[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t no = marks_at(fx, s2, 0.35f, mo);
        bool differs = no != ns;
        for (std::size_t i = 0; !differs && i < ns; ++i) {
            differs = mo[i].x != ms[i].x || mo[i].y != ms[i].y;
        }
        expect_true(differs, "T-026S: a different record lays out differently");
    }

    // Soft Flash: fill-dominant afterglow, not particles.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::SoftFlash, 0, 0, 16u, 0.4f);
        const std::size_t n = marks_at(fx, e, 0.4f, marks);
        expect_true(n >= 1 && count_kind(marks, n, WakeMarkKind::Disc) == static_cast<int>(n),
                    "T-026S: Soft Flash emits fill-dominant glow discs only");
        expect_true(count_kind(marks, n, WakeMarkKind::Ring) == 0,
                    "T-026S: Soft Flash emits no ring stroke");
    }

    // Dot Ring: a chain of glowing beads, plus at most one small ring echo.
    {
        int emissions_with_bead = 0;
        int emissions_with_ring = 0;
        for (uint32_t seed = 1; seed <= 24; ++seed) {
            const HoldWakeEmission e =
                make_emission(ClickStyle::DotRing, 0, 0, seed * 747796405u, 0.5f);
            const std::size_t n = marks_at(fx, e, 0.3f, marks);
            const int beads = count_kind(marks, n, WakeMarkKind::Particle);
            const int rings = count_kind(marks, n, WakeMarkKind::Ring);
            if (beads >= 1) ++emissions_with_bead;
            if (rings >= 1) ++emissions_with_ring;
            expect_true(rings <= 1,
                        "T-026S: Dot Ring never turns into a ring/particle cloud");
        }
        expect_true(emissions_with_bead == 24,
                    "T-026S: every Dot Ring emission leaves a glowing bead");
        expect_true(emissions_with_ring > 0,
                    "T-026S: Dot Ring occasionally leaves a ring echo");
    }

    // Air: motes displaced TANGENTIALLY, and the curl reverses over the
    // emission's life instead of just drifting radially.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Air, 0, 0, 17u, 0.6f);
        WakeMark early[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark late[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ne = marks_at(fx, e, 0.25f, early);
        const std::size_t nl = marks_at(fx, e, 0.75f, late);
        expect_true(ne >= 2 && nl >= 2, "T-026S: Air sheds several motes");
        // The emission's tangent is +x, so the TANGENTIAL axis is y. A mote
        // that only drifted radially would keep a constant displacement
        // direction; an eddy changes it, and dominantly sideways.
        const float dx = late[0].x - early[0].x;
        const float dy = late[0].y - early[0].y;
        expect_true(std::fabs(dy) > 0.0f,
                    "T-026S: Air displacement has a tangential component");
        expect_true(std::fabs(dy) > std::fabs(dx),
                    "T-026S: the Air curl is dominantly tangential, not radial");
    }

    // Fire: embers RISE monotonically and cool with age.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Fire, 0, 0, 18u, 0.4f);
        WakeMark early[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark late[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ne = marks_at(fx, e, 0.15f, early);
        const std::size_t nl = marks_at(fx, e, 0.90f, late);
        expect_true(ne >= 1 && nl >= 1, "T-026S: Fire sheds embers");
        expect_true(late[0].y < early[0].y, "T-026S: Fire embers rise over time");
        expect_true(late[0].y < e.y, "T-026S: embers end up above their anchor");
        WakeMark mid[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t nm = marks_at(fx, e, 0.5f, mid);
        expect_true(nm >= 1 && mid[0].y < early[0].y && mid[0].y > late[0].y,
                    "T-026S: the ember rise is monotone in time");
        expect_true(early[0].b > late[0].b,
                    "T-026S: Fire shifts its tint toward deep red as it cools");
    }

    // Water: droplets AND a local ripple left where they came off.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Water, 0, 0, 19u, 0.5f);
        WakeMark early[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark late[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ne = marks_at(fx, e, 0.3f, early);
        const std::size_t nl = marks_at(fx, e, 0.95f, late);
        expect_true(count_kind(early, ne, WakeMarkKind::Particle) >= 1,
                    "T-026S: Water throws droplets");
        expect_true(count_kind(early, ne, WakeMarkKind::Ring) >= 1,
                    "T-026S: Water leaves a ripple at the emission point");
        // The droplet arcs: thrown up, then pulled back down.
        float y_early = 0.0f;
        float y_late = 0.0f;
        for (std::size_t i = 0; i < ne; ++i) {
            if (early[i].kind == WakeMarkKind::Particle) y_early = early[i].y;
        }
        for (std::size_t i = 0; i < nl; ++i) {
            if (late[i].kind == WakeMarkKind::Particle) y_late = late[i].y;
        }
        expect_true(y_early < e.y,
                    "T-026S: Water droplets are thrown UP out of the emission point");
        expect_true(y_late > e.y,
                    "T-026S: Water droplets are pulled back DOWN under gravity");
        expect_true(y_late > y_early,
                    "T-026S: the droplet path is a bounded arc, not a launch");
        expect_true(std::fabs(y_late - e.y) <= e.size_basis_px * 2.0f,
                    "T-026S: the droplet arc stays bounded");
    }

    // Earth: heavy debris that LAGS and SETTLES instead of orbiting.
    {
        const HoldWakeEmission e = make_emission(ClickStyle::Earth, 0, 0, 20u, 0.5f);
        WakeMark early[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark late[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ne = marks_at(fx, e, 0.15f, early);
        const std::size_t nl = marks_at(fx, e, 0.90f, late);
        expect_true(ne >= 2 && nl >= 2, "T-026S: Earth sheds debris and dust");
        expect_true(count_kind(early, ne, WakeMarkKind::Disc) >= 1,
                    "T-026S: Earth includes a short-lived dust puff");
        bool settled = true;
        bool low = true;
        for (std::size_t i = 0; i < nl; ++i) {
            if (late[i].kind != WakeMarkKind::Particle) continue;
            settled = settled && late[i].y > e.y;
            low = low && std::fabs(late[i].y - e.y) <= e.size_basis_px * 1.2f;
        }
        expect_true(settled, "T-026S: Earth debris ends up below the anchor");
        expect_true(low, "T-026S: Earth debris stays heavy and low, never orbiting");
        // Per-chunk: marks stay index-stable, so debris that settled can never
        // have risen relative to its own earlier position -- no orbit.
        bool never_rises = ne == nl;
        for (std::size_t i = 0; i < ne && i < nl; ++i) {
            if (early[i].kind != WakeMarkKind::Particle
                || late[i].kind != WakeMarkKind::Particle) continue;
            never_rises = never_rises && late[i].y >= early[i].y;
        }
        expect_true(never_rises, "T-026S: Earth debris never travels upward over time");
    }
}

// ------------------------------------------------------------------ dynamics

void test_dynamics() {
    // Tangent and speed are captured from the segment, bounded and finite.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(220 * kMs, 0, 40));  // straight down, 40 px in 20 ms
        expect_true(fx.wake_count() > 0, "T-026Y: movement emits");
        const HoldWakeEmission& e = fx.wake_emissions().front();
        expect_near(e.tangent_x, 0.0f, 1.0e-3f, "T-026Y: tangent x is captured");
        expect_near(e.tangent_y, 1.0f, 1.0e-3f, "T-026Y: tangent y is captured");
        expect_finite(e.motion_speed, "T-026Y: speed is finite");
        expect_true(e.motion_speed > 0.0f && e.motion_speed <= 20000.0f,
                    "T-026Y: speed is bounded");
        expect_true(e.motion_energy >= 0.0f && e.motion_energy <= 1.0f,
                    "T-026Y: motion energy is normalized");
    }

    // Fast movement is measurably more energetic than slow movement.
    {
        ClickBubbleEffect slow(wake_cfg());
        activate(slow, 0, 0, 200 * kMs);
        slow.on_cursor_moved(move(1000 * kMs, 30, 0));   // 30 px in 800 ms

        ClickBubbleEffect fast(wake_cfg());
        activate(fast, 0, 0, 200 * kMs);
        fast.on_cursor_moved(move(215 * kMs, 30, 0));    // 30 px in 15 ms

        expect_true(slow.wake_count() > 0 && fast.wake_count() > 0,
                    "T-026Y: slow movement still creates a readable wake");
        const float es = slow.wake_emissions().front().motion_energy;
        const float ef = fast.wake_emissions().front().motion_energy;
        expect_true(ef > es, "T-026Y: fast movement is more energetic");
        // The energy is visible in the geometry, not only in the record.
        WakeMark sl[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        WakeMark fa[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t ns = marks_at(slow, slow.wake_emissions().front(), 0.5f, sl);
        const std::size_t nf = marks_at(fast, fast.wake_emissions().front(), 0.5f, fa);
        expect_true(ns >= 1 && nf >= 1, "T-026Y: both energies render marks");
        expect_true(fa[0].radius_px > sl[0].radius_px,
                    "T-026Y: a fast wake is spatially bigger than a slow one");
        // Slow movement must not fall through a floor into invisibility.
        expect_true(sl[0].alpha > 0.0f, "T-026Y: a slow wake is still visible");
    }

    // Old emissions do not react to later cursor velocity.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(300 * kMs, 30, 0));  // slow
        WakeMark before[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t nb =
            marks_at(fx, fx.wake_emissions().front(), 0.5f, before);
        const HoldWakeEmission copy = fx.wake_emissions().front();
        fx.on_cursor_moved(move(305 * kMs, 3000, 0));  // violent
        WakeMark after[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t na =
            marks_at(fx, fx.wake_emissions().front(), 0.5f, after);
        expect_true(nb == na, "T-026Y: a born emission keeps its mark count");
        bool identical = true;
        for (std::size_t i = 0; i < nb; ++i) {
            identical = identical && before[i].x == after[i].x
                     && before[i].y == after[i].y
                     && before[i].alpha == after[i].alpha;
        }
        expect_true(identical,
                    "T-026Y: later cursor velocity cannot change old geometry");
        expect_true(same_emission(copy, fx.wake_emissions().front()),
                    "T-026Y: later velocity cannot rewrite an old record");
    }

    // Style and colour changes affect NEW wake only: an emission carries the
    // full snapshot it was born with.
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 40, 0));
        const HoldWakeEmission born = fx.wake_emissions().front();

        ptd::ClickConfig next = wake_cfg();
        next.style = ClickStyle::Earth;
        next.color_r = 255;
        next.color_g = 0;
        next.color_b = 0;
        next.element_tint = 0.0f;
        fx.set_config(next);
        fx.on_cursor_moved(move(300 * kMs, 80, 0));

        expect_true(fx.wake_count() >= 2, "T-026Y: new emissions after a change");
        expect_true(fx.wake_emissions().front().style == ClickStyle::Ring,
                    "T-026Y: a style change leaves old emissions alone");
        expect_true(fx.wake_emissions().back().style == ClickStyle::Earth,
                    "T-026Y: a style change applies to new emissions");
        expect_true(same_emission(born, fx.wake_emissions().front()),
                    "T-026Y: the old record is untouched by set_config");
        expect_near(fx.wake_emissions().front().color_g, born.color_g, 0.0f,
                    "T-026Y: a colour change leaves old emissions alone");
        expect_near(fx.wake_emissions().front().color_r, 0.0f, 0.0f,
                    "T-026Y: the old emission keeps its colour");
        expect_near(fx.wake_emissions().back().color_r, 255.0f, 0.0f,
                    "T-026Y: a colour change applies to new emissions");

        // And the geometry of the old emission is still RING geometry.
        WakeMark marks[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t n =
            marks_at(fx, fx.wake_emissions().front(), 0.5f, marks);
        expect_true(count_kind(marks, n, WakeMarkKind::Ring) == static_cast<int>(n),
                    "T-026Y: an old Ring emission still renders as rings");
    }

    // Equal record + equal elapsed = identical geometry, whatever the frame
    // cadence that reached that instant.
    {
        ClickBubbleEffect fx(wake_cfg());
        const HoldWakeEmission e = make_emission(ClickStyle::SparkBurst, 5.0f, 7.0f,
                                                0xABCDEFu, 0.7f);
        WakeMark direct[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        const std::size_t nd = marks_at(fx, e, 0.42f, direct);

        // Walk there one small step at a time, rendering every step.
        WakeMark stepped[ClickBubbleEffect::kMaxWakeMarksPerEmission];
        std::size_t ns = 0;
        for (int i = 1; i <= 42; ++i) {
            ns = marks_at(fx, e, static_cast<float>(i) / 100.0f, stepped);
        }
        expect_true(nd == ns, "T-026Y: cadence does not change the mark count");
        bool identical = true;
        for (std::size_t i = 0; i < nd; ++i) {
            identical = identical && direct[i].x == stepped[i].x
                     && direct[i].y == stepped[i].y
                     && direct[i].alpha == stepped[i].alpha
                     && direct[i].radius_px == stepped[i].radius_px;
        }
        expect_true(identical, "T-026Y: cadence does not change the geometry");
    }

    // The wake is drawn as HISTORY: it must reach the sink before the live
    // aura and it must never be silently dropped by draw().
    {
        ClickBubbleEffect fx(wake_cfg());
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 200, 0));
        RecordSink sink;
        fx.draw(400 * kMs, sink);
        expect_true(!sink.rings.empty() || !sink.dots.empty(),
                    "T-026Y: draw() emits the detached wake");
        expect_true(sink.reserved >= 1,
                    "T-026Y: the wake is part of the single reserve hint");
        // Detached means detached: nothing is drawn near the live cursor.
        for (const auto& r : sink.rings) {
            expect_true(std::fabs(r.x) < 250.0f,
                        "T-026Y: wake marks stay at their own world anchors");
        }
    }
}

// ------------------------------------------------------------ T-027 controls

void test_hold_controls() {
    // Schema 9 is the boundary that introduced the Hold Controls / Motion
    // Wake block, and the Hold FX behaviour boundary is untouched by it.
    {
        // T-032: the CURRENT schema moved past 9, so this asserts the
        // boundary that T-027 owns (schema >= 9) rather than pinning the
        // number, which would turn every later schema bump into a false
        // failure of an unrelated ticket.
        expect_true(ptd::AppConfig::kCurrentSchemaVersion >= 9,
                    "T-027C: Hold Controls needed schema 9 or later");
        expect_true(ptd::AppConfig::kHoldWakeSchema == 9,
                    "T-027C: the Motion Wake boundary is named");
        expect_true(ptd::AppConfig::kHoldFxSchema == 8,
                    "T-027C: the schema-8 Hold FX boundary is not reinterpreted");
    }

    // Defaults: fresh config has Hold FX and Motion Wake on, identities at
    // baseline.
    {
        const ptd::ClickConfig c;
        expect_true(c.hold_enabled, "T-027C: Hold FX defaults on");
        expect_true(c.hold_wake_enabled, "T-027C: Motion Wake defaults on");
        expect_near(c.hold_intensity, 1.0f, 0.0f, "T-027C: intensity default");
        expect_near(c.hold_wake_density, 1.0f, 0.0f, "T-027C: density default");
        expect_near(c.hold_wake_lifetime_ms,
                    ptd::ClickConfig::kDefaultHoldWakeLifetimeMs, 0.0f,
                    "T-027C: wake life default");
        expect_near(c.hold_release_strength, 1.0f, 0.0f,
                    "T-027C: release strength default");
    }

    // Hard bounds: out-of-range values clamp, NaN recovers to the default.
    {
        ptd::ClickConfig c;
        c.hold_intensity = 99.0f;
        c.hold_wake_density = -5.0f;
        c.hold_wake_lifetime_ms = 1.0e9f;
        c.hold_release_strength = 0.0f;
        ptd::ClickConfig v = ptd::ClickConfig::validated(c);
        expect_near(v.hold_intensity, ptd::ClickConfig::kMaxHoldIntensity, 0.0f,
                    "T-027C: intensity clamps to its maximum");
        expect_near(v.hold_wake_density, ptd::ClickConfig::kMinHoldWakeDensity, 0.0f,
                    "T-027C: density clamps to its minimum");
        expect_near(v.hold_wake_lifetime_ms,
                    ptd::ClickConfig::kMaxHoldWakeLifetimeMs, 0.0f,
                    "T-027C: wake life clamps to its maximum");
        expect_near(v.hold_release_strength,
                    ptd::ClickConfig::kMinHoldReleaseStrength, 0.0f,
                    "T-027C: release strength clamps to its minimum");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        ptd::ClickConfig bad;
        bad.hold_intensity = nan;
        bad.hold_wake_density = nan;
        bad.hold_wake_lifetime_ms = nan;
        bad.hold_release_strength = nan;
        ptd::ClickConfig r = ptd::ClickConfig::validated(bad);
        expect_true(std::isfinite(r.hold_intensity) && std::isfinite(r.hold_wake_density)
                        && std::isfinite(r.hold_wake_lifetime_ms)
                        && std::isfinite(r.hold_release_strength),
                    "T-027C: NaN multipliers cannot reach the renderer");
        expect_near(r.hold_intensity, 1.0f, 0.0f, "T-027C: NaN intensity recovers");
        expect_near(r.hold_wake_lifetime_ms,
                    ptd::ClickConfig::kDefaultHoldWakeLifetimeMs, 0.0f,
                    "T-027C: NaN wake life recovers");
    }

    // Wake Density maps to SPATIAL spacing: bounded, monotone, and exactly
    // the documented baseline at 1.0.
    {
        const float at_one = ClickBubbleEffect::wake_spacing_px(1.0f);
        expect_near(at_one, ClickBubbleEffect::kBaseWakeSpacingPx, 0.01f,
                    "T-027C: density 1.0 is the baseline 12 px spacing");
        const float low = ClickBubbleEffect::wake_spacing_px(0.25f);
        const float high = ClickBubbleEffect::wake_spacing_px(2.0f);
        expect_true(low > at_one && at_one > high,
                    "T-027C: higher density means tighter spacing");
        expect_true(low <= 40.0f && high >= 5.0f,
                    "T-027C: the density mapping stays inside a usable range");
        expect_true(ClickBubbleEffect::wake_spacing_px(1.0e9f) <= 40.0f,
                    "T-027C: an absurd density is still bounded");
        expect_true(ClickBubbleEffect::wake_spacing_px(0.0f) <= 40.0f,
                    "T-027C: a zero density is still bounded");
    }

    // Density actually changes HOW MANY anchors a path produces -- it is not
    // a particle-count knob.
    {
        // 96 px stays inside the per-sample birth budget at every density, so
        // this measures the SPACING mapping and not the fast-motion cap.
        const float densities[3] = {0.25f, 1.0f, 2.0f};
        int produced[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            ptd::ClickConfig c = wake_cfg();
            c.hold_wake_density = densities[k];
            ClickBubbleEffect fx(c);
            activate(fx, 0, 0, 200 * kMs);
            fx.on_cursor_moved(move(260 * kMs, 96, 0));
            produced[k] = static_cast<int>(fx.wake_count());
            expect_true(produced[k] <= ClickBubbleEffect::kMaxWakeBirthsPerMovement,
                        "T-027C: the density mapping respects the birth budget");
        }
        expect_true(produced[2] > produced[1] && produced[1] > produced[0],
                    "T-027C: density changes emission spacing, higher = more anchors");
        expect_true(produced[0] >= 1,
                    "T-027C: even the lowest density still leaves a wake");
    }

    // Wake Life scales the detached lifetime only, and each emission keeps
    // the lifetime it was BORN with.
    {
        ptd::ClickConfig c = wake_cfg();
        c.hold_wake_lifetime_ms = 200.0f;
        ClickBubbleEffect fx(c);
        activate(fx, 0, 0, 200 * kMs);
        fx.on_cursor_moved(move(250 * kMs, 60, 0));
        expect_true(fx.wake_count() > 0, "T-027C: short-life wake is emitted");

        ptd::ClickConfig longer = c;
        longer.hold_wake_lifetime_ms = 2000.0f;
        fx.set_config(longer);
        expect_true(fx.has_live_wake(300 * kMs),
                    "T-027C: the short-life emission is alive inside its life");
        expect_true(!fx.has_live_wake(600 * kMs),
                    "T-027C: a later Wake Life change cannot stretch an old emission");
        expect_near(fx.wake_emissions().front().lifetime_ms, 200.0f, 0.0f,
                    "T-027C: the emission keeps its born lifetime");

        fx.on_cursor_moved(move(700 * kMs, 120, 0));
        expect_true(fx.has_live_wake(800 * kMs),
                    "T-027C: new emissions use the new Wake Life");
        expect_near(fx.wake_emissions().back().lifetime_ms, 2000.0f, 0.0f,
                    "T-027C: a new emission snapshots the current Wake Life");
        // Wake Life must not touch the ordinary click duration.
        expect_near(fx.config().duration_ms, wake_cfg().duration_ms, 0.0f,
                    "T-027C: Wake Life leaves the click duration alone");
    }

    // Hold Intensity scales the attached aura and is exactly the identity at
    // 1.0 relative to the accepted T-024 baseline.
    {
        const auto max_aura_alpha = [](float intensity) {
            ptd::ClickConfig c = wake_cfg();
            c.hold_intensity = intensity;
            c.style = ClickStyle::Ring;
            ClickBubbleEffect fx(c);
            fx.on_button_down(down(0, 100, 100));
            RecordSink sink;
            fx.draw(1000 * kMs, sink);   // fully charged, past the onset ramp
            float best = 0.0f;
            for (const auto& r : sink.rings) best = std::max(best, r.ring_alpha);
            return best;
        };
        const float base = max_aura_alpha(1.0f);
        const float half = max_aura_alpha(0.5f);
        const float doubled = max_aura_alpha(2.0f);
        expect_true(base > 0.0f, "T-027C: the hold aura is visible at 1.0");
        expect_near(half, base * 0.5f, 1.0e-4f,
                    "T-027C: Hold Intensity scales the attached aura linearly");
        expect_true(doubled > base, "T-027C: more intensity is a louder aura");
        expect_true(doubled <= 1.0f * 2.0f,
                    "T-027C: the aura stays inside a bounded range");
        // Minimum intensity is still visible, never an invisible dead state.
        expect_true(max_aura_alpha(ptd::ClickConfig::kMinHoldIntensity) > 0.0f,
                    "T-027C: the minimum intensity is still visible");
    }

    // Release Strength scales the charged payoff, identity at 1.0, and the
    // release stays spatially bounded at maximum.
    {
        const auto release_power = [](float strength) {
            ptd::ClickConfig c = wake_cfg();
            c.hold_release_strength = strength;
            ClickBubbleEffect fx(c);
            fx.on_button_down(down(0, 100, 100));
            fx.on_button_up(up(1000 * kMs, 100, 100));  // fully charged
            float power = 0.0f;
            for (const auto& b : fx.bubbles()) power = std::max(power, b.power);
            return power;
        };
        const float one = release_power(1.0f);
        expect_near(one, 2.2f, 1.0e-4f,
                    "T-027C: at 1.0 the release is exactly the accepted T-024 payoff");
        const float low = release_power(ptd::ClickConfig::kMinHoldReleaseStrength);
        const float high = release_power(ptd::ClickConfig::kMaxHoldReleaseStrength);
        expect_true(low < one && one < high,
                    "T-027C: Release Strength scales the payoff in both directions");
        expect_true(high <= 3.5f, "T-027C: the maximum release stays bounded");
        expect_true(low > 1.0f,
                    "T-027C: even the smallest release still pays off a hold");
    }

    // Frame cadence independence of the whole pipeline: the same gesture at
    // the same instant renders identically whether it was reached by 8 coarse
    // frames or 160 fine ones, and rendering never mutates the wake records.
    {
        ptd::ClickConfig c = wake_cfg();
        c.style = ClickStyle::Fire;
        ClickBubbleEffect a(c);
        activate(a, 0, 0, 200 * kMs);
        a.on_cursor_moved(move(300 * kMs, 120, 0));
        ClickBubbleEffect b(c);
        activate(b, 0, 0, 200 * kMs);
        b.on_cursor_moved(move(300 * kMs, 120, 0));
        expect_true(a.wake_count() == b.wake_count(),
                    "T-026Y: identical gestures produce an identical wake");

        RecordSink sa;
        RecordSink sb;
        for (int i = 1; i <= 8; ++i) {           // 8 coarse frames
            a.draw(300 * kMs + i * 50 * kMs, sa);
        }
        const std::vector<HoldWakeEmission> after_coarse = a.wake_emissions();
        for (int i = 1; i <= 80; ++i) {          // 80 fine frames, same span
            b.draw(300 * kMs + i * 5 * kMs, sb);
        }
        expect_true(a.wake_count() == b.wake_count(),
                    "T-026Y: cadence cannot change wake generation");
        bool records_untouched = a.wake_count() == after_coarse.size();
        for (std::size_t i = 0; records_untouched && i < after_coarse.size(); ++i) {
            records_untouched = same_emission(after_coarse[i], a.wake_emissions()[i]);
        }
        expect_true(records_untouched,
                    "T-026Y: rendering mutates no wake record");

        // The single finest instant of each run is the shared timeline point
        // 700 ms; rendering THAT instant must be byte-identical.
        RecordSink qa;
        RecordSink qb;
        a.draw(700 * kMs, qa);
        b.draw(700 * kMs, qb);
        expect_true(qa.dots.size() == qb.dots.size()
                        && qa.rings.size() == qb.rings.size(),
                    "T-026Y: one frame's geometry is independent of cadence");
        bool identical = qa.dots.size() == qb.dots.size();
        for (std::size_t i = 0; identical && i < qa.dots.size(); ++i) {
            identical = qa.dots[i].x == qb.dots[i].x
                     && qa.dots[i].y == qb.dots[i].y
                     && qa.dots[i].alpha == qb.dots[i].alpha;
        }
        expect_true(identical, "T-026Y: identical instants render identically");
    }
}

// ------------------------------------------- T-36 advanced motion wake

// T-36: bounds, migration-neutral defaults, the minimum-speed gate, the
// strength/size/spread multipliers, and the Turn/Stop accents. Everything is
// driven through the public API so the production path is what is proven.
void test_advanced_motion_wake() {
    // Defaults are identity and accents OFF, so a migrated config reproduces
    // the accepted T-26/T-27 appearance exactly.
    {
        const ptd::ClickConfig c;
        expect_near(c.wake_strength, 1.0f, 0.0f, "T-36: wake strength default 1.0");
        expect_near(c.wake_size, 1.0f, 0.0f, "T-36: wake size default 1.0");
        expect_near(c.wake_spread, 1.0f, 0.0f, "T-36: wake spread default 1.0");
        expect_near(c.speed_response, 1.0f, 0.0f, "T-36: speed response default 1.0");
        expect_near(c.min_motion_speed_px_s, 0.0f, 0.0f,
                    "T-36: min motion speed default 0");
        expect_true(!c.turn_accent, "T-36: Turn Accent default off");
        expect_true(!c.stop_accent, "T-36: Stop Accent default off");
    }

    // Bounds: out-of-range clamps; NaN recovers to the documented default.
    {
        ptd::ClickConfig c;
        c.wake_strength = 99.0f;
        c.wake_size = -5.0f;
        c.wake_spread = 99.0f;
        c.speed_response = -3.0f;
        c.min_motion_speed_px_s = 99999.0f;
        ptd::ClickConfig v = ptd::ClickConfig::validated(c);
        expect_near(v.wake_strength, ptd::ClickConfig::kMaxWakeStrength, 0.0f,
                    "T-36: strength clamps to max");
        expect_near(v.wake_size, ptd::ClickConfig::kMinWakeSize, 0.0f,
                    "T-36: size clamps to min");
        expect_near(v.wake_spread, ptd::ClickConfig::kMaxWakeSpread, 0.0f,
                    "T-36: spread clamps to max");
        expect_near(v.speed_response, ptd::ClickConfig::kMinSpeedResponse, 0.0f,
                    "T-36: speed response clamps to min");
        expect_near(v.min_motion_speed_px_s,
                    ptd::ClickConfig::kMaxMotionSpeedPxPerSec, 0.0f,
                    "T-36: min motion speed clamps to max");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        ptd::ClickConfig bad;
        bad.wake_strength = nan;
        bad.wake_size = nan;
        bad.wake_spread = nan;
        bad.speed_response = nan;
        bad.min_motion_speed_px_s = nan;
        ptd::ClickConfig r = ptd::ClickConfig::validated(bad);
        expect_true(std::isfinite(r.wake_strength) && std::isfinite(r.wake_size)
                        && std::isfinite(r.wake_spread)
                        && std::isfinite(r.speed_response)
                        && std::isfinite(r.min_motion_speed_px_s),
                    "T-36: NaN multipliers cannot reach the renderer");
        expect_near(r.wake_strength, 1.0f, 0.0f, "T-36: NaN strength recovers");
        expect_near(r.wake_size, 1.0f, 0.0f, "T-36: NaN size recovers");
        expect_near(r.wake_spread, 1.0f, 0.0f, "T-36: NaN spread recovers");
        expect_near(r.speed_response, 1.0f, 0.0f, "T-36: NaN speed response recovers");
        expect_near(r.min_motion_speed_px_s, 0.0f, 0.0f,
                    "T-36: NaN min motion speed recovers to 0");
    }

    // Pure gate: 0 always passes; a positive threshold suppresses below it.
    {
        expect_true(ClickBubbleEffect::wake_speed_passes_gate(1.0f, 0.0f),
                    "T-36: gate 0 passes any movement");
        expect_true(ClickBubbleEffect::wake_speed_passes_gate(500.0f, 300.0f),
                    "T-36: gate passes above threshold");
        expect_true(!ClickBubbleEffect::wake_speed_passes_gate(100.0f, 300.0f),
                    "T-36: gate suppresses below threshold");
    }

    // Pure speed response: 1.0 is the accepted energy term; 0 flattens; 2.0
    // doubles; the result is always finite and in [0,2].
    {
        const float ref = ClickBubbleEffect::kWakeReferenceSpeedPxPerSec;
        expect_near(ClickBubbleEffect::speed_response_factor(ref * 0.5f, ref, 1.0f),
                    0.5f, 1.0e-4f, "T-36: speed response 1.0 == accepted term");
        expect_near(ClickBubbleEffect::speed_response_factor(ref, ref, 0.0f),
                    0.0f, 1.0e-4f, "T-36: speed response 0 removes amplification");
        expect_near(ClickBubbleEffect::speed_response_factor(ref * 0.5f, ref, 2.0f),
                    1.0f, 1.0e-4f, "T-36: speed response 2 doubles the read");
    }

    // Pure turn angle: 0 for straight, pi/2 for a right angle, ~pi for a
    // reversal.
    {
        expect_near(ClickBubbleEffect::turn_angle_rad(1, 0, 1, 0), 0.0f, 1.0e-4f,
                    "T-36: no turn on a straight path");
        expect_near(ClickBubbleEffect::turn_angle_rad(1, 0, 0, 1), 1.5707963f,
                    1.0e-4f, "T-36: right angle is pi/2");
        expect_near(ClickBubbleEffect::turn_angle_rad(1, 0, -1, 0), 3.1415927f,
                    1.0e-4f, "T-36: reversal is pi");
    }

    // Minimum-speed gate suppresses detached wake below the threshold and
    // restores it above. A slow drag produces no emissions; a fast one does.
    {
        ptd::ClickConfig c = wake_cfg();
        c.min_motion_speed_px_s = 800.0f;
        ClickBubbleEffect fx(c);
        activate(fx, 500, 400, 200 * kMs);
        // A slow segment: 20 px in 200 ms = 100 px/s, under the gate.
        fx.on_cursor_moved(move(400 * kMs, 520, 400));
        const std::size_t slow = fx.wake_count();
        expect_true(slow == 0, "T-36: below-threshold motion emits no wake");

        // A fast segment: 120 px in 20 ms = 6000 px/s, over the gate.
        fx.on_cursor_moved(move(420 * kMs, 640, 400));
        expect_true(fx.wake_count() > 0, "T-36: above-threshold motion emits wake");
    }

    // Strength scales the emitted alpha monotonically; size scales the radius;
    // spread is identity at 1.0. Driven through the pure wake_marks() seam.
    {
        auto max_ring_alpha = [](float strength) {
            ClickBubbleEffect fx(wake_cfg());
            ptd::ClickConfig c = wake_cfg();
            c.wake_strength = strength;
            fx.set_config(c);
            HoldWakeEmission e = make_emission(ClickStyle::Ring, 100, 100,
                                               0x1234u, 0.5f);
            e.wake_strength = strength;
            WakeMark marks[16];
            const std::size_t n = marks_at(fx, e, 0.4f, marks);
            float best = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                if (marks[i].kind == WakeMarkKind::Ring) {
                    best = std::max(best, marks[i].alpha);
                }
            }
            return best;
        };
        const float a1 = max_ring_alpha(1.0f);
        const float a2 = max_ring_alpha(2.0f);
        expect_true(a1 > 0.0f, "T-36: wake is visible at strength 1.0");
        expect_true(a2 > a1, "T-36: wake strength scales alpha upward");

        auto max_ring_radius = [](float size) {
            ClickBubbleEffect fx(wake_cfg());
            HoldWakeEmission e = make_emission(ClickStyle::Ring, 100, 100,
                                               0x22u, 0.5f);
            e.wake_size = size;
            WakeMark marks[16];
            const std::size_t n = marks_at(fx, e, 0.4f, marks);
            float best = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                if (marks[i].kind == WakeMarkKind::Ring) {
                    best = std::max(best, marks[i].radius_px);
                }
            }
            return best;
        };
        expect_true(max_ring_radius(2.0f) > max_ring_radius(1.0f),
                    "T-36: wake size scales radius upward");
    }

    // Turn Accent: OFF emits none; ON emits at most one per bounded turn and
    // suppresses jitter spam via the cooldown. Stop Accent: exactly one per
    // moving -> stationary transition, re-armed by movement.
    {
        auto run_path = [](bool turn, bool stop) {
            ptd::ClickConfig c = wake_cfg();
            c.turn_accent = turn;
            c.stop_accent = stop;
            ClickBubbleEffect fx(c);
            activate(fx, 500, 400, 200 * kMs);
            int64_t t = 400 * kMs;
            // Move right fast (meaningful movement).
            fx.on_cursor_moved(move(t, 560, 400));
            t += 20 * kMs;
            // Sharp reversal (turn).
            fx.on_cursor_moved(move(t, 500, 400));
            t += 20 * kMs;
            // Continue, then stop (zero-length stationary sample).
            fx.on_cursor_moved(move(t, 440, 400));
            t += 20 * kMs;
            fx.on_cursor_moved(move(t, 440, 400));
            return fx;
        };

        const ClickBubbleEffect no_accents = run_path(false, false);
        int accent_off = 0;
        for (const auto& e : no_accents.wake_emissions()) {
            if (e.accent) ++accent_off;
        }
        expect_true(accent_off == 0, "T-36: accents OFF emit no accent records");

        const ClickBubbleEffect turn_on = run_path(true, false);
        int turn_accent = 0;
        for (const auto& e : turn_on.wake_emissions()) {
            if (e.accent) ++turn_accent;
        }
        expect_true(turn_accent >= 1, "T-36: Turn Accent fires on a reversal");

        const ClickBubbleEffect stop_on = run_path(false, true);
        int stop_accent = 0;
        for (const auto& e : stop_on.wake_emissions()) {
            if (e.accent) ++stop_accent;
        }
        expect_true(stop_accent == 1,
                    "T-36: Stop Accent fires exactly once per stop transition");

        // Every accent is world-anchored and finite, and stays put after
        // birth: rendering at a later time must not move its anchor.
        for (const auto& e : stop_on.wake_emissions()) {
            expect_finite(e.x, "T-36: accent anchor x finite");
            expect_finite(e.y, "T-36: accent anchor y finite");
        }
        {
            ClickBubbleEffect anchor_fx = run_path(false, true);
            // Capture the first accent anchor, then render several frames and
            // prove the record (and therefore its anchor) is unchanged.
            std::vector<HoldWakeEmission> before = anchor_fx.wake_emissions();
            RecordSink sink;
            for (int i = 0; i < 5; ++i) {
                anchor_fx.draw(2000 * kMs + i * 16 * kMs, sink);
            }
            bool anchors_stable = anchor_fx.wake_count() <= before.size();
            for (std::size_t i = 0; anchors_stable && i < anchor_fx.wake_count(); ++i) {
                anchors_stable = same_emission(before[i], anchor_fx.wake_emissions()[i]);
            }
            expect_true(anchors_stable,
                        "T-36: accents stay world-anchored after birth");
        }

        // Motion Wake OFF disables detached wake AND accents: the config
        // lifecycle clears the wake and no new emission can be born.
        {
            ptd::ClickConfig off = wake_cfg();
            off.hold_wake_enabled = false;
            off.turn_accent = true;
            off.stop_accent = true;
            ClickBubbleEffect fx(off);
            fx.on_button_down(down(0, 500, 400));
            fx.on_cursor_moved(move(200 * kMs, 500, 400));
            fx.on_cursor_moved(move(220 * kMs, 700, 400));
            fx.on_cursor_moved(move(240 * kMs, 500, 400));
            fx.on_cursor_moved(move(260 * kMs, 500, 400));
            expect_true(fx.wake_count() == 0,
                        "T-36: Motion Wake OFF emits no wake or accents");
        }

        // Turn-off emits no accents even with the same path.
        const ClickBubbleEffect no_turn = run_path(false, false);
        int none = 0;
        for (const auto& e : no_turn.wake_emissions()) {
            if (e.accent) ++none;
        }
        expect_true(none == 0, "T-36: accent toggles gate emission");
    }

    // Turn Accent cooldown suppresses jitter spam: a rapid back-and-forth
    // within the cooldown window cannot produce one accent per sample.
    {
        ptd::ClickConfig c = wake_cfg();
        c.turn_accent = true;
        ClickBubbleEffect fx(c);
        activate(fx, 500, 400, 200 * kMs);
        int64_t t = 400 * kMs;
        int flipped = 0;
        for (int i = 0; i < 8; ++i) {
            const int x = (i % 2 == 0) ? 700 : 500;
            fx.on_cursor_moved(move(t, x, 400));
            t += 10 * kMs;  // well inside the 250 ms cooldown
            ++flipped;
        }
        int accents = 0;
        for (const auto& e : fx.wake_emissions()) {
            if (e.accent) ++accents;
        }
        expect_true(accents <= 1,
                    "T-36: turn accent cooldown prevents jitter spam");
        (void)flipped;
    }
}

}  // namespace

int main() {
    test_input_stream_determinism();
    test_lifecycle();
    test_variable_lifetimes();
    test_activation_straddle();
    test_bounds();
    test_style_structure();
    test_dynamics();
    test_hold_controls();
    test_advanced_motion_wake();

    std::printf("test_hold_wake: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
