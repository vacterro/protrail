#pragma once

#include <cstdint>

namespace ptd {

// Trail fade-curve family (MVP 05 Phase G). Exactly three user-facing
// choices; no easing zoo. The curve applies to fade_progress AFTER the
// fade_start plateau, so alpha still reaches exactly 0 at the lifetime
// boundary for every curve.
enum class FadeCurve : uint8_t {
    Linear = 0,   // alpha falls proportionally with age (T-008 default)
    Smooth = 1,   // smoothstep 3p^2 - 2p^3: gentle start/end, steeper middle
    EaseOut = 2,  // 1 - (1-p)^3: strong initial drop, long gentle tail
};

// Trail color mode (T-015 Phase 2).
enum class TrailColorMode : uint8_t {
    Full = 0,
    StartAccent = 1,
    FadeAccent = 2,
    Gradient = 3
};

// Trail render style (T-016 / Post-MVP V2). Exactly eight user-facing
// styles; the effect owns per-segment alpha/width/spacing math, the
// renderer implements only the multi-pass glow stroke policy for
// SoftGlow/Neon. No shaders, no particles.
enum class TrailStyle : uint8_t {
    Classic = 0,   // approved baseline stroke
    SoftGlow = 1,  // wide soft outer pass + core (renderer multi-pass)
    Comet = 2,     // strong quadratic tail taper
    Neon = 3,      // intense glow: wider + stronger outer pass
    Dotted = 4,    // evenly spaced dots along the path
    Pulse = 5,     // time-based alpha pulse (pure function of now)
    Ribbon = 6,    // forced full tail-to-head width taper
    Spark = 7,     // dots + deterministic per-dot alpha flicker
};

// Runtime trail configuration (T-008 B2, extended MVP 05 Phases E-G, T-015).
// Defaults/setters only this milestone: persistence owned by ConfigStorage.
// Values are validated at the construction site via TrailConfig::validated()
// so a future snapshot publisher cannot leak a degenerate config into the
// renderer (ARCHITECTURE.md configuration policy).
//
// DEFAULT VISUAL COMPATIBILITY CONTRACT (Phase E / T-015 Phase 5):
// Defaults reproduce the approved MVP solid yellow trail exactly:
// mode = Full, Start = 255,255,0, Fade = 255,255,0.
struct TrailConfig {
    // Hard bounds shared by the GUI (Phase Q: value ranges come from these
    // named constants, not duplicated magic numbers in UI code).
    static constexpr float kMinThicknessPx = 0.5f;
    static constexpr float kMaxThicknessPx = 40.0f;
    static constexpr float kMinLifetimeMs = 50.0f;
    static constexpr float kMaxLifetimeMs = 2000.0f;
    static constexpr float kMinOpacity = 0.05f;
    static constexpr float kMaxOpacity = 1.0f;
    static constexpr float kMinSmoothing = 0.0f;
    static constexpr float kMaxSmoothing = 1.0f;
    static constexpr float kMinFadeStart = 0.0f;
    static constexpr float kMaxFadeStart = 0.95f;
    static constexpr float kMinGlowStrength = 0.0f;    // T-016
    static constexpr float kMaxGlowStrength = 1.0f;    // T-016
    static constexpr float kMinSegmentSpacingPx = 2.0f;   // T-016
    static constexpr float kMaxSegmentSpacingPx = 64.0f;  // T-016

    // Dual-color model (T-015 Phase 2):
    // Start / Head: newest trail area nearest the cursor, t = 1.
    // Fade / Tail: oldest visible trail area, t = 0.
    uint8_t start_color_r = 255;
    uint8_t start_color_g = 255;
    uint8_t start_color_b = 0;
    uint8_t fade_color_r = 255;
    uint8_t fade_color_g = 255;
    uint8_t fade_color_b = 0;
    TrailColorMode color_mode = TrailColorMode::Full;

    // Style contract (T-016 / Post-MVP V2):
    //   style          selects the render style family
    //   glow_strength  0..1 weight of the outer glow pass (SoftGlow/Neon)
    //   segment_spacing_px
    //                  arc-length gap between dots (Dotted/Spark)
    TrailStyle style = TrailStyle::Classic;
    float glow_strength = 0.5f;
    float segment_spacing_px = 12.0f;

