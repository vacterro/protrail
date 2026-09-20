#pragma once

#include "../core/cursor_sample.h"
#include "click_config.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptd {

// ClickBubbleEffect (T-009 / MVP 04): owns ALL click-bubble lifecycle,
// animation-state and geometry decisions. It consumes normalized input
// events (CursorSample button transitions) and emits per-bubble render
// state; it never touches Direct2D, windows, or the raw-input API
// (rendering ownership stays with OverlayWindow, input with MouseInput --
// same split as TrailEffect, ROADMAP/ARCHITECTURE.md).
//
// Event flow (C3): MouseInput -> CursorSample -> Application/controller ->
// on_button_down() -> spawn(). The spawn position is the position stored
// IN the click CursorSample (the authoritative GetCursorPos coordinate of
// the down packet); the bubble stays anchored there even if the mouse
// moves immediately afterward.
//
// Time model (C5): every animation value is a pure function of
// elapsed = now_ns - start_timestamp_ns (monotonic QPC clock). No
// per-frame state mutation, no pixels-per-frame integration, so results
// are identical at any frame cadence.
//
//   progress = clamp(elapsed / duration, 0..1)
//   ease     = ease_out_cubic(progress) = 1 - (1 - progress)^3
//   radius   = start_radius + (end_radius - start_radius) * ease
//   opacity  = base_opacity * (1 - progress)
//
// A bubble is live while progress < 1. Expired bubbles are removed by
// prune(). Memory is bounded: fixed vector + kMaxActiveBubbles cap (C9);
// on overflow completed bubbles are pruned first, then the OLDEST active
// bubble is discarded. Single-threaded (GUI thread), like CursorHistory.

// Output interface for ClickBubbleEffect drawing (C7 + MVP 05 Phase K),
// mirroring TrailGeometrySink: the effect describes render state (center,
// radii, opacity, outline, fill), the renderer issues the Direct2D calls.
// Coordinates arrive in virtual-screen physical pixels; the overlay-local
// transform is applied by the renderer sink, not here (same contract as
// the trail, C8).
class ClickBubbleSink {
public:
    virtual ~ClickBubbleSink() = default;

    // Called once before the first add_bubble with the number of bubbles
    // about to be drawn (sinks preallocate once; no per-frame churn).
    // Named distinctly from TrailGeometrySink::reserve_hint so a renderer
    // implementing both interfaces keeps the two contracts separate.
    virtual void reserve_bubbles_hint(int bubble_count) = 0;

    // One bubble: centered at (cx, cy), radius_px outer radius,
    // outline_thickness_px stroke width, color channels 0..255, ring
    // alpha in [0, base_opacity], fill_alpha in [0, fill_opacity] -- the
    // EFFECT computes both alphas (Phase K); the renderer must not
    // re-derive the fill from the ring.
    virtual void add_bubble(float cx, float cy, float radius_px,
                            float outline_thickness_px,
                            float r, float g, float b,
                            float ring_alpha, float fill_alpha) = 0;

    // T-017: one particle dot (Burst / Spark Burst / Dot Ring center).
    // Default empty body keeps existing sink implementations (test
    // doubles) compiling; styles that never emit particles pay nothing.
    virtual void add_particle(float cx, float cy, float radius_px,
                              float r, float g, float b, float alpha) {
        (void)cx; (void)cy; (void)radius_px;
        (void)r; (void)g; (void)b; (void)alpha;
    }
};

// T-026: one wake mark -- the bounded unit of geometry ONE wake emission may
// produce. The renderer contract is the existing ClickBubbleSink; a mark only
// says which call it becomes:
//   Ring      -> add_bubble(..., ring_alpha = alpha, fill_alpha = 0)
//   Disc      -> add_bubble(..., ring_alpha = 0, fill_alpha = alpha)
//   Particle  -> add_particle(...)
// Exposed so the style regressions can assert STRUCTURE ("Ring emits ring
// geometry and no particle cloud") instead of comparing screenshots.
enum class WakeMarkKind : uint8_t {
    Ring = 0,
    Disc = 1,
    Particle = 2,
};

