// T-009 C12: pure click-bubble math tests. No Qt, no Direct2D runtime
// calls -- ClickBubbleEffect is deliberately free of both, so correctness
// does not depend on screenshots/manual testing.

#include "../src/effects/click_bubble_effect.h"
#include "../src/render/render_color.h"
#include "../src/effects/effect_palette.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

    std::printf("test_click_bubble_effect: %d checks, %d failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
