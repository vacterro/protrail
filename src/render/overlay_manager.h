#pragma once

#include "overlay_window.h"

#include <Windows.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace ptd {

struct MonitorInfo {
    HMONITOR hmonitor = nullptr;
    RECT bounds{};           // Virtual desktop physical pixels; may be negative.
    RECT work_area{};
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    float scale = 1.0f;      // Metadata only; never multiply physical coordinates.
    bool is_primary = false;
    std::wstring device_name;
};

// Live identity pairing. Monitor metadata and its HWND/resource owner can
// never become index-misaligned when one overlay creation fails.
struct MonitorOverlay {
    MonitorInfo monitor;
    std::unique_ptr<OverlayWindow> window;
};

// Manages one physical-pixel overlay HWND per monitor. Cursor/effect state
// stays canonical in virtual-screen physical pixels; OverlayWindow performs
// the final origin subtraction with no DPI scaling.
class OverlayManager {
public:
    using CreateWindowFn = std::function<std::unique_ptr<OverlayWindow>(
        const MonitorInfo&, HINSTANCE, bool)>;
    // Deterministic seam for partial-create tests; production leaves empty.
    using EnumerateFn = std::function<std::vector<MonitorInfo>()>;
    // Narrow deterministic seam (T-013R2): the live-HWND DPI authority.
    // Production default is dpi::window_dpi (GetDpiForWindow); tests may
    // inject deterministic values. Never a mock Win32 subsystem.
    using WindowDpiFn = std::function<bool(HWND, UINT&, UINT&)>;

    OverlayManager() = default;
    ~OverlayManager();

    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    static std::vector<MonitorInfo> enumerate_monitors();
    static bool monitor_topology_equal(const std::vector<MonitorInfo>& a,
                                       const std::vector<MonitorInfo>& b);

    bool create(HINSTANCE instance, bool diagnostic);
    void show();
    void hide();
    void destroy();

    // Deferred reconciliation target. This function is NEVER called from
    // OverlayWindow::wnd_proc; Application queues/coalesces it first.
    bool refresh_topology();

    void render_frame(const TrailEffect& effect,
                      const CursorHistory& history,
                      const TrailConfig& trail_config,
                      const ClickBubbleEffect& click_effect,
                      const ClickConfig& click_config,
                      int64_t now_ns);

    // Enumerated topology may include a monitor whose overlay failed; live
    // overlays always carry their own MonitorInfo directly.
    const std::vector<MonitorInfo>& monitors() const { return monitors_; }
    const std::vector<MonitorOverlay>& monitor_overlays() const { return overlays_; }
    std::size_t overlay_count() const { return overlays_.size(); }
    bool is_valid() const { return !overlays_.empty(); }

    // Deterministic seam for partial-create tests; production leaves empty.
    void set_create_window_for_test(CreateWindowFn fn) { create_window_ = std::move(fn); }
    // Deterministic synthetic-topology seam (T-013R1): replaces hardware
    // enumeration for create(); production leaves empty.
    void set_enumeration_for_test(EnumerateFn fn) { enumerate_ = std::move(fn); }
    // Deterministic live-HWND DPI seam (T-013R2): replaces GetDpiForWindow
    // for create()/refresh_topology(); production leaves empty.
    void set_window_dpi_for_test(WindowDpiFn fn) { window_dpi_ = std::move(fn); }

    void set_topology_change_callback(std::function<void()> cb) {
        on_topology_changed_ = std::move(cb);
    }

private:
    bool create_one(const MonitorInfo& monitor, MonitorOverlay& result);
    bool query_window_dpi(HWND hwnd, UINT& dpi_x, UINT& dpi_y) const;
    void normalize_live_dpi(std::vector<MonitorInfo>& discovered) const;
    // T-013R3 topology-liveness invariant: true only when every discovered
    // monitor has a live, identity-matched MonitorOverlay whose metadata is
    // current. A metadata-equal topology with a missing live overlay is NOT
    // converged.
    bool live_overlay_coverage_complete(
        const std::vector<MonitorInfo>& discovered) const;
    // T-013R2/R3: canonical snapshot agrees with the live MonitorOverlay DPI
    // (identity-matched, never by index) for every successfully created
    // display; shared by create() and refresh_topology(). Failed/missing
    // overlay monitors keep their enumeration DPI until a live HWND exists.
    void normalize_snapshot_from_overlays(std::vector<MonitorInfo>& snapshot) const;
    static bool monitor_equal(const MonitorInfo& a, const MonitorInfo& b);
    static std::wstring identity(const MonitorInfo& monitor);

    HINSTANCE instance_ = nullptr;
    bool diagnostic_ = false;
    std::vector<MonitorInfo> monitors_;
    std::vector<MonitorOverlay> overlays_;
    std::function<void()> on_topology_changed_;
    CreateWindowFn create_window_;
    EnumerateFn enumerate_;
    WindowDpiFn window_dpi_;
};

} // namespace ptd