struct WakeMark {
    float x = 0.0f;
    float y = 0.0f;
    float radius_px = 0.0f;
    float thickness_px = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float alpha = 0.0f;
    WakeMarkKind kind = WakeMarkKind::Particle;
};

// T-026 Hold Motion Wake: ONE immutable world-space emission, born while an
// ACTIVE hold moves. Every field is frozen at birth; render geometry is
// geometry = f(emission, elapsed) with no evolving particle state, no
// per-frame integration and no per-frame RNG. That is what makes an emission
// keep its anchor -- and its appearance -- after the cursor has moved on.
//
// Deliberately NOT stored: particle positions, particle velocities, any
// mutable accumulator. The only mutable part of the whole feature is the
// INPUT-side bookkeeping on Hold (last point, residual distance, ordinal).
struct HoldWakeEmission {
    float x = 0.0f;              // IMMUTABLE world anchor, virtual-screen px
    float y = 0.0f;              // IMMUTABLE world anchor
    int64_t birth_timestamp_ns = 0;
    uint32_t seed = 0;           // deterministic variation, fixed at birth
    ClickStyle style = ClickStyle::Ring;
    float tangent_x = 0.0f;      // normalized motion direction at birth
    float tangent_y = 0.0f;
    float motion_speed = 0.0f;   // px/s snapshot (finite, bounded)
    float motion_energy = 0.0f;  // clamp(speed / kWakeReferenceSpeedPxPerSec)
    float charge = 0.0f;         // 0..1 charge at birth
    // ---- visual snapshot: changing Settings affects NEW emissions only ----
    float color_r = 0.0f;        // user Click color at birth (0..255)
    float color_g = 0.0f;
    float color_b = 0.0f;
    float element_tint = 0.0f;
    int particle_amount = 0;     // within-emission budget snapshot
    float size_basis_px = 0.0f;  // end_radius_px at birth
    float thickness_px = 0.0f;
    float intensity = 1.0f;      // hold_intensity at birth
    float lifetime_ms = 800.0f;  // hold_wake_lifetime_ms at birth
    // T-36 Advanced Motion Wake snapshot. Each multiplier is frozen here so a
    // later Settings change affects NEW emissions only; all are identity at
    // their neutral value, so a migrated emission is bit-identical to T-26/T-27.
    float wake_strength = 1.0f;
    float wake_size = 1.0f;
    float wake_spread = 1.0f;
    // T-36: a Turn/Stop accent is a world-anchored emission exactly like a
    // normal wake record, distinguished only by this flag so the regressions
    // can assert accent births without changing the geometry contract.
    bool accent = false;
};

class ClickBubbleEffect {
public:
    // Defensive active-bubble cap (C9). 128 covers any realistic clicking
    // cadence for the 250 ms bubble life (including synthetic/stress
    // input) while bounding per-frame draw cost and memory.
    static constexpr std::size_t kMaxActiveBubbles = 128;

    // ---- T-026 bounded wake policy ----
    // Hard global cap on detached wake memory and per-frame draw cost. When
    // the cap is hit, EXPIRED emissions are pruned first; if that is not
    // enough the OLDEST emission is discarded. Overflow NEVER touches active
    // hold state -- losing wake history must not cancel a gesture.
    static constexpr std::size_t kMaxWakeEmissions = 192;
    // A single mouse movement sample may not create more than this many
    // emissions, no matter how large the cursor teleport was. A movement that
    // needs more crossings than the budget is interpolated deterministically
    // across the budget instead of spawning thousands of records.
    static constexpr int kMaxWakeBirthsPerMovement = 14;
    // Bounded geometry: the most marks ONE emission may ever produce.
    static constexpr std::size_t kMaxWakeMarksPerEmission = 10;
    // Speed that maps to motion_energy 1.0. Slower still reads as a wake;
    // faster never removes a cap, it only raises the artistic energy term.
    static constexpr float kWakeReferenceSpeedPxPerSec = 2200.0f;
    // Emission spacing at wake density 1.0. Wake Density scales it
    // (higher density -> smaller spacing); this is a SPATIAL unit, never a
    // per-frame or per-event particle count.
    static constexpr float kBaseWakeSpacingPx = 12.0f;
    // Base loudness of a wake emission at intensity 1.0.
    static constexpr float kWakeIntensity = 0.62f;

