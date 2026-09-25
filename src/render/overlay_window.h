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
#include <cstdint>
#include <functional>

#include "screen_map.h"
#include "trail_stroke_policy.h"
#include "frame_geometry.h"
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
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool create(HINSTANCE instance, bool diagnostic, const RECT* explicit_bounds = nullptr);
    void show();
    void hide();
    void destroy();

    // ---- T-018R1 bounded device-recovery authority ----
    // One recovery owner: this window. Device loss (EndDraw / Present /
    // Commit) releases the whole render chain, enters RecoveryPending and
    // is retried through ONE policy-gated path. No recursive
    // init_render()/diagnostic/recreate chain exists anymore; the HWND
    // always survives.
    enum class RecoveryPhase { Ready, RecoveryPending };

    RecoveryPhase recovery_phase() const {
        return recovery_.pending ? RecoveryPhase::RecoveryPending
                                 : RecoveryPhase::Ready;
    }
    // A window is only renderable when the full chain exists; a partially
    // initialized chain is never treated as Ready.
    bool has_render_resources() const {
        return static_cast<bool>(d2d_context_) && static_cast<bool>(swap_chain_)
            && static_cast<bool>(dcomp_device_);
    }

    // Recoverable device-loss classification. Non-device HRESULTs are
    // logged but never request a full device-chain rebuild.
    static bool is_device_loss_hresult(HRESULT hr);

    // Enter RecoveryPending: release every render resource (so stale or
    // partially initialized resources are never exposed) and schedule the
    // first bounded retry.
    void note_device_loss(const char* origin, HRESULT hr);

    // Policy-gated retry: at most one recreate attempt per backoff window
    // (250 ms doubling up to 5 s). Persistent failure therefore stays
    // rate-limited (never a per-frame spin) and remains retryable.
    bool try_recovery();

    // Forced synchronous recovery attempt (explicit caller request / the
    // integration smoke). Same single authority; never recursive.
    bool recreate_render_resources();

    // Deterministic test seam: replace the recreate step and the clock.
    // Passing nullptr restores the default step (full chain rebuild).
    void set_recovery_hooks(std::function<bool()> recreate_step,
                            std::function<int64_t()> clock);

    const RECT& bounds() const { return bounds_; }
    HWND hwnd() const { return hwnd_; }
    // Deferred topology notification (MVP 08 Phase 6). The callback must
    // queue work; WndProc never calls OverlayManager::refresh_topology().
    static void set_display_change_callback(std::function<void()> cb);
    static void clear_display_change_callback();
    // W2-001 integration seam: fire the currently installed display-change
    // notification exactly as a real WM_DISPLAYCHANGE WndProc would, so an
    // Application-level test can model the ONE external topology event without
    // a live HWND. No-op when no callback is installed. Production never calls
    // this; the real WndProc path is unchanged.
    static void fire_display_change_for_tests();

    // Diagnostic: redraw the test primitive. Called on demand.
    void draw_diagnostic_frame();

    // Frame-exact rendering consumes only the indices assigned by the single
    // FrameGeometry partition pass. There is no global-frame replay path.
    void render_frame(const FrameGeometry& frame,
                      const std::vector<std::size_t>& primitive_indices,
                      int64_t now_ns);

    OverlayDirtyState& dirty_state() { return dirty_; }
    const OverlayDirtyState& dirty_state() const { return dirty_; }

private:
    static LRESULT CALLBACK wnd_proc_static(HWND, UINT, WPARAM, LPARAM);
    LRESULT wnd_proc(HWND, UINT, WPARAM, LPARAM);

    // Pure resource creation. Never draws, never presents, never calls the
    // recovery authority (that is what broke the init-time recursion).
    // On any failure it releases its own partial resources and returns
    // false, so a half-built chain is never exposed.
    bool init_render();
    void release_render_resources();

    // T-019 continuous-stroke cap policy: stroke-style lookup for a cap
    // policy (all styles are cached in init_render(), never per frame).
    ID2D1StrokeStyle1* stroke_style_for(TrailCapPolicy policy) const;

    // PERF-001 frame-consumer primitives. Each transforms a world-space
    // primitive from the immutable frame to overlay-local physical pixels,
    // culls it against this overlay, and emits it to Direct2D. No effect
    // model, no config, no geometry build happens here.
    void emit_trail_segment(const FramePrimitive& segment);
    void emit_sparkle(const FramePrimitive& sparkle);
    void emit_bubble(const FramePrimitive& bubble);
    void emit_particle(const FramePrimitive& particle);

    // T-018R1 bounded recovery state.
    static constexpr int64_t kRecoveryInitialDelayMs = 250;
    static constexpr int64_t kRecoveryMaxDelayMs = 5000;
    struct RecoveryState {
        bool pending = false;
        int consecutive_failures = 0;
        int64_t delay_ms = kRecoveryInitialDelayMs;
        int64_t next_attempt_ns = 0;
    };
    RecoveryState recovery_{};
    std::function<bool()> recreate_step_;    // default: full chain rebuild
    std::function<int64_t()> recovery_clock_; // default: QPC monotonic ns
    int64_t recovery_now() const;

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

    // PERF-001 per-overlay visibility state (see frame_geometry.h).
    OverlayDirtyState dirty_{};

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
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> triangle_geometry_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle1> round_stroke_style_;
    // T-019: continuous styles never round-cap internal subdivisions.
    // flat-flat serves internal joints, flat-round the head segment;
    // round-round stays for Dotted/Spark dot stubs, bubbles and the
    // single-segment trail. All three are created once in init_render().
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle1> flat_flat_stroke_style_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle1> flat_round_stroke_style_;
};

} // namespace ptd
