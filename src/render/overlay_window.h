#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <functional>

#include "screen_map.h"
#include "../core/log.h"
#include "../effects/trail_effect.h"
#include "../effects/click_bubble_effect.h"

namespace ptd {

// Frameless, non-activating, click-through overlay window backed by a
// DirectComposition visual tree (DXGI swapchain -> DComp device -> hwnd
// targeting).
//
// Extended styles (see create() in overlay_window.cpp):
//   WS_EX_LAYERED     cross-process hit-test transparency. Kept deliberately:
//                     WS_EX_TRANSPARENT alone does not remove the window from
//                     cross-process hit-testing (WindowFromPoint still
//                     resolved to the overlay before LAYERED was added; the
//                     verified click-through behavior was obtained only after
//                     adding it). LAYERED does NOT introduce a GDI
//                     UpdateLayeredWindow loop: pixels still come from the
//                     DComp visual tree, and SetLayeredWindowAttributes
//                     (LWA_ALPHA 255) satisfies the layered hit-test contract.
//   WS_EX_TRANSPARENT click-through
//   WS_EX_NOACTIVATE  never takes focus
//   WS_EX_TOOLWINDOW  no taskbar / Alt+Tab entry
//   WS_EX_TOPMOST     stays above other windows
//
// Frame rendering (T-008 B7 / T-009 C7): render_frame() is the real
// per-frame path -- BeginDraw, full transparent Clear, optional
// diagnostics (PROTRAIL_DIAG=1), TrailEffect stroke segments, active
// ClickBubbleEffect bubbles, EndDraw, Present, Commit. Device resources
// (device, context, target bitmap, brushes, stroke style) are created
// once in init_render() and reused every frame; only brush COLOR changes
// per frame (no per-frame resource churn, B10/C7). The device chain is
// NOT rebuilt for bubbles (C7).
//
// Coordinate transform (T-008 B4): CursorHistory and click bubbles store
// virtual-screen physical pixels. This overlay spans the virtual screen,
// positioned at (vx, vy) = (SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN), which
// is NOT guaranteed to be (0, 0) on multi-monitor layouts. TrailEffect
// and ClickBubbleEffect emit virtual-screen coordinates; the sinks below
// (this class) subtract the overlay origin:
//
//     overlay_local_x = screen_x - virtual_screen_left
//     overlay_local_y = screen_y - virtual_screen_top
//
// so negative screen coordinates (monitors left of/above primary) map
// correctly -- for BOTH effects (C8).
//
// MVP 08: each instance spans ONE monitor's physical-pixel RECT. All
// transforms/culling are owned by OverlayTransform (screen_map.h) -- the
// production helper the tests also call.
//
// Event contract (MVP 08 Phase 6): WM_DISPLAYCHANGE and WM_DPICHANGED
// NEVER reconcile topology synchronously inside wnd_proc -- the executing
// window would be destroyed under its own feet. wnd_proc only schedules a
// DEFERRED, coalesced notification through a small native mechanism
// (PostMessage to this window, handled after WndProc returns), and the
// manager/Application performs reconciliation afterward.
class OverlayWindow : public TrailGeometrySink, public ClickBubbleSink {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool create(HINSTANCE instance, bool diagnostic, const RECT* explicit_bounds = nullptr);
    void show();
    void hide();
    void destroy();

    // T-018: device loss recovery
    void release_render_resources();
    bool recreate_render_resources();

    const RECT& bounds() const { return bounds_; }
    HWND hwnd() const { return hwnd_; }
    // Deferred topology notification (MVP 08 Phase 6). The callback must
    // queue work; WndProc never calls OverlayManager::refresh_topology().
    static void set_display_change_callback(std::function<void()> cb);
    static void clear_display_change_callback();

    // Diagnostic: redraw the test primitive. Called on demand.
    void draw_diagnostic_frame();

    // Real frame path (B7). `effect`, `history`, `clicks` and configs live
    // in the Application; this method never mutates them (single-threaded
    // GUI-thread contract). `now_ns` is the monotonic timestamp for this
    // frame (Phase A clock). Configs supply color/thickness for this
    // frame; they are borrowed for the duration of the call only.
    void render_frame(const TrailEffect& effect,
                      const CursorHistory& history,
                      const TrailConfig& trail_config,
                      const ClickBubbleEffect& click_effect,
                      const ClickConfig& click_config,
                      int64_t now_ns);

    // TrailGeometrySink (consumed inside render_frame while the trail
    // builds geometry). Coordinates arrive in virtual-screen pixels and
    // are transformed to overlay-local here (B4). Thickness arrives per
    // segment (MVP 05 Phase F): the effect owns width/taper math, this
    // class only strokes it.
    void reserve_hint(int segment_count) override;
    void add_segment(float x1, float y1, float x2, float y2,
                     float alpha, float thickness_px,
                     TrailColorF color) override;

    // ClickBubbleSink (consumed inside render_frame while the click
    // effect emits bubble render state). Same transform contract (C8).
    // Ring and fill alphas both arrive precomputed (Phase K): this class
    // never re-derives the fill from the ring.
    // Name kept distinct from the trail's reserve_hint on purpose: a
    // renderer implementing both interfaces must never conflate the two
    // preallocation contracts.
    void reserve_bubbles_hint(int bubble_count) override;
    void add_bubble(float cx, float cy, float radius_px,
                    float outline_thickness_px,
                    float r, float g, float b,
                    float ring_alpha, float fill_alpha) override;

    // T-017: particle dot for the burst click styles (same transform +
    // cull contract as add_bubble).
    void add_particle(float cx, float cy, float radius_px,
                      float r, float g, float b, float alpha) override;

private:
    static LRESULT CALLBACK wnd_proc_static(HWND, UINT, WPARAM, LPARAM);
    LRESULT wnd_proc(HWND, UINT, WPARAM, LPARAM);

    bool init_render();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool diagnostic_ = false;

    // Monitor RECT captured at create() (MVP 08 OverlayTransform).
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
    RECT bounds_{};
    OverlayTransform transform_{};

    // Last known window DPI (Phase 5) for WM_DPICHANGED comparisons.
    UINT dpi_x_ = 96;
    UINT dpi_y_ = 96;
    static std::function<void()> s_display_change_callback_;

    // Borrowed for the duration of one render_frame() call (GUI thread
    // only): the sink callbacks read color/thickness from them.
    const TrailConfig* frame_trail_config_ = nullptr;
    const ClickConfig* frame_click_config_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2d_context_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2d_target_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcomp_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcomp_visual_;

    // Cached reusable rendering resources (B7/B10/C7): created once,
    // reused every frame. trail_brush_ serves trail segments; bubble
    // brush/stroke style serve bubbles; no per-bubble or per-segment
    // resource creation.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trail_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bubble_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bubble_fill_brush_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle1> round_stroke_style_;
};

} // namespace ptd
