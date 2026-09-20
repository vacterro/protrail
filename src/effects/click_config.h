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

    // T-022 Elemental Click VFX. Four elemental families that differ by
    // MOTION SIGNATURE first (the same rule T-021 applied to sparkles:
    // recognizable without reading the selected button) and carry an
    // elemental hue bias blended over the user's Click color by
    // ClickConfig::element_tint -- they never ignore the configured color.
    Air = 7,         // fast thin wide ring + tangentially swirling motes
    Fire = 8,        // buoyant embers rising with wobble, cooling with age
    Water = 9,       // up to 3 concentric ripple rings, staggered births
    Earth = 10,      // low dust ring + chunky debris arcing under gravity
};

// T-022: true only for the four elemental styles. Declared next to the
// enum so the config layer, the effect and the Settings UI all share ONE
// definition of "is this elemental" instead of repeating the comparison.
constexpr bool is_elemental_click_style(ClickStyle style) {
    return style == ClickStyle::Air || style == ClickStyle::Fire
        || style == ClickStyle::Water || style == ClickStyle::Earth;
}

// T-022: per-element hue target blended over the user's Click color.
// Exposed as a named struct (not three loose floats) so the effect, the
// renderer contract and the tests all speak about the same triple.
struct ElementTintTarget {
    float r = 0.0f;  // 0..255
    float g = 0.0f;
    float b = 0.0f;
};

