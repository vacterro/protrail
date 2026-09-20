#pragma once

// PERF-001: ONE bounded, reusable world-space frame representation, built
// exactly once per global scheduler frame.
//
// Defect removed: OverlayManager::render_frame() used to hand the effects to
// every OverlayWindow, so each monitor independently ran
// TrailEffect::build_geometry() and ClickBubbleEffect::draw(). World-space
// effect math is monitor-independent, so the same geometry was regenerated
// once per monitor and every monitor paid a full BeginDraw/Clear/Present/
// Commit even when no primitive could land on it.
//
// Architecture after the repair:
//   - FrameGeometry::build() runs the effects ONCE into an ordered primitive
//     list (world-space = virtual-screen physical pixels), preserving the
//     existing draw order byte-for-byte;
//   - OverlayManager feeds that immutable frame to every OverlayWindow;
//   - each OverlayWindow only transforms, culls and emits to Direct2D;
//   - per-overlay dirty state decides whether an overlay presents content,
//     presents ONE transparent clear, or skips BeginDraw/Clear/Present/Commit
//     entirely.
//
// The frame is NEVER cached across different `now_ns` values: its scope is
// exactly one scheduler frame.

#include "screen_map.h"
#include "trail_stroke_policy.h"
#include "../effects/trail_effect.h"
#include "../effects/click_bubble_effect.h"

#include <cstdint>
#include <vector>

namespace ptd {

// One ordered world-space primitive of a frame. A tagged record keeps the
// EXACT emission order of the effect stream (trail segments and sparkles,
// then the click wake/bubble/hold layers with their interleaved bubbles and
// particles), which is what the renderer replays.
enum class FramePrimitiveKind : uint8_t {
    TrailSegment = 0,
    TrailSparkle = 1,
    Bubble = 2,
    Particle = 3,
};

struct FramePrimitive {
    FramePrimitiveKind kind = FramePrimitiveKind::TrailSegment;
    // TrailSegment: (x1,y1) -> (x2,y2). Sparkle/Bubble/Particle: anchor (x1,y1).
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float alpha = 0.0f;
    float thickness = 0.0f;
    TrailColorF color{};
    // Resolved T-019 continuous-stroke cap policy for a TrailSegment; the
    // world-space frame is the single authority for it, so every overlay
    // strokes the same segment identically.
    TrailCapPolicy caps = TrailCapPolicy::RoundRound;
    // Resolved T-016 outer glow pass for a TrailSegment (SoftGlow / Neon).
    // has_outer == false means the core stroke only. Precomputing the factors
    // keeps the renderer free of any config dependency.
    bool has_outer = false;
    float outer_width_px = 0.0f;
    float outer_alpha = 0.0f;
    // TrailSparkle.
    float size_px = 0.0f;
    float rotation_rad = 0.0f;
    TrailSparkleShape shape = TrailSparkleShape::Dot;
    // Bubble / Particle.
    float radius_px = 0.0f;
    float outline_px = 0.0f;
    float ring_alpha = 0.0f;
    float fill_alpha = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// PERF-001 per-overlay dirty/visibility contract. A monitor overlay starts
// known-clear; it presents content while the frame intersects it, receives
// exactly ONE transparent clear on the frame after its content disappears,
// and from then on skips BeginDraw/Clear/Present/Commit until it is
// intersected again.
class OverlayDirtyState {
public:
    enum class Decision {
        Skip,           // known clear and no intersection: do nothing
        PresentContent, // frame intersects this overlay
        PresentClear,   // had visible content, none now: one transparent clear
    };

    // Pure: reports what should happen WITHOUT changing state. The caller
    // commits with commit() only after the frame actually presented, so a
    // failed/device-lost frame never falsely records the overlay as clear.
    Decision peek(bool intersects) const {
        if (intersects) return Decision::PresentContent;
        return clear_ ? Decision::Skip : Decision::PresentClear;
    }

    // Commits the transition the presenter actually performed.
    void commit(bool intersects) {
        clear_ = !intersects;
    }

    Decision decide(bool intersects) {
        const Decision d = peek(intersects);
        commit(intersects);
        return d;
    }

    void mark_clear() { clear_ = true; }
    void mark_dirty() { clear_ = false; }
    bool is_clear() const { return clear_; }

private:
    bool clear_ = true;
};

// Reusable world-space frame. Storage is a single reused primitive vector, so
// a steady-state frame allocates nothing after the first high-water frame.
class FrameGeometry {
public:
    FrameGeometry() = default;

    void clear() {
        primitives_.clear();
        has_bounds_ = false;
        min_x_ = min_y_ = max_x_ = max_y_ = 0.0f;
    }

