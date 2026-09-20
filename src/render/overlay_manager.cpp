#include "overlay_manager.h"

#include "../core/log.h"
#include "../platform/dpi_awareness.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace ptd {
namespace {

using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

GetDpiForMonitorFn get_dpi_for_monitor_proc() {
    static HMODULE shcore = LoadLibraryW(L"shcore.dll");
    return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                        GetProcAddress(shcore, "GetDpiForMonitor"))
                  : nullptr;
}

BOOL CALLBACK monitor_enum_proc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) return TRUE;

    MonitorInfo info{};
    info.hmonitor = hmon;
    info.bounds = mi.rcMonitor;
    info.work_area = mi.rcWork;
    info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    info.device_name = mi.szDevice;

    // Enumeration metadata. After each overlay HWND is created, GetDpiForWindow
    // becomes the authoritative DPI for that live MonitorOverlay.
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    const auto get_dpi_for_monitor = get_dpi_for_monitor_proc();
    if (!get_dpi_for_monitor ||
        FAILED(get_dpi_for_monitor(hmon, 0 /* MDT_EFFECTIVE_DPI */, &dpi_x, &dpi_y))) {
        dpi_x = 96;
        dpi_y = 96;
    }
    info.dpi_x = dpi_x ? dpi_x : 96;
    info.dpi_y = dpi_y ? dpi_y : 96;
    info.scale = static_cast<float>(info.dpi_x) / 96.0f;
    list->push_back(std::move(info));
    return TRUE;
}

bool rect_equal(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top &&
           a.right == b.right && a.bottom == b.bottom;
}

std::string narrow_device(const std::wstring& value) {
    if (value.empty()) return "<unknown>";
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 1) return "<unknown>";
    std::string result(static_cast<std::size_t>(count - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                        result.data(), count - 1, nullptr, nullptr);
    return result;
}

std::string describe(const MonitorInfo& monitor) {
    char buf[320];
    const int n = _snprintf_s(
        buf, sizeof(buf), _TRUNCATE,
        "%s bounds=[%ld,%ld-%ld,%ld] dpi=%ux%u scale=%.2f primary=%s",
        narrow_device(monitor.device_name).c_str(),
        monitor.bounds.left, monitor.bounds.top,
        monitor.bounds.right, monitor.bounds.bottom,
        monitor.dpi_x, monitor.dpi_y, monitor.scale,
        monitor.is_primary ? "yes" : "no");
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "<monitor>";
}

} // namespace

OverlayManager::~OverlayManager() {
    destroy();
}

std::wstring OverlayManager::identity(const MonitorInfo& monitor) {
    if (!monitor.device_name.empty()) return monitor.device_name;
    return std::to_wstring(reinterpret_cast<std::uintptr_t>(monitor.hmonitor));
}

bool OverlayManager::monitor_equal(const MonitorInfo& a, const MonitorInfo& b) {
    return identity(a) == identity(b) && rect_equal(a.bounds, b.bounds) &&
           rect_equal(a.work_area, b.work_area) &&
           a.dpi_x == b.dpi_x && a.dpi_y == b.dpi_y &&
           a.is_primary == b.is_primary;
}

bool OverlayManager::monitor_topology_equal(const std::vector<MonitorInfo>& a,
                                            const std::vector<MonitorInfo>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& left : a) {
        const auto left_id = identity(left);
        const auto it = std::find_if(b.begin(), b.end(), [&](const MonitorInfo& right) {
            return identity(right) == left_id;
        });
        if (it == b.end() || !monitor_equal(left, *it)) return false;
    }
    return true;
}

std::vector<MonitorInfo> OverlayManager::enumerate_monitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc,
                        reinterpret_cast<LPARAM>(&list));

    if (list.empty()) {
        MonitorInfo fallback{};
        fallback.bounds = {0, 0, GetSystemMetrics(SM_CXSCREEN),
                           GetSystemMetrics(SM_CYSCREEN)};
        fallback.work_area = fallback.bounds;
        fallback.is_primary = true;
        fallback.device_name = L"fallback-primary";
        list.push_back(std::move(fallback));
    }
    return list;
}

bool OverlayManager::query_window_dpi(HWND hwnd, UINT& dpi_x, UINT& dpi_y) const {
    return window_dpi_ ? window_dpi_(hwnd, dpi_x, dpi_y)
                       : dpi::window_dpi(hwnd, dpi_x, dpi_y);
}