// T-027: NaN-safe bounded clamp for a Hold multiplier.
//   in range        -> unchanged
//   NaN             -> the documented default (`fallback`)
//   below/above     -> clamped to the hard bound
// `!(v == v)` is the NaN test and is deliberately not written as `v != v`,
// which some compilers assume away under fast-math style flags.
[[nodiscard]] constexpr float clamp_hold_multiplier(float v, float lo, float hi,
                                                    float fallback) {
    if (!(v == v)) return fallback;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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
    static constexpr float kMinElementTint = 0.0f;  // T-022
    static constexpr float kMaxElementTint = 1.0f;  // T-022

    // T-027 Hold FX control bounds. These are ARTISTIC MULTIPLIERS with hard
    // bounds, not renderer magic numbers: the effect multiplies its named
    // internal baselines by them, so every value in range is safe by
    // construction and out-of-range values are clamped instead of trusted.
    static constexpr float kMinHoldIntensity = 0.25f;      // 1.0 == T-024 look
    static constexpr float kMaxHoldIntensity = 2.0f;
    static constexpr float kMinHoldWakeDensity = 0.25f;    // 1.0 == 12 px spacing
    static constexpr float kMaxHoldWakeDensity = 2.0f;
    static constexpr float kMinHoldWakeLifetimeMs = 150.0f;
    static constexpr float kMaxHoldWakeLifetimeMs = 2500.0f;
    static constexpr float kDefaultHoldWakeLifetimeMs = 800.0f;
    static constexpr float kMinHoldReleaseStrength = 0.5f;  // 1.0 == T-024 look
    static constexpr float kMaxHoldReleaseStrength = 2.0f;

    // T-36 Advanced Motion Wake bounds. The first four are ARTISTIC
    // MULTIPLIERS (identity at 1.0, so a migrated config reproduces the
    // accepted T-26/T-27 appearance exactly); the fifth is a movement gate.
    static constexpr float kMinWakeStrength = 0.25f;
    static constexpr float kMaxWakeStrength = 2.0f;
    static constexpr float kMinWakeSize = 0.5f;
    static constexpr float kMaxWakeSize = 2.0f;
    static constexpr float kMinWakeSpread = 0.0f;
    static constexpr float kMaxWakeSpread = 2.0f;
    static constexpr float kMinSpeedResponse = 0.0f;
    static constexpr float kMaxSpeedResponse = 2.0f;
    // 0 == any meaningful movement may emit wake.
    static constexpr float kMinMotionSpeedPxPerSec = 0.0f;
    static constexpr float kMaxMotionSpeedPxPerSec = 1000.0f;

    // D2D color channels in 0..255; alpha handled by the fade.
    // Cyan: complements the user-approved yellow trail.
    uint8_t color_r = 0;
    uint8_t color_g = 200;
    uint8_t color_b = 255;

    // Style contract (T-017 / Post-MVP V3): style family + particle count
    // for the burst styles. Ring keeps the exact approved MVP 04 look.
    ClickStyle style = ClickStyle::Ring;
    uint8_t particle_amount = 8;

    // T-022: strength of the elemental hue bias for the four elemental
    // styles ONLY (Air/Fire/Water/Earth); the seven T-017 styles ignore
    // it completely and keep rendering in the exact configured color.
    //   0.0 -> exactly the user's Click color (no elemental bias)
    //   1.0 -> exactly the element's target hue
    // The 0.65 default reads clearly elemental while the user's color is
    // still present in the result.
    float element_tint = 0.65f;

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

    // T-024 press-and-hold. A held button is a DIFFERENT gesture from a
    // click and now looks like one: once the button has been down past the
    // internal activation threshold the style builds a continuous charge
    // aura at the cursor, and releasing pays it off with a bubble scaled by
    // how long it was held. The one-shot click is untouched -- with
    // hold_enabled false the effect behaves exactly as it did before this
    // field existed, and a press shorter than the threshold stays exactly
    // one ordinary click in either case.
    //
    // T-027 removed the "only one hold control" limitation: the Hold FX
    // section now exposes Hold FX, Motion Wake, Intensity, Wake Density,
    // Wake Life and Release Strength. ClickStyle remains the ONLY style
    // authority -- there is deliberately no second hold style selector --
    // and the activation threshold stays an internal constant so the
    // short-click / HOLD gesture boundary remains stable and predictable.
    bool hold_enabled = true;

    // T-026/T-027 Hold Motion Wake. While an ACTIVE hold moves, the cursor
    // sheds style-specific effects into WORLD SPACE. Each emission keeps its
    // birth anchor forever and animates independently of the cursor, so the
    // pointer leaves visible consequences behind it instead of dragging one
    // attached animation around.
    bool hold_wake_enabled = true;

    // T-027 artistic multipliers. Each is EXACTLY the identity at 1.0, so a
    // default config reproduces the accepted T-024 appearance and behaviour.
    float hold_intensity = 1.0f;         // scales the ATTACHED aura only
    float hold_wake_density = 1.0f;      // maps to SPATIAL emission spacing
    float hold_wake_lifetime_ms = kDefaultHoldWakeLifetimeMs;
    float hold_release_strength = 1.0f;  // scales the charged release payoff

    // T-36 Advanced Motion Wake. Each multiplier is EXACTLY the identity at
    // its neutral value, so a config migrated from schema <= 10 keeps the
    // accepted T-26/T-27 look; accents default OFF.
    float wake_strength = 1.0f;          // alpha/brightness energy, not count
    float wake_size = 1.0f;              // spatial scale of detached geometry
    float wake_spread = 1.0f;            // lateral/angular scatter
    float speed_response = 1.0f;         // how strongly velocity drives energy
    float min_motion_speed_px_s = 0.0f;  // below: suppress detached wake
    bool turn_accent = false;            // one bounded accent per turn event
    bool stop_accent = false;            // one bounded accent per stop event

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
            && c.style != ClickStyle::DotRing
            && c.style != ClickStyle::Air
            && c.style != ClickStyle::Fire
            && c.style != ClickStyle::Water
            && c.style != ClickStyle::Earth) {
            c.style = ClickStyle::Ring;
        }
        if (c.element_tint < kMinElementTint) c.element_tint = kMinElementTint;
        if (c.element_tint > kMaxElementTint) c.element_tint = kMaxElementTint;
        if (c.particle_amount < kMinParticleAmount) c.particle_amount = kMinParticleAmount;
        if (c.particle_amount > kMaxParticleAmount) c.particle_amount = kMaxParticleAmount;
        // T-027: the Hold multipliers are clamped, never trusted. A NaN that
        // slipped in through JSON (Qt parses `null`/garbage as 0, never NaN,
        // but validated() must not depend on that) fails every comparison
        // below and is repaired to its documented default, so no slider can
        // reach a state that produces NaN geometry and no value outside the
        // published range reaches the renderer.
        c.hold_intensity = clamp_hold_multiplier(c.hold_intensity,
                                                kMinHoldIntensity,
                                                kMaxHoldIntensity, 1.0f);
        c.hold_wake_density = clamp_hold_multiplier(c.hold_wake_density,
                                                   kMinHoldWakeDensity,
                                                   kMaxHoldWakeDensity, 1.0f);
        c.hold_wake_lifetime_ms = clamp_hold_multiplier(
            c.hold_wake_lifetime_ms, kMinHoldWakeLifetimeMs,
            kMaxHoldWakeLifetimeMs, kDefaultHoldWakeLifetimeMs);
        c.hold_release_strength = clamp_hold_multiplier(
            c.hold_release_strength, kMinHoldReleaseStrength,
            kMaxHoldReleaseStrength, 1.0f);
        // T-36: the Advanced Motion Wake multipliers are clamped the same way.
        c.wake_strength = clamp_hold_multiplier(c.wake_strength,
                                                kMinWakeStrength,
                                                kMaxWakeStrength, 1.0f);
        c.wake_size = clamp_hold_multiplier(c.wake_size, kMinWakeSize,
                                            kMaxWakeSize, 1.0f);
        c.wake_spread = clamp_hold_multiplier(c.wake_spread, kMinWakeSpread,
                                              kMaxWakeSpread, 1.0f);
        c.speed_response = clamp_hold_multiplier(c.speed_response,
                                                 kMinSpeedResponse,
                                                 kMaxSpeedResponse, 1.0f);
        c.min_motion_speed_px_s = clamp_hold_multiplier(
            c.min_motion_speed_px_s, kMinMotionSpeedPxPerSec,
            kMaxMotionSpeedPxPerSec, 0.0f);
        return c;
    }

    // T-33: value equality. TrailConfig, RenderConfig and AppConfig already
    // compare by value; without this member AppConfig's defaulted operator==
    // is implicitly DELETED, which silently disables whole-config equality
    // (exactly what the Release Defaults and coverage regressions need).
    bool operator==(const ClickConfig&) const = default;
};

} // namespace ptd