    // Width contract (Phase F). head_thickness_px replaces the single
    // thickness_px; tail + taper_strength add the taper control surface.
    //   final_width(t) = lerp(head, target(t), taper_strength)
    //   target(t)      = lerp(tail, head, t)      // t: 0=tail, 1=head
    // so taper_strength 0 -> constant head width (approved baseline) and
    // 1 -> full tail-to-head interpolation.
    float head_thickness_px = 3.0f;   // thin line (approved baseline width)
    float tail_thickness_px = 3.0f;   // equal to head: no taper by default
    float taper_strength = 0.0f;      // 0 = constant (approved baseline)

    float lifetime_ms = 350.0f;     // short-lived trail
    float base_opacity = 0.9f;      // high at head, fades to ~0 at tail
    float smoothing = 0.5f;         // moderate; 0 = raw polyline, 1 = strongest

    // Fade contract (Phase G), age-progress based:
    //   age_progress 0 = newest/head, 1 = lifetime boundary.
    //   fade_start 0     -> fade immediately from the head (legacy behavior)
    //   fade_start 0.5   -> newest 50% stays at base opacity, older 50% fades
    //   fade_start ~1    -> fades only near expiry.
    // Max is 0.95, not 1.0: alpha must still reach zero at the lifetime
    // boundary, so a non-empty fading span must survive validation.
    float fade_start = 0.0f;
    FadeCurve fade_curve = FadeCurve::Linear;

    bool enabled = true;

    // Returns a clamped, render-safe copy. Every field has a hard bound so
    // a future settings GUI / persisted JSON cannot crash the renderer.
    [[nodiscard]] static constexpr TrailConfig validated(TrailConfig c) {
        if (c.head_thickness_px < kMinThicknessPx) c.head_thickness_px = kMinThicknessPx;
        if (c.head_thickness_px > kMaxThicknessPx) c.head_thickness_px = kMaxThicknessPx;
        if (c.tail_thickness_px < kMinThicknessPx) c.tail_thickness_px = kMinThicknessPx;
        if (c.tail_thickness_px > kMaxThicknessPx) c.tail_thickness_px = kMaxThicknessPx;
        if (c.taper_strength < 0.0f) c.taper_strength = 0.0f;
        if (c.taper_strength > 1.0f) c.taper_strength = 1.0f;
        if (c.lifetime_ms < kMinLifetimeMs) c.lifetime_ms = kMinLifetimeMs;
        if (c.lifetime_ms > kMaxLifetimeMs) c.lifetime_ms = kMaxLifetimeMs;
        if (c.base_opacity < kMinOpacity) c.base_opacity = kMinOpacity;
        if (c.base_opacity > kMaxOpacity) c.base_opacity = kMaxOpacity;
        if (c.smoothing < kMinSmoothing) c.smoothing = kMinSmoothing;
        if (c.smoothing > kMaxSmoothing) c.smoothing = kMaxSmoothing;
        if (c.fade_start < kMinFadeStart) c.fade_start = kMinFadeStart;
        if (c.fade_start > kMaxFadeStart) c.fade_start = kMaxFadeStart;
        if (c.fade_curve != FadeCurve::Linear
            && c.fade_curve != FadeCurve::Smooth
            && c.fade_curve != FadeCurve::EaseOut) {
            c.fade_curve = FadeCurve::Linear;
        }
        if (c.color_mode != TrailColorMode::Full
            && c.color_mode != TrailColorMode::StartAccent
            && c.color_mode != TrailColorMode::FadeAccent
            && c.color_mode != TrailColorMode::Gradient) {
            c.color_mode = TrailColorMode::Full;
        }
        if (c.style != TrailStyle::Classic
            && c.style != TrailStyle::SoftGlow
            && c.style != TrailStyle::Comet
            && c.style != TrailStyle::Neon
            && c.style != TrailStyle::Dotted
            && c.style != TrailStyle::Pulse
            && c.style != TrailStyle::Ribbon
            && c.style != TrailStyle::Spark) {
            c.style = TrailStyle::Classic;
        }
        if (c.glow_strength < kMinGlowStrength) c.glow_strength = kMinGlowStrength;
        if (c.glow_strength > kMaxGlowStrength) c.glow_strength = kMaxGlowStrength;
        if (c.segment_spacing_px < kMinSegmentSpacingPx) c.segment_spacing_px = kMinSegmentSpacingPx;
        if (c.segment_spacing_px > kMaxSegmentSpacingPx) c.segment_spacing_px = kMaxSegmentSpacingPx;
        return c;
    }

    bool operator==(const TrailConfig&) const = default;
};

} // namespace ptd