// T-013R2: live HWND DPI is the authority after window creation. Before any
// "topology unchanged" decision, normalize each discovered monitor's DPI
// from its matching live overlay (matched by stable monitor identity). A
// WM_DPICHANGED that kept monitor bounds/device identity is only visible
// through GetDpiForWindow, never through re-enumeration metadata.
void OverlayManager::normalize_live_dpi(std::vector<MonitorInfo>& discovered) const {
    for (auto& monitor : discovered) {
        const auto id = identity(monitor);
        const auto it = std::find_if(overlays_.begin(), overlays_.end(),
            [&](const MonitorOverlay& overlay) {
                return overlay.window != nullptr && identity(overlay.monitor) == id;
            });
        if (it == overlays_.end()) continue;

        UINT dpi_x = 0;
        UINT dpi_y = 0;
        if (!query_window_dpi(it->window->hwnd(), dpi_x, dpi_y)) continue;
        monitor.dpi_x = dpi_x;
        monitor.dpi_y = dpi_y;
        monitor.scale = static_cast<float>(dpi_x) / 96.0f;
    }
}

// T-013R3: "topology unchanged" additionally requires that every discovered
// monitor currently has a live, identity-matched MonitorOverlay whose
// metadata equals the discovered metadata. Without this, an overlay whose
// creation failed transiently could never be retried: enumeration would
// keep returning the same monitors, metadata comparison would short-circuit
// refresh_topology(), and the missing overlay would be permanent.
bool OverlayManager::live_overlay_coverage_complete(
    const std::vector<MonitorInfo>& discovered) const {
    for (const auto& monitor : discovered) {
        const auto id = identity(monitor);
        const auto it = std::find_if(overlays_.begin(), overlays_.end(),
            [&](const MonitorOverlay& overlay) {
                return overlay.window != nullptr && identity(overlay.monitor) == id &&
                       monitor_equal(overlay.monitor, monitor);
            });
        if (it == overlays_.end()) return false;
    }
    return true;
}

// T-013R2/R3 shared invariant: canonical snapshot agrees with the live
// MonitorOverlay DPI (matched by stable device identity, never by index)
// for every successfully created display. A monitor whose overlay is
// missing has no live HWND authority and keeps its enumeration DPI.
void OverlayManager::normalize_snapshot_from_overlays(
    std::vector<MonitorInfo>& snapshot) const {
    for (const auto& overlay : overlays_) {
        const auto id = identity(overlay.monitor);
        const auto it = std::find_if(snapshot.begin(), snapshot.end(),
            [&](const MonitorInfo& monitor) { return identity(monitor) == id; });
        if (it == snapshot.end()) continue;
        it->dpi_x = overlay.monitor.dpi_x;
        it->dpi_y = overlay.monitor.dpi_y;
        it->scale = overlay.monitor.scale;
    }
}

bool OverlayManager::create_one(const MonitorInfo& monitor, MonitorOverlay& result) {
    std::unique_ptr<OverlayWindow> window;
    if (create_window_) {
        window = create_window_(monitor, instance_, diagnostic_);
    } else {
        window = std::make_unique<OverlayWindow>();
        if (!window->create(instance_, diagnostic_, &monitor.bounds)) return false;
    }
    if (!window) return false;

    MonitorInfo live = monitor;
    UINT dpi_x = live.dpi_x;
    UINT dpi_y = live.dpi_y;
    if (query_window_dpi(window->hwnd(), dpi_x, dpi_y)) {
        live.dpi_x = dpi_x;
        live.dpi_y = dpi_y;
        live.scale = static_cast<float>(dpi_x) / 96.0f;
    }

    result.monitor = std::move(live);
    result.window = std::move(window);
    return true;
}

bool OverlayManager::create(HINSTANCE instance, bool diagnostic) {
    instance_ = instance;
    diagnostic_ = diagnostic;
    monitors_ = enumerate_ ? enumerate_() : enumerate_monitors();
    overlays_.clear();
    overlays_.reserve(monitors_.size());

    for (const auto& monitor : monitors_) {
        MonitorOverlay live;
        if (create_one(monitor, live)) {
            log_write(LogLevel::Info,
                      "overlay_manager: created " + describe(live.monitor));
            overlays_.push_back(std::move(live));
        } else {
            log_write(LogLevel::Warn,
                      "overlay_manager: failed to create " + describe(monitor));
        }
    }

    log_write(LogLevel::Info, "overlay_manager: initialized " +
              std::to_string(overlays_.size()) + " of " +
              std::to_string(monitors_.size()) + " per-monitor overlays");

    normalize_snapshot_from_overlays(monitors_);

    return !overlays_.empty();
}

void OverlayManager::show() {
    for (auto& overlay : overlays_) overlay.window->show();
}

