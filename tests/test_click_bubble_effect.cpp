// T-009 C12: pure click-bubble math tests. No Qt, no Direct2D runtime
// calls -- ClickBubbleEffect is deliberately free of both, so correctness
// does not depend on screenshots/manual testing.

#include "../src/effects/click_bubble_effect.h"
#include "../src/render/render_color.h"
#include "../src/effects/effect_palette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstddef>
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

constexpr int64_t kMs = 1'000'000; // ns per ms

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
    return s;
}

// Recording sink: captures emitted bubble render states.
class RecordSink : public ptd::ClickBubbleSink {
public:
    void reserve_bubbles_hint(int) override {}
    void add_bubble(float cx, float cy, float radius_px,
                    float outline_thickness_px,
                    float r, float g, float b,
                    float ring_alpha, float fill_alpha) override {
        items.push_back(Item{cx, cy, radius_px, outline_thickness_px,
                             r, g, b, ring_alpha, fill_alpha});
    }
    struct Item {
        float cx, cy, radius, thickness, r, g, b, ring_alpha, fill_alpha;
    };
    std::vector<Item> items;

    struct ParticleItem {
        float cx, cy, radius, r, g, b, alpha;
    };
    std::vector<ParticleItem> particles;

    void add_particle(float cx, float cy, float radius_px,
                      float r, float g, float b, float alpha) override {
        particles.push_back(ParticleItem{cx, cy, radius_px, r, g, b, alpha});
    }

    void clear() {
        items.clear();
        particles.clear();
    }
};

} // namespace

