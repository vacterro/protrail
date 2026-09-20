#include "overlay_window.h"
#include "render_color.h"

#include "../platform/dpi_awareness.h"
#include "../platform/timestamp.h"

#include <Windows.h>
#include <d2d1_1helper.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <cmath>
#include <string>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace ptd {

namespace {

constexpr wchar_t kClassName[] = L"ProTrailOverlay";

void overlay_log(ptd::LogLevel level, const std::string_view& msg) {
    ptd::log_write(level, msg);
}



} // namespace

std::function<void()> OverlayWindow::s_display_change_callback_;

void OverlayWindow::set_display_change_callback(std::function<void()> cb) {
    s_display_change_callback_ = std::move(cb);
}

void OverlayWindow::clear_display_change_callback() {
    s_display_change_callback_ = nullptr;
}

OverlayWindow::OverlayWindow() {
    // Default recreate step: full chain rebuild via the pure init_render().
    // The recovery authority is the ONLY caller after a loss; init_render
    // itself never re-enters anything.
    recreate_step_ = [this] {
        release_render_resources();
        return init_render();
    };
}

OverlayWindow::~OverlayWindow() {
    destroy();
}

// T-018R1: single device-loss classification used by every terminal
// HRESULT site (EndDraw / Present / Commit). Non-device failures never
// enter the rebuild authority.
bool OverlayWindow::is_device_loss_hresult(HRESULT hr) {
    return hr == D2DERR_RECREATE_TARGET
        || hr == DXGI_ERROR_DEVICE_REMOVED
        || hr == DXGI_ERROR_DEVICE_RESET;
}

int64_t OverlayWindow::recovery_now() const {
    if (recovery_clock_) return recovery_clock_();
    LARGE_INTEGER freq{};
    LARGE_INTEGER ticks{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ticks);
    return ptd::ticks_to_ns(ticks.QuadPart, freq.QuadPart);
}

void OverlayWindow::set_recovery_hooks(std::function<bool()> recreate_step,
                                       std::function<int64_t()> clock) {
    recreate_step_ = std::move(recreate_step);
    recovery_clock_ = std::move(clock);
}

void OverlayWindow::note_device_loss(const char* origin, HRESULT hr) {
    // Capture the D3D removed reason while the device object still lives.
    if (hr == DXGI_ERROR_DEVICE_REMOVED && d3d_device_) {
        const HRESULT reason = d3d_device_->GetDeviceRemovedReason();
        overlay_log(ptd::LogLevel::Warn,
                    std::string("overlay: D3D removed reason hr=") + std::to_string(reason)
                        + " (" + origin + ")");
    }
    // Old COM resources are released eagerly: stale resources are never
    // exposed after a detected loss. The HWND is untouched.
    release_render_resources();
    recovery_.pending = true;
    recovery_.consecutive_failures = 0;
    recovery_.delay_ms = kRecoveryInitialDelayMs;
    recovery_.next_attempt_ns = recovery_now() + recovery_.delay_ms * 1'000'000;
    overlay_log(ptd::LogLevel::Warn,
                std::string("overlay: recoverable device loss in ") + origin
                    + " (hr=" + std::to_string(hr) + "); recovery pending");
}

bool OverlayWindow::try_recovery() {
    if (!recovery_.pending) return has_render_resources();

    // Policy gate: at most one attempt per backoff window. Persistent
    // failure stays bounded (250 ms doubling to 5 s), never a per-frame
    // spin, and always retryable.
    const int64_t now = recovery_now();
    if (now < recovery_.next_attempt_ns) return false;

    if (recreate_step_ && recreate_step_()) {
        const int failed_attempts = recovery_.consecutive_failures;
        recovery_ = RecoveryState{};
        overlay_log(ptd::LogLevel::Info,
                    "overlay: render resources recovered after " +
                        std::to_string(failed_attempts) + " failed attempt(s); Ready");
        return true;
    }

    ++recovery_.consecutive_failures;
    recovery_.delay_ms = std::min(recovery_.delay_ms * 2, kRecoveryMaxDelayMs);
    recovery_.next_attempt_ns = recovery_now() + recovery_.delay_ms * 1'000'000;
    overlay_log(ptd::LogLevel::Warn,
                "overlay: recovery attempt " +
                    std::to_string(recovery_.consecutive_failures) +
                    " failed; next bounded retry in " +
                    std::to_string(recovery_.delay_ms) + " ms");
    return false;
}