void OverlayManager::hide() {
    for (auto& overlay : overlays_) overlay.window->hide();
}

void OverlayManager::destroy() {
    for (auto& overlay : overlays_) overlay.window->destroy();
    overlays_.clear();
    monitors_.clear();
}

bool OverlayManager::refresh_topology() {
    // Same enumeration authority as create(): the injected seam when present,
    // otherwise native enumeration (test seam only; production unchanged).
    auto discovered = enumerate_ ? enumerate_() : enumerate_monitors();
    // Live HWND DPI is authoritative: normalize before any "unchanged"
    // decision so a deferred WM_DPICHANGED cannot become a no-op just
    // because bounds/device identity are unchanged.
    normalize_live_dpi(discovered);
    // T-013R3 topology-liveness invariant: metadata equality alone never
    // proves convergence. A monitor whose overlay creation failed (or whose
    // recreation failed during an earlier refresh) must be retried here on
    // the next deferred refresh, not permanently dropped.
    if (monitor_topology_equal(discovered, monitors_) &&
        live_overlay_coverage_complete(discovered)) return false;

    log_write(LogLevel::Info, "overlay_manager: topology transition begin");
    for (const auto& old_monitor : monitors_)
        log_write(LogLevel::Info, "overlay_manager: old " + describe(old_monitor));
    for (const auto& new_monitor : discovered)
        log_write(LogLevel::Info, "overlay_manager: new " + describe(new_monitor));

    // Move unchanged live overlays by stable device identity. Changed/new
    // monitors receive fresh HWND/render resources. Removed/changed old
    // entries fall out of `remaining` and destruct here, after every
    // triggering WndProc returned (Application deferred callback contract).
    std::vector<MonitorOverlay> remaining = std::move(overlays_);
    std::vector<MonitorOverlay> reconciled;
    reconciled.reserve(discovered.size());

    for (const auto& monitor : discovered) {
        const auto id = identity(monitor);
        auto it = std::find_if(remaining.begin(), remaining.end(),
            [&](const MonitorOverlay& old) {
                return identity(old.monitor) == id && monitor_equal(old.monitor, monitor);
            });
        if (it != remaining.end()) {
            reconciled.push_back(std::move(*it));
            remaining.erase(it);
            continue;
        }

        MonitorOverlay created;
        if (create_one(monitor, created)) {
            created.window->show();
            reconciled.push_back(std::move(created));
        } else {
            log_write(LogLevel::Warn,
                      "overlay_manager: topology create failed " + describe(monitor));
        }
    }

    overlays_ = std::move(reconciled);

    // T-013R3 post-reconciliation snapshot convergence: a newly created or
    // recreated overlay may report a different live DPI than the
    // pre-reconciliation enumeration or the destroyed old HWND. The canonical
    // monitors_ snapshot must agree with the live authority for every live
    // overlay; failed/missing monitors retain their enumeration DPI until a
    // live HWND exists.
    normalize_snapshot_from_overlays(discovered);

    monitors_ = std::move(discovered);
    log_write(LogLevel::Info, "overlay_manager: topology transition complete, live=" +
              std::to_string(overlays_.size()));

    if (on_topology_changed_) on_topology_changed_();
    return true;
}

void OverlayManager::render_frame(const TrailEffect& effect,
                                  const CursorHistory& history,
                                  const TrailConfig& trail_config,
                                  const ClickBubbleEffect& click_effect,
                                  const ClickConfig& click_config,
                                  int64_t now_ns) {
    // PERF-001: build the world-space frame ONCE per scheduler frame -- the
    // effect models are monitor-independent, so running them per overlay was
    // pure multiplication by monitor count. The frame's scope is exactly this
    // call; it is never cached across a different now_ns.
    frame_geometry_.build(effect, history, trail_config,
                          click_effect, now_ns);
    (void)click_config;  // ClickBubbleEffect::draw() carries its own config
    ++frame_builds_;

    last_presenting_ = 0;
    for (auto& overlay : overlays_) {
        // Monitor transform + culling happen inside the window. The dirty
        // contract (frame_geometry.h) is committed by the window only after a
        // successful present, so a frame with no primitives clears every
        // dirty overlay exactly once and then stops presenting clean monitors.
        const bool intersects = frame_geometry_.intersects(overlay.monitor.bounds);
        const OverlayDirtyState::Decision decision =
            overlay.window->dirty_state().peek(intersects);
        overlay.window->render_frame(frame_geometry_, intersects, now_ns);
        if (decision == OverlayDirtyState::Decision::PresentContent) {
            ++last_presenting_;
        }
    }
}

} // namespace ptd