    // T-36 motion-condition thresholds (internal, never user-editable safety
    // or gesture constants). A movement counts as "meaningful" for the motion
    // state only when no explicit minimum-speed gate is configured; the turn
    // accent requires a sharp angle and a cooldown so jitter cannot spam it.
    static constexpr float kMotionMeaningfulSpeedPxPerSec = 120.0f;
    static constexpr float kTurnAccentMinAngleRad = 1.3962634f;  // ~80 degrees
    static constexpr int64_t kTurnAccentCooldownNs = 250'000'000LL;  // 250 ms

    // Wake Density -> emission spacing. Smooth, bounded, monotone
    // decreasing: density 0.25 -> ~34 px, 1.00 -> 12 px, 2.00 -> ~7 px.
    static float wake_spacing_px(float density);

    // T-36: minimum-speed gate. True when the configured threshold does NOT
    // suppress a wake whose speed snapshot is `speed_px_s`. Pure and testable:
    // the effect calls it per eligible segment, and 0 always passes.
    static bool wake_speed_passes_gate(float speed_px_s, float min_speed_px_s);

    // T-36: bounded speed-response factor in [0, 2]. 1.0 reproduces the
    // accepted T-26 energy term; 0 removes amplification; 2 roughly doubles
    // the throw/spread read without relaxing any cap.
    static float speed_response_factor(float speed_px_s,
                                       float reference_px_s,
                                       float response);

    // T-36: the turn angle (radians, 0..pi) between two unit directions.
    // Pure; returns 0 when either direction is degenerate.
    static float turn_angle_rad(float dir_ax, float dir_ay,
                                float dir_bx, float dir_by);

    // One bubble's bounded state (C4): anchor + birth timestamp. Derived
    // values (progress/radius/opacity) are computed on demand from elapsed
    // time; no frame-by-frame history is stored.
    struct Bubble {
        float x = 0.0f;             // virtual-screen physical pixels
        float y = 0.0f;             // virtual-screen physical pixels
        int64_t start_timestamp_ns = 0;  // monotonic clock
        // T-025: this bubble's own variation seed, fixed at spawn from the
        // anchor and the birth timestamp. It is what makes two clicks look
        // like two clicks instead of the same stencil twice, WITHOUT giving
        // up the pure-time-function contract: the seed is stored once and
        // never mutated, so every derived value stays a pure function of
        // (bubble, elapsed) and is identical at any frame cadence.
        uint32_t seed = 0;
        // T-024: release payoff. 1.0 for an ordinary click; a bubble spawned
        // by RELEASING a held button carries more, scaled by how long the
        // button was held, so the payoff is proportional to the charge the
        // user actually watched build up.
        float power = 1.0f;
    };

    // T-024: one press-and-hold lifecycle record. At most one per physical
    // button, so this is bounded by kMaxActiveHolds and needs no cap policy
    // of its own. Like Bubble it stores only an anchor, a birth time and a
    // seed: BOTH the Candidate/ActiveHold distinction AND the charge are
    // DERIVED from elapsed time, never accumulated or promoted per frame, so
    // a dropped or doubled frame cannot change either.
    //
    // A record exists from the Down transition onward and is a CANDIDATE
    // while (now - start) < kHoldActivationMs: it keeps enough lifecycle
    // state to be promoted, but it draws nothing and pays off nothing. Past
    // that threshold it is an ACTIVE HOLD: the aura becomes visible and a
    // later Up earns the release payoff. That is what keeps a short ordinary
    // click exactly one ordinary click -- see is_active_at().
    struct Hold {
        MouseButton button = MouseButton::None;
        float x = 0.0f;   // follows the cursor while held (see on_cursor_moved)
        float y = 0.0f;
        int64_t start_timestamp_ns = 0;  // the Down timestamp
        uint32_t seed = 0;