bool OverlayWindow::recreate_render_resources() {
    overlay_log(ptd::LogLevel::Info, "overlay: recreating render resources");
    if (recreate_step_ && recreate_step_()) {
        recovery_ = RecoveryState{};
        return true;
    }
    recovery_.pending = true;
    ++recovery_.consecutive_failures;
    recovery_.delay_ms = std::min(recovery_.delay_ms * 2, kRecoveryMaxDelayMs);
    recovery_.next_attempt_ns = recovery_now() + recovery_.delay_ms * 1'000'000;
    return false;
}

LRESULT CALLBACK OverlayWindow::wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE && lp) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wnd_proc(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            // Click-through: everything is transparent to the mouse.
            return HTTRANSPARENT;
        case WM_PAINT: {
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        case WM_DISPLAYCHANGE:
            overlay_log(ptd::LogLevel::Info, "overlay: WM_DISPLAYCHANGE received; queueing topology reconciliation");
            // Lightweight notification only. Application callback MUST
            // queue/coalesce; never destroy this HWND from this call stack.
            if (s_display_change_callback_) s_display_change_callback_();
            return 0;
        case WM_DPICHANGED: {
            const UINT new_dpi_x = LOWORD(wp);
            const UINT new_dpi_y = HIWORD(wp);
            dpi_x_ = new_dpi_x ? new_dpi_x : dpi_x_;
            dpi_y_ = new_dpi_y ? new_dpi_y : dpi_y_;
            overlay_log(ptd::LogLevel::Info, "overlay: WM_DPICHANGED received; queueing topology reconciliation");
            // Do not call SetWindowPos from this WndProc. The recommended
            // RECT is reflected by fresh monitor enumeration; deferred
            // reconciliation keeps monitor identity/resources coherent and
            // avoids resizing/recreating the executing window synchronously.
            if (s_display_change_callback_) s_display_change_callback_();
            return 0;
        }
        case WM_CLOSE:
            // Overlay is torn down programmatically by Application via
            // destroy(); ignore OS/tool WM_CLOSE so a stray termination
            // broadcast cannot destroy it out-of-band.
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool OverlayWindow::create(HINSTANCE instance, bool diagnostic, const RECT* explicit_bounds) {
    instance_ = instance;
    diagnostic_ = diagnostic;

    int vx = 0, vy = 0, w = 0, h = 0;
    if (explicit_bounds) {
        bounds_ = *explicit_bounds;
        vx = bounds_.left;
        vy = bounds_.top;
        w = bounds_.right - bounds_.left;
        h = bounds_.bottom - bounds_.top;
    } else {
        vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        bounds_.left = vx;
        bounds_.top = vy;
        bounds_.right = vx + w;
        bounds_.bottom = vy + h;
    }
    transform_ = OverlayTransform::from_bounds(bounds_);
    origin_x_ = transform_.origin_x;
    origin_y_ = transform_.origin_y;
    width_ = transform_.width;
    height_ = transform_.height;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &OverlayWindow::wnd_proc_static;
    wc.hInstance = instance;
    wc.hCursor = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        overlay_log(ptd::LogLevel::Info, "overlay: RegisterClassExW failed");
        return false;
    }

    DWORD style = WS_POPUP;
    // Click-through recipe: WS_EX_TRANSPARENT + WS_EX_LAYERED. Note on the
    // milestone's design constraint: LAYERED here does NOT mean we use a
    // GDI UpdateLayeredWindow loop -- the pixels still come from the DComp
    // visual tree; LAYERED only fixes cross-process hit-testing transparency
    // (WindowFromPoint), which WS_EX_TRANSPARENT alone cannot do.
    DWORD ex_style = WS_EX_LAYERED                   // cross-process hit-test transparency
                   | WS_EX_TRANSPARENT               // click-through
                   | WS_EX_NOACTIVATE                // never take focus
                   | WS_EX_TOOLWINDOW                // no taskbar / Alt+Tab
                   | WS_EX_TOPMOST;

    hwnd_ = CreateWindowExW(ex_style, kClassName, L"ProTrailOverlay", style,
                            vx, vy, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        overlay_log(ptd::LogLevel::Info, "overlay: CreateWindowExW failed");
        return false;
    }

    // Window-specific DPI is authoritative only after HWND creation
    // (MVP 08 Phase 5). No scale is applied to coordinates or geometry.
    if (!dpi::window_dpi(hwnd_, dpi_x_, dpi_y_)) {
        dpi_x_ = 96;
        dpi_y_ = 96;
    }

    // Fully opaque layered alpha: DComp content provides the real pixels;
    // LWA_ALPHA=255 keeps every pixel visible while LAYERED+TRANSPARENT
    // routes hit-testing around the window entirely.
    if (!SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA)) {
        overlay_log(ptd::LogLevel::Info, "overlay: SetLayeredWindowAttributes failed");
    }

    if (!init_render()) {
        // init_render released its own partial resources; the recovery
        // authority can still rebuild later through recreate paths, but a
        // failed initial creation reports failure honestly.
        overlay_log(ptd::LogLevel::Info, "overlay: render init failed");
        return false;
    }

    // T-018R1: initialization-time present is NOT recursive. A device loss
    // on this first frame enters the bounded recovery authority instead of
    // re-entering full initialization on the same call stack.
    draw_diagnostic_frame();
    return true;
}

