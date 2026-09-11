#pragma once

#include <cstdint>

namespace ptd {

// Click easing family (MVP 05 Phase J). Exactly three user-facing
// choices mapped onto the internal animation math; no easing zoo, no
// exposure of implementation details beyond these names.
enum class ClickEasing : uint8_t {
    Linear = 0,   // constant expansion rate
    Smooth = 1,   // smoothstep 3p^2 - 2p^3: gentle start/end
    EaseOut = 2,  // 1 - (1-p)^3 -- the user-approved MVP 04 curve (default)
};

// Click render style (T-017 / Post-MVP V3). The effect owns the per-style
// emit decisions (rings, particles, flash); the renderer only draws what
// the sink receives. No shaders, no particle editor.
enum class ClickStyle : uint8_t {
    Ring = 0,        // approved MVP 04 bubble ring (+ subtle fill)
    DoubleRing = 1,  // main ring + inner secondary ring
    Ripple = 2,      // thin pure ring, no fill
    Burst = 3,       // ring + particles flying outward
    SparkBurst = 4,  // deterministic-jitter particle burst (fewer rings)
    SoftFlash = 5,   // soft filled flash disc, no ring
    DotRing = 6,     // central dot + ring
};

// Runtime click-bubble configuration (T-009 / MVP 04, extended MVP 05
// Phase J). Defaults reproduce the user-approved click appearance. Values
// are validated at the construction site via ClickConfig::validated() so
// a settings GUI / persisted JSON cannot crash the renderer
// (ARCHITECTURE.md configuration policy), the same contract as
// TrailConfig.
struct ClickConfig {
    // Hard bounds shared by the GUI (Phase Q: ranges come from these
    // named constants, not duplicated magic numbers in UI code).
    static constexpr float kMinRadiusPx = 0.0f;
    static constexpr float kMaxStartRadiusPx = 128.0f;
    static constexpr float kMaxEndRadiusPx = 512.0f;
    static constexpr float kMinDurationMs = 50.0f;
    static constexpr float kMaxDurationMs = 2000.0f;
    static constexpr float kMinOpacity = 0.05f;
    static constexpr float kMaxOpacity = 1.0f;
    static constexpr float kMinOutlinePx = 0.5f;
    static constexpr float kMaxOutlinePx = 20.0f;
    static constexpr float kMinFillOpacity = 0.0f;
    static constexpr float kMaxFillOpacity = 1.0f;
    static constexpr int kMinParticleAmount = 0;    // T-017
    static constexpr int kMaxParticleAmount = 24;   // T-017

    // D2D color channels in 0..255; alpha handled by the fade.
    // Cyan: complements the user-approved yellow trail.
    uint8_t color_r = 0;
    uint8_t color_g = 200;
    uint8_t color_b = 255;

    // Style contract (T-017 / Post-MVP V3): style family + particle count
    // for the burst styles. Ring keeps the exact approved MVP 04 look.
    ClickStyle style = ClickStyle::Ring;
    uint8_t particle_amount = 8;

    // Per-button triggers (Phase J). All three enabled by default, which
    // matches the MVP 04 behavior (any ButtonAction::Down spawned).
    bool trigger_left = true;
    bool trigger_right = true;
    bool trigger_middle = true;

    float start_radius_px = 8.0f;        // bubble born just around the click
    float end_radius_px = 26.0f;         // expands to a compact ring
    float duration_ms = 250.0f;          // quick life (roadmap: 200-300 ms)
    float base_opacity = 0.85f;          // clearly visible, not loud
    float outline_thickness_px = 2.5f;   // thin ring stroke

    // Fill contract (Phase K): the effect emits the fill alpha; the
    // renderer must not re-derive it. 0.15 approximates the previous
    // hard-coded 0.18 * base blend at the mid-life of the approved look
    // (subtle translucent inner disc, never loud).
    float fill_opacity = 0.15f;

    ClickEasing easing = ClickEasing::EaseOut;  // approved MVP 04 character

    bool enabled = true;

    // Returns a clamped, render-safe copy. Every field has a hard bound;
    // end radius is additionally forced to stay >= start radius so the
    // animation can never shrink the bubble (degenerate configs included).
    [[nodiscard]] static constexpr ClickConfig validated(ClickConfig c) {
        if (c.start_radius_px < kMinRadiusPx) c.start_radius_px = kMinRadiusPx;
        if (c.start_radius_px > kMaxStartRadiusPx) c.start_radius_px = kMaxStartRadiusPx;
        if (c.end_radius_px < 1.0f) c.end_radius_px = 1.0f;
        if (c.end_radius_px > kMaxEndRadiusPx) c.end_radius_px = kMaxEndRadiusPx;
        if (c.end_radius_px < c.start_radius_px) c.end_radius_px = c.start_radius_px;
        if (c.duration_ms < kMinDurationMs) c.duration_ms = kMinDurationMs;
        if (c.duration_ms > kMaxDurationMs) c.duration_ms = kMaxDurationMs;
        if (c.base_opacity < kMinOpacity) c.base_opacity = kMinOpacity;
        if (c.base_opacity > kMaxOpacity) c.base_opacity = kMaxOpacity;
        if (c.outline_thickness_px < kMinOutlinePx) c.outline_thickness_px = kMinOutlinePx;
        if (c.outline_thickness_px > kMaxOutlinePx) c.outline_thickness_px = kMaxOutlinePx;
        if (c.fill_opacity < kMinFillOpacity) c.fill_opacity = kMinFillOpacity;
        if (c.fill_opacity > kMaxFillOpacity) c.fill_opacity = kMaxFillOpacity;
        if (c.easing != ClickEasing::Linear
            && c.easing != ClickEasing::Smooth
            && c.easing != ClickEasing::EaseOut) {
            c.easing = ClickEasing::EaseOut;
        }
        if (c.style != ClickStyle::Ring
            && c.style != ClickStyle::DoubleRing
            && c.style != ClickStyle::Ripple
            && c.style != ClickStyle::Burst
            && c.style != ClickStyle::SparkBurst
            && c.style != ClickStyle::SoftFlash
            && c.style != ClickStyle::DotRing) {
            c.style = ClickStyle::Ring;
        }
        if (c.particle_amount < kMinParticleAmount) c.particle_amount = kMinParticleAmount;
        if (c.particle_amount > kMaxParticleAmount) c.particle_amount = kMaxParticleAmount;
        return c;
    }
};

} // namespace ptd
