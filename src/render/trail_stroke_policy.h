#pragma once

#include "../effects/trail_config.h"
#include "../effects/trail_effect.h"

namespace ptd {

// T-019 continuous-stroke cap policy (pure, header-only, no Direct2D).
//
// Verified root cause of the striped/beaded trail appearance: TrailEffect
// emits every Catmull-Rom subdivision as an independent segment and the
// renderer stroked each one with round-round caps under separate alpha
// blending, so every internal joint received repeated round-cap coverage.
// The fix is a compositing contract, not a geometry change:
//
//   - continuous styles (Classic, SoftGlow, Comet, Neon, Pulse, Ribbon)
//     stroke internal segments FLAT-FLAT (no cap overdraw at joints) and
//     the final/head segment FLAT-ROUND (soft cursor-side head);
//   - a frame that contains exactly one continuous segment may use
//     ROUND-ROUND (a young trail has no internal joints);
//   - discrete-dot styles (Dotted, Spark) keep ROUND-ROUND stubs on
//     purpose: their dots ARE the visual, and the stubs never share a
//     joint.
//
// The same policy applies to every stroke pass of a segment (SoftGlow /
// Neon outer pass and core alike), otherwise the glow would keep showing
// periodic bright joint spots even with a smooth core.

// Emission role of one continuous segment within a frame (oldest ->
// newest order, as produced by TrailEffect::build_geometry).
enum class TrailSegmentRole {
    None,       // no segment (displacement query with empty buffer)
    Only,       // the frame's single continuous segment
    Internal,   // displaced by a newer segment: a successor exists
    Final,      // last segment of a multi-segment frame (the head)
};

// Cap style pair for one stroke pass.
enum class TrailCapPolicy {
    RoundRound,
    FlatFlat,
    FlatRound,
};

// Dotted and Spark remain intentionally discrete: short round-ended stubs
// placed by the arc-length DotSampler, never fused into a continuous run.
constexpr bool trail_style_is_discrete_dots(TrailStyle style) {
    return style == TrailStyle::Dotted || style == TrailStyle::Spark;
}

// The cap contract for one stroke pass of a trail segment.
constexpr TrailCapPolicy trail_cap_policy(TrailStyle style, TrailSegmentRole role) {
    if (trail_style_is_discrete_dots(style)) return TrailCapPolicy::RoundRound;
    switch (role) {
        case TrailSegmentRole::Internal: return TrailCapPolicy::FlatFlat;
        case TrailSegmentRole::Final:    return TrailCapPolicy::FlatRound;
        case TrailSegmentRole::Only:     return TrailCapPolicy::RoundRound;
        case TrailSegmentRole::None:
        default:                         return TrailCapPolicy::RoundRound;
    }
}

// One deferred trail segment record (render-side sink state). Plain
// struct: the one-segment deferred buffer never allocates.
struct TrailSegmentRecord {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float alpha = 0.0f;
    float thickness_px = 0.0f;
    TrailColorF color{};
};

// One-segment deferred continuous-stroke sequencer (T-019 Phase 2).
//
// Consumes continuous segments in emission order and defines the exact
// draw sequence that removes internal cap overdraw:
//   - the first continuous segment of a frame is stored as pending;
//   - when another continuous segment arrives, the pending segment is
//     drawn first (role Internal, flat-flat) and the incoming segment
//     replaces it as pending -- so no internal joint is ever stroked
//     twice;
//   - after the frame's geometry build, the segment still pending is the
//     head: flush it with role Final (flat-round), or Only (round-round)
//     when it was the frame's only continuous segment;
//   - an empty or disabled trail never has a pending segment, so the
//     flush draws nothing.
// State is two scalars: no container, no COM object, no heap churn.
class TrailContinuousBatch {
public:
    // Resets per-frame state (called from the sink's reserve_hint).
    void begin_frame() {
        continuous_seen_ = 0;
        pending_ = false;
    }

    bool pending() const { return pending_; }
    int continuous_seen() const { return continuous_seen_; }

    // Admits one incoming continuous segment. Returns the emission role
    // of the previously pending segment, which the owner must draw
    // BEFORE storing the incoming segment as the new pending one:
    //   None      -- incoming is the frame's first continuous segment;
    //   Internal  -- the displaced segment has a successor.
    TrailSegmentRole admit() {
        const TrailSegmentRole displaced = pending_ ? TrailSegmentRole::Internal
                                                    : TrailSegmentRole::None;
        ++continuous_seen_;
        pending_ = true;
        return displaced;
    }

    // Emission role for the segment still pending after the frame's
    // geometry build (the flush).
    TrailSegmentRole flush_role() const {
        return continuous_seen_ <= 1 ? TrailSegmentRole::Only
                                     : TrailSegmentRole::Final;
    }

    void mark_flushed() { pending_ = false; }

private:
    int continuous_seen_ = 0;
    bool pending_ = false;
};

} // namespace ptd