bool OverlayWindow::init_render() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT w = static_cast<UINT>(rc.right - rc.left);
    const UINT h = static_cast<UINT>(rc.bottom - rc.top);
    if (w == 0 || h == 0) {
        overlay_log(ptd::LogLevel::Info, "overlay: zero-size client rect");
        return false;
    }

    D3D_FEATURE_LEVEL fl_out{};
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                   D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
                                   &fl_out, nullptr);

    auto fail = [&](const char* what) {
        overlay_log(ptd::LogLevel::Error, std::string(what) + " hr=" + std::to_string(hr));
        // Never expose a partially initialized chain as usable.
        release_render_resources();
        return false;
    };

    if (FAILED(hr)) return fail("D3D11CreateDevice");

    hr = d3d_device_.As(&dxgi_device_);
    if (FAILED(hr)) return fail("QI IDXGIDevice");

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = w;
    scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferCount = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.SampleDesc.Count = 1;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgi_factory_.GetAddressOf()));
    if (FAILED(hr)) return fail("CreateDXGIFactory1");

    hr = dxgi_factory_->CreateSwapChainForComposition(d3d_device_.Get(), &scd, nullptr, swap_chain_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateSwapChainForComposition");

    ComPtr<ID2D1Factory1> d2d_factory;
    D2D1_FACTORY_OPTIONS fo{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                           reinterpret_cast<void**>(d2d_factory.GetAddressOf()));
    if (FAILED(hr)) return fail("D2D1CreateFactory");

    hr = d2d_factory->CreateDevice(dxgi_device_.Get(), d2d_device_.GetAddressOf());
    if (FAILED(hr)) return fail("D2D1Factory::CreateDevice");

    hr = d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2d_context_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateDeviceContext");

    // T-008 B7 render-target strategy: create the D2D target bitmap ONCE,
    // bound to swap-chain buffer 0. This is the standard Direct2D/DXGI
    // flip-model interop pattern (surface acquired via GetBuffer(0) before
    // any Present; see Microsoft's "devices and device contexts" D2D
    // docs). Two properties keep frames correct:
    //  1) D2D device contexts perform the flip-model back-buffer re-bind
    //     internally across BeginDraw/EndDraw cycles (the manual re-bind
    //     rule applies to raw D3D11 render-target views);
    //  2) render_frame() clears the ENTIRE target every frame, so buffer
    //     content is frame-exact regardless of which physical buffer the
    //     DWM presents next -- no accumulation, no ghosting.
    // A re-bind-per-frame variant (GetBuffer(BufferCount-1) after each
    // Present) was attempted first and failed at startup with E_INVALIDARG:
    // flip-model buffers beyond index 0 are lazily allocated and are not
    // valid GetBuffer targets before the first Present.
    ComPtr<IDXGISurface2> surface;
    hr = swap_chain_->GetBuffer(0, __uuidof(IDXGISurface2),
                                reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return fail("GetBuffer");

    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = d2d_context_->CreateBitmapFromDxgiSurface(surface.Get(), bp, d2d_target_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateBitmapFromDxgiSurface");

    d2d_context_->SetTarget(d2d_target_.Get());

    // PHYSICAL-PIXEL CONTRACT (MVP 08 Phase 2): D2D normally interprets
    // coordinates as DIPs. Force the context to 96 DPI so one D2D unit is
    // one swap-chain physical pixel on every monitor. Never feed monitor
    // scale into cursor coordinates, trail geometry, bubble geometry,
    // thickness or radius: that would double-scale at 125/150%. (The
    // surface-backed ID2D1Bitmap1 itself carries no SetDpi; the context's
    // DIP mapping is the single authority for the unit==pixel contract.)
    d2d_context_->SetDpi(96.0f, 96.0f);

    // Cached reusable resources (B7/B10): brush color is the only per-frame
    // mutation; stroke styles and target bitmaps are stable.
    //
    // T-019 continuous-stroke cap policy: three cached stroke styles, all
    // created here and never per frame.
    //   round-round  Dotted/Spark dot stubs, bubbles, single-segment trail
    //   flat-flat    internal joints of continuous styles (no repeated
    //                round-cap alpha coverage at Catmull-Rom subdivisions)
    //   flat-round   head segment of a continuous multi-segment trail
    D2D1_STROKE_STYLE_PROPERTIES1 stroke{};
    stroke.startCap = D2D1_CAP_STYLE_ROUND;
    stroke.endCap = D2D1_CAP_STYLE_ROUND;
    stroke.dashCap = D2D1_CAP_STYLE_ROUND;
    stroke.lineJoin = D2D1_LINE_JOIN_ROUND;
    stroke.miterLimit = 4.0f;
    hr = d2d_factory->CreateStrokeStyle(stroke, nullptr, 0, round_stroke_style_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateStrokeStyle");

    stroke.startCap = D2D1_CAP_STYLE_FLAT;
    stroke.endCap = D2D1_CAP_STYLE_FLAT;
    stroke.dashCap = D2D1_CAP_STYLE_FLAT;
    hr = d2d_factory->CreateStrokeStyle(stroke, nullptr, 0, flat_flat_stroke_style_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateStrokeStyle(flat-flat)");

    stroke.startCap = D2D1_CAP_STYLE_FLAT;
    stroke.endCap = D2D1_CAP_STYLE_ROUND;
    stroke.dashCap = D2D1_CAP_STYLE_ROUND;
    hr = d2d_factory->CreateStrokeStyle(stroke, nullptr, 0, flat_round_stroke_style_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateStrokeStyle(flat-round)");

    hr = d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0),
                                             trail_brush_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateSolidColorBrush");

    // T-023: one normalized filled triangle reused by every Shard. The
    // geometry belongs to the D2D resource chain and is recreated on loss;
    // no geometry or sink is allocated per sparkle.
    hr = d2d_factory->CreatePathGeometry(triangle_geometry_.GetAddressOf());
    if (FAILED(hr)) return fail("CreatePathGeometry(triangle)");
    ComPtr<ID2D1GeometrySink> triangle_sink;
    hr = triangle_geometry_->Open(triangle_sink.GetAddressOf());
    if (FAILED(hr)) return fail("OpenPathGeometry(triangle)");
    const D2D1_POINT_2F triangle_points[] = {
        D2D1::Point2F(0.0f, -0.58f),
        D2D1::Point2F(-0.50f, 0.42f),
        D2D1::Point2F(0.50f, 0.42f),
    };
    triangle_sink->BeginFigure(triangle_points[0], D2D1_FIGURE_BEGIN_FILLED);
    triangle_sink->AddLines(&triangle_points[1], 2);
    triangle_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    hr = triangle_sink->Close();
    if (FAILED(hr)) return fail("ClosePathGeometry(triangle)");

    // Bubble brushes (T-009 C7): cached once, only SetColor mutates per
    // frame/bubble -- no per-bubble resource creation.
    hr = d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0),
                                             bubble_brush_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateSolidColorBrush(bubble)");

    hr = d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0),
                                             bubble_fill_brush_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateSolidColorBrush(bubble fill)");

    hr = DCompositionCreateDevice(dxgi_device_.Get(), __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void**>(dcomp_device_.GetAddressOf()));
    if (FAILED(hr)) return fail("DCompositionCreateDevice");

    hr = dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE, dcomp_target_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateTargetForHwnd");

    hr = dcomp_device_->CreateVisual(dcomp_visual_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateVisual");

    hr = dcomp_visual_->SetContent(swap_chain_.Get());
    if (FAILED(hr)) return fail("SetContent");

    hr = dcomp_target_->SetRoot(dcomp_visual_.Get());
    if (FAILED(hr)) return fail("SetRoot");

    // T-018R1: init_render is pure resource creation. No diagnostic draw,
    // no present, no Commit here -- the initial present happens once in
    // create() through the non-recursive path, and device loss anywhere
    // enters the bounded recovery authority. This removes the
    // init_render -> draw_diagnostic_frame -> recreate -> init_render
    // recursion entirely.

    overlay_log(ptd::LogLevel::Info, "overlay: render chain initialized (D3D11 -> DXGI flip -> D2D -> DComp)");
    return true;
}

void OverlayWindow::draw_diagnostic_frame() {
    // Never renders with pending recovery or a partial chain; never
    // re-enters init_render() from this call stack (T-018R1).
    if (recovery_.pending || !d2d_context_ || !swap_chain_ || !dcomp_device_) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float fw = static_cast<float>(rc.right - rc.left);
    const float fh = static_cast<float>(rc.bottom - rc.top);

    d2d_context_->BeginDraw();
    // Fully transparent where nothing is drawn.
    d2d_context_->Clear(D2D1::ColorF(0, 0.0f));

    if (diagnostic_) {
        // Temporary diagnostic primitive: 100x100 red square at a fixed
        // offset from the virtual screen origin. Proves alpha, coordinates
        // and the whole composition chain.
        ComPtr<ID2D1SolidColorBrush> brush;
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(255, 0, 0, 0.85f), brush.GetAddressOf());
        D2D1_RECT_F rect{20.0f, 20.0f, 120.0f, 120.0f};
        d2d_context_->FillRectangle(rect, brush.Get());

        ComPtr<ID2D1SolidColorBrush> brush2;
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0, 120, 255, 0.5f), brush2.GetAddressOf());
        D2D1_RECT_F rect2{140.0f, 20.0f, 340.0f, 120.0f};
        d2d_context_->FillRectangle(rect2, brush2.Get());

        // Half-transparent border ring around the whole virtual screen so
        // monitor bounds are visible.
        ComPtr<ID2D1SolidColorBrush> ring;
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(255, 255, 255, 0.15f), ring.GetAddressOf());
        D2D1_RECT_F border{0.5f, 0.5f, fw - 0.5f, fh - 0.5f};
        d2d_context_->DrawRectangle(border, ring.Get(), 1.0f);
    }

    HRESULT hr = d2d_context_->EndDraw();
    if (is_device_loss_hresult(hr)) {
        note_device_loss("diagnostic EndDraw", hr);
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Error, std::string("diagnostic EndDraw hr=") + std::to_string(hr));
        return;
    }
    hr = swap_chain_->Present(0, 0);
    if (is_device_loss_hresult(hr)) {
        note_device_loss("diagnostic Present", hr);
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Error, std::string("diagnostic Present hr=") + std::to_string(hr));
        return;
    }
    hr = dcomp_device_->Commit();
    if (is_device_loss_hresult(hr)) {
        note_device_loss("diagnostic Commit", hr);
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Warn, std::string("diagnostic Commit hr=") + std::to_string(hr));
    }
    // No manual flip-model rebind here: D2D device contexts handle the
    // back-buffer rotation internally across BeginDraw/EndDraw cycles, and
    // every frame begins with a full Clear (see init_render rationale).
}

