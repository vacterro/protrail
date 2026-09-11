#include "overlay_window.h"
#include "render_color.h"

#include "../platform/dpi_awareness.h"

#include <Windows.h>
#include <d2d1_1helper.h>
#include <dxgi1_2.h>

#include <cstdint>
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

OverlayWindow::~OverlayWindow() {
    destroy();
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
        overlay_log(ptd::LogLevel::Info, "overlay: render init failed");
        return false;
    }
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
    // mutation; stroke style and target bitmaps are stable.
    D2D1_STROKE_STYLE_PROPERTIES1 stroke{};
    stroke.startCap = D2D1_CAP_STYLE_ROUND;
    stroke.endCap = D2D1_CAP_STYLE_ROUND;
    stroke.dashCap = D2D1_CAP_STYLE_ROUND;
    stroke.lineJoin = D2D1_LINE_JOIN_ROUND;
    stroke.miterLimit = 4.0f;
    hr = d2d_factory->CreateStrokeStyle(stroke, nullptr, 0, round_stroke_style_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateStrokeStyle");

    hr = d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0),
                                             trail_brush_.GetAddressOf());
    if (FAILED(hr)) return fail("CreateSolidColorBrush");

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

    draw_diagnostic_frame();

    hr = dcomp_device_->Commit();
    if (FAILED(hr)) return fail("Commit");

    overlay_log(ptd::LogLevel::Info, "overlay: render chain initialized (D3D11 -> DXGI flip -> D2D -> DComp)");
    return true;
}

void OverlayWindow::draw_diagnostic_frame() {
    if (!d2d_context_) return;

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
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        overlay_log(ptd::LogLevel::Warn, "overlay: device loss in draw_diagnostic_frame EndDraw, recreating render target");
        recreate_render_resources();
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Error, std::string("EndDraw hr=") + std::to_string(hr));
        return;
    }
    if (swap_chain_) {
        hr = swap_chain_->Present(0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            overlay_log(ptd::LogLevel::Warn, "overlay: device loss in draw_diagnostic_frame Present, recreating render target");
            recreate_render_resources();
            return;
        }
    }
    if (dcomp_device_) dcomp_device_->Commit();
    // No manual flip-model rebind here: D2D device contexts handle the
    // back-buffer rotation internally across BeginDraw/EndDraw cycles, and
    // every frame begins with a full Clear (see init_render rationale).
}

void OverlayWindow::render_frame(const TrailEffect& effect,
                                 const CursorHistory& history,
                                 const TrailConfig& config,
                                 const ClickBubbleEffect& click_effect,
                                 const ClickConfig& click_config,
                                 int64_t now_ns) {
    if (!d2d_context_ || !swap_chain_ || !dcomp_device_) return;

    frame_trail_config_ = &config;
    frame_click_config_ = &click_config;

    d2d_context_->BeginDraw();
    // Frame-exact content: full transparent clear every frame. Nothing is
    // accumulated across frames; buffer state is irrelevant by design.
    d2d_context_->Clear(D2D1::ColorF(0, 0.0f));

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

    // TrailEffect decides the trail geometry (B1); this class only
    // presents it, applying the virtual-screen -> overlay-local transform
    // in add_segment() (B4).
    effect.build_geometry(history, now_ns, nullptr,
                          static_cast<int>(history.max_samples()), *this);

    // Click bubbles (T-009 C7): the effect emits per-bubble render state
    // (pure function of elapsed time); this class issues the Direct2D
    // calls via add_bubble() with the same transform contract. draw() is
    // const and never mutates the effect; the Application prunes expired
    // bubbles on the scheduler side (C9).
    click_effect.draw(now_ns, *this);

    frame_trail_config_ = nullptr;
    frame_click_config_ = nullptr;

    HRESULT hr = d2d_context_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        overlay_log(ptd::LogLevel::Warn, "overlay: device loss in render_frame EndDraw (hr=" + std::to_string(hr) + "), recreating render target");
        recreate_render_resources();
        return;
    }
    if (FAILED(hr)) {
        overlay_log(ptd::LogLevel::Error, std::string("render_frame EndDraw hr=") + std::to_string(hr));
        return;
    }
    if (swap_chain_) {
        hr = swap_chain_->Present(0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            overlay_log(ptd::LogLevel::Warn, "overlay: device loss in render_frame Present (hr=" + std::to_string(hr) + "), recreating render target");
            recreate_render_resources();
            return;
        }
    }
    if (dcomp_device_) dcomp_device_->Commit();
    // No manual flip-model rebind: see init_render rationale (D2D device
    // context rebinds internally; full-frame Clear keeps buffers exact).
}

void OverlayWindow::reserve_hint(int segment_count) {
    // Fixed resource set: nothing to reserve (B10 -- no per-frame churn).
    (void)segment_count;
}

void OverlayWindow::reserve_bubbles_hint(int bubble_count) {
    // Fixed resource set: brushes are created once in init_render(); the
    // hint exists so sinks CAN preallocate (tests do) -- the renderer
    // needs no per-frame allocation (C7).
    (void)bubble_count;
}