int main() {
    using ptd::ClickBubbleEffect;
    using ptd::ClickConfig;

    const ClickConfig defaults{}; // 8 -> 26 px, 250 ms, 0.85, 2.5 px

    // ---- 1: spawn creates one live bubble ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        expect_true(e.spawn(100, 200, t0), "1: spawn returns true");
        expect_true(e.active_count() == 1, "1: one active bubble");
        expect_true(e.has_live_content(t0), "1: live at birth");
    }

    // ---- 2: click coordinates preserved (from the CursorSample) ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        expect_true(e.on_button_down(down(t0, 123, 456)), "2: Down spawns");
        const ptd::ClickBubbleEffect::Bubble& b = e.bubbles().front();
        expect_near(static_cast<float>(b.x), 123.0f, 0.0f, "2: x preserved");
        expect_near(static_cast<float>(b.y), 456.0f, 0.0f, "2: y preserved");
    }

    // ---- 3/4/5: progress 0 at start, ~0.5 halfway, clamps at 1 ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(0, 0, t0);
        const auto& b = e.bubbles().front();
        expect_near(e.progress_at(b, t0), 0.0f, 1e-6f, "3: progress 0 at start");
        expect_near(e.progress_at(b, t0 + 125 * kMs), 0.5f, 1e-4f,
                    "4: progress ~0.5 halfway");
        expect_near(e.progress_at(b, t0 + 250 * kMs), 1.0f, 1e-6f,
                    "5a: progress 1 at expiry");
        expect_near(e.progress_at(b, t0 + 10'000 * kMs), 1.0f, 1e-6f,
                    "5b: progress clamps at 1");
        expect_near(e.progress_at(b, t0 - 5 * kMs), 0.0f, 1e-6f,
                    "5c: pre-birth timestamp clamps to 0");
    }

    // ---- 6/7/8: radius starts at start, reaches end, never shrinks ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(0, 0, t0);
        const auto& b = e.bubbles().front();
        expect_near(e.radius_at(b, t0), 8.0f, 1e-4f,
                    "6: radius = start_radius at birth");
        expect_near(e.radius_at(b, t0 + 250 * kMs), 26.0f, 1e-3f,
                    "7: radius reaches end_radius");
        float prev = -1.0f;
        bool monotone = true;
        for (int i = 0; i <= 40; ++i) {
            const int64_t t = t0 + static_cast<int64_t>(250.0 * i / 40.0 * kMs);
            const float r = e.radius_at(b, t);
            if (r < prev - 1e-5f) monotone = false;
            prev = r;
            expect_true(std::isfinite(r), "8: radius finite");
        }
        expect_true(monotone, "8: radius never shrinks during animation");
    }

    // ---- 9/10: opacity begins at base, reaches zero at expiry ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(0, 0, t0);
        const auto& b = e.bubbles().front();
        expect_near(e.opacity_at(b, t0), 0.85f, 1e-5f,
                    "9: opacity = base_opacity at birth");
        expect_near(e.opacity_at(b, t0 + 250 * kMs), 0.0f, 1e-6f,
                    "10a: opacity zero at expiry");
        expect_near(e.opacity_at(b, t0 + 500 * kMs), 0.0f, 1e-6f,
                    "10b: opacity stays zero after expiry");
    }

    // ---- 11: expired bubbles are removed ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(1, 2, t0);
        e.spawn(3, 4, t0 + 10 * kMs);
        expect_true(e.active_count() == 2, "11a: two active");
        const std::size_t removed = e.prune(t0 + 300 * kMs);
        expect_true(removed == 2, "11b: both expired removed");
        expect_true(e.empty(), "11c: collection empty after prune");
        expect_true(!e.has_live_content(t0 + 300 * kMs), "11d: no live content");
    }

    // ---- 12: rapid clicks create multiple distinct bubbles ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        for (int i = 0; i < 5; ++i) {
            expect_true(e.on_button_down(down(t0 + i * 30 * kMs, 10 * i, 20 * i)),
                        "12: click spawns");
        }
        expect_true(e.active_count() == 5, "12a: five coexist");
        bool distinct = true;
        for (std::size_t i = 0; i < e.bubbles().size(); ++i) {
            for (std::size_t j = i + 1; j < e.bubbles().size(); ++j) {
                const auto& a = e.bubbles()[i];
                const auto& b = e.bubbles()[j];
                if (a.x == b.x && a.y == b.y
                    && a.start_timestamp_ns == b.start_timestamp_ns) {
                    distinct = false;
                }
            }
        }
        expect_true(distinct, "12b: bubbles distinct");
        // New click does not replace an older live bubble: the first is
        // still present with its original anchor.
        expect_near(static_cast<float>(e.bubbles().front().x), 0.0f, 0.0f,
                    "12c: oldest bubble kept");
    }

    // ---- 13: active count remains bounded; oldest discarded on overflow ----
    {
        ClickBubbleEffect e(defaults);
        const std::size_t cap = ClickBubbleEffect::kMaxActiveBubbles;
        const int64_t t0 = 5'000 * kMs;
        for (std::size_t i = 0; i < cap * 2; ++i) {
            e.spawn(static_cast<float>(i), 0.0f,
                    t0 + static_cast<int64_t>(i) * kMs);
        }
        expect_true(e.active_count() == cap, "13a: count capped");
        // Oldest discarded: bubble 0 is gone, newest kept.
        expect_near(static_cast<float>(e.bubbles().front().x),
                    static_cast<float>(cap), 0.0f, "13b: oldest evicted");
        expect_near(static_cast<float>(e.bubbles().back().x),
                    static_cast<float>(cap * 2 - 1), 0.0f, "13c: newest kept");
        // Still renders bounded output.
        RecordSink sink;
        e.draw(t0 + static_cast<int64_t>(cap) * kMs, sink);
        expect_true(sink.items.size() <= cap, "13d: draw output bounded");
    }

    // ---- 14: negative virtual coordinates preserved ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        expect_true(e.on_button_down(down(t0, -1920, -1080)), "14: spawns");
        const auto& b = e.bubbles().front();
        expect_near(static_cast<float>(b.x), -1920.0f, 0.0f, "14a: x preserved");
        expect_near(static_cast<float>(b.y), -1080.0f, 0.0f, "14b: y preserved");
        RecordSink sink;
        e.draw(t0 + 50 * kMs, sink);
        expect_true(sink.items.size() == 1, "14c: emitted");
        expect_near(sink.items[0].cx, -1920.0f, 0.0f, "14d: emit x preserved");
        expect_near(sink.items[0].cy, -1080.0f, 0.0f, "14e: emit y preserved");
    }

    // ---- 15: same elapsed time -> same result regardless of frame cadence ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(7, 9, t0);
        const auto& b = e.bubbles().front();
        const int64_t t_end = t0 + 250 * kMs;

        // Direct evaluation at the end of the animation.
        const float r_direct = e.radius_at(b, t_end);
        const float o_direct = e.opacity_at(b, t_end);

        // Stepped cadence: many intermediate frames at uneven intervals,
        // interleaved with draws and prunes, then evaluate at the same
        // wall-clock instant.
        int64_t t = t0;
        while (t < t_end) {
            RecordSink sink;
            e.draw(t, sink);
            e.prune(t);
            t += 17 * kMs; // ~59 Hz-ish uneven steps
        }
        const float r_stepped = e.radius_at(b, t_end);
        const float o_stepped = e.opacity_at(b, t_end);
        expect_near(r_direct, r_stepped, 0.0f, "15a: radius cadence-free");
        expect_near(o_direct, o_stepped, 0.0f, "15b: opacity cadence-free");

        // draw() is pure: state untouched by any number of draws.
        const std::size_t count_before = e.active_count();
        for (int i = 0; i < 1000; ++i) {
            RecordSink sink;
            e.draw(t0 + (i % 400) * kMs, sink);
        }
        expect_true(e.active_count() == count_before, "15c: draw non-mutating");
    }

    // ---- 16: malformed config safely clamped ----
    {
        ClickConfig bad{};
        bad.start_radius_px = -5.0f;
        bad.end_radius_px = -1.0f;
        bad.duration_ms = 0.0f;
        bad.base_opacity = 5.0f;
        bad.outline_thickness_px = -3.0f;
        const ClickConfig v = ClickConfig::validated(bad);
        expect_near(v.start_radius_px, 0.0f, 1e-5f, "16a: start radius >= 0");
        expect_true(v.end_radius_px >= v.start_radius_px, "16b: end >= start");
        expect_near(v.duration_ms, 50.0f, 1e-5f, "16c: duration floor 50 ms");
        expect_true(v.base_opacity <= 1.0f, "16d: opacity <= 1");
        expect_true(v.outline_thickness_px >= 0.5f, "16e: thickness floor");

        // Inverted radii: end forced to start -> radius constant, no shrink.
        ClickConfig inv{};
        inv.start_radius_px = 30.0f;
        inv.end_radius_px = 10.0f;
        ClickBubbleEffect e(inv);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(0, 0, t0);
        const auto& b = e.bubbles().front();
        expect_near(e.radius_at(b, t0), 30.0f, 1e-4f, "16f: start honored");
        expect_near(e.radius_at(b, t0 + 500 * kMs), 30.0f, 1e-4f,
                    "16g: clamped end radius, no shrink");
        // Degenerate duration floor respected in progress math.
        expect_true(e.progress_at(b, t0 + 25 * kMs) < 1.0f,
                    "16h: clamped duration in effect");
    }

    // ---- 17: disabled click effect spawns nothing ----
    {
        ClickConfig off{};
        off.enabled = false;
        ClickBubbleEffect e(off);
        const int64_t t0 = 5'000 * kMs;
        expect_true(!e.on_button_down(down(t0, 50, 60)), "17a: Down ignored");
        expect_true(!e.on_button_down(up(t0, 50, 60)), "17b: Up ignored");
        expect_true(e.empty(), "17c: nothing spawned");
        expect_true(!e.has_live_content(t0), "17d: no live content");
    }

    // ---- normalized-input contract: only Down spawns ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        expect_true(!e.on_button_down(up(t0, 1, 1)), "up: no spawn");
        expect_true(!e.on_button_down(move(t0, 2, 2)), "move: no spawn");
        expect_true(e.empty(), "up/move: no bubbles");
        expect_true(e.on_button_down(down(t0 + kMs, 3, 3)), "down: spawns");
        // Left/right/middle Down all spawn while their triggers are on
        // (Phase J defaults: all three enabled).
        ptd::CursorSample r = down(t0 + 2 * kMs, 4, 4);
        r.button = ptd::MouseButton::Right;
        expect_true(e.on_button_down(r), "right-down: spawns");
        ptd::CursorSample m = down(t0 + 3 * kMs, 5, 5);
        m.button = ptd::MouseButton::Middle;
        expect_true(e.on_button_down(m), "middle-down: spawns");
    }

    // ---- Phase J: trigger-button contract ----
    {
        // Right-only config: left and middle Down must NOT spawn.
        ClickConfig c{};
        c.trigger_left = false;
        c.trigger_right = true;
        c.trigger_middle = false;
        ClickBubbleEffect e(c);
        const int64_t t0 = 5'000 * kMs;
        expect_true(!e.on_button_down(down(t0, 1, 1)),
                    "J: left disabled, no spawn");
        ptd::CursorSample r = down(t0 + kMs, 2, 2);
        r.button = ptd::MouseButton::Right;
        expect_true(e.on_button_down(r), "J: right enabled, spawns");
        ptd::CursorSample m = down(t0 + 2 * kMs, 3, 3);
        m.button = ptd::MouseButton::Middle;
        expect_true(!e.on_button_down(m),
                    "J: middle disabled, no spawn");
        expect_true(e.active_count() == 1, "J: exactly one bubble");
    }

    // ---- Phase J: easing variants ----
    {
        // Curve endpoints identical across easing; midpoints differ.
        expect_near(ClickBubbleEffect::apply_easing(ptd::ClickEasing::Linear, 0.0f), 0.0f, 1e-6f, "J: linear(0)");
        expect_near(ClickBubbleEffect::apply_easing(ptd::ClickEasing::Linear, 1.0f), 1.0f, 1e-6f, "J: linear(1)");
        expect_near(ClickBubbleEffect::apply_easing(ptd::ClickEasing::Linear, 0.5f), 0.5f, 1e-6f, "J: linear mid");
        expect_near(ClickBubbleEffect::apply_easing(ptd::ClickEasing::Smooth, 0.5f), 0.5f, 1e-6f, "J: smooth mid");
        expect_true(ClickBubbleEffect::apply_easing(ptd::ClickEasing::EaseOut, 0.5f) > 0.5f, "J: easeout front-loaded");
        // All bounded 0..1.
        for (ptd::ClickEasing ez : {ptd::ClickEasing::Linear, ptd::ClickEasing::Smooth,
                                    ptd::ClickEasing::EaseOut}) {
            for (float p = -0.5f; p <= 1.5f; p += 0.01f) {
                const float f = ClickBubbleEffect::apply_easing(ez, p);
                expect_true(f >= 0.0f && f <= 1.0f, "J: easing bounded");
            }
        }
        // Radius at birth/expiry identical for every easing; monotone for
        // every easing.
        for (ptd::ClickEasing ez : {ptd::ClickEasing::Linear, ptd::ClickEasing::Smooth,
                                    ptd::ClickEasing::EaseOut}) {
            ClickConfig c{};
            c.easing = ez;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(0, 0, t0);
            const auto& b = e.bubbles().front();
            expect_near(e.radius_at(b, t0), 8.0f, 1e-4f, "J: birth radius");
            expect_near(e.radius_at(b, t0 + 250 * kMs), 26.0f, 1e-3f, "J: expiry radius");
            float prev = -1.0f;
            bool mono = true;
            for (int i = 0; i <= 40; ++i) {
                const float rr = e.radius_at(b, t0 + static_cast<int64_t>(250.0 * i / 40.0 * kMs));
                if (rr < prev - 1e-5f) mono = false;
                prev = rr;
            }
            expect_true(mono, "J: radius monotone for easing");
        }
    }

    // ---- Phase K: fill-opacity contract ----
    {
        // Fill clamps to [0,1].
        ClickConfig bad{};
        bad.fill_opacity = 5.0f;
        expect_near(ClickConfig::validated(bad).fill_opacity, 1.0f, 1e-5f,
                    "K: fill opacity clamped high");
        bad.fill_opacity = -1.0f;
        expect_near(ClickConfig::validated(bad).fill_opacity, 0.0f, 1e-5f,
                    "K: fill opacity clamped low");
        // Effect emits ring and fill alphas separately.
        ClickConfig c{};
        c.fill_opacity = 0.4f;
        ClickBubbleEffect e(c);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(7, 8, t0);
        const auto& b = e.bubbles().front();
        expect_near(e.fill_alpha_at(b, t0), 0.4f, 1e-5f, "K: fill at birth");
        expect_near(e.fill_alpha_at(b, t0 + 250 * kMs), 0.0f, 1e-6f, "K: fill zero at expiry");
        RecordSink sink;
        e.draw(t0 + 125 * kMs, sink);
        expect_true(sink.items.size() == 1, "K: emitted");
        expect_near(sink.items[0].fill_alpha, 0.2f, 1e-5f, "K: fill alpha mid-life");
        expect_true(sink.items[0].fill_alpha < sink.items[0].ring_alpha,
                    "K: fill quieter than ring");
        // Fill 0 disables the disc but not the ring.
        ClickConfig zero_fill{};
        zero_fill.fill_opacity = 0.0f;
        ClickBubbleEffect ez(zero_fill);
        ez.spawn(1, 1, t0);
        RecordSink sink2;
        ez.draw(t0 + 50 * kMs, sink2);
        expect_true(sink2.items.size() == 1, "K: ring-only still emitted");
        expect_near(sink2.items[0].fill_alpha, 0.0f, 1e-6f, "K: zero fill");
        expect_true(sink2.items[0].ring_alpha > 0.0f, "K: ring alive");
    }

    // ---- draw() render-state sanity ----
    {
        ClickBubbleEffect e(defaults);
        const int64_t t0 = 5'000 * kMs;
        e.spawn(10, 20, t0);
        RecordSink sink;
        e.draw(t0 + 125 * kMs, sink);
        expect_true(sink.items.size() == 1, "draw: one live bubble emitted");
        expect_near(sink.items[0].cx, 10.0f, 0.0f, "draw: cx");
        expect_near(sink.items[0].cy, 20.0f, 0.0f, "draw: cy");
        expect_true(sink.items[0].radius > 8.0f && sink.items[0].radius < 26.0f,
                    "draw: radius between bounds");
        expect_true(std::fabs(sink.items[0].r) < 1e-5f
                    && std::fabs(sink.items[0].g - 200.0f) < 1e-3f
                    && std::fabs(sink.items[0].b - 255.0f) < 1e-3f,
                    "draw: default cyan color channels");
        expect_true(sink.items[0].ring_alpha > 0.0f && sink.items[0].ring_alpha <= 0.85f,
                    "draw: ring alpha within range");
    }

    // ---- T-017 Click Style Math Regression Suite ----
    {
        // 1. Ring: exact MVP 04 compatibility (1 bubble, 0 particles, ring + fill alpha)
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::Ring;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(100.0f, 200.0f, t0);
            RecordSink sink;
            e.draw(t0 + 100 * kMs, sink);
            expect_true(sink.items.size() == 1, "T-017 Ring: exactly 1 bubble emitted");
            expect_true(sink.particles.empty(), "T-017 Ring: 0 particles emitted");
            expect_near(sink.items[0].cx, 100.0f, 1e-4f, "T-017 Ring: cx");
            expect_near(sink.items[0].cy, 200.0f, 1e-4f, "T-017 Ring: cy");
            expect_true(sink.items[0].ring_alpha > 0.0f && sink.items[0].fill_alpha > 0.0f,
                        "T-017 Ring: both ring and fill alphas positive");
        }

        // 2. Double Ring: exactly 2 rings, inner at 62% radius, fill_alpha == 0
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::DoubleRing;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(50.0f, 60.0f, t0);
            RecordSink sink;
            e.draw(t0 + 100 * kMs, sink);
            expect_true(sink.items.size() == 2, "T-017 DoubleRing: exactly 2 bubbles emitted");
            expect_true(sink.particles.empty(), "T-017 DoubleRing: 0 particles emitted");
            expect_near(sink.items[0].cx, 50.0f, 1e-4f, "T-017 DoubleRing: outer cx");
            expect_near(sink.items[1].cx, 50.0f, 1e-4f, "T-017 DoubleRing: inner cx");
            expect_near(sink.items[1].radius, sink.items[0].radius * 0.62f, 1e-3f,
                        "T-017 DoubleRing: inner radius 62% of outer");
            expect_near(sink.items[0].fill_alpha, 0.0f, 1e-6f, "T-017 DoubleRing: outer fill 0");
            expect_near(sink.items[1].fill_alpha, 0.0f, 1e-6f, "T-017 DoubleRing: inner fill 0");
            expect_near(sink.items[1].ring_alpha, sink.items[0].ring_alpha * 0.7f, 1e-4f,
                        "T-017 DoubleRing: inner alpha 70% of outer");
        }

        // 3. Ripple: thin pure ring (60% thickness), fill_alpha == 0
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::Ripple;
            c.outline_thickness_px = 4.0f;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(30.0f, 40.0f, t0);
            RecordSink sink;
            e.draw(t0 + 100 * kMs, sink);
            expect_true(sink.items.size() == 1, "T-017 Ripple: exactly 1 bubble emitted");
            expect_true(sink.particles.empty(), "T-017 Ripple: 0 particles emitted");
            expect_near(sink.items[0].thickness, 4.0f * 0.6f, 1e-4f, "T-017 Ripple: 60% thickness");
            expect_near(sink.items[0].fill_alpha, 0.0f, 1e-6f, "T-017 Ripple: no fill");
        }

        // 4. Burst: 1 subtle ring + monotonic non-decreasing outward particles
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::Burst;
            c.particle_amount = 8;
            c.end_radius_px = 30.0f;
            c.duration_ms = 250.0f;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(100.0f, 100.0f, t0);

            RecordSink sink;
            e.draw(t0 + 50 * kMs, sink);
            expect_true(sink.items.size() == 1, "T-017 Burst: 1 subtle ring emitted");
            expect_true(sink.particles.size() == 8, "T-017 Burst: 8 particles emitted");

            // Monotonic non-decreasing particle travel check across progress
            for (int p_idx = 0; p_idx < 8; ++p_idx) {
                float prev_dist = -1.0f;
                bool monotonic = true;
                for (int step = 0; step <= 20; ++step) {
                    const int64_t t = t0 + static_cast<int64_t>(250.0f * step / 20.0f * kMs);
                    RecordSink step_sink;
                    e.draw(t, step_sink);
                    if (step_sink.particles.size() == 8) {
                        const auto& p = step_sink.particles[p_idx];
                        expect_true(std::isfinite(p.cx) && std::isfinite(p.cy)
                                    && std::isfinite(p.radius) && std::isfinite(p.alpha),
                                    "T-017 Burst: particle values finite");
                        const float dx = p.cx - 100.0f;
                        const float dy = p.cy - 100.0f;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist < prev_dist - 1e-4f) {
                            monotonic = false;
                        }
                        prev_dist = dist;
                    }
                }
                expect_true(monotonic, "T-017 Burst: particle radial distance is monotonic non-decreasing");
            }
        }

        // 5. SparkBurst: 0 rings + deterministic monotonic outward jittered particles
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::SparkBurst;
            c.particle_amount = 12;
            c.end_radius_px = 40.0f;
            c.duration_ms = 250.0f;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(200.0f, 300.0f, t0);

            RecordSink sink1, sink2;
            e.draw(t0 + 100 * kMs, sink1);
            e.draw(t0 + 100 * kMs, sink2);
            expect_true(sink1.items.empty(), "T-017 SparkBurst: 0 rings emitted");
            expect_true(sink1.particles.size() == 12, "T-017 SparkBurst: 12 particles emitted");
            // Deterministic: identical calls yield identical results
            for (std::size_t i = 0; i < sink1.particles.size(); ++i) {
                expect_near(sink1.particles[i].cx, sink2.particles[i].cx, 1e-5f, "T-017 SparkBurst: deterministic cx");
                expect_near(sink1.particles[i].cy, sink2.particles[i].cy, 1e-5f, "T-017 SparkBurst: deterministic cy");
                expect_near(sink1.particles[i].alpha, sink2.particles[i].alpha, 1e-5f, "T-017 SparkBurst: deterministic alpha");
            }

            // Monotonic non-decreasing travel for each jittered particle
            for (int p_idx = 0; p_idx < 12; ++p_idx) {
                float prev_dist = -1.0f;
                bool monotonic = true;
                for (int step = 0; step <= 20; ++step) {
                    const int64_t t = t0 + static_cast<int64_t>(250.0f * step / 20.0f * kMs);
                    RecordSink step_sink;
                    e.draw(t, step_sink);
                    if (step_sink.particles.size() == 12) {
                        const auto& p = step_sink.particles[p_idx];
                        expect_true(std::isfinite(p.cx) && std::isfinite(p.cy)
                                    && std::isfinite(p.radius) && std::isfinite(p.alpha),
                                    "T-017 SparkBurst: values finite");
                        const float dx = p.cx - 200.0f;
                        const float dy = p.cy - 300.0f;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist < prev_dist - 1e-4f) {
                            monotonic = false;
                        }
                        prev_dist = dist;
                    }
                }
                expect_true(monotonic, "T-017 SparkBurst: particle distance monotonic non-decreasing");
            }
        }

        // 6. SoftFlash: flash disc, no ring outline (ring_alpha == 0)
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::SoftFlash;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(10.0f, 20.0f, t0);
            RecordSink sink;
            e.draw(t0 + 50 * kMs, sink);
            expect_true(sink.items.size() == 1, "T-017 SoftFlash: exactly 1 item emitted");
            expect_true(sink.particles.empty(), "T-017 SoftFlash: 0 particles emitted");
            expect_near(sink.items[0].ring_alpha, 0.0f, 1e-6f, "T-017 SoftFlash: ring outline alpha == 0");
            expect_true(sink.items[0].fill_alpha > 0.0f, "T-017 SoftFlash: fill flash active");
        }

        // 7. DotRing: center dot + ring
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::DotRing;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(45.0f, 75.0f, t0);
            RecordSink sink;
            e.draw(t0 + 50 * kMs, sink);
            expect_true(sink.items.size() == 1, "T-017 DotRing: 1 ring emitted");
            expect_true(sink.particles.size() == 1, "T-017 DotRing: 1 center dot emitted");
            expect_near(sink.particles[0].cx, 45.0f, 1e-4f, "T-017 DotRing: dot cx");
            expect_near(sink.particles[0].cy, 75.0f, 1e-4f, "T-017 DotRing: dot cy");
            expect_near(sink.particles[0].radius, 3.0f, 1e-4f, "T-017 DotRing: dot radius 3px");
        }

        // 8. Zero particles safety
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::Burst;
            c.particle_amount = 0;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(0.0f, 0.0f, t0);
            RecordSink sink;
            e.draw(t0 + 50 * kMs, sink);
            expect_true(sink.particles.empty(), "T-017 Zero particles: safe empty emission");
        }

        // 9. Negative virtual screen coordinates
        {
            ClickConfig c{};
            c.style = ptd::ClickStyle::SparkBurst;
            c.particle_amount = 6;
            ClickBubbleEffect e(c);
            const int64_t t0 = 5'000 * kMs;
            e.spawn(-1500.0f, -800.0f, t0);
            RecordSink sink;
            e.draw(t0 + 50 * kMs, sink);
            expect_true(sink.particles.size() == 6, "T-017 Negative coords: 6 particles emitted");
            for (const auto& p : sink.particles) {
                expect_true(p.cx < -1400.0f && p.cx > -1600.0f, "T-017 Negative coords: cx in expected bounds");
                expect_true(p.cy < -700.0f && p.cy > -900.0f, "T-017 Negative coords: cy in expected bounds");
            }
        }

        // T-017R1: Click RGB color normalization contract regressions
        {
            // 1. Channel normalization basic and boundary values
            expect_near(ptd::normalize_color_channel(0.0f), 0.0f, 1e-6f, "T-017R1 0 -> 0.0");
            expect_near(ptd::normalize_color_channel(255.0f), 1.0f, 1e-6f, "T-017R1 255 -> 1.0");
            expect_near(ptd::normalize_color_channel(128.0f), 0.50196f, 1e-4f, "T-017R1 128 -> ~0.50196");
            expect_near(ptd::normalize_color_channel(200.0f), 0.78431f, 1e-4f, "T-017R1 200 -> ~0.78431");

            // 2. Defensive clamping against malformed inputs
            expect_near(ptd::normalize_color_channel(-10.0f), 0.0f, 1e-6f, "T-017R1 negative clamped to 0.0");
            expect_near(ptd::normalize_color_channel(-0.001f), 0.0f, 1e-6f, "T-017R1 sub-zero clamped to 0.0");
            expect_near(ptd::normalize_color_channel(300.0f), 1.0f, 1e-6f, "T-017R1 >255 clamped to 1.0");
            expect_near(ptd::normalize_color_channel(9999.0f), 1.0f, 1e-6f, "T-017R1 large clamped to 1.0");
            expect_near(ptd::normalize_color_channel(std::numeric_limits<float>::quiet_NaN()), 0.0f, 1e-6f, "T-017R1 NaN clamped to 0.0");

            // 3. Representative custom click RGB: 15, 25, 35
            {
                const auto custom = ptd::normalize_click_rgb(15.0f, 25.0f, 35.0f);
                expect_near(custom.r, 15.0f / 255.0f, 1e-5f, "T-017R1 custom R ratio");
                expect_near(custom.g, 25.0f / 255.0f, 1e-5f, "T-017R1 custom G ratio");
                expect_near(custom.b, 35.0f / 255.0f, 1e-5f, "T-017R1 custom B ratio");
                expect_true(custom.r < custom.g && custom.g < custom.b, "T-017R1 custom strictly monotonic R < G < B");
                expect_near(custom.g / custom.r, 25.0f / 15.0f, 1e-4f, "T-017R1 custom G/R ratio preserved");
                expect_near(custom.b / custom.r, 35.0f / 15.0f, 1e-4f, "T-017R1 custom B/R ratio preserved");
            }

            // 4. All 14 canonical click palette colors through normalization contract
            for (const auto& pc : ptd::kEffectPalette) {
                const auto n = ptd::normalize_click_rgb(static_cast<float>(pc.r),
                                                        static_cast<float>(pc.g),
                                                        static_cast<float>(pc.b));
                expect_near(n.r, static_cast<float>(pc.r) / 255.0f, 1e-5f, "T-017R1 palette R exact");
                expect_near(n.g, static_cast<float>(pc.g) / 255.0f, 1e-5f, "T-017R1 palette G exact");
                expect_near(n.b, static_cast<float>(pc.b) / 255.0f, 1e-5f, "T-017R1 palette B exact");
                expect_true(n.r >= 0.0f && n.r <= 1.0f, "T-017R1 palette R in [0,1]");
                expect_true(n.g >= 0.0f && n.g <= 1.0f, "T-017R1 palette G in [0,1]");
                expect_true(n.b >= 0.0f && n.b <= 1.0f, "T-017R1 palette B in [0,1]");
            }

            // 5. Specific canonical palette color semantic checks
            // Red does not become white (Red: 255, 64, 64)
            {
                const auto red = ptd::normalize_click_rgb(255.0f, 64.0f, 64.0f);
                expect_near(red.r, 1.0f, 1e-5f, "T-017R1 Red R == 1.0");
                expect_near(red.g, 64.0f / 255.0f, 1e-5f, "T-017R1 Red G ~ 0.251");
                expect_near(red.b, 64.0f / 255.0f, 1e-5f, "T-017R1 Red B ~ 0.251");
                expect_true(red.g < 0.3f && red.b < 0.3f, "T-017R1 Red does not become white");
            }
            // Orange preserves green and blue below red (Orange: 255, 128, 32)
            {
                const auto orange = ptd::normalize_click_rgb(255.0f, 128.0f, 32.0f);
                expect_near(orange.r, 1.0f, 1e-5f, "T-017R1 Orange R == 1.0");
                expect_true(orange.g < orange.r, "T-017R1 Orange green below red");
                expect_true(orange.b < orange.g, "T-017R1 Orange blue below green");
                expect_true(orange.b < orange.r, "T-017R1 Orange blue below red");
            }
            // Green preserves its intended channel proportions (Green: 64, 220, 96)
            {
                const auto green = ptd::normalize_click_rgb(64.0f, 220.0f, 96.0f);
                expect_true(green.g > green.r, "T-017R1 Green G > R");
                expect_true(green.g > green.b, "T-017R1 Green G > B");
                expect_true(green.b > green.r, "T-017R1 Green B > R");
                expect_near(green.g, 220.0f / 255.0f, 1e-5f, "T-017R1 Green G ~ 0.863");
            }
            // Cyan remains cyan (Cyan: 0, 200, 255)
            {
                const auto cyan = ptd::normalize_click_rgb(0.0f, 200.0f, 255.0f);
                expect_near(cyan.r, 0.0f, 1e-6f, "T-017R1 Cyan R == 0");
                expect_near(cyan.b, 1.0f, 1e-5f, "T-017R1 Cyan B == 1.0");
                expect_near(cyan.g, 200.0f / 255.0f, 1e-5f, "T-017R1 Cyan G ~ 0.784");
                expect_true(cyan.b > cyan.g && cyan.g > cyan.r, "T-017R1 Cyan remains cyan");
            }
            // Violet preserves distinct R/G/B proportions (Violet: 150, 80, 255)
            {
                const auto violet = ptd::normalize_click_rgb(150.0f, 80.0f, 255.0f);
                expect_true(violet.b > violet.r, "T-017R1 Violet B > R");
                expect_true(violet.r > violet.g, "T-017R1 Violet R > G");
                expect_near(violet.b, 1.0f, 1e-5f, "T-017R1 Violet B == 1.0");
                expect_near(violet.r, 150.0f / 255.0f, 1e-5f, "T-017R1 Violet R ~ 0.588");
                expect_near(violet.g, 80.0f / 255.0f, 1e-5f, "T-017R1 Violet G ~ 0.314");
            }
            // Pink preserves distinct R/G/B proportions (Pink: 255, 120, 170)
            {
                const auto pink = ptd::normalize_click_rgb(255.0f, 120.0f, 170.0f);
                expect_near(pink.r, 1.0f, 1e-5f, "T-017R1 Pink R == 1.0");
                expect_true(pink.r > pink.b, "T-017R1 Pink R > B");
                expect_true(pink.b > pink.g, "T-017R1 Pink B > G");
                expect_near(pink.b, 170.0f / 255.0f, 1e-5f, "T-017R1 Pink B ~ 0.667");
                expect_near(pink.g, 120.0f / 255.0f, 1e-5f, "T-017R1 Pink G ~ 0.471");
            }

            // 6. Production renderer simulation: draw() emits to sink, and sink normalizes via normalize_click_rgb
            {
                class RendererContractSink : public ptd::ClickBubbleSink {
                public:
                    struct Bubble {
                        float cx, cy, radius, thickness;
                        ptd::NormalizedRgb color;
                        float ring_alpha, fill_alpha;
                    };
                    struct Particle {
                        float cx, cy, radius;
                        ptd::NormalizedRgb color;
                        float alpha;
                    };
                    std::vector<Bubble> bubbles;
                    std::vector<Particle> particles;

                    void reserve_bubbles_hint(int) override {}
                    void add_bubble(float cx, float cy, float radius_px,
                                    float outline_thickness_px,
                                    float r, float g, float b,
                                    float ring_alpha, float fill_alpha) override {
                        bubbles.push_back({cx, cy, radius_px, outline_thickness_px,
                                           ptd::normalize_click_rgb(r, g, b),
                                           ring_alpha, fill_alpha});
                    }
                    void add_particle(float cx, float cy, float radius_px,
                                      float r, float g, float b, float alpha) override {
                        particles.push_back({cx, cy, radius_px,
                                             ptd::normalize_click_rgb(r, g, b),
                                             alpha});
                    }
                };

                // Test Burst style with custom color 15, 25, 35
                ClickConfig cfg{};
                cfg.style = ptd::ClickStyle::Burst;
                cfg.particle_amount = 8;
                cfg.color_r = 15;
                cfg.color_g = 25;
                cfg.color_b = 35;
                ClickBubbleEffect effect(cfg);
                const int64_t t0 = 1'000 * kMs;
                effect.spawn(100.0f, 200.0f, t0);

                RendererContractSink sink;
                effect.draw(t0 + 50 * kMs, sink);

                expect_true(sink.bubbles.size() == 1, "T-017R1 sink: 1 bubble emitted");
                expect_true(sink.particles.size() == 8, "T-017R1 sink: 8 particles emitted");

                expect_near(sink.bubbles[0].color.r, 15.0f / 255.0f, 1e-5f, "T-017R1 sink bubble R normalized");
                expect_near(sink.bubbles[0].color.g, 25.0f / 255.0f, 1e-5f, "T-017R1 sink bubble G normalized");
                expect_near(sink.bubbles[0].color.b, 35.0f / 255.0f, 1e-5f, "T-017R1 sink bubble B normalized");

                for (const auto& p : sink.particles) {
                    expect_near(p.color.r, 15.0f / 255.0f, 1e-5f, "T-017R1 sink particle R normalized");
                    expect_near(p.color.g, 25.0f / 255.0f, 1e-5f, "T-017R1 sink particle G normalized");
                    expect_near(p.color.b, 35.0f / 255.0f, 1e-5f, "T-017R1 sink particle B normalized");
                }
            }
        }
    }

    // ================= T-022 Elemental Click VFX =================
    // The four elements must be recognizable by MOTION, not by the button
    // that selected them, so every check below pins a motion signature:
    // Air turns, Fire rises, Water repeats, Earth falls.
    {
        using ptd::ClickStyle;
        using ptd::ElementTintTarget;

        auto elemental_cfg = [&](ClickStyle style, float tint, int particles) {
            ClickConfig c{};
            c.style = style;
            c.element_tint = tint;
            c.particle_amount = static_cast<uint8_t>(particles);
            c.color_r = 0; c.color_g = 200; c.color_b = 255;  // cyan default
            return ClickConfig::validated(c);
        };

        // ---- T-022 A: tint blend endpoints, cooling and distinctness ----
        {
            // strength 0 -> exactly the user's colour, whatever the element.
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                const ElementTintTarget c =
                    ClickBubbleEffect::elemental_color(st, 0.0f, 200.0f, 255.0f, 0.0f, 0.0f);
                expect_near(c.r, 0.0f, 1e-4f, "T-022 A: tint 0 keeps user R");
                expect_near(c.g, 200.0f, 1e-4f, "T-022 A: tint 0 keeps user G");
                expect_near(c.b, 255.0f, 1e-4f, "T-022 A: tint 0 keeps user B");
            }
            // strength 1 -> exactly the element target.
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                const ElementTintTarget want = ClickBubbleEffect::element_target(st, 0.0f);
                const ElementTintTarget got =
                    ClickBubbleEffect::elemental_color(st, 0.0f, 200.0f, 255.0f, 1.0f, 0.0f);
                expect_near(got.r, want.r, 1e-4f, "T-022 A: tint 1 reaches target R");
                expect_near(got.g, want.g, 1e-4f, "T-022 A: tint 1 reaches target G");
                expect_near(got.b, want.b, 1e-4f, "T-022 A: tint 1 reaches target B");
            }
            // Half strength lands exactly halfway (linear blend, no surprises).
            {
                const ElementTintTarget t = ClickBubbleEffect::element_target(ClickStyle::Water, 0.0f);
                const ElementTintTarget got =
                    ClickBubbleEffect::elemental_color(ClickStyle::Water, 0.0f, 200.0f, 255.0f, 0.5f, 0.0f);
                expect_near(got.r, 0.5f * t.r, 1e-4f, "T-022 A: half blend R");
                expect_near(got.g, 0.5f * (200.0f + t.g), 1e-4f, "T-022 A: half blend G");
                expect_near(got.b, 0.5f * (255.0f + t.b), 1e-4f, "T-022 A: half blend B");
            }
            // A non-elemental style ignores element_tint entirely.
            {
                const ElementTintTarget got =
                    ClickBubbleEffect::elemental_color(ClickStyle::Ring, 1.0f, 2.0f, 3.0f, 1.0f, 1.0f);
                expect_near(got.r, 1.0f, 0.0f, "T-022 A: Ring ignores tint R");
                expect_near(got.g, 2.0f, 0.0f, "T-022 A: Ring ignores tint G");
                expect_near(got.b, 3.0f, 0.0f, "T-022 A: Ring ignores tint B");
            }
            // Fire cools with age: green and blue both fall, red stays hot.
            {
                const ElementTintTarget hot = ClickBubbleEffect::element_target(ClickStyle::Fire, 0.0f);
                const ElementTintTarget cold = ClickBubbleEffect::element_target(ClickStyle::Fire, 1.0f);
                expect_true(cold.g < hot.g, "T-022 A: Fire cools in G");
                expect_true(cold.b < hot.b, "T-022 A: Fire cools in B");
                expect_near(cold.r, hot.r, 1e-4f, "T-022 A: Fire keeps R hot");
            }
            // The four targets are pairwise distinct (no two elements read alike).
            {
                const ClickStyle all[4] = {ClickStyle::Air, ClickStyle::Fire,
                                           ClickStyle::Water, ClickStyle::Earth};
                for (int i = 0; i < 4; ++i) {
                    for (int j = i + 1; j < 4; ++j) {
                        const ElementTintTarget a = ClickBubbleEffect::element_target(all[i], 0.0f);
                        const ElementTintTarget b = ClickBubbleEffect::element_target(all[j], 0.0f);
                        const float d = std::fabs(a.r - b.r) + std::fabs(a.g - b.g)
                                      + std::fabs(a.b - b.b);
                        expect_true(d > 60.0f, "T-022 A: element targets are distinct");
                    }
                }
            }
            // is_elemental covers exactly the four new styles.
            expect_true(ClickBubbleEffect::is_elemental(ClickStyle::Air), "T-022 A: Air elemental");
            expect_true(ClickBubbleEffect::is_elemental(ClickStyle::Fire), "T-022 A: Fire elemental");
            expect_true(ClickBubbleEffect::is_elemental(ClickStyle::Water), "T-022 A: Water elemental");
            expect_true(ClickBubbleEffect::is_elemental(ClickStyle::Earth), "T-022 A: Earth elemental");
            for (const ClickStyle st : {ClickStyle::Ring, ClickStyle::DoubleRing,
                                        ClickStyle::Ripple, ClickStyle::Burst,
                                        ClickStyle::SparkBurst, ClickStyle::SoftFlash,
                                        ClickStyle::DotRing}) {
                expect_true(!ClickBubbleEffect::is_elemental(st),
                            "T-022 A: T-017 styles stay non-elemental");
            }
        }

        // ---- T-022 B: per-element particle budgets ----
        {
            expect_true(ClickBubbleEffect::elemental_particle_count(ClickStyle::Air, 8) == 8,
                        "T-022 B: Air uses the configured amount");
            expect_true(ClickBubbleEffect::elemental_particle_count(ClickStyle::Fire, 8) == 8,
                        "T-022 B: Fire uses the configured amount");
            expect_true(ClickBubbleEffect::elemental_particle_count(ClickStyle::Water, 8) == 2,
                        "T-022 B: Water droplets are an accent (amount/3)");
            expect_true(ClickBubbleEffect::elemental_particle_count(ClickStyle::Earth, 8) == 4,
                        "T-022 B: Earth throws fewer, heavier chunks");
            expect_true(ClickBubbleEffect::elemental_particle_count(ClickStyle::Earth, 1) == 3,
                        "T-022 B: Earth debris never drops below 3 while enabled");
            // Particles off is absolute for every element.
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                expect_true(ClickBubbleEffect::elemental_particle_count(st, 0) == 0,
                            "T-022 B: amount 0 emits no particles");
            }
            // The configured maximum is never exceeded.
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                const int n = ClickBubbleEffect::elemental_particle_count(
                    st, ClickConfig::kMaxParticleAmount);
                expect_true(n <= ClickConfig::kMaxParticleAmount,
                            "T-022 B: never above the particle cap");
            }
        }

        // ---- T-022 C: AIR turns (tangential swirl, not a radial burst) ----
        {
            ClickBubbleEffect e(elemental_cfg(ClickStyle::Air, 0.65f, 8));
            const int64_t t0 = 1'000 * kMs;
            e.spawn(500.0f, 400.0f, t0);
            const auto& b = e.bubbles().front();

            auto angle_of = [&](int i, float p) {
                const auto q = e.elemental_particle(b, i, 8, p);
                return std::atan2(q.y - 400.0f, q.x - 500.0f);
            };
            auto dist_of = [&](int i, float p) {
                const auto q = e.elemental_particle(b, i, 8, p);
                return std::hypot(q.x - 500.0f, q.y - 400.0f);
            };

            for (int i = 0; i < 8; ++i) {
                // The angular position ADVANCES by the swirl rate: this is
                // what separates Air from every radial style in the app.
                float d = angle_of(i, 0.8f) - angle_of(i, 0.2f);
                while (d < 0.0f) d += 6.2831853f;
                while (d >= 6.2831853f) d -= 6.2831853f;
                expect_near(d, 2.4f * 0.6f, 1e-3f, "T-022 C: Air angle advances with progress");
                // ...while the outward creep stays monotone and modest.
                float prev = -1.0f;
                bool monotone = true;
                for (float p = 0.0f; p <= 1.0001f; p += 0.05f) {
                    const float cur = dist_of(i, p);
                    if (cur + 1e-4f < prev) monotone = false;
                    prev = cur;
                }
                expect_true(monotone, "T-022 C: Air drifts outward monotonically");
            }
        }

        // ---- T-022 D: FIRE rises (buoyancy is the whole signature) ----
        {
            ClickBubbleEffect e(elemental_cfg(ClickStyle::Fire, 0.65f, 8));
            const int64_t t0 = 1'000 * kMs;
            e.spawn(500.0f, 400.0f, t0);
            const auto& b = e.bubbles().front();

            for (int i = 0; i < 8; ++i) {
                float prev = 400.0f + 1.0f;
                bool rising = true;
                bool never_below = true;
                for (float p = 0.0f; p <= 1.0001f; p += 0.05f) {
                    const auto q = e.elemental_particle(b, i, 8, p);
                    if (q.y > prev + 1e-4f) rising = false;   // y grows = falling
                    if (q.y > 400.0f + 1e-4f) never_below = false;
                    prev = q.y;
                    // Fire feeds its own age back into the tint.
                    expect_near(q.cool, p > 1.0f ? 1.0f : p, 1e-4f,
                                "T-022 D: Fire cool term follows progress");
                }
                expect_true(rising, "T-022 D: Fire embers only ever rise");
                expect_true(never_below, "T-022 D: no ember falls below the click");
            }
            // The lateral wobble stays a wobble, never a burst.
            for (int i = 0; i < 8; ++i) {
                for (float p = 0.0f; p <= 1.0001f; p += 0.1f) {
                    const auto q = e.elemental_particle(b, i, 8, p);
                    const float lateral = std::fabs(q.x - 500.0f);
                    expect_true(lateral <= 26.0f * 0.40f + 1e-3f,
                                "T-022 D: Fire stays in a narrow column");
                }
            }
        }

        // ---- T-022 E: WATER repeats (staggered concentric ripples) ----
        {
            ClickBubbleEffect e(elemental_cfg(ClickStyle::Water, 0.65f, 8));
            const int64_t t0 = 1'000 * kMs;
            e.spawn(500.0f, 400.0f, t0);
            const auto& b = e.bubbles().front();

            auto live_count = [&](float p) {
                int n = 0;
                for (int k = 0; k < ClickBubbleEffect::kWaterRipples; ++k) {
                    if (e.water_ripple(b, k, p).live) ++n;
                }
                return n;
            };
            expect_true(live_count(0.05f) == 1, "T-022 E: only the first ripple exists early");
            expect_true(live_count(0.30f) == 2, "T-022 E: the second ripple follows");
            expect_true(live_count(0.60f) == 3, "T-022 E: all three ripples run mid-life");

            // Outer-first ordering: an older ripple is always the larger one,
            // and always the fainter one.
            const auto r0 = e.water_ripple(b, 0, 0.60f);
            const auto r1 = e.water_ripple(b, 1, 0.60f);
            const auto r2 = e.water_ripple(b, 2, 0.60f);
            expect_true(r0.radius_px > r1.radius_px, "T-022 E: ripple 0 leads ripple 1");
            expect_true(r1.radius_px > r2.radius_px, "T-022 E: ripple 1 leads ripple 2");
            expect_true(r0.alpha < r1.alpha, "T-022 E: the leading ripple is the faintest");
            // Out-of-range indices are inert, not undefined.
            expect_true(!e.water_ripple(b, -1, 0.5f).live, "T-022 E: negative ring index inert");
            expect_true(!e.water_ripple(b, ClickBubbleEffect::kWaterRipples, 0.5f).live,
                        "T-022 E: past-the-end ring index inert");
        }

        // ---- T-022 F: EARTH falls (gravity, weight, chunk size) ----
        {
            ClickBubbleEffect earth(elemental_cfg(ClickStyle::Earth, 0.65f, 8));
            ClickBubbleEffect air(elemental_cfg(ClickStyle::Air, 0.65f, 8));
            const int64_t t0 = 1'000 * kMs;
            earth.spawn(500.0f, 400.0f, t0);
            air.spawn(500.0f, 400.0f, t0);
            const auto& be = earth.bubbles().front();
            const auto& ba = air.bubbles().front();

            const int n = ClickBubbleEffect::elemental_particle_count(ClickStyle::Earth, 8);
            for (int i = 0; i < n; ++i) {
                // Every chunk ends BELOW the click: that is the fall.
                const auto end = earth.elemental_particle(be, i, n, 1.0f);
                expect_true(end.y > 400.0f, "T-022 F: Earth debris ends below the click");
                // Horizontal travel is monotone outward (thrown, not orbiting).
                float prev = -1.0f;
                bool monotone = true;
                for (float p = 0.0f; p <= 1.0001f; p += 0.05f) {
                    const auto q = earth.elemental_particle(be, i, n, p);
                    const float dx = std::fabs(q.x - 500.0f);
                    if (dx + 1e-4f < prev) monotone = false;
                    prev = dx;
                }
                expect_true(monotone, "T-022 F: Earth debris flies outward monotonically");
                // Chunks are visibly heavier than Air motes at the same moment.
                const auto chunk = earth.elemental_particle(be, i, n, 0.5f);
                const auto mote = air.elemental_particle(ba, i, 8, 0.5f);
                expect_true(chunk.radius_px > mote.radius_px * 1.5f,
                            "T-022 F: Earth debris is chunky next to Air motes");
            }
        }

        // ---- T-022 G: emitted geometry per element (sink contract) ----
        {
            const int64_t t0 = 1'000 * kMs;
            struct Expect { ClickStyle style; std::size_t bubbles; std::size_t particles; const char* what; };
            const Expect cases[4] = {
                {ClickStyle::Air,   1, 8, "T-022 G: Air = 1 gust ring + 8 motes"},
                {ClickStyle::Fire,  1, 8, "T-022 G: Fire = 1 base flash + 8 embers"},
                {ClickStyle::Water, 2, 2, "T-022 G: Water = 2 live ripples + 2 droplets at mid-life"},
                {ClickStyle::Earth, 1, 4, "T-022 G: Earth = 1 dust ring + 4 chunks"},
            };
            for (const Expect& c : cases) {
                ClickBubbleEffect e(elemental_cfg(c.style, 0.65f, 8));
                e.spawn(500.0f, 400.0f, t0);
                RecordSink sink;
                e.draw(t0 + 125 * kMs, sink);  // exactly mid-life
                expect_true(sink.items.size() == c.bubbles, c.what);
                expect_true(sink.particles.size() == c.particles, c.what);
                for (const auto& it : sink.items) {
                    expect_true(std::isfinite(it.cx) && std::isfinite(it.cy)
                                && std::isfinite(it.radius),
                                "T-022 G: finite ring geometry");
                }
                for (const auto& p : sink.particles) {
                    expect_true(std::isfinite(p.cx) && std::isfinite(p.cy)
                                && p.radius > 0.0f && p.alpha > 0.0f,
                                "T-022 G: finite, visible particles");
                }
            }
            // Fire emits a FILLED base flash and no outlined ring; Air emits an
            // outlined ring and no fill. That is the shape half of the identity.
            {
                ClickBubbleEffect fire(elemental_cfg(ClickStyle::Fire, 0.65f, 8));
                fire.spawn(0.0f, 0.0f, t0);
                RecordSink fs;
                fire.draw(t0 + 125 * kMs, fs);
                expect_true(fs.items.size() == 1 && fs.items[0].ring_alpha == 0.0f
                            && fs.items[0].fill_alpha > 0.0f,
                            "T-022 G: Fire base flash is filled, never outlined");

                ClickBubbleEffect airf(elemental_cfg(ClickStyle::Air, 0.65f, 8));
                airf.spawn(0.0f, 0.0f, t0);
                RecordSink as;
                airf.draw(t0 + 125 * kMs, as);
                expect_true(as.items.size() == 1 && as.items[0].ring_alpha > 0.0f
                            && as.items[0].fill_alpha == 0.0f,
                            "T-022 G: Air gust ring is outlined, never filled");
            }
            // particle_amount 0 silences the particles but keeps the element's ring.
            {
                ClickBubbleEffect e(elemental_cfg(ClickStyle::Earth, 0.65f, 0));
                e.spawn(0.0f, 0.0f, t0);
                RecordSink sink;
                e.draw(t0 + 125 * kMs, sink);
                expect_true(sink.particles.empty(), "T-022 G: amount 0 emits no debris");
                expect_true(sink.items.size() == 1, "T-022 G: the dust ring survives amount 0");
            }
        }

        // ---- T-022 H: determinism and non-elemental isolation ----
        {
            const int64_t t0 = 1'000 * kMs;
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                ClickBubbleEffect e(elemental_cfg(st, 0.65f, 8));
                e.spawn(321.0f, 654.0f, t0);
                RecordSink a, b2;
                e.draw(t0 + 100 * kMs, a);
                e.draw(t0 + 100 * kMs, b2);  // same instant, same instance
                expect_true(a.items.size() == b2.items.size()
                            && a.particles.size() == b2.particles.size(),
                            "T-022 H: identical emission counts");
                bool identical = true;
                for (std::size_t i = 0; i < a.particles.size(); ++i) {
                    const auto& p = a.particles[i];
                    const auto& q = b2.particles[i];
                    if (p.cx != q.cx || p.cy != q.cy || p.radius != q.radius
                        || p.alpha != q.alpha || p.r != q.r || p.g != q.g || p.b != q.b) {
                        identical = false;
                    }
                }
                expect_true(identical, "T-022 H: elemental emission is bit-identical");
            }
            // The seven T-017 styles must be untouched by T-022, including by
            // a maxed-out element_tint they are supposed to ignore.
            {
                ClickConfig c{};
                c.style = ClickStyle::Ring;
                c.element_tint = 1.0f;
                c.color_r = 10; c.color_g = 20; c.color_b = 30;
                ClickBubbleEffect e(ClickConfig::validated(c));
                e.spawn(0.0f, 0.0f, t0);
                RecordSink sink;
                e.draw(t0 + 125 * kMs, sink);
                expect_true(sink.items.size() == 1 && sink.particles.empty(),
                            "T-022 H: Ring still emits exactly one bubble");
                expect_near(sink.items[0].r, 10.0f, 1e-4f, "T-022 H: Ring keeps user R");
                expect_near(sink.items[0].g, 20.0f, 1e-4f, "T-022 H: Ring keeps user G");
                expect_near(sink.items[0].b, 30.0f, 1e-4f, "T-022 H: Ring keeps user B");
            }
        }

        // ---- T-022 I: config validation of the new surface ----
        {
            ClickConfig c{};
            c.style = static_cast<ClickStyle>(11);   // past Earth
            c.element_tint = 4.0f;                   // out of range
            const ClickConfig v = ClickConfig::validated(c);
            expect_true(v.style == ClickStyle::Ring, "T-022 I: invalid style recovers to Ring");
            expect_near(v.element_tint, 1.0f, 0.0f, "T-022 I: element_tint clamps to 1");

            ClickConfig c2{};
            c2.element_tint = -0.5f;
            expect_near(ClickConfig::validated(c2).element_tint, 0.0f, 0.0f,
                        "T-022 I: element_tint clamps to 0");
            expect_near(ClickConfig{}.element_tint, 0.65f, 0.0f,
                        "T-022 I: default element_tint is 0.65");
            // Every elemental style survives validation unchanged.
            for (const ClickStyle st : {ClickStyle::Air, ClickStyle::Fire,
                                        ClickStyle::Water, ClickStyle::Earth}) {
                ClickConfig c3{};
                c3.style = st;
                expect_true(ClickConfig::validated(c3).style == st,
                            "T-022 I: elemental styles survive validation");
            }
        }
    }

    // ---- T-025: per-click variation (Burst / Spark Burst / elemental) ----
    //
    // The defect this guards: every jitter used to come from the particle
    // INDEX alone, so the Nth particle of every click sat at the same angle
    // at the same distance forever. A user clicking twice saw one stencil
    // stamped twice. These checks assert that two DIFFERENT clicks differ,
    // while a single click still replays bit-identically.
    {
        using ptd::ClickStyle;
        const int64_t t0 = 9000 * kMs;
        const auto layout = [](ClickStyle style, float x, float y, int64_t ts,
                               RecordSink& out) {
            ClickConfig c{};
            c.style = style;
            c.particle_amount = 12;
            c.end_radius_px = 40.0f;
            c.duration_ms = 250.0f;
            ClickBubbleEffect e(c);
            e.spawn(x, y, ts);
            e.draw(ts + 100 * kMs, out);
        };

        for (const ClickStyle style : {ClickStyle::SparkBurst, ClickStyle::Burst,
                                       ClickStyle::Air, ClickStyle::Fire,
                                       ClickStyle::Water, ClickStyle::Earth}) {
            RecordSink a, b, a_again;
            layout(style, 200.0f, 300.0f, t0, a);
            layout(style, 640.0f, 480.0f, t0 + 37 * kMs, b);
            layout(style, 200.0f, 300.0f, t0, a_again);

            // Same click, rebuilt from scratch -> bit-identical. The seed is
            // a pure function of (x, y, timestamp), never RNG state.
            expect_true(a.particles.size() == a_again.particles.size(),
                        "T-025: identical click emits identical count");
            bool replay_identical = a.particles.size() == a_again.particles.size();
            for (std::size_t i = 0; i < a.particles.size() && replay_identical; ++i) {
                if (a.particles[i].cx != a_again.particles[i].cx
                    || a.particles[i].cy != a_again.particles[i].cy
                    || a.particles[i].alpha != a_again.particles[i].alpha) {
                    replay_identical = false;
                }
            }
            expect_true(replay_identical,
                        "T-025: identical click replays bit-identically");

            // Different click -> a genuinely different arrangement. Compared
            // as offsets from each anchor, so this cannot pass merely because
            // the two clicks sit at different screen positions.
            expect_true(!a.particles.empty()
                        && a.particles.size() == b.particles.size(),
                        "T-025: both clicks emit the same particle count");
            int differing = 0;
            for (std::size_t i = 0; i < a.particles.size() && i < b.particles.size(); ++i) {
                const float ax = a.particles[i].cx - 200.0f;
                const float ay = a.particles[i].cy - 300.0f;
                const float bx = b.particles[i].cx - 640.0f;
                const float by = b.particles[i].cy - 480.0f;
                if (std::fabs(ax - bx) > 0.5f || std::fabs(ay - by) > 0.5f) {
                    ++differing;
                }
            }
            expect_true(differing >= static_cast<int>(a.particles.size()) / 2,
                        "T-025: two different clicks scatter differently");
        }

        // Spark Burst specifically: the old stencil put particle 0 exactly on
        // the +x axis of the anchor (angle 0, so cy == anchor y). Prove that
        // is no longer a fixed property of the style.
        {
            bool any_off_axis = false;
            for (int k = 0; k < 8; ++k) {
                RecordSink s0;
                layout(ClickStyle::SparkBurst, 100.0f, 100.0f,
                       t0 + static_cast<int64_t>(k) * 13 * kMs, s0);
                if (!s0.particles.empty()
                    && std::fabs(s0.particles[0].cy - 100.0f) > 1.0f) {
                    any_off_axis = true;
                }
            }
            expect_true(any_off_axis,
                        "T-025: Spark Burst is no longer pinned to the +x axis");
        }
    }


    // =====================================================================
    // T-025 PART 2: per-particle LIFETIME variation.
    //
    // Spatial scatter alone still made a burst vanish as one flat sheet --
    // every particle lived exactly the parent duration. Each particle now
    // owns a deterministic visible-life FRACTION of the parent bubble and
    // simply stops being emitted past it. Nothing is reversed, nothing is
    // added: this can only ever remove marks.
    // =====================================================================
    {
        using ptd::ClickStyle;
        const int64_t t0 = 21'000 * kMs;

        auto burst_cfg = [](ClickStyle style, int amount) {
            ClickConfig c{};
            c.style = style;
            c.particle_amount = static_cast<uint8_t>(amount);
            c.end_radius_px = 40.0f;
            c.duration_ms = 250.0f;
            c.hold_enabled = false;  // one-shot click math only in this block
            return ClickConfig::validated(c);
        };

        // The live index set at a given progress is fully predictable from
        // the pure lifetime function, which is what lets a sink recording
        // (which carries no indices) be mapped back onto particle indices.
        auto live_indices = [](uint32_t seed, int n, bool spark, float progress) {
            std::vector<int> live;
            for (int i = 0; i < n; ++i) {
                if (progress < ClickBubbleEffect::burst_particle_life(seed, i, spark)) {
                    live.push_back(i);
                }
            }
            return live;
        };

        // ---- bounded ranges, and a real spread inside them ----
        for (int spark_i = 0; spark_i < 2; ++spark_i) {
            const bool spark = spark_i == 1;
            const float lo_bound = spark ? 0.55f : 0.78f;
            float seen_lo = 2.0f;
            float seen_hi = -1.0f;
            bool in_range = true;
            for (uint32_t s = 1; s <= 64u; ++s) {
                for (int i = 0; i < 12; ++i) {
                    const float life =
                        ClickBubbleEffect::burst_particle_life(s * 2654435761u, i, spark);
                    if (!(life >= lo_bound - 1e-5f) || !(life <= 1.0f + 1e-5f)) {
                        in_range = false;
                    }
                    seen_lo = std::min(seen_lo, life);
                    seen_hi = std::max(seen_hi, life);
                }
            }
            expect_true(in_range,
                        spark ? "T-025L: SparkBurst lifetimes stay in 0.55..1.00"
                              : "T-025L: Burst lifetimes stay in 0.78..1.00");
            // The spread must actually be used, or "variation" is a comment.
            expect_true(seen_hi - seen_lo > (spark ? 0.35f : 0.15f),
                        spark ? "T-025L: SparkBurst spread is wide"
                              : "T-025L: Burst spread is real");
        }
        // Spark Burst must vary MORE strongly than Burst.
        {
            float burst_span = 0.0f;
            float spark_span = 0.0f;
            float b_lo = 2.0f, b_hi = -1.0f, s_lo = 2.0f, s_hi = -1.0f;
            for (uint32_t s = 1; s <= 64u; ++s) {
                for (int i = 0; i < 12; ++i) {
                    const float bl =
                        ClickBubbleEffect::burst_particle_life(s * 40503u, i, false);
                    const float sl =
                        ClickBubbleEffect::burst_particle_life(s * 40503u, i, true);
                    b_lo = std::min(b_lo, bl); b_hi = std::max(b_hi, bl);
                    s_lo = std::min(s_lo, sl); s_hi = std::max(s_hi, sl);
                }
            }
            burst_span = b_hi - b_lo;
            spark_span = s_hi - s_lo;
            expect_true(spark_span > burst_span,
                        "T-025L: SparkBurst lifetime spread exceeds Burst");
        }

        // ---- same click replays identical lifetime decisions ----
        {
            const ClickConfig c = burst_cfg(ClickStyle::SparkBurst, 12);
            ClickBubbleEffect a(c);
            ClickBubbleEffect b(c);
            a.spawn(300.0f, 220.0f, t0);
            b.spawn(300.0f, 220.0f, t0);
            bool identical = true;
            // Sweep the whole life, including past the earliest death time.
            for (int step = 0; step <= 20; ++step) {
                const int64_t now = t0 + static_cast<int64_t>(step * 12) * kMs;
                RecordSink sa, sb;
                a.draw(now, sa);
                b.draw(now, sb);
                if (sa.particles.size() != sb.particles.size()) identical = false;
                for (std::size_t i = 0; i < sa.particles.size()
                                        && i < sb.particles.size(); ++i) {
                    if (std::fabs(sa.particles[i].cx - sb.particles[i].cx) > 1e-4f
                        || std::fabs(sa.particles[i].cy - sb.particles[i].cy) > 1e-4f) {
                        identical = false;
                    }
                }
            }
            expect_true(identical,
                        "T-025L: same click replays identical lifetime decisions");
        }

        // ---- frame cadence cannot change the result at equal elapsed time ----
        {
            const ClickConfig c = burst_cfg(ClickStyle::SparkBurst, 12);
            ClickBubbleEffect coarse(c);
            ClickBubbleEffect fine(c);
            coarse.spawn(300.0f, 220.0f, t0);
            fine.spawn(300.0f, 220.0f, t0);
            // "fine" is walked in 5 ms steps first; "coarse" jumps straight
            // there. Equal elapsed time must give an equal picture.
            for (int step = 1; step <= 36; ++step) {
                RecordSink junk;
                fine.draw(t0 + static_cast<int64_t>(step * 5) * kMs, junk);
            }
            RecordSink sc, sf;
            coarse.draw(t0 + 180 * kMs, sc);
            fine.draw(t0 + 180 * kMs, sf);
            bool same = sc.particles.size() == sf.particles.size();
            for (std::size_t i = 0; i < sc.particles.size()
                                    && i < sf.particles.size(); ++i) {
                if (std::fabs(sc.particles[i].cx - sf.particles[i].cx) > 1e-5f
                    || std::fabs(sc.particles[i].cy - sf.particles[i].cy) > 1e-5f) {
                    same = false;
                }
            }
            expect_true(same,
                        "T-025L: equal elapsed time is frame-cadence independent");
        }

        // ---- different clicks produce different lifetime LAYOUTS ----
        {
            const uint32_t s1 = ClickBubbleEffect::bubble_seed(200.0f, 300.0f, t0);
            const uint32_t s2 =
                ClickBubbleEffect::bubble_seed(640.0f, 480.0f, t0 + 37 * kMs);
            int differing = 0;
            for (int i = 0; i < 12; ++i) {
                if (std::fabs(ClickBubbleEffect::burst_particle_life(s1, i, true)
                              - ClickBubbleEffect::burst_particle_life(s2, i, true))
                    > 0.02f) {
                    ++differing;
                }
            }
            expect_true(differing >= 6,
                        "T-025L: two different clicks lay out lifetimes differently");
        }

        // ---- partial burnout: some sparks gone while others remain ----
        {
            const ClickConfig c = burst_cfg(ClickStyle::SparkBurst, 12);
            bool saw_partial = false;
            for (int k = 0; k < 16 && !saw_partial; ++k) {
                ClickBubbleEffect e(c);
                const int64_t ts = t0 + static_cast<int64_t>(k) * 17 * kMs;
                e.spawn(400.0f, 400.0f, ts);
                RecordSink s;
                e.draw(ts + 200 * kMs, s);  // progress 0.80
                if (!s.particles.empty()
                    && s.particles.size() < static_cast<std::size_t>(12)) {
                    saw_partial = true;
                }
            }
            expect_true(saw_partial,
                        "T-025L: at late progress some sparks are gone, others remain");
        }

        // ---- count never exceeds the configured amount, at any progress ----
        {
            const ClickConfig c = burst_cfg(ClickStyle::SparkBurst, 9);
            ClickBubbleEffect e(c);
            e.spawn(150.0f, 150.0f, t0);
            bool bounded = true;
            for (int step = 0; step <= 25; ++step) {
                RecordSink s;
                e.draw(t0 + static_cast<int64_t>(step * 10) * kMs, s);
                if (s.particles.size() > 9u) bounded = false;
            }
            expect_true(bounded,
                        "T-025L: particle count never exceeds particle_amount");
        }

        // ---- travel stays MONOTONE while a particle is live ----
        //
        // Lifetime is a constant threshold per particle, so the live set only
        // ever shrinks: the emission at a later progress is a subsequence of
        // the earlier one, and indices can be recovered from live_indices().
        for (int spark_i = 0; spark_i < 2; ++spark_i) {
            const bool spark = spark_i == 1;
            const ClickStyle style = spark ? ClickStyle::SparkBurst : ClickStyle::Burst;
            const ClickConfig c = burst_cfg(style, 12);
            ClickBubbleEffect e(c);
            const float ax = 500.0f;
            const float ay = 360.0f;
            e.spawn(ax, ay, t0);
            const uint32_t seed = e.bubbles().front().seed;

            bool monotone = true;
            bool mapping_ok = true;
            bool never_resurrects = true;
            std::vector<float> prev_dist(12, -1.0f);
            std::vector<bool> prev_live(12, false);
            for (int step = 0; step <= 24; ++step) {
                const float prog = static_cast<float>(step) / 25.0f;
                const int64_t now = t0 + static_cast<int64_t>(prog * 250.0f) * kMs;
                RecordSink s;
                e.draw(now, s);
                // draw() re-derives progress from the same clock arithmetic;
                // rebuild it the same way so the mapping cannot drift.
                const float p = e.progress_at(e.bubbles().front(), now);
                const std::vector<int> live = live_indices(seed, 12, spark, p);
                if (live.size() != s.particles.size()) mapping_ok = false;
                for (std::size_t k = 0; k < live.size() && k < s.particles.size(); ++k) {
                    const int idx = live[k];
                    const float dx = s.particles[k].cx - ax;
                    const float dy = s.particles[k].cy - ay;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (prev_dist[static_cast<std::size_t>(idx)] >= 0.0f
                        && d < prev_dist[static_cast<std::size_t>(idx)] - 1e-3f) {
                        monotone = false;
                    }
                    // A particle that died must never come back.
                    if (prev_dist[static_cast<std::size_t>(idx)] < 0.0f
                        && prev_live[static_cast<std::size_t>(idx)]) {
                        never_resurrects = false;
                    }
                    prev_dist[static_cast<std::size_t>(idx)] = d;
                }
                std::vector<bool> now_live(12, false);
                for (const int idx : live) now_live[static_cast<std::size_t>(idx)] = true;
                for (std::size_t i = 0; i < 12u; ++i) {
                    if (prev_live[i] && !now_live[i]) {
                        // Mark as dead: a later reappearance is a defect.
                        prev_dist[i] = -1.0f;
                    }
                }
                prev_live = now_live;
            }
            expect_true(mapping_ok,
                        spark ? "T-025L: SparkBurst emission matches the live set"
                              : "T-025L: Burst emission matches the live set");
            expect_true(monotone,
                        spark ? "T-025L: SparkBurst travel monotone while live"
                              : "T-025L: Burst travel monotone while live");
            expect_true(never_resurrects,
                        spark ? "T-025L: a dead spark never reappears"
                              : "T-025L: a dead Burst particle never reappears");
        }
    }

    // =====================================================================
    // T-024 press-and-hold: Candidate -> ActiveHold state machine.
    // =====================================================================
    {
        using ptd::ClickStyle;
        using ptd::MouseButton;
        using ptd::ButtonAction;

        const int64_t t0 = 40'000 * kMs;
        const int64_t kAct =
            static_cast<int64_t>(ClickBubbleEffect::kHoldActivationMs) * kMs;
        const int64_t kFull =
            static_cast<int64_t>(ClickBubbleEffect::kHoldChargeMs) * kMs;

        auto btn = [](int64_t ts, int x, int y, MouseButton b, ButtonAction a) {
            ptd::CursorSample s;
            s.timestamp_ns = ts;
            s.x = x;
            s.y = y;
            s.button = b;
            s.action = a;
            return s;
        };
        auto hold_cfg = [](ClickStyle style) {
            ClickConfig c{};
            c.style = style;
            c.particle_amount = 12;
            c.end_radius_px = 40.0f;
            c.duration_ms = 250.0f;
            c.hold_enabled = true;
            return ClickConfig::validated(c);
        };

        // ---- SHORT CLICK: no aura, no phantom second bubble ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            expect_true(e.on_button_down(btn(t0, 100, 100, MouseButton::Left,
                                             ButtonAction::Down)),
                        "T-024: Down spawns the one-shot click");
            expect_true(e.active_count() == 1, "T-024: exactly one bubble on Down");
            expect_true(e.hold_record_count() == 1,
                        "T-024: Down opens a hold candidate");
            expect_true(e.candidate_count(t0) == 1 && e.active_hold_count(t0) == 0,
                        "T-024: the record starts as a CANDIDATE");
            expect_true(!e.has_active_hold(t0 + 50 * kMs),
                        "T-024: no active hold before the threshold");

            // A candidate draws nothing: the frame is identical to the same
            // effect with Hold FX off entirely.
            ClickConfig off = hold_cfg(ClickStyle::Ring);
            off.hold_enabled = false;
            ClickBubbleEffect e_off(off);
            e_off.on_button_down(btn(t0, 100, 100, MouseButton::Left,
                                     ButtonAction::Down));
            RecordSink s_cand, s_off;
            e.draw(t0 + 120 * kMs, s_cand);
            e_off.draw(t0 + 120 * kMs, s_off);
            expect_true(s_cand.items.size() == s_off.items.size()
                        && s_cand.particles.size() == s_off.particles.size(),
                        "T-024: a candidate emits NO hold aura");

            // Up before the threshold: candidate removed, nothing spawned.
            expect_true(!e.on_button_up(btn(t0 + 120 * kMs, 100, 100,
                                            MouseButton::Left, ButtonAction::Up)),
                        "T-024: short-click Up spawns nothing");
            expect_true(e.active_count() == 1,
                        "T-024: short click stays exactly ONE bubble");
            expect_true(e.hold_record_count() == 0,
                        "T-024: short-click Up drops the candidate");
        }

        // ---- ACTIVATION while stationary: no movement required ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 300, 300, MouseButton::Left, ButtonAction::Down));
            expect_true(e.candidate_count(t0 + kAct - kMs) == 1,
                        "T-024: still a candidate 1 ms before the threshold");
            expect_true(e.active_hold_count(t0 + kAct) == 1,
                        "T-024: promoted to ActiveHold at the threshold");
            expect_true(e.has_active_hold(t0 + kAct + 100 * kMs),
                        "T-024: hold stays active past the threshold");

            // The one-shot bubble (250 ms) is long gone by now; the hold alone
            // must keep the shared scheduler awake and keep drawing.
            const int64_t late = t0 + 2'000 * kMs;
            expect_true(e.has_live_content(late),
                        "T-024: an active hold is live scheduler content");
            RecordSink s;
            e.draw(late, s);
            expect_true(!s.items.empty() || !s.particles.empty(),
                        "T-024: a stationary hold keeps drawing without any movement");

            // A CANDIDATE must also keep the scheduler awake, or a short click
            // duration would put it to sleep before it could ever activate.
            ClickConfig quick = hold_cfg(ClickStyle::Ring);
            quick.duration_ms = ClickConfig::kMinDurationMs;  // 50 ms
            ClickBubbleEffect e2(quick);
            e2.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            expect_true(e2.has_live_content(t0 + 100 * kMs),
                        "T-024: a candidate keeps the scheduler awake past bubble death");
        }

        // ---- MOVEMENT: the aura follows, the Down bubble does not ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 100, 100, MouseButton::Left, ButtonAction::Down));
            e.on_cursor_moved(move(t0 + 400 * kMs, 900, 650));
            expect_true(e.hold_record_count() == 1, "T-024: drag keeps the hold");
            expect_near(e.holds().front().x, 900.0f, 0.001f,
                        "T-024: hold anchor follows the cursor (x)");
            expect_near(e.holds().front().y, 650.0f, 0.001f,
                        "T-024: hold anchor follows the cursor (y)");
            expect_near(e.bubbles().front().x, 100.0f, 0.001f,
                        "T-024: the Down one-shot stays at the press point (x)");
            expect_near(e.bubbles().front().y, 100.0f, 0.001f,
                        "T-024: the Down one-shot stays at the press point (y)");
        }

        // ---- RELEASE: payoff anchor and charge-scaled power ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 100, 100, MouseButton::Left, ButtonAction::Down));
            const int64_t up_ts = t0 + kAct + kFull;  // exactly full charge
            expect_true(e.on_button_up(btn(up_ts, 700, 500, MouseButton::Left,
                                           ButtonAction::Up)),
                        "T-024: Up after activation spawns the release payoff");
            expect_true(e.hold_record_count() == 0, "T-024: Up terminates the hold");
            const auto& payoff = e.bubbles().back();
            expect_near(payoff.x, 700.0f, 0.001f,
                        "T-024: payoff anchors at the UP coordinate (x)");
            expect_near(payoff.y, 500.0f, 0.001f,
                        "T-024: payoff anchors at the UP coordinate (y)");
            expect_near(payoff.power, 2.2f, 1e-4f,
                        "T-024: full charge pays off at 2.2x");
        }
        {
            // Charge is measured from ACTIVATION, so the threshold window
            // buys nothing: releasing at the instant of activation is a plain
            // click-strength payoff.
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 0, 0, MouseButton::Left, ButtonAction::Down));
            e.on_button_up(btn(t0 + kAct, 0, 0, MouseButton::Left, ButtonAction::Up));
            expect_near(e.bubbles().back().power, 1.0f, 1e-4f,
                        "T-024: releasing at the activation instant is power 1.0");
        }
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 0, 0, MouseButton::Left, ButtonAction::Down));
            e.on_button_up(btn(t0 + kAct + kFull / 2, 0, 0, MouseButton::Left,
                               ButtonAction::Up));
            expect_near(e.bubbles().back().power, 1.6f, 1e-3f,
                        "T-024: half charge pays off at 1.6x");
        }
        {
            // Bounded: a minute-long hold pays off no harder than full charge.
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 0, 0, MouseButton::Left, ButtonAction::Down));
            e.on_button_up(btn(t0 + 60'000 * kMs, 0, 0, MouseButton::Left,
                               ButtonAction::Up));
            expect_near(e.bubbles().back().power, 2.2f, 1e-4f,
                        "T-024: full-charge payoff is bounded at 2.2x");
        }

        // ---- PER-BUTTON independence, max three ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            e.on_button_down(btn(t0 + kMs, 20, 20, MouseButton::Right,
                                 ButtonAction::Down));
            e.on_button_down(btn(t0 + 2 * kMs, 30, 30, MouseButton::Middle,
                                 ButtonAction::Down));
            expect_true(e.hold_record_count() == 3,
                        "T-024: three independent per-button holds");
            expect_true(e.hold_record_count()
                        <= ptd::ClickBubbleEffect::kMaxActiveHolds,
                        "T-024: hold records are bounded at three");
            // A repeated Down for the same button replaces, never stacks.
            e.on_button_down(btn(t0 + 3 * kMs, 40, 40, MouseButton::Left,
                                 ButtonAction::Down));
            expect_true(e.hold_record_count() == 3,
                        "T-024: a repeated Down replaces rather than stacks");
            e.on_button_up(btn(t0 + kAct + 10 * kMs, 20, 20, MouseButton::Right,
                               ButtonAction::Up));
            expect_true(e.hold_record_count() == 2,
                        "T-024: releasing one button leaves the others held");
        }

        // ---- a disabled trigger creates NEITHER a click NOR a hold ----
        {
            ClickConfig c = hold_cfg(ClickStyle::Ring);
            c.trigger_right = false;
            ClickBubbleEffect e(c);
            expect_true(!e.on_button_down(btn(t0, 10, 10, MouseButton::Right,
                                              ButtonAction::Down)),
                        "T-024: a disabled trigger spawns no bubble");
            expect_true(e.hold_record_count() == 0,
                        "T-024: a disabled trigger opens no hold");
        }

        // ---- LOST UP: physical reconciliation, no phantom payoff ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            const std::size_t before = e.active_count();
            expect_true(e.reconcile_physical_buttons(true, false, false) == 0,
                        "T-024: a genuinely held button survives reconciliation");
            expect_true(e.reconcile_physical_buttons(false, false, false) == 1,
                        "T-024: reconciliation cancels a stale CANDIDATE");
            expect_true(e.hold_record_count() == 0, "T-024: the candidate is gone");
            expect_true(e.active_count() == before,
                        "T-024: cancelling a candidate invents no payoff");
        }
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Middle, ButtonAction::Down));
            const std::size_t before = e.active_count();
            expect_true(e.active_hold_count(t0 + kAct + kFull) == 1,
                        "T-024: the middle-button hold is active");
            expect_true(e.reconcile_physical_buttons(false, false, true) == 0,
                        "T-024: reconciliation respects the correct button");
            expect_true(e.reconcile_physical_buttons(true, true, false) == 1,
                        "T-024: reconciliation cancels a stale ACTIVE hold");
            expect_true(e.active_count() == before,
                        "T-024: cancelling an active hold invents no payoff");
            RecordSink s;
            e.draw(t0 + 5'000 * kMs, s);
            expect_true(s.items.empty() && s.particles.empty(),
                        "T-024: no stale aura survives reconciliation");
        }

        // ---- LONG HOLD: no arbitrary timeout, bounded magnitude ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 500, 500, MouseButton::Left, ButtonAction::Down));
            for (const int64_t secs : {30LL, 60LL, 600LL}) {
                const int64_t now = t0 + secs * 1000 * kMs;
                e.prune(now);  // prune must NEVER kill a hold
                expect_true(e.hold_record_count() == 1,
                            "T-024: prune never expires a hold");
                expect_true(e.has_active_hold(now),
                            "T-024: a multi-second hold stays active");
                expect_true(e.has_live_content(now),
                            "T-024: a long hold keeps the scheduler awake");
                expect_near(e.charge_at(e.holds().front(), now), 1.0f, 1e-5f,
                            "T-024: charge saturates at 1.0 and stays there");
                RecordSink s;
                e.draw(now, s);
                bool bounded = !s.items.empty() || !s.particles.empty();
                for (const auto& it : s.items) {
                    if (!std::isfinite(it.cx) || !std::isfinite(it.cy)
                        || !std::isfinite(it.radius) || !(it.radius <= 4000.0f)
                        || !(it.ring_alpha >= 0.0f && it.ring_alpha <= 1.0f)
                        || !(it.fill_alpha >= 0.0f && it.fill_alpha <= 1.0f)) {
                        bounded = false;
                    }
                }
                for (const auto& pt : s.particles) {
                    if (!std::isfinite(pt.cx) || !std::isfinite(pt.cy)
                        || !std::isfinite(pt.radius) || !(pt.radius <= 200.0f)
                        || !(pt.alpha >= 0.0f && pt.alpha <= 1.0f)) {
                        bounded = false;
                    }
                }
                expect_true(bounded,
                            "T-024: a very long hold stays finite and bounded");
            }
        }

        // ---- CONFIG LIFECYCLE ----
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            ClickConfig off = hold_cfg(ClickStyle::Ring);
            off.hold_enabled = false;
            e.set_config(off);
            expect_true(e.hold_record_count() == 0,
                        "T-024: Hold FX OFF cancels live holds immediately");
            // Turning it back on must not resurrect anything.
            e.set_config(hold_cfg(ClickStyle::Ring));
            expect_true(e.hold_record_count() == 0,
                        "T-024: re-enabling Hold FX resurrects no stale state");
        }
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            ClickConfig off = hold_cfg(ClickStyle::Ring);
            off.enabled = false;
            e.set_config(off);
            expect_true(e.hold_record_count() == 0,
                        "T-024: disabling the Click effect cancels live holds");
        }
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Ring));
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            e.cancel_holds();
            expect_true(e.hold_record_count() == 0,
                        "T-024: cancel_holds (master OFF path) clears hold state");
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            e.clear();
            expect_true(e.hold_record_count() == 0 && e.empty(),
                        "T-024: clear() clears bubbles AND hold state");
        }
        {
            // With Hold FX off the Up path is inert: no candidate was ever
            // opened, so no payoff can be produced from a lost/forged Up.
            ClickConfig off = hold_cfg(ClickStyle::Ring);
            off.hold_enabled = false;
            ClickBubbleEffect e(off);
            e.on_button_down(btn(t0, 10, 10, MouseButton::Left, ButtonAction::Down));
            expect_true(e.hold_record_count() == 0,
                        "T-024: Hold FX off opens no candidate at all");
            expect_true(!e.on_button_up(btn(t0 + 5'000 * kMs, 10, 10,
                                            MouseButton::Left, ButtonAction::Up)),
                        "T-024: Hold FX off never pays off an Up");
            expect_true(e.active_count() == 1,
                        "T-024: Hold FX off leaves the classic one-shot behaviour");
        }

        // ---- DETERMINISM of the aura ----
        {
            const ClickConfig c = hold_cfg(ClickStyle::SparkBurst);
            ClickBubbleEffect a(c), b(c);
            a.on_button_down(btn(t0, 250, 250, MouseButton::Left, ButtonAction::Down));
            b.on_button_down(btn(t0, 250, 250, MouseButton::Left, ButtonAction::Down));
            // Walk b at a different cadence before sampling the same instant.
            for (int i = 1; i <= 40; ++i) {
                RecordSink junk;
                b.draw(t0 + static_cast<int64_t>(i * 11) * kMs, junk);
            }
            RecordSink sa, sb;
            const int64_t at = t0 + kAct + 333 * kMs;
            a.draw(at, sa);
            b.draw(at, sb);
            bool same = sa.items.size() == sb.items.size()
                     && sa.particles.size() == sb.particles.size();
            for (std::size_t i = 0; i < sa.particles.size()
                                    && i < sb.particles.size(); ++i) {
                if (std::fabs(sa.particles[i].cx - sb.particles[i].cx) > 1e-4f
                    || std::fabs(sa.particles[i].cy - sb.particles[i].cy) > 1e-4f
                    || std::fabs(sa.particles[i].alpha - sb.particles[i].alpha) > 1e-4f) {
                    same = false;
                }
            }
            expect_true(same,
                        "T-024: same hold identity + elapsed time = identical geometry");
        }

        // ---- STYLE IDENTITY: all 11 hold families are distinct ----
        {
            const ClickStyle all[] = {
                ClickStyle::Ring, ClickStyle::DoubleRing, ClickStyle::Ripple,
                ClickStyle::Burst, ClickStyle::SparkBurst, ClickStyle::SoftFlash,
                ClickStyle::DotRing, ClickStyle::Air, ClickStyle::Fire,
                ClickStyle::Water, ClickStyle::Earth};
            const int64_t at = t0 + kAct + 400 * kMs;

            std::vector<std::vector<float>> sigs;
            for (const ClickStyle st : all) {
                ClickBubbleEffect e(hold_cfg(st));
                e.on_button_down(btn(t0, 400, 400, MouseButton::Left,
                                     ButtonAction::Down));
                e.prune(at);  // drop the dead one-shot: the aura alone is the subject
                RecordSink s;
                e.draw(at, s);
                std::vector<float> sig;
                sig.push_back(static_cast<float>(s.items.size()));
                sig.push_back(static_cast<float>(s.particles.size()));
                for (const auto& it : s.items) {
                    sig.push_back(it.cx); sig.push_back(it.cy); sig.push_back(it.radius);
                }
                for (const auto& pt : s.particles) {
                    sig.push_back(pt.cx); sig.push_back(pt.cy); sig.push_back(pt.radius);
                }
                expect_true(!s.items.empty() || !s.particles.empty(),
                            "T-024: every hold family draws something");
                sigs.push_back(sig);
            }
            int identical_pairs = 0;
            for (std::size_t i = 0; i < sigs.size(); ++i) {
                for (std::size_t j = i + 1; j < sigs.size(); ++j) {
                    bool same = sigs[i].size() == sigs[j].size();
                    if (same) {
                        for (std::size_t k = 0; k < sigs[i].size(); ++k) {
                            if (std::fabs(sigs[i][k] - sigs[j][k]) > 1e-3f) {
                                same = false;
                                break;
                            }
                        }
                    }
                    if (same) ++identical_pairs;
                }
            }
            expect_true(identical_pairs == 0,
                        "T-024: all 11 HOLD families are structurally distinct");
        }

        // Air: tangential/angular motion (the angle advances, the radius does not).
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Air));
            e.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            const int64_t a1 = t0 + kAct + 300 * kMs;
            const int64_t a2 = a1 + 120 * kMs;
            e.prune(a1);
            RecordSink s1, s2;
            e.draw(a1, s1);
            e.draw(a2, s2);
            bool angular = false;
            bool radius_stable = true;
            for (std::size_t i = 0; i < s1.particles.size()
                                    && i < s2.particles.size(); ++i) {
                const float r1 = std::hypot(s1.particles[i].cx - 400.0f,
                                            s1.particles[i].cy - 400.0f);
                const float r2 = std::hypot(s2.particles[i].cx - 400.0f,
                                            s2.particles[i].cy - 400.0f);
                if (std::fabs(s1.particles[i].cx - s2.particles[i].cx) > 1.0f
                    || std::fabs(s1.particles[i].cy - s2.particles[i].cy) > 1.0f) {
                    angular = true;
                }
                if (std::fabs(r1 - r2) > 0.5f) radius_stable = false;
            }
            expect_true(angular, "T-024: Air hold motes actually move");
            expect_true(radius_stable,
                        "T-024: Air hold motion is tangential, not radial");
        }
        // Fire: embers rise (above the anchor).
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Fire));
            e.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            const int64_t at = t0 + kAct + 500 * kMs;
            e.prune(at);
            RecordSink s;
            e.draw(at, s);
            bool any_above = false;
            bool none_below = true;
            for (const auto& pt : s.particles) {
                if (pt.cy < 400.0f - 1.0f) any_above = true;
                if (pt.cy > 400.0f + 1.0f) none_below = false;
            }
            expect_true(any_above, "T-024: Fire hold embers rise above the anchor");
            expect_true(none_below, "T-024: Fire hold embers never sink");
        }
        // Water: ripple rings AND droplets (not merely another Ripple).
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Water));
            e.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            const int64_t at = t0 + kAct + 500 * kMs;
            e.prune(at);
            RecordSink s;
            e.draw(at, s);
            expect_true(!s.items.empty(), "T-024: Water hold emits ripple rings");
            expect_true(!s.particles.empty(),
                        "T-024: Water hold emits droplets too, so it is not Ripple");
        }
        // Earth: low and heavy, never orbiting.
        {
            ClickBubbleEffect e(hold_cfg(ClickStyle::Earth));
            e.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            const int64_t e1 = t0 + kAct + 400 * kMs;
            const int64_t e2 = e1 + 250 * kMs;
            e.prune(e1);
            RecordSink s1, s2;
            e.draw(e1, s1);
            e.draw(e2, s2);
            bool low = true;
            float max_travel = 0.0f;
            for (const auto& pt : s1.particles) {
                if (pt.cy < 400.0f - 20.0f) low = false;
            }
            for (std::size_t i = 0; i < s1.particles.size()
                                    && i < s2.particles.size(); ++i) {
                max_travel = std::max(max_travel,
                                      std::hypot(s1.particles[i].cx - s2.particles[i].cx,
                                                 s1.particles[i].cy - s2.particles[i].cy));
            }
            expect_true(low, "T-024: Earth hold chunks stay low");
            expect_true(max_travel < 10.0f,
                        "T-024: Earth hold chunks tremble rather than orbit");
            expect_true(max_travel > 0.0f, "T-024: Earth hold chunks do tremble");
        }
        // Burst vs Spark Burst holds must not be the same field.
        {
            ClickBubbleEffect eb(hold_cfg(ClickStyle::Burst));
            ClickBubbleEffect es(hold_cfg(ClickStyle::SparkBurst));
            eb.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            es.on_button_down(btn(t0, 400, 400, MouseButton::Left, ButtonAction::Down));
            const int64_t at = t0 + kAct + 400 * kMs;
            eb.prune(at);
            es.prune(at);
            RecordSink sb2, ss2;
            eb.draw(at, sb2);
            es.draw(at, ss2);
            int differing = 0;
            for (std::size_t i = 0; i < sb2.particles.size()
                                    && i < ss2.particles.size(); ++i) {
                if (std::fabs(sb2.particles[i].cx - ss2.particles[i].cx) > 0.5f
                    || std::fabs(sb2.particles[i].cy - ss2.particles[i].cy) > 0.5f) {
                    ++differing;
                }
            }
            expect_true(differing > 0,
                        "T-024: Burst and Spark Burst hold distributions differ");
        }

        // ---- RELEASE POWER touches EVERY family, not just the rings ----
        //
        // Controlled comparison: the payoff bubble seed derives from (x, y,
        // up timestamp), so spawning an ordinary bubble at the SAME anchor and
        // timestamp gives a bubble identical in every respect except power.
        {
            const ClickStyle families[] = {
                ClickStyle::Ring, ClickStyle::DoubleRing, ClickStyle::Ripple,
                ClickStyle::Burst, ClickStyle::SparkBurst, ClickStyle::SoftFlash,
                ClickStyle::DotRing, ClickStyle::Air, ClickStyle::Fire,
                ClickStyle::Water, ClickStyle::Earth};
            const char* names[] = {
                "Ring", "DoubleRing", "Ripple", "Burst", "SparkBurst",
                "SoftFlash", "DotRing", "Air", "Fire", "Water", "Earth"};

            auto extent = [](RecordSink& s, float ax, float ay) {
                float m = 0.0f;
                for (const auto& it : s.items) {
                    m = std::max(m, std::hypot(it.cx - ax, it.cy - ay) + it.radius);
                }
                for (const auto& pt : s.particles) {
                    m = std::max(m, std::hypot(pt.cx - ax, pt.cy - ay));
                }
                return m;
            };

            for (std::size_t k = 0; k < 11; ++k) {
                const ClickConfig c = hold_cfg(families[k]);
                const float ax = 800.0f;
                const float ay = 600.0f;
                const int64_t up_ts = t0 + kAct + kFull;

                ClickBubbleEffect charged(c);
                charged.on_button_down(btn(t0, 100, 100, MouseButton::Left,
                                           ButtonAction::Down));
                const bool paid = charged.on_button_up(
                    btn(up_ts, static_cast<int>(ax), static_cast<int>(ay),
                        MouseButton::Left, ButtonAction::Up));
                expect_true(paid, "T-024P: full-charge Up produces a payoff");
                charged.prune(up_ts);  // the Down one-shot is long dead

                ClickConfig plain_cfg = c;
                plain_cfg.hold_enabled = false;
                ClickBubbleEffect plain(plain_cfg);
                plain.spawn(ax, ay, up_ts);

                RecordSink sc, sp;
                const int64_t at = up_ts + 125 * kMs;  // progress 0.5
                charged.draw(at, sc);
                plain.draw(at, sp);

                const float ec = extent(sc, ax, ay);
                const float ep = extent(sp, ax, ay);
                char what[128];
                std::snprintf(what, sizeof(what),
                              "T-024P: %s full-charge release is spatially bigger",
                              names[k]);
                expect_true(ep > 0.0f && ec > ep * 1.5f, what);
                std::snprintf(what, sizeof(what),
                              "T-024P: %s release stays bounded", names[k]);
                expect_true(ec < ep * 3.0f, what);
                // The ordinary click itself is untouched by T-024.
                expect_near(plain.bubbles().front().power, 1.0f, 0.0f,
                            "T-024P: an ordinary click is exactly power 1.0");
            }
            expect_near(ClickBubbleEffect::power_size_gain(1.0f), 1.0f, 0.0f,
                        "T-024P: power 1.0 is exactly the identity size gain");
            expect_near(ClickBubbleEffect::power_alpha(0.73f, 1.0f), 0.73f, 0.0f,
                        "T-024P: power 1.0 is exactly the identity alpha");
            expect_true(ClickBubbleEffect::power_alpha(0.99f, 2.2f) <= 1.0f,
                        "T-024P: a charged alpha is hard-clamped at 1.0");
        }
    }

    std::printf("test_click_bubble_effect: %d checks, %d failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