void OverlayWindow::render_frame(const FrameGeometry& frame, bool intersects,
                                 int64_t now_ns) {
    (void)now_ns;
    if (recovery_.pending || !has_render_resources()) {
        // T-018R1: absent resources after a recoverable device failure keep
        // RecoveryPending and retry ONLY when the bounded policy permits. A
        // transient GPU failure must not permanently disable rendering, and
        // a persistent one must not spin per frame. This runs BEFORE any
        // dirty-state skip so a clean overlay never suppresses an already-due
        // device recovery attempt.
        try_recovery();
        return;
    }

    // PERF-001 dirty-overlay contract. Asked BEFORE presenting, committed only
    // after a successful present (see the early returns below): a frame that
    // fails mid-present must not record the overlay as clear. Known-clear and
    // not intersected: skip BeginDraw/Clear/Present/Commit entirely. Previous
    // content, none now: present exactly ONE transparent clear.
    const OverlayDirtyState::Decision decision = dirty_.peek(intersects);
    if (decision == OverlayDirtyState::Decision::Skip) return;
    const bool clear_only = decision == OverlayDirtyState::Decision::PresentClear;

    d2d_context_->BeginDraw();
    // Frame-exact content: full transparent clear every frame. Nothing is
    // accumulated across frames; buffer state is irrelevant by design.
    d2d_context_->Clear(D2D1::ColorF(0, 0.0f));

    if (!clear_only) {
        // Optional diagnostics (PROTRAIL_DIAG=1): border ring only, so the
        // effect visuals themselves stay unobstructed.
        if (diagnostic_) {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            const float fw = static_cast<float>(rc.right - rc.left);
            const float fh = static_cast<float>(rc.bottom - rc.top);
            ComPtr<ID2D1SolidColorBrush> ring;
            d2d_context_->CreateSolidColorBrush(D2D1::ColorF(255, 255, 255, 0.15f),
                                                ring.GetAddressOf());
            D2D1_RECT_F border{0.5f, 0.5f, fw - 0.5f, fh - 0.5f};
            d2d_context_->DrawRectangle(border, ring.Get(), 1.0f);
        }

        // PERF-001: replay the immutable world-space frame in canonical
        // emission order. No effect model, no geometry build and no config
        // is touched here -- every overlay consumes the same primitives.
        for (const FramePrimitive& primitive : frame.primitives()) {
            switch (primitive.kind) {
                case FramePrimitiveKind::TrailSegment: emit_trail_segment(primitive); break;
                case FramePrimitiveKind::TrailSparkle: emit_sparkle(primitive); break;
                case FramePrimitiveKind::Bubble:       emit_bubble(primitive); break;
                case FramePrimitiveKind::Particle:     emit_particle(primitive); break;
            }
        }
    }

    HRESULT hr = d2d_context_->EndDraw();
    if (is_device_loss_hresult(hr)) {
        note_device_loss("render_frame EndDraw", hr);
        return;
    }
    if (FAILED(hr)) {
        // Non-device failure: logged, no blind full device-chain rebuild.
        overlay_log(ptd::LogLevel::Error, std::string("render_frame EndDraw hr=") + std::to_string(hr));
        return;
    }
    hr = swap_chain_->Present(0, 0);
    if (is_device_loss_hresult(hr)) {
        note_device_loss("render_frame Present", hr);
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Error, std::string("render_frame Present hr=") + std::to_string(hr));
        return;
    }
    // The frame reached the swap chain: NOW record the dirty-state transition
    // that this present performed. A frame that failed before here returned
    // without committing, so the overlay is not falsely marked clear.
    dirty_.commit(intersects);
    hr = dcomp_device_->Commit();
    if (is_device_loss_hresult(hr)) {
        note_device_loss("render_frame Commit", hr);
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Warn, std::string("render_frame Commit hr=") + std::to_string(hr));
    }
    // No manual flip-model rebind: see init_render rationale (D2D device
    // context rebinds internally; full-frame Clear keeps buffers exact).
}