void OverlayWindow::add_segment(float x1, float y1, float x2, float y2,
                                float alpha, float thickness_px,
                                TrailColorF color) {
    if (!d2d_context_ || !trail_brush_ || !round_stroke_style_) return;
    if (!(alpha > 0.0f)) return; // fully faded: skip the draw call

    // MVP 08 production transform: virtual-screen physical pixels ->
    // overlay-local physical pixels. No DPI multiplication.
    const float lx1 = transform_.to_local_x(x1);
    const float ly1 = transform_.to_local_y(y1);
    const float lx2 = transform_.to_local_x(x2);
    const float ly2 = transform_.to_local_y(y2);

    // Keep seam-crossing segments: each adjacent overlay draws/culls the
    // same canonical virtual segment independently.
    //
    // T-016 culling repair: the margin must be derived from the LARGEST
    // stroke that can actually be drawn for this segment, not from the
    // core thickness alone. SoftGlow/Neon stroke a much wider low-alpha
    // outer pass first (Neon up to core * (3 + 3 * glow) = 6x at glow 1.0);
    // culling with the core-only margin clipped the glow while the outer
    // stroke still intersected the overlay (visible hard edge at overlay
    // borders / multi-monitor seams). The conservative margin below uses
    // the same constants as the stroke policy underneath.
    float draw_thickness_max = thickness_px;
    if (frame_trail_config_
        && (frame_trail_config_->style == ptd::TrailStyle::SoftGlow
            || frame_trail_config_->style == ptd::TrailStyle::Neon)) {
        const bool neon = frame_trail_config_->style == ptd::TrailStyle::Neon;
        const float glow = frame_trail_config_->glow_strength;
        draw_thickness_max = thickness_px * (neon ? 3.0f + 3.0f * glow
                                                  : 2.0f + 2.0f * glow);
    }
    // Half the widest stroke (round caps extend thickness/2) plus a small
    // epsilon headroom; the legacy 2x core margin is kept as the floor so
    // Classic and every existing baseline case is unchanged.
    const float margin = thickness_px * 2.0f > draw_thickness_max * 0.5f + 1.0f
                       ? thickness_px * 2.0f
                       : draw_thickness_max * 0.5f + 1.0f;
    if (transform_.cull_segment(lx1, ly1, lx2, ly2, margin)) return;

    trail_brush_->SetColor(D2D1::ColorF(color.r, color.g, color.b, alpha));

    // T-016 stroke policy: SoftGlow/Neon add ONE wide low-alpha outer pass
    // under the core stroke (weight = config glow_strength). The effect
    // owns per-segment math; the renderer only decides HOW a segment is
    // stroked. No shaders, no per-frame resource creation.
    if (frame_trail_config_
        && (frame_trail_config_->style == ptd::TrailStyle::SoftGlow
            || frame_trail_config_->style == ptd::TrailStyle::Neon)) {
        const bool neon = frame_trail_config_->style == ptd::TrailStyle::Neon;
        const float glow = frame_trail_config_->glow_strength;
        const float outer_w = thickness_px * (neon ? 3.0f + 3.0f * glow
                                                   : 2.0f + 2.0f * glow);
        const float outer_a = alpha * (neon ? 0.45f * glow : 0.30f * glow);
        trail_brush_->SetColor(D2D1::ColorF(color.r, color.g, color.b, outer_a));
        d2d_context_->DrawLine(D2D1::Point2F(lx1, ly1), D2D1::Point2F(lx2, ly2),
                               trail_brush_.Get(), outer_w,
                               round_stroke_style_.Get());
        trail_brush_->SetColor(D2D1::ColorF(color.r, color.g, color.b, alpha));
    }

    d2d_context_->DrawLine(D2D1::Point2F(lx1, ly1), D2D1::Point2F(lx2, ly2),
                           trail_brush_.Get(), thickness_px,
                           round_stroke_style_.Get());
}


// T-017: one particle dot (small filled disc), same transform/cull
// contract as add_bubble; cached brush only, no per-particle resources.
void OverlayWindow::add_particle(float cx, float cy, float radius_px,
                                 float r, float g, float b, float alpha) {
    if (!d2d_context_ || !bubble_brush_) return;
    if (!(alpha > 0.0f) || !(radius_px > 0.0f)) return;
    const float lx = transform_.to_local_x(cx);
    const float ly = transform_.to_local_y(cy);
    if (transform_.cull_circle(cx, cy, radius_px)) return;
    const auto norm = normalize_click_rgb(r, g, b);
    bubble_brush_->SetColor(D2D1::ColorF(norm.r, norm.g, norm.b, alpha));
    d2d_context_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(lx, ly), radius_px, radius_px),
        bubble_brush_.Get());
}

void OverlayWindow::add_bubble(float cx, float cy, float radius_px,
                               float outline_thickness_px,
                               float r, float g, float b,
                               float ring_alpha, float fill_alpha) {
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
    // contract as add_segment/unit-correct culling helper).
    const float lx = transform_.to_local_x(cx);
    const float ly = transform_.to_local_y(cy);
    if (transform_.cull_circle(cx, cy, radius_px + outline_thickness_px))
        return;

    const auto norm = normalize_click_rgb(r, g, b);
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
    if (trail_brush_) trail_brush_.Reset();
    if (bubble_brush_) bubble_brush_.Reset();
    if (bubble_fill_brush_) bubble_fill_brush_.Reset();
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

bool OverlayWindow::recreate_render_resources() {
    overlay_log(ptd::LogLevel::Info, "overlay: recreating render resources");
    release_render_resources();
    return init_render();
}

void OverlayWindow::destroy() {
    release_render_resources();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace ptd