        // ---- T-026 input-driven wake bookkeeping ----
        // These are the ONLY mutable values in the whole wake feature, and
        // they describe the INPUT, never the rendered effect. An emission
        // recorded from them is immutable from the moment it is born.
        float last_x = 0.0f;         // previous movement point of this hold
        float last_y = 0.0f;
        int64_t last_move_ns = 0;    // timestamp of that previous point
        float residual_px = 0.0f;    // distance carried toward next emission
        uint32_t wake_ordinal = 0;   // deterministic emission counter

        // ---- T-36 motion-condition state (input side only) ----
        // All DERIVED from movement vectors and timestamps, never from frame
        // cadence. `last_dir_x/y` is the previous unit direction (0,0 before
        // the first movement); `moving` is the hysteresis state used to fire
        // exactly one Stop Accent per movement episode. `stop_armed` is set
        // true on any meaningful movement and cleared when the stop accent
        // fires, so a restart re-arms the next one.
        float last_dir_x = 0.0f;
        float last_dir_y = 0.0f;
        bool has_dir = false;
        bool moving = false;
        bool stop_armed = false;
        int64_t last_turn_accent_ns = 0;
    };

    // T-025: the per-bubble seed. Pure, no RNG state, no global entropy --
    // the same (x, y, timestamp) always yields the same seed, which is what
    // keeps replay and the determinism regressions meaningful.
    static uint32_t bubble_seed(float x, float y, int64_t start_timestamp_ns);

    explicit ClickBubbleEffect(ClickConfig config = {});

    // T-024: a config change is a lifecycle event now, not a blind
    // assignment. Turning the click effect or Hold FX off cancels every
    // candidate/active hold IMMEDIATELY -- the user must never be left
    // watching an aura that the settings say is disabled, and no Up may
    // later pay off a hold that was cancelled.
    void set_config(const ClickConfig& config);
    const ClickConfig& config() const { return config_; }

    // C3/C17: normalized input entry point. Spawns exactly one bubble for
    // a ButtonAction::Down transition when the sample's button trigger is
    // enabled in the config (trigger_left/right/middle, Phase J) AND the
    // effect is enabled. Up transitions and movement samples spawn
    // nothing. Returns true when a bubble was spawned.
    bool on_button_down(const CursorSample& sample);

    // Direct spawn primitive (used by on_button_down; exposed for tests).
    // Anchors the bubble at (x, y) with birth time start_timestamp_ns.
    bool spawn(float x, float y, int64_t start_timestamp_ns);

    // ---- T-024 press-and-hold ----
    //
    // C3 again, for the transition the app used to throw away: an Up
    // transition ends the matching hold and spawns the RELEASE payoff --
    // one bubble at the release position whose power scales with how long
    // the button was held. Returns true when a payoff bubble was spawned.
    // An Up with no matching hold (hold disabled, effect disabled, button
    // not triggered, or the Down was never seen) is a no-op returning false,
    // so a lost Down can never produce a payoff out of nowhere.
    bool on_button_up(const CursorSample& sample);

    // A held button that the user then DRAGS must not leave its aura behind
    // at the press point -- the aura belongs to the pressing finger. Feed
    // movement samples here; button samples are ignored (their own handlers
    // own those). Cheap: it moves at most kMaxActiveHolds anchors.
    void on_cursor_moved(const CursorSample& sample);

    // T-024 INTERNAL constants. Deliberately NOT ClickConfig fields: the
    // milestone exposes exactly one user control (Hold FX on/off), so these
    // are named constants rather than hidden persisted knobs no UI can edit.

    // Candidate -> ActiveHold threshold. Below it a press is only ever an
    // ordinary click. 175 ms sits inside the 150-200 ms band: past any
    // normal click (which lands well under 150 ms) and still well short of
    // what a user experiences as holding a button down.
    static constexpr float kHoldActivationMs = 175.0f;

    // Time from ACTIVATION (not from Down) to full charge.
    static constexpr float kHoldChargeMs = 600.0f;

    // Base loudness of the hold aura. The release payoff does not depend on
    // it -- muting the aura would never mute the discharge.
    static constexpr float kHoldIntensity = 0.80f;