ID2D1StrokeStyle1* OverlayWindow::stroke_style_for(TrailCapPolicy policy) const {
    switch (policy) {
        case TrailCapPolicy::FlatFlat:  return flat_flat_stroke_style_.Get();
        case TrailCapPolicy::FlatRound: return flat_round_stroke_style_.Get();
        case TrailCapPolicy::RoundRound:
        default:                        return round_stroke_style_.Get();
    }
}

// PERF-001: one trail segment of the frame -- transform, conservative cull,
// optional SoftGlow/Neon outer pass, core stroke. The cap policy and the
// outer glow factors arrive RESOLVED in the primitive (FrameGeometry), so
// every overlay strokes the segment identically and no config is consulted
// here. Cached resources only.
void OverlayWindow::emit_trail_segment(const FramePrimitive& segment) {
    if (!d2d_context_ || !trail_brush_) return;
    if (!(segment.alpha > 0.0f)) return; // fully faded: skip the draw call
    ID2D1StrokeStyle1* const stroke_style = stroke_style_for(segment.caps);
    if (!stroke_style) return;

    // MVP 08 production transform: virtual-screen physical pixels ->
    // overlay-local physical pixels. No DPI multiplication.
    const float lx1 = transform_.to_local_x(segment.x1);
    const float ly1 = transform_.to_local_y(segment.y1);
    const float lx2 = transform_.to_local_x(segment.x2);
    const float ly2 = transform_.to_local_y(segment.y2);

    // Keep seam-crossing segments: each adjacent overlay draws/culls the
    // same canonical virtual segment independently. The conservative margin
    // mirrors FrameGeometry's bounding-box extent -- the widest stroke this
    // segment can draw (core, or the SoftGlow/Neon outer pass) -- so the
    // frame box never admits a segment whose stroke is then clipped at a
    // seam. The legacy 2x core margin stays as the floor, unchanged.
    const float draw_thickness_max = segment.has_outer
        ? (segment.outer_width_px > segment.thickness
               ? segment.outer_width_px : segment.thickness)
        : segment.thickness;
    const float margin = segment.thickness * 2.0f > draw_thickness_max * 0.5f + 1.0f
                       ? segment.thickness * 2.0f
                       : draw_thickness_max * 0.5f + 1.0f;
    if (transform_.cull_segment(lx1, ly1, lx2, ly2, margin)) return;

    trail_brush_->SetColor(D2D1::ColorF(segment.color.r, segment.color.g,
                                        segment.color.b, segment.alpha));

    // T-016 stroke policy: SoftGlow/Neon add ONE wide low-alpha outer pass
    // under the core stroke. T-019: the outer pass uses the SAME cap policy
    // as the core, so glow joints cannot keep beading after the core is
    // smooth. No shaders, no per-frame resource creation.
    if (segment.has_outer && segment.outer_alpha > 0.0f) {
        trail_brush_->SetColor(D2D1::ColorF(segment.color.r, segment.color.g,
                                            segment.color.b, segment.outer_alpha));
        d2d_context_->DrawLine(D2D1::Point2F(lx1, ly1), D2D1::Point2F(lx2, ly2),
                               trail_brush_.Get(), segment.outer_width_px, stroke_style);
        trail_brush_->SetColor(D2D1::ColorF(segment.color.r, segment.color.g,
                                            segment.color.b, segment.alpha));
    }

    d2d_context_->DrawLine(D2D1::Point2F(lx1, ly1), D2D1::Point2F(lx2, ly2),
                           trail_brush_.Get(), segment.thickness, stroke_style);
}



