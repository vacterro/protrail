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
    Comet = 2,     // bright enlarged head with thin fading tail
    Neon = 3,      // intense glow: wider + stronger outer pass
    Dotted = 4,    // evenly spaced dots along the path
    Pulse = 5,     // traveling spatial width/alpha wave
    Ribbon = 6,    // smooth alternating broad/narrow twist lobes
    Spark = 7,     // dots + deterministic per-dot alpha flicker
};

// T-021: sparkle DECORATION overlay family. This is deliberately a separate
// enum, never a new TrailStyle value: a sparkle mode composes with every
// existing style (Classic + Twinkle, Soft Glow + Stardust, ...). The
// historical TrailStyle::Spark stays exactly what it was and is neither
// removed nor reinterpreted. Off (the default) emits nothing, so fresh
// configurations keep the accepted T-019 appearance byte-for-byte.
enum class TrailSparkleMode : uint8_t {
    Off = 0,       // no sparkle layer (product default)
    Stardust = 1,  // soft tiny dust hugging the path
    Twinkle = 2,   // sparse star-like flashes that pulse
    Glitter = 3,   // dense tiny sharp shimmer
    Firefly = 4,   // sparse drifting luminous points
    Shards = 5,    // detached rotating triangular fragments
};

// Runtime trail configuration (T-008 B2, extended MVP 05 Phases E-G, T-015).
// Defaults/setters only this milestone: persistence owned by ConfigStorage.
// Values are validated at the construction site via TrailConfig::validated()
// so a future snapshot publisher cannot leak a degenerate config into the
// renderer (ARCHITECTURE.md configuration policy).
//
// DEFAULT VISUAL CONTRACT (Phase E / T-015 Phase 5; revised T-019):
// mode = Full, Start = 255,255,0, Fade = 255,255,0 (the yellow default is
// unchanged). T-019 retunes the shape defaults toward a visually
// continuous, soft line: smoothing 0.75 + Smooth fade curve. Fresh
// configurations and both restore paths construct this struct, so they
// all observe the revised values; persisted user settings are untouched.
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

    // Sparkle decoration bounds (T-021). Named constants are the single
    // authority for the GUI ranges and the persistence validation.
    static constexpr float kMinSparkleAmount = 0.0f;
    static constexpr float kMaxSparkleAmount = 1.0f;
    static constexpr float kMinSparkleSizePx = 1.0f;
    // T-023 rework (user visual FAIL: "barely noticeable"): the old 8 px
    // ceiling was the hard limit on how loud the layer could ever get --
    // at Stardust's 0.42 size scale the maximum emitted dot was 3.4 px.
    // Widening a clamp range never reinterprets a persisted value (every
    // stored size stays in range and keeps its meaning), so this needs no
    // schema bump.
    static constexpr float kMaxSparkleSizePx = 16.0f;
    static constexpr float kMinSparkleSpreadPx = 0.0f;
    static constexpr float kMaxSparkleSpreadPx = 32.0f;
    // Product defaults (mode Off keeps the T-019 appearance unchanged).
    // T-023 rework: turning a mode on must LOOK like turning it on, so the
    // shipped defaults sit in clearly-visible territory instead of at the
    // faint end the user rejected.
    static constexpr float kDefaultSparkleAmount = 0.70f;
    static constexpr float kDefaultSparkleSizePx = 4.5f;
    static constexpr float kDefaultSparkleSpreadPx = 10.0f;

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
    float smoothing = 0.75f;        // T-019: strong continuous curve
                                    // (0 = raw polyline, 1 = strongest)

    // T-021 sparkle decoration overlay. The layer is anchored to the SAME
    // visible path the stroke uses and derives its color from the SAME
    // local trail color (Solid/Head/Tail/Gradient); it adds no separate
    // color editor. Defaults keep the layer OFF.
    TrailSparkleMode sparkle_mode = TrailSparkleMode::Off;
    float sparkle_amount = kDefaultSparkleAmount;      // 0..1 intensity
    float sparkle_size_px = kDefaultSparkleSizePx;     // 1..16 px
    float sparkle_spread_px = kDefaultSparkleSpreadPx; // 0..32 px

    // Fade contract (Phase G), age-progress based:
    //   age_progress 0 = newest/head, 1 = lifetime boundary.
    //   fade_start 0     -> fade immediately from the head (legacy behavior)
    //   fade_start 0.5   -> newest 50% stays at base opacity, older 50% fades
    //   fade_start ~1    -> fades only near expiry.
    // Max is 0.95, not 1.0: alpha must still reach zero at the lifetime
    // boundary, so a non-empty fading span must survive validation.
    float fade_start = 0.0f;
    FadeCurve fade_curve = FadeCurve::Smooth;  // T-019: soft natural tail

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
        // T-020 Phase 8: an invalid persisted enum repairs to the current
        // safe product default (T-019 Smooth), never to the historical
        // Linear fallback. Valid Linear values pass through untouched.
        if (c.fade_curve != FadeCurve::Linear
            && c.fade_curve != FadeCurve::Smooth
            && c.fade_curve != FadeCurve::EaseOut) {
            c.fade_curve = FadeCurve::Smooth;
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
        // T-021 sparkle validation: out-of-range values clamp; a NaN (a
        // non-value that would poison every downstream comparison) repairs
        // to the product default. An invalid enum repairs to Off so a
        // corrupted persisted value can never light the layer accidentally.
        if (c.sparkle_mode != TrailSparkleMode::Off
            && c.sparkle_mode != TrailSparkleMode::Stardust
            && c.sparkle_mode != TrailSparkleMode::Twinkle
            && c.sparkle_mode != TrailSparkleMode::Glitter
            && c.sparkle_mode != TrailSparkleMode::Firefly
            && c.sparkle_mode != TrailSparkleMode::Shards) {
            c.sparkle_mode = TrailSparkleMode::Off;
        }
        if (c.sparkle_amount != c.sparkle_amount) c.sparkle_amount = kDefaultSparkleAmount;
        if (c.sparkle_amount < kMinSparkleAmount) c.sparkle_amount = kMinSparkleAmount;
        if (c.sparkle_amount > kMaxSparkleAmount) c.sparkle_amount = kMaxSparkleAmount;
        if (c.sparkle_size_px != c.sparkle_size_px) c.sparkle_size_px = kDefaultSparkleSizePx;
        if (c.sparkle_size_px < kMinSparkleSizePx) c.sparkle_size_px = kMinSparkleSizePx;
        if (c.sparkle_size_px > kMaxSparkleSizePx) c.sparkle_size_px = kMaxSparkleSizePx;
        if (c.sparkle_spread_px != c.sparkle_spread_px) c.sparkle_spread_px = kDefaultSparkleSpreadPx;
        if (c.sparkle_spread_px < kMinSparkleSpreadPx) c.sparkle_spread_px = kMinSparkleSpreadPx;
        if (c.sparkle_spread_px > kMaxSparkleSpreadPx) c.sparkle_spread_px = kMaxSparkleSpreadPx;
        return c;
    }

    bool operator==(const TrailConfig&) const = default;
};

} // namespace ptd
