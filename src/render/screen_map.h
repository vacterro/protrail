#pragma once

// MVP 08 Phase 2/3: the single production owner of the virtual-screen ->
// overlay-local physical-pixel transform and overlay bounds culling.
//
// PHYSICAL-PIXEL INVARIANT (do not regress):
//   CursorHistory, TrailEffect and ClickBubbleEffect all operate in
//   virtual-screen PHYSICAL pixels. Each per-monitor overlay HWND is
//   positioned at its monitor's PHYSICAL-pixel RECT. Therefore the ONLY
//   correct transform is a pure origin subtraction:
//
//       local_x = virtual_x - monitor_left
//       local_y = virtual_y - monitor_top
//
//   Monitor DPI / scale NEVER multiplies these coordinates a second time
//   (a 150% monitor must not scale the trail by 1.5 again). The D2D
//   render target is created at an explicit 96 DPI so one effect unit maps
//   to exactly one physical pixel (see overlay_window.cpp init_render).
//
// Tests call this same production helper instead of re-implementing the
// arithmetic (Phase 3); OverlayWindow uses it for every segment/bubble.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {

struct OverlayTransform {
    float origin_x = 0.0f;   // monitor RECT left  (physical px)
    float origin_y = 0.0f;   // monitor RECT top   (physical px)
    float width = 0.0f;      // monitor RECT width  (physical px)
    float height = 0.0f;     // monitor RECT height (physical px)

    static OverlayTransform from_bounds(const RECT& b) {
        OverlayTransform t;
        t.origin_x = static_cast<float>(b.left);
        t.origin_y = static_cast<float>(b.top);
        t.width = static_cast<float>(b.right) - t.origin_x;
        t.height = static_cast<float>(b.bottom) - t.origin_y;
        return t;
    }

    // Virtual-screen physical px -> overlay-local physical px.
    float to_local_x(float virtual_x) const { return virtual_x - origin_x; }
    float to_local_y(float virtual_y) const { return virtual_y - origin_y; }

    // Culling: the segment is completely outside this overlay even with a
    // thickness margin (rounded caps need ~thickness/2; 2x is headroom).
    bool cull_segment(float x1, float y1, float x2, float y2,
                      float margin_px) const {
        if (width <= 0.0f || height <= 0.0f) return false;
        const float max_x = x1 > x2 ? x1 : x2;
        const float min_x = x1 < x2 ? x1 : x2;
        const float max_y = y1 > y2 ? y1 : y2;
        const float min_y = y1 < y2 ? y1 : y2;
        return max_x < -margin_px || min_x > width + margin_px ||
               max_y < -margin_px || min_y > height + margin_px;
    }

    // Culling: a circle (click bubble) whose outer edge (radius + outline
    // thickness) lies completely outside this overlay.
    bool cull_circle(float center_x, float center_y,
                     float outer_radius_px) const {
        if (width <= 0.0f || height <= 0.0f) return false;
        const float lx = to_local_x(center_x);
        const float ly = to_local_y(center_y);
        return lx + outer_radius_px < 0.0f || lx - outer_radius_px > width ||
               ly + outer_radius_px < 0.0f || ly - outer_radius_px > height;
    }
};

} // namespace ptd