// T-021: one sparkle decoration. Same virtual-screen -> overlay-local
// transform and cull contract as every other primitive; only cached
// resources are touched (trail brush + the three cached stroke styles),
// so any sparkle count stays allocation-free per frame.
void OverlayWindow::emit_sparkle(const FramePrimitive& sparkle) {
    const float x = sparkle.x1;
    const float y = sparkle.y1;
    const float size_px = sparkle.size_px;
    const float rotation_rad = sparkle.rotation_rad;
    const float alpha = sparkle.alpha;
    const TrailColorF color = sparkle.color;
    const TrailSparkleShape shape = sparkle.shape;
    if (!d2d_context_ || !trail_brush_) return;
    if (!(alpha > 0.0f) || !(size_px > 0.0f)) return;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size_px)
        || !std::isfinite(alpha)) return;

    // Cull with a conservative outer radius (rotated Cross/Diamond arms
    // reach size_px/2 from the center; size_px covers every shape plus
    // stroke half-width headroom).
    if (transform_.cull_circle(x, y, size_px)) return;

    const float lx = transform_.to_local_x(x);
    const float ly = transform_.to_local_y(y);
    trail_brush_->SetColor(
        D2D1::ColorF(color.r, color.g, color.b, alpha));

    switch (shape) {
        case TrailSparkleShape::Dot: {
            const float radius = size_px * 0.5f;
            d2d_context_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(lx, ly), radius, radius),
                trail_brush_.Get());
            break;
        }
        case TrailSparkleShape::Cross: {
            // Two short cached-style lines through the center, rotated.
            // Round caps give the star its soft arm tips.
            const float half = size_px * 0.5f;
            const float thickness = size_px * 0.28f;
            const float c = std::cos(rotation_rad);
            const float s = std::sin(rotation_rad);
            const float ax = c * half, ay = s * half;
            const float bx = s * half, by = -c * half;
            d2d_context_->DrawLine(D2D1::Point2F(lx - ax, ly - ay),
                                   D2D1::Point2F(lx + ax, ly + ay),
                                   trail_brush_.Get(), thickness,
                                   round_stroke_style_.Get());
            d2d_context_->DrawLine(D2D1::Point2F(lx - bx, ly - by),
                                   D2D1::Point2F(lx + bx, ly + by),
                                   trail_brush_.Get(), thickness,
                                   round_stroke_style_.Get());
            break;
        }
        case TrailSparkleShape::Diamond: {
            // A small rotated square drawn as four cached-style lines --
            // a bounded line representation with zero COM allocation.
            const float r = size_px * 0.5f;
            const float thickness = size_px * 0.22f;
            const float c = std::cos(rotation_rad);
            const float s = std::sin(rotation_rad);
            const D2D1_POINT_2F p[4] = {
                D2D1::Point2F(lx + c * r,        ly + s * r),
                D2D1::Point2F(lx - s * r,        ly + c * r),
                D2D1::Point2F(lx - c * r,        ly - s * r),
                D2D1::Point2F(lx + s * r,        ly - c * r),
            };
            for (int i = 0; i < 4; ++i) {
                const D2D1_POINT_2F& a = p[i];
                const D2D1_POINT_2F& b = p[(i + 1) % 4];
                d2d_context_->DrawLine(a, b, trail_brush_.Get(), thickness,
                                       round_stroke_style_.Get());
            }
            break;
        }
        case TrailSparkleShape::Triangle: {
            if (!triangle_geometry_) break;
            D2D1_MATRIX_3X2_F previous{};
            d2d_context_->GetTransform(&previous);
            const D2D1_MATRIX_3X2_F local =
                D2D1::Matrix3x2F::Scale(size_px, size_px)
                * D2D1::Matrix3x2F::Rotation(rotation_rad)
                * D2D1::Matrix3x2F::Translation(lx, ly);
            d2d_context_->SetTransform(local);
            d2d_context_->FillGeometry(triangle_geometry_.Get(),
                                       trail_brush_.Get());
            d2d_context_->SetTransform(previous);
            break;
        }
    }
}