    // Charge in 0..1, measured from ACTIVATION:
    //   charge = clamp((elapsed - kHoldActivationMs) / kHoldChargeMs, 0..1)
    // A pure function of elapsed monotonic time, exactly like progress_at().
    // A button may stay held indefinitely after reaching 1.0; there is NO
    // maximum hold duration anywhere in this effect.
    float charge_at(const Hold& hold, int64_t now_ns) const;

    // Candidate -> ActiveHold test, derived (never stored): true once the
    // button has been physically down for at least kHoldActivationMs.
    static bool is_active_at(const Hold& hold, int64_t now_ns);

    // ---- T-026 Hold Motion Wake ----

    // One hold per physical button; there are three.
    static constexpr std::size_t kMaxActiveHolds = 3;

    // Every emission ever born in the current session, oldest first, bounded
    // by kMaxWakeEmissions. Birth-ordered, so "oldest" is begin().
    const std::vector<HoldWakeEmission>& wake_emissions() const { return wake_; }
    std::size_t wake_count() const { return wake_.size(); }

    // Emissions still inside their own snapshotted lifetime at now_ns. The
    // scheduler must stay awake while this is non-zero even with no hold and
    // no bubble alive: the consequences outlive the gesture by design.
    std::size_t live_wake_count(int64_t now_ns) const;
    bool has_live_wake(int64_t now_ns) const;

    // Elapsed-normalized progress of one emission in 0..1 (1 at/after the
    // emission's OWN snapshotted lifetime). Pure function of the record.
    static float wake_progress_at(const HoldWakeEmission& e, int64_t now_ns);

    // The geometry of ONE emission at now_ns, as a small bounded array of
    // marks. This is the single source of truth for both draw() and the
    // style regressions: whatever this returns is exactly what is rendered.
    // Returns the number of marks written (never more than capacity, and
    // never more than kMaxWakeMarksPerEmission).
    std::size_t wake_marks(const HoldWakeEmission& e, int64_t now_ns,
                           WakeMark* out, std::size_t capacity) const;

    // C9: remove completed bubbles (age >= duration) AND expired wake
    // emissions. Safe to call every frame. Returns the number of removed
    // items. Never touches a hold record.
    std::size_t prune(int64_t now_ns);

    // Every lifecycle record, candidates included. Bounded by kMaxActiveHolds.
    std::size_t hold_record_count() const { return holds_.size(); }
    bool has_hold_records() const { return !holds_.empty(); }

    // Records that have crossed the activation threshold at now_ns.
    std::size_t active_hold_count(int64_t now_ns) const;

    const std::vector<Hold>& holds() const { return holds_; }

    // Records that have NOT yet crossed it at now_ns.
    std::size_t candidate_count(int64_t now_ns) const;

    // Whether any hold is ACTIVE (drawing an aura) at now_ns.
    bool has_active_hold(int64_t now_ns) const;

    // T-024 lost-Up recovery. An Up can genuinely go missing (focus loss, a
    // dropped raw-input packet, alt-tab mid-drag). The answer is NOT an
    // arbitrary maximum hold duration -- a user is allowed to hold a button
    // for as long as they like -- it is reconciling the logical state against
    // what the OS says the physical buttons are actually doing.
    //
    // Pure and injectable on purpose: the OS query (GetAsyncKeyState) lives in
    // the Application, this method only takes the three booleans. A record
    // whose button is logically down but physically up is CANCELLED: no
    // payoff is invented, and no stale aura survives. Returns how many
    // records were cancelled.
    //
    // The caller must only run this while hold records exist; there is no
    // idle polling timer anywhere in the product.
    std::size_t reconcile_physical_buttons(bool left_down, bool right_down,
                                           bool middle_down);

    // Ends every candidate/hold WITHOUT paying off. Used by the master-off
    // path and by set_config(): the user turned something off, so nothing
    // should fire.
    //
    // T-026: holds only. Already-born wake emissions are NOT erased here
    // because this is also the lost-Up recovery path, where the visual
    // history of the drag is explicitly allowed to finish. The paths that
    // must erase it (master OFF, Click FX OFF, Hold FX OFF, Motion Wake OFF,
    // explicit clear) call clear_wake() or clear().
    void cancel_holds() { holds_.clear(); }