    // Builds the canonical frame ONCE from the live effect state. Emission
    // order is the effects' own order; `now_ns` is the single frame time.
    void build(const TrailEffect& effect,
               const CursorHistory& history,
               const TrailConfig& trail_config,
               const ClickBubbleEffect& click_effect,
               int64_t now_ns) {
        clear();
        trail_config_ = &trail_config;
        trail_style_ = trail_config.style;
        BuilderSink sink(*this);
        effect.build_geometry(history, now_ns, nullptr,
                              static_cast<int>(history.max_samples()), sink);
        click_effect.draw(now_ns, sink);
        resolve_trail_style();
        trail_config_ = nullptr;
    }

    bool empty() const { return primitives_.empty(); }
    std::size_t size() const { return primitives_.size(); }
    const std::vector<FramePrimitive>& primitives() const { return primitives_; }

    bool has_bounds() const { return has_bounds_; }
    float min_x() const { return min_x_; }
    float min_y() const { return min_y_; }
    float max_x() const { return max_x_; }
    float max_y() const { return max_y_; }

    // Conservative world-space bounding-box overlap. The box already carries
    // each primitive's widest conservative extent (stroke/glow margin,
    // sparkle envelope, bubble outline), so a `false` here means the overlay's
    // own per-primitive culling would discard everything too. Inclusive on
    // boundaries so a seam-touching primitive is never dropped.
    bool intersects(const RECT& monitor) const {
        if (!has_bounds_) return false;
        if (max_x_ < static_cast<float>(monitor.left)) return false;
        if (min_x_ > static_cast<float>(monitor.right)) return false;
        if (max_y_ < static_cast<float>(monitor.top)) return false;
        if (min_y_ > static_cast<float>(monitor.bottom)) return false;
        return true;
    }

    // Appends in canonical order. Public so a test can construct a frame
    // without running the effects; production uses build().
    void append_segment(float x1, float y1, float x2, float y2,
                        float alpha, float thickness_px, TrailColorF color) {
        if (!(alpha > 0.0f)) return;
        FramePrimitive p;
        p.kind = FramePrimitiveKind::TrailSegment;
        p.x1 = x1; p.y1 = y1; p.x2 = x2; p.y2 = y2;
        p.alpha = alpha; p.thickness = thickness_px; p.color = color;
        const float margin = trail_margin_px(thickness_px);
        expand(x1, y1, margin);
        expand(x2, y2, margin);
        primitives_.push_back(p);
    }

    void append_sparkle(float x, float y, float size_px, float rotation_rad,
                        float alpha, TrailColorF color, TrailSparkleShape shape) {
        if (!(alpha > 0.0f) || !(size_px > 0.0f)) return;
        FramePrimitive p;
        p.kind = FramePrimitiveKind::TrailSparkle;
        p.x1 = x; p.y1 = y; p.alpha = alpha; p.color = color;
        p.size_px = size_px; p.rotation_rad = rotation_rad; p.shape = shape;
        expand(x, y, size_px);
        primitives_.push_back(p);
    }

    void append_bubble(float cx, float cy, float radius_px,
                       float outline_thickness_px,
                       float r, float g, float b,
                       float ring_alpha, float fill_alpha) {
        if (!(ring_alpha > 0.0f) && !(fill_alpha > 0.0f)) return;
        if (!(radius_px > 0.0f)) return;
        FramePrimitive p;
        p.kind = FramePrimitiveKind::Bubble;
        p.x1 = cx; p.y1 = cy; p.radius_px = radius_px;
        p.outline_px = outline_thickness_px;
        p.r = r; p.g = g; p.b = b;
        p.ring_alpha = ring_alpha; p.fill_alpha = fill_alpha;
        expand(cx, cy, radius_px + outline_thickness_px);
        primitives_.push_back(p);
    }

    void append_particle(float cx, float cy, float radius_px,
                         float r, float g, float b, float alpha) {
        if (!(alpha > 0.0f) || !(radius_px > 0.0f)) return;
        FramePrimitive p;
        p.kind = FramePrimitiveKind::Particle;
        p.x1 = cx; p.y1 = cy; p.radius_px = radius_px;
        p.r = r; p.g = g; p.b = b; p.alpha = alpha;
        expand(cx, cy, radius_px);
        primitives_.push_back(p);
    }

private:
    // The exact conservative margin OverlayWindow::draw_trail_segment() uses
    // for culling, so the frame bounding box is never narrower than the
    // per-primitive culling test.
    float trail_margin_px(float thickness_px) const {
        float draw_thickness_max = thickness_px;
        if (trail_config_
            && (trail_config_->style == TrailStyle::SoftGlow
                || trail_config_->style == TrailStyle::Neon)) {
            const bool neon = trail_config_->style == TrailStyle::Neon;
            const float glow = trail_config_->glow_strength;
            draw_thickness_max = thickness_px
                * (neon ? 3.0f + 3.0f * glow : 2.0f + 2.0f * glow);
        }
        const float core = thickness_px * 2.0f;
        const float wide = draw_thickness_max * 0.5f + 1.0f;
        return core > wide ? core : wide;
    }