// T-017: one particle dot (small filled disc), same transform/cull
// contract as emit_bubble; cached brush only, no per-particle resources.
void OverlayWindow::emit_particle(const FramePrimitive& particle) {
    const float cx = particle.x1;
    const float cy = particle.y1;
    const float radius_px = particle.radius_px;
    const float alpha = particle.alpha;
    if (!d2d_context_ || !bubble_brush_) return;
    if (!(alpha > 0.0f) || !(radius_px > 0.0f)) return;
    const float lx = transform_.to_local_x(cx);
    const float ly = transform_.to_local_y(cy);
    if (transform_.cull_circle(cx, cy, radius_px)) return;
    const auto norm = normalize_click_rgb(particle.r, particle.g, particle.b);
    bubble_brush_->SetColor(D2D1::ColorF(norm.r, norm.g, norm.b, alpha));
    d2d_context_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(lx, ly), radius_px, radius_px),
        bubble_brush_.Get());
}

void OverlayWindow::emit_bubble(const FramePrimitive& bubble) {
    const float cx = bubble.x1;
    const float cy = bubble.y1;
    const float radius_px = bubble.radius_px;
    const float outline_thickness_px = bubble.outline_px;
    const float ring_alpha = bubble.ring_alpha;
    const float fill_alpha = bubble.fill_alpha;
    // C7/C6/Phase K: one antialiased circular outline + a subtle
    // translucent inner fill (kept cheap: same ellipse, two cached
    // brushes, no per-bubble resource creation). Ring and fill alphas
    // arrive PRECOMPUTED from the effect -- the renderer never re-derives
    // the fill from the ring.
    if (!d2d_context_ || !bubble_brush_ || !bubble_fill_brush_
        || !round_stroke_style_) return;
    if (!(ring_alpha > 0.0f) && !(fill_alpha > 0.0f)) return;
    if (!(radius_px > 0.0f)) return;

    // C8 transform: virtual-screen physical pixels -> overlay-local (same
    // contract as emit_trail_segment/unit-correct culling helper).
    const float lx = transform_.to_local_x(cx);
    const float ly = transform_.to_local_y(cy);
    if (transform_.cull_circle(cx, cy, radius_px + outline_thickness_px))
        return;

    const auto norm = normalize_click_rgb(bubble.r, bubble.g, bubble.b);
    const D2D1_POINT_2F center = D2D1::Point2F(lx, ly);
    const D2D1_ELLIPSE ellipse = D2D1::Ellipse(center, radius_px, radius_px);

    if (fill_alpha > 0.0f) {
        bubble_fill_brush_->SetColor(D2D1::ColorF(norm.r, norm.g, norm.b, fill_alpha));
        d2d_context_->FillEllipse(ellipse, bubble_fill_brush_.Get());
    }

    if (ring_alpha > 0.0f) {
        bubble_brush_->SetColor(D2D1::ColorF(norm.r, norm.g, norm.b, ring_alpha));
        d2d_context_->DrawEllipse(ellipse, bubble_brush_.Get(),
                                  outline_thickness_px, round_stroke_style_.Get());
    }
}