    // T-026: drop every detached wake emission NOW (no fade). Used when the
    // user switches Motion Wake off, or when Hold FX / the effect goes off:
    // the setting response must be visually immediate, and stale wake must
    // never outlive the feature that produced it.
    void clear_wake() { wake_.clear(); }

    // C5/C10 + T-026: whether any click-side content is still animating at
    // `now_ns` -- bubbles, hold records (candidates included) or detached
    // wake emissions. Drives the shared active-only render scheduler
    // together with the trail.
    bool has_live_content(int64_t now_ns) const;

    // Number of currently active (unexpired) bubbles.
    std::size_t active_count() const { return bubbles_.size(); }
    bool empty() const { return bubbles_.empty(); }
    void clear() { bubbles_.clear(); holds_.clear(); wake_.clear(); }

    const std::vector<Bubble>& bubbles() const { return bubbles_; }

    // ---- Pure animation math (C12 + MVP 05 Phase J: testable without
    // Direct2D) ----

    // Easing application (Phase J). f: [0,1] -> [0,1], f(0)=0, f(1)=1:
    //   Linear  -> p                     (constant expansion rate)
    //   Smooth  -> 3p^2 - 2p^3            (gentle start and end)
    //   EaseOut -> 1 - (1-p)^3           (the approved MVP 04 curve)
    static float apply_easing(ClickEasing easing, float p);

    // Kept for regression compatibility: the approved MVP 04 curve.
    static float ease_out_cubic(float progress);

    // progress = clamp((now - start) / duration, 0..1); pure function of
    // elapsed monotonic time and the config duration.
    float progress_at(const Bubble& bubble, int64_t now_ns) const;

    // radius = start + (end - start) * apply_easing(easing, progress).
    // Monotone non-decreasing in progress for every easing; exactly
    // start_radius at birth and exactly end_radius at expiry.
    float radius_at(const Bubble& bubble, int64_t now_ns) const;

    // opacity = base_opacity * (1 - progress). Exactly base_opacity at
    // birth, exactly 0 at/after expiry.
    float opacity_at(const Bubble& bubble, int64_t now_ns) const;

    // Fill alpha (Phase K): fill_opacity * (1 - progress) -- the subtle
    // inner disc fades with the same time contract as the ring. Exactly
    // fill_opacity at birth, exactly 0 at/after expiry.
    float fill_alpha_at(const Bubble& bubble, int64_t now_ns) const;

    // ---- T-022 Elemental Click VFX: pure math (no Direct2D, no state) ----
    //
    // The four elemental families are defined by their MOTION SIGNATURE,
    // exactly the way T-021 defined the sparkle families: recognizable
    // without looking at the selected button. Every value below is a pure
    // function of (bubble anchor, particle index, progress), so the result
    // is identical at any frame cadence and fully testable here.

    // True only for Air/Fire/Water/Earth. The seven T-017 styles are
    // untouched by every elemental path, including the tint.
    static bool is_elemental(ClickStyle style);

    // Deterministic 0..1 value for per-particle variation. No RNG, no
    // frame state: the same index always yields the same number.
    static float hash01(uint32_t seed);

    // T-025 per-particle LIFETIME. Without this every Burst/SparkBurst
    // particle lived exactly the parent duration, so a burst always
    // disappeared as one flat sheet -- the variation was spatial only. The
    // returned value is the FRACTION of the parent bubble life for which
    // this particle stays visible:
    //   Burst      0.78 .. 1.00  (a mild, readable spread)
    //   SparkBurst 0.55 .. 1.00  (sparks visibly die at different times)
    // Derived from the bubble seed, the particle index and a dedicated
    // lifetime salt, so it is a pure function with no mutable RNG: the same
    // click always replays the identical lifetime layout, and two different
    // clicks lay out differently. A particle is emitted while
    // progress < lifetime fraction and simply STOPS being emitted after --
    // its trajectory is never reversed, so visible radial distance stays
    // monotone non-decreasing right up to the moment it disappears.
    static float burst_particle_life(uint32_t bubble_seed, int index, bool spark);