    void expand(float x, float y, float margin) {
        if (!has_bounds_) {
            min_x_ = x - margin; max_x_ = x + margin;
            min_y_ = y - margin; max_y_ = y + margin;
            has_bounds_ = true;
            return;
        }
        if (x - margin < min_x_) min_x_ = x - margin;
        if (x + margin > max_x_) max_x_ = x + margin;
        if (y - margin < min_y_) min_y_ = y - margin;
        if (y + margin > max_y_) max_y_ = y + margin;
    }

    // T-019 cap policy + T-016 glow pass, resolved ONCE per frame for the
    // whole primitive stream: continuous styles stroke every internal joint
    // flat-flat and the frame's final/head segment flat-round (round-round
    // when it is the only one); discrete-dot styles keep round-round stubs on
    // every piece. Glow factors are likewise frozen into the frame.
    void resolve_trail_style() {
        const bool glow_style = trail_style_ == TrailStyle::SoftGlow
                             || trail_style_ == TrailStyle::Neon;
        const bool neon = trail_style_ == TrailStyle::Neon;
        const float glow = trail_config_ ? trail_config_->glow_strength : 0.0f;
        const bool discrete = trail_style_is_discrete_dots(trail_style_);

        std::size_t count = 0;
        for (const auto& p : primitives_) {
            if (p.kind == FramePrimitiveKind::TrailSegment) ++count;
        }
        std::size_t seen = 0;
        for (auto& p : primitives_) {
            if (p.kind != FramePrimitiveKind::TrailSegment) continue;
            ++seen;
            if (discrete) {
                p.caps = TrailCapPolicy::RoundRound;
            } else {
                const TrailSegmentRole role = count <= 1
                    ? TrailSegmentRole::Only
                    : (seen == count ? TrailSegmentRole::Final
                                     : TrailSegmentRole::Internal);
                p.caps = trail_cap_policy(trail_style_, role);
            }
            if (glow_style) {
                p.has_outer = true;
                p.outer_width_px = p.thickness
                    * (neon ? 3.0f + 3.0f * glow : 2.0f + 2.0f * glow);
                p.outer_alpha = p.alpha * (neon ? 0.45f * glow : 0.30f * glow);
            }
        }
    }

    class BuilderSink : public TrailGeometrySink, public ClickBubbleSink {
    public:
        explicit BuilderSink(FrameGeometry& owner) : owner_(owner) {}

        // Preallocation hints are no-ops: the owner's vector is reused across
        // frames and already bounded by the effects' own hard caps.
        void reserve_hint(int) override {}
        void reserve_bubbles_hint(int) override {}

        void add_segment(float x1, float y1, float x2, float y2,
                         float alpha, float thickness_px,
                         TrailColorF color) override {
            owner_.append_segment(x1, y1, x2, y2, alpha, thickness_px, color);
        }

        void add_sparkle(float x, float y, float size_px, float rotation_rad,
                         float alpha, TrailColorF color,
                         TrailSparkleShape shape) override {
            owner_.append_sparkle(x, y, size_px, rotation_rad, alpha, color, shape);
        }

        void add_bubble(float cx, float cy, float radius_px,
                        float outline_thickness_px,
                        float r, float g, float b,
                        float ring_alpha, float fill_alpha) override {
            owner_.append_bubble(cx, cy, radius_px, outline_thickness_px,
                                 r, g, b, ring_alpha, fill_alpha);
        }

        void add_particle(float cx, float cy, float radius_px,
                          float r, float g, float b, float alpha) override {
            owner_.append_particle(cx, cy, radius_px, r, g, b, alpha);
        }

    private:
        FrameGeometry& owner_;
    };

    std::vector<FramePrimitive> primitives_;
    const TrailConfig* trail_config_ = nullptr;
    TrailStyle trail_style_ = TrailStyle::Classic;
    bool has_bounds_ = false;
    float min_x_ = 0.0f;
    float min_y_ = 0.0f;
    float max_x_ = 0.0f;
    float max_y_ = 0.0f;
};

} // namespace ptd