void OverlayWindow::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        overlay_log(ptd::LogLevel::Info, "overlay: shown (no activate)");
    }
}

void OverlayWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::release_render_resources() {
    const bool had_render_resources = static_cast<bool>(dcomp_device_) || static_cast<bool>(d3d_device_);
    if (round_stroke_style_) round_stroke_style_.Reset();
    if (flat_flat_stroke_style_) flat_flat_stroke_style_.Reset();
    if (flat_round_stroke_style_) flat_round_stroke_style_.Reset();
    if (trail_brush_) trail_brush_.Reset();
    if (bubble_brush_) bubble_brush_.Reset();
    if (bubble_fill_brush_) bubble_fill_brush_.Reset();
    if (triangle_geometry_) triangle_geometry_.Reset();
    if (dcomp_visual_) dcomp_visual_.Reset();
    if (dcomp_target_) dcomp_target_.Reset();
    if (dcomp_device_) dcomp_device_.Reset();
    if (d2d_target_) d2d_target_.Reset();
    if (d2d_context_) d2d_context_.Reset();
    if (d2d_device_) d2d_device_.Reset();
    if (swap_chain_) swap_chain_.Reset();
    if (dxgi_factory_) dxgi_factory_.Reset();
    if (dxgi_device_) dxgi_device_.Reset();
    if (d3d_device_) d3d_device_.Reset();
    if (had_render_resources) {
        overlay_log(ptd::LogLevel::Info, "overlay: resources released");
    }
}

void OverlayWindow::destroy() {
    release_render_resources();
    // No recovery without an HWND; drop pending state with the window.
    recovery_ = RecoveryState{};
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace ptd