    // T-024: geometry and alpha gains for a release payoff of the given
    // power (1.0 for an ordinary click). Exposed because EVERY style family
    // must honour the charge, not just the ring-based ones, and the
    // regressions assert that through these functions.
    static float power_size_gain(float power);
    static float power_alpha(float alpha, float power);

    // The element's target hue in 0..255 channels. `cool` (0..1) is used
    // ONLY by Fire, whose embers cool from bright yellow-orange (0) to
    // deep red-orange (1) as they age; the other elements ignore it.
    static ElementTintTarget element_target(ClickStyle style, float cool);

    // The user's Click color biased toward the element target by
    // `strength` (ClickConfig::element_tint):
    //   strength 0 -> exactly the user color, strength 1 -> exactly the
    //   element target, linear in between. A non-elemental style returns
    //   the user color unchanged whatever the strength is.
    static ElementTintTarget elemental_color(ClickStyle style,
                                             float user_r, float user_g, float user_b,
                                             float strength, float cool);

    // Per-element particle budget derived from the user's particle_amount
    // (0 always means 0 -- particles off stays off):
    //   Air   -> particle_amount        (airy motes)
    //   Fire  -> particle_amount        (embers)
    //   Water -> particle_amount / 3    (a few droplets; the rings carry it)
    //   Earth -> about half, min 3      (fewer, heavier chunks)
    static int elemental_particle_count(ClickStyle style, int particle_amount);

    // One elemental particle at `progress`. `cool` is the Fire age term
    // fed back into elemental_color; other elements leave it at 0.
    struct ElementParticle {
        float x = 0.0f;
        float y = 0.0f;
        float radius_px = 0.0f;
        float alpha = 0.0f;
        float cool = 0.0f;
    };
    ElementParticle elemental_particle(const Bubble& b, int index, int count,
                                       float progress) const;

    // Water emits up to three concentric ripple rings whose births are
    // staggered in progress, so the click reads as a repeating wave rather
    // than one expanding circle. A ring that has not been born yet (or has
    // already completed) reports live = false.
    static constexpr int kWaterRipples = 3;
    struct RippleRing {
        float radius_px = 0.0f;
        float thickness_px = 0.0f;
        float alpha = 0.0f;
        bool live = false;
    };
    RippleRing water_ripple(const Bubble& b, int ring_index, float progress) const;

    // Emits the render state of every live bubble into `sink` for the
    // frame at `now_ns` (C7). Pure: never mutates state; expired bubbles
    // emit nothing (alpha 0). Call prune() separately to reclaim them.
    void draw(int64_t now_ns, ClickBubbleSink& sink) const;

private:
    // T-024: the per-frame hold aura for one hold. Emits through the SAME
    // sink contract as everything else (rings + dots), which is why the
    // renderer needed no change at all for this feature.
    void draw_hold(const Hold& hold, int64_t now_ns, ClickBubbleSink& sink) const;

    // T-026: one emission of the detached motion wake. Returns nothing --
    // the caller (on_cursor_moved) owns the distance bookkeeping and decides
    // HOW MANY anchors a movement segment earns; this records ONE of them.
    void emit_wake(const Hold& hold, uint32_t seed, float x, float y,
                   int64_t birth_ns, float tangent_x, float tangent_y,
                   float speed, float motion_energy, bool accent = false);

    // T-026: the per-frame geometry of one wake emission, split out so
    // wake_marks() (pure, testable) stays the single source of truth while
    // draw() only translates marks into sink calls.
    void draw_wake_emission(const HoldWakeEmission& e, int64_t now_ns,
                            ClickBubbleSink& sink) const;

    ClickConfig config_;
    std::vector<Bubble> bubbles_;  // bounded by kMaxActiveBubbles
    std::vector<Hold> holds_;      // bounded by kMaxActiveHolds
    // T-026: bounded by kMaxWakeEmissions, birth-ordered (begin() == oldest).
    std::vector<HoldWakeEmission> wake_;
};

} // namespace ptd
