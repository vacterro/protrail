// T-013 MVP 08: Multi-Monitor and DPI regression tests.
// T-013R1 hardening: the coalescing/WndProc/partial-create proofs exercise
// the production DeferredCoalescer and a synthetic monitor topology -- no
// decorative assertions, no hardware dependence for the deterministic parts.
// T-013R2 hardening: live HWND DPI is the refresh authority -- deterministic
// WindowDpiFn-seam proofs for initial snapshot normalization, DPI-change
// reconciliation, refresh no-op, and isolated multi-monitor DPI changes.

#include "../src/platform/dpi_awareness.h"
#include "../src/render/overlay_manager.h"
#include "../src/render/screen_map.h"
#include "../src/render/deferred_coalescer.h"
#include "../src/platform/mouse_input.h"
#include "../src/render/overlay_window.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <functional>
#include <vector>

class TestMultiMonitor : public QObject {
    Q_OBJECT
private slots:
    static RECT make_rect(int l, int t, int r, int b) {
        RECT rc{}; rc.left=l; rc.top=t; rc.right=r; rc.bottom=b; return rc;
    }

    void monitor_enumeration_returns_valid_displays();
    void primary_monitor_origin_and_positive_bounds();
    void per_monitor_dpi_is_valid_scale_metadata();
    void physical_pixel_invariant_no_second_scaling();
    void production_transform_primary_right_left();
    void production_transform_above_below_bothNegative();
    void production_transform_negative_coords_correct();
    void overlay_transform_roundtrip_preserves_virtual();
    void seam_horizontal_and_vertical_continuity();
    void segment_spanning_two_overlays_culling();
    void neon_and_softglow_conservative_culling_margin();
    void bubble_close_to_edge_routing_and_culling();
    void mixed_dpi_metadata_does_not_alter_transform();
    void hardware_mixed_dpi_coordinate_invariant();
    void topology_equality_and_change_detection();
    void monitor_overlay_pairing_and_partial_create_failure();
    void initial_snapshot_normalized_from_live_window_dpi();
    void live_dpi_change_is_detected_and_reconciled();
    void unchanged_live_dpi_refresh_is_noop();
    void multi_monitor_partial_dpi_change_isolated();
    void transient_create_failure_is_retried_by_refresh();
    void changed_monitor_recreate_failure_is_retried();
    void replacement_hwnd_dpi_difference_snapshot_regression();
    void newly_added_monitor_dpi_normalizes_snapshot();
    void deferred_refresh_coalescing_contract();
    void dpi_awareness_query_shape();
    void topology_notification_requests_deferred_work_only();
};

void TestMultiMonitor::monitor_enumeration_returns_valid_displays() {
    const auto monitors = ptd::OverlayManager::enumerate_monitors();
    QVERIFY(!monitors.empty());
    for (const auto& mon : monitors) {
        QVERIFY(mon.bounds.right - mon.bounds.left > 0);
        QVERIFY(mon.bounds.bottom - mon.bounds.top > 0);
        QVERIFY(mon.dpi_x >= 48 && mon.dpi_x <= 480);
        QVERIFY(mon.dpi_y >= 48 && mon.dpi_y <= 480);
        QVERIFY(mon.scale >= 0.5f && mon.scale <= 5.0f);
    }
}

void TestMultiMonitor::primary_monitor_origin_and_positive_bounds() {
    const auto monitors = ptd::OverlayManager::enumerate_monitors();
    const auto primary = std::find_if(monitors.begin(), monitors.end(),
        [](const auto& mon) { return mon.is_primary; });
    QVERIFY(primary != monitors.end());
    QCOMPARE(primary->bounds.left, 0l);
    QCOMPARE(primary->bounds.top, 0l);
    QVERIFY(primary->bounds.right > primary->bounds.left);
    QVERIFY(primary->bounds.bottom > primary->bounds.top);
}

void TestMultiMonitor::per_monitor_dpi_is_valid_scale_metadata() {
    const auto monitors = ptd::OverlayManager::enumerate_monitors();
    for (const auto& mon : monitors) {
        QVERIFY(mon.dpi_x >= 96);
        QVERIFY(mon.scale >= 1.0f);
        QCOMPARE(mon.scale, float(mon.dpi_x) / 96.0f);
    }
}

void TestMultiMonitor::physical_pixel_invariant_no_second_scaling() {
    const RECT bounds = make_rect(1920, 0, 3840, 1080);
    const ptd::OverlayTransform transform = ptd::OverlayTransform::from_bounds(bounds);
    // Pure origin subtraction: the monitor origin maps to local zero, an
    // absolute position keeps its physical offset, and NO scale metadata
    // multiplies the result (scale is metadata only, never applied here).
    QVERIFY(qFuzzyIsNull(transform.to_local_x(float(bounds.left))));
    QVERIFY(qFuzzyIsNull(transform.to_local_y(float(bounds.top))));
    QCOMPARE(transform.to_local_x(2500.0f), 580.0f);
    QCOMPARE(transform.to_local_x(1920.0f + 580.0f), 580.0f);
    QCOMPARE(transform.to_local_y(400.0f), 400.0f);
    QCOMPARE(transform.width, 1920.0f);
    QCOMPARE(transform.height, 1080.0f);
}

void TestMultiMonitor::production_transform_primary_right_left() {
    const auto mon_primary = ptd::OverlayTransform::from_bounds(make_rect(0, 0, 1920, 1080));
    const auto mon_right   = ptd::OverlayTransform::from_bounds(make_rect(1920, 0, 3840, 1080));
    const auto mon_left    = ptd::OverlayTransform::from_bounds(make_rect(-1920, 0, 0, 1080));
    QCOMPARE(mon_primary.to_local_x(100.0f), 100.0f);
    QCOMPARE(mon_primary.to_local_y(100.0f), 100.0f);
    QCOMPARE(mon_right.to_local_x(1920.0f), 0.0f);
    QCOMPARE(mon_right.to_local_x(2500.0f), 580.0f);
    QCOMPARE(mon_right.to_local_y(400.0f), 400.0f);
    QCOMPARE(mon_left.to_local_x(-500.0f), -500.0f - float(-1920));
    QCOMPARE(mon_left.to_local_x(-500.0f), 1420.0f);
    QCOMPARE(mon_left.to_local_y(300.0f), 300.0f);
}

void TestMultiMonitor::production_transform_above_below_bothNegative() {
    const auto mon_above = ptd::OverlayTransform::from_bounds(make_rect(0, -1080, 1920, 0));
    const auto mon_below = ptd::OverlayTransform::from_bounds(make_rect(0, 1080, 1920, 2160));
    const auto mon_diag  = ptd::OverlayTransform::from_bounds(make_rect(-1920, -1080, 0, 0));
    QCOMPARE(mon_above.to_local_y(-400.0f), -400.0f - float(-1080));
    QCOMPARE(mon_above.to_local_x(960.0f), 960.0f);
    QCOMPARE(mon_below.to_local_y(1100.0f), 1100.0f - 1080.0f);
    QCOMPARE(mon_below.to_local_x(10.0f), 10.0f);
    QCOMPARE(mon_diag.to_local_x(-100.0f), -100.0f - float(-1920));
    QCOMPARE(mon_diag.to_local_y(-100.0f), -100.0f - float(-1080));
}

void TestMultiMonitor::production_transform_negative_coords_correct() {
    const auto transform = ptd::OverlayTransform::from_bounds(make_rect(-1920, -1080, 0, 0));
    QCOMPARE(transform.to_local_x(-1920.0f), 0.0f);
    QCOMPARE(transform.to_local_y(-1080.0f), 0.0f);
    QCOMPARE(transform.to_local_x(-1.0f), 1919.0f);
    QCOMPARE(transform.to_local_y(-1.0f), 1079.0f);
}

void TestMultiMonitor::overlay_transform_roundtrip_preserves_virtual() {
    const RECT rect = make_rect(-3840, -540, -1920, 540);
    const auto transform = ptd::OverlayTransform::from_bounds(rect);
    const float virtual_x = -3000.0f, virtual_y = 0.0f;
    QCOMPARE(transform.origin_x + transform.to_local_x(virtual_x), virtual_x);
    QCOMPARE(transform.origin_y + transform.to_local_y(virtual_y), virtual_y);
}

void TestMultiMonitor::seam_horizontal_and_vertical_continuity() {
    const auto mon_a = ptd::OverlayTransform::from_bounds(make_rect(0, 0, 1920, 1080));
    const auto mon_b = ptd::OverlayTransform::from_bounds(make_rect(1920, 0, 3840, 1080));
    const auto mon_top = ptd::OverlayTransform::from_bounds(make_rect(0, -1080, 1920, 0));
    // Horizontal seam at x=1920: the same virtual point renders at the right
    // edge of A and the left edge of B; a crossing segment is culled by
    // neither overlay, and local positions differ by the CONSTANT monitor
    // width everywhere (no gap can appear at the seam).
    QCOMPARE(mon_a.to_local_x(1920.0f), 1920.0f);
    QCOMPARE(mon_b.to_local_x(1920.0f), 0.0f);
    QVERIFY(!mon_a.cull_segment(mon_a.to_local_x(1910.0f), mon_a.to_local_y(400.0f),
                                mon_a.to_local_x(1930.0f), mon_a.to_local_y(400.0f), 4.0f));
    QVERIFY(!mon_b.cull_segment(mon_b.to_local_x(1910.0f), mon_b.to_local_y(400.0f),
                                mon_b.to_local_x(1930.0f), mon_b.to_local_y(400.0f), 4.0f));
    QCOMPARE(mon_b.to_local_x(2500.0f) - mon_a.to_local_x(2500.0f), -1920.0f);
    // Vertical seam at y=0 between the top and primary monitors: the same
    // constant-offset continuity with the monitor height.
    QCOMPARE(mon_top.to_local_y(0.0f), 1080.0f);
    QCOMPARE(mon_a.to_local_y(0.0f), 0.0f);
    QCOMPARE(mon_top.to_local_y(500.0f) - mon_a.to_local_y(500.0f), 1080.0f);
}

void TestMultiMonitor::segment_spanning_two_overlays_culling() {
    const auto mon_a = ptd::OverlayTransform::from_bounds(make_rect(0, 0, 1920, 1080));
    const auto mon_b = ptd::OverlayTransform::from_bounds(make_rect(1920, 0, 3840, 1080));
    QVERIFY(mon_a.cull_segment(mon_a.to_local_x(500.0f), mon_a.to_local_y(500.0f),
                                mon_a.to_local_x(3100.0f), mon_a.to_local_y(500.0f), 4.0f) == false);
    QVERIFY(mon_b.cull_segment(mon_b.to_local_x(500.0f), mon_b.to_local_y(500.0f),
                                mon_b.to_local_x(3100.0f), mon_b.to_local_y(500.0f), 4.0f) == false);
    const auto mon_far = ptd::OverlayTransform::from_bounds(make_rect(-1920, 0, 0, 1080));
    QVERIFY(mon_far.cull_segment(mon_far.to_local_x(2500.0f), mon_far.to_local_y(500.0f),
                                  mon_far.to_local_x(2700.0f), mon_far.to_local_y(500.0f), 12.0f));
}

void TestMultiMonitor::neon_and_softglow_conservative_culling_margin() {
    // T-016 culling repair: Neon/SoftGlow outer stroke is substantially wider
    // than the core trail thickness (Neon up to 6x core at glow_strength 1.0).
    // The renderer must derive the culling margin from the largest actual stroke radius.
    // Near a multi-monitor seam or overlay edge, a segment whose core stroke lies
    // outside the overlay must NOT be culled while its wide glow still intersects.
    const auto mon_b = ptd::OverlayTransform::from_bounds(make_rect(1920, 0, 3840, 1080));
    const float core_thickness = 10.0f;
    const float glow_strength = 1.0f;

    // Segment on monitor A near seam: virtual x in [1885, 1895].
    // On monitor B, local x in [-35, -25].
    const float lx1 = mon_b.to_local_x(1885.0f); // -35.0f
    const float ly1 = 400.0f;
    const float lx2 = mon_b.to_local_x(1895.0f); // -25.0f
    const float ly2 = 400.0f;

    // 1. Classic baseline margin: 2x core thickness = 20.0f.
    // Core is outside mon_b bounds (max_x = -25 < -20) -> culled.
    const float classic_margin = core_thickness * 2.0f;
    QVERIFY(mon_b.cull_segment(lx1, ly1, lx2, ly2, classic_margin));

    // 2. Neon stroke margin calculation matching OverlayWindow::add_segment:
    // draw_thickness_max = core * (3 + 3 * glow) = 60.0f.
    // margin = max(core * 2, draw_thickness_max * 0.5f + 1.0f) = max(20, 31) = 31.0f.
    const float neon_draw_max = core_thickness * (3.0f + 3.0f * glow_strength);
    const float neon_margin = std::max(core_thickness * 2.0f, neon_draw_max * 0.5f + 1.0f);
    QCOMPARE(neon_margin, 31.0f);
    // Outer stroke reaches -25 + 30 = +5.0f into mon_b -> must NOT be culled!
    QVERIFY(!mon_b.cull_segment(lx1, ly1, lx2, ly2, neon_margin));

    // 3. Far outside segment is still culled under Neon margin:
    const float far_lx1 = mon_b.to_local_x(1800.0f); // -120.0f
    const float far_lx2 = mon_b.to_local_x(1850.0f); // -70.0f
    QVERIFY(mon_b.cull_segment(far_lx1, ly1, far_lx2, ly2, neon_margin));
}

void TestMultiMonitor::bubble_close_to_edge_routing_and_culling() {
    const auto mon_a = ptd::OverlayTransform::from_bounds(make_rect(0, 0, 1920, 1080));
    const auto mon_b = ptd::OverlayTransform::from_bounds(make_rect(1920, 0, 3840, 1080));
    // 30 px bubble centered at the edge-inclusive x=1920 is within mon_a's margin.
    QVERIFY(!mon_a.cull_circle(1920.0f, 500.0f, 30.0f + 2.0f));
    // Far mon should cull the same position.
    QVERIFY(mon_a.cull_circle(6000.0f, 500.0f, 30.0f + 2.0f));
    QVERIFY(!mon_b.cull_circle(1930.0f, 500.0f, 30.0f + 2.0f));
    QVERIFY(mon_b.cull_circle(100.0f, 100.0f, 30.0f + 2.0f));
}

void TestMultiMonitor::mixed_dpi_metadata_does_not_alter_transform() {
    // Pure (never skips): DPI/scale is metadata; the physical-pixel origin
    // subtraction is identical for every scale. A 144-DPI monitor must not
    // move or stretch coordinates compared with the same rect at 96 DPI.
    const RECT rect = make_rect(1920, 0, 3840, 1080);
    ptd::MonitorInfo hi_dpi{};
    hi_dpi.bounds = rect;
    hi_dpi.dpi_x = 144;
    hi_dpi.dpi_y = 144;
    hi_dpi.scale = 1.5f;
    ptd::MonitorInfo lo_dpi = hi_dpi;
    lo_dpi.dpi_x = 96;
    lo_dpi.dpi_y = 96;
    lo_dpi.scale = 1.0f;

    const auto hi = ptd::OverlayTransform::from_bounds(hi_dpi.bounds);
    const auto lo = ptd::OverlayTransform::from_bounds(lo_dpi.bounds);
    QCOMPARE(hi.to_local_x(2500.0f), 580.0f);
    QCOMPARE(hi.to_local_y(540.0f), 540.0f);
    QCOMPARE(lo.to_local_x(2500.0f), hi.to_local_x(2500.0f));
    QCOMPARE(lo.to_local_y(540.0f), hi.to_local_y(540.0f));
    QCOMPARE(lo.to_local_x(float(rect.left)), hi.to_local_x(float(rect.left)));
    QVERIFY(qFuzzyIsNull(hi.to_local_x(float(rect.left))));
    // Culling decisions are scale-blind too.
    QCOMPARE(lo.cull_circle(3840.0f, 500.0f, 32.0f), hi.cull_circle(3840.0f, 500.0f, 32.0f));
    QCOMPARE(lo.cull_segment(300.0f, 10.0f, 600.0f, 10.0f, 4.0f),
             hi.cull_segment(300.0f, 10.0f, 600.0f, 10.0f, 4.0f));
    QVERIFY(!hi.cull_segment(500.0f, 500.0f, 600.0f, 500.0f, 4.0f));
    QVERIFY(hi.cull_segment(-3000.0f, 500.0f, -2500.0f, 500.0f, 12.0f));
}

void TestMultiMonitor::hardware_mixed_dpi_coordinate_invariant() {
    const auto monitors = ptd::OverlayManager::enumerate_monitors();
    if (monitors.size() < 2) QSKIP("need at least two monitors to prove mixed-DPI");
    QVERIFY(monitors[0].scale >= 1.0f);
    QVERIFY(monitors[1].scale >= 1.0f);
    const float virtual_x = float(monitors[1].bounds.left) + 10.0f;
    const float virtual_y = float(monitors[1].bounds.top) + 10.0f;
    // Physical origin subtraction alone is correct; multiplying by scale again jumps.
    const auto mon1 = ptd::OverlayTransform::from_bounds(monitors[1].bounds);
    QCOMPARE(mon1.to_local_x(virtual_x), 10.0f);
    QCOMPARE(mon1.to_local_y(virtual_y), 10.0f);
    const float double_scaled = mon1.to_local_x(virtual_x) * monitors[1].scale;
    QVERIFY(double_scaled != virtual_x - float(monitors[1].bounds.left) || monitors[1].scale == 1.0f);
}

void TestMultiMonitor::topology_equality_and_change_detection() {
    auto snapshot = ptd::OverlayManager::enumerate_monitors();
    QVERIFY(ptd::OverlayManager::monitor_topology_equal(snapshot, snapshot));
    auto added = snapshot;
    RECT extra = make_rect(3840, 0, 5760, 1080);
    ptd::MonitorInfo extra_info{}; extra_info.bounds = extra;
    extra_info.device_name = L"\\\\.\\DISPLAY99";
    added.push_back(extra_info);
    QVERIFY(!ptd::OverlayManager::monitor_topology_equal(snapshot, added));
    auto resolution = snapshot;
    if (!resolution.empty()) { resolution[0].bounds.right += 20; resolution[0].dpi_x += 24; }
    QVERIFY(!ptd::OverlayManager::monitor_topology_equal(snapshot, resolution));
    auto moved = snapshot;
    if (moved.size() > 1) {
        const RECT r0 = moved[0].bounds;
        const RECT r1 = moved[1].bounds;
        moved[0].bounds = r1; moved[1].bounds = r0;
        QVERIFY(!ptd::OverlayManager::monitor_topology_equal(snapshot, moved));
    }
}

void TestMultiMonitor::monitor_overlay_pairing_and_partial_create_failure() {
    // T-013R1: deterministic synthetic topology -- no dependence on how many
    // monitors the running machine has. DISPLAY_MAIN's creation is forced to
    // fail; the two outer monitors must survive with their own MonitorInfo,
    // no index drift and no phantom record for the failed one.
    ptd::OverlayManager manager;

    std::vector<ptd::MonitorInfo> synthetic;
    const wchar_t* kLeft = L"\\\\.\\DISPLAY1";
    const wchar_t* kMain = L"\\\\.\\DISPLAY2";
    const wchar_t* kRight = L"\\\\.\\DISPLAY3";
    const RECT left_bounds = make_rect(-1920, 0, 0, 1080);
    const RECT main_bounds = make_rect(0, 0, 1920, 1080);
    const RECT right_bounds = make_rect(1920, 0, 3840, 1080);
    auto add = [&](const wchar_t* name, const RECT& bounds, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = bounds;
        info.work_area = bounds;
        info.is_primary = primary;
        info.dpi_x = 96;
        info.dpi_y = 96;
        info.scale = 1.0f;
        synthetic.push_back(std::move(info));
    };
    add(kLeft, left_bounds, false);
    add(kMain, main_bounds, true);
    add(kRight, right_bounds, false);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    int attempts = 0;
    int left_attempts = 0;
    int main_attempts = 0;
    int right_attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo& info, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            ++attempts;
            if (info.device_name == kLeft) ++left_attempts;
            if (info.device_name == kMain) ++main_attempts;
            if (info.device_name == kRight) ++right_attempts;
            if (info.device_name == kMain) return nullptr; // middle creation fails
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));

    // Creation attempted once per monitor, including the failing one.
    QCOMPARE(attempts, 3);
    QCOMPARE(left_attempts, 1);
    QCOMPARE(main_attempts, 1);
    QCOMPARE(right_attempts, 1);

    // Exactly two live records, paired 1:1 with their own MonitorInfo.
    const auto& live = manager.monitor_overlays();
    QCOMPARE(manager.overlay_count(), std::size_t(2));
    QCOMPARE(live.size(), std::size_t(2));
    QVERIFY(live[0].window != nullptr);
    QVERIFY(live[1].window != nullptr);

    // No index drift: survivors keep enumeration order and their OWN bounds.
    QVERIFY(live[0].monitor.device_name == kLeft);
    QVERIFY(live[1].monitor.device_name == kRight);
    QCOMPARE(live[0].monitor.bounds.left, left_bounds.left);
    QCOMPARE(live[0].monitor.bounds.right, left_bounds.right);
    QCOMPARE(live[1].monitor.bounds.left, right_bounds.left);
    QCOMPARE(live[1].monitor.bounds.right, right_bounds.right);
    QCOMPARE(live[0].monitor.bounds.top, 0l);
    QCOMPARE(live[1].monitor.bounds.bottom, 1080l);

    // No phantom DISPLAY_MAIN record survives anywhere.
    for (const auto& entry : live) {
        QVERIFY(entry.monitor.device_name != kMain);
        QVERIFY(entry.monitor.bounds.left != main_bounds.left ||
                entry.monitor.bounds.right != main_bounds.right);
    }
    // Enumerated topology still describes all three displays; live records
    // are the subset that actually materialized.
    QCOMPARE(manager.monitors().size(), std::size_t(3));

    manager.destroy();
    QCOMPARE(manager.overlay_count(), std::size_t(0));
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::initial_snapshot_normalized_from_live_window_dpi() {
    // T-013R2 Phase 2: after create(), the canonical monitors_ snapshot must
    // agree with the live MonitorOverlay DPI for every successfully created
    // display. Matched by stable device identity; a monitor whose overlay
    // failed creation keeps its enumeration DPI (no live HWND authority).
    ptd::OverlayManager manager;
    std::vector<ptd::MonitorInfo> synthetic;
    const wchar_t* kOk = L"\\\\.\\SYNTHDISPLAY1";
    const wchar_t* kFail = L"\\\\.\\SYNTHDISPLAY2";
    auto add = [&](const wchar_t* name, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = make_rect(0, 0, 1920, 1080);
        info.work_area = info.bounds;
        info.is_primary = primary;
        info.dpi_x = 96;
        info.dpi_y = 96;
        info.scale = 1.0f;
        synthetic.push_back(std::move(info));
    };
    add(kOk, true);
    add(kFail, false);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    // Enumeration says 96 everywhere; the live HWND authority says 120.
    UINT reported = 120;
    int queries = 0;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        ++queries;
        x = reported;
        y = reported;
        return true;
    });
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo& info, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            if (info.device_name == kFail) return nullptr; // creation fails
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));

    QCOMPARE(manager.overlay_count(), std::size_t(1));
    QVERIFY(manager.monitor_overlays()[0].monitor.device_name == kOk);
    // Live overlay carries the authoritative 120 ...
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 120u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_y, 120u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.scale, 120.0f / 96.0f);
    // ... and the canonical snapshot agrees with the live overlay ...
    QCOMPARE(manager.monitors().size(), std::size_t(2));
    QCOMPARE(manager.monitors()[0].dpi_x, 120u);
    QCOMPARE(manager.monitors()[0].dpi_y, 120u);
    QCOMPARE(manager.monitors()[0].scale, 120.0f / 96.0f);
    // ... while the failed-creation monitor retains enumeration DPI.
    QCOMPARE(manager.monitors()[1].dpi_x, 96u);
    QCOMPARE(manager.monitors()[1].dpi_y, 96u);
    QVERIFY(queries >= 1);

    manager.destroy();
    QCOMPARE(manager.overlay_count(), std::size_t(0));
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::live_dpi_change_is_detected_and_reconciled() {
    // T-013R2 Phase 5: bounds, work area, device identity and enumeration
    // DPI all stay the same; only the live HWND DPI query changes (the
    // observable content of a WM_DPICHANGED). refresh_topology() must
    // detect the transition through live-DPI normalization, reconcile the
    // affected overlay, and never multiply coordinates by scale.
    ptd::OverlayManager manager;
    std::vector<ptd::MonitorInfo> synthetic;
    ptd::MonitorInfo info{};
    info.device_name = L"\\\\.\\SYNTHDISPLAY1";
    info.bounds = make_rect(0, 0, 1920, 1080);
    info.work_area = info.bounds;
    info.is_primary = true;
    info.dpi_x = 96;
    info.dpi_y = 96;
    info.scale = 1.0f;
    synthetic.push_back(info);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    UINT live = 96;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        x = live;
        y = live;
        return true;
    });
    int attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo&, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            ++attempts;
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(attempts, 1);
    QCOMPARE(manager.monitors()[0].dpi_x, 96u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 96u);
    const auto* window_before = manager.monitor_overlays()[0].window.get();
    const auto before = ptd::OverlayTransform::from_bounds(
        manager.monitor_overlays()[0].monitor.bounds);
    QCOMPARE(before.to_local_x(1000.0f), 1000.0f);
    QCOMPARE(before.to_local_y(500.0f), 500.0f);

    live = 144; // WM_DPICHANGED-equivalent: only the live query result moved.
    const bool changed = manager.refresh_topology();
    QVERIFY(changed); // must not early-return "unchanged"
    QCOMPARE(attempts, 2); // exactly one expected recreation
    QCOMPARE(manager.overlay_count(), std::size_t(1));
    QVERIFY(manager.monitor_overlays()[0].window.get() != window_before);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 144u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_y, 144u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.scale, 1.5f);
    QCOMPARE(manager.monitors()[0].dpi_x, 144u);

    // Physical-pixel contract: bounds unchanged, transform unchanged, and
    // no scale multiplication anywhere in the local mapping.
    const auto after = ptd::OverlayTransform::from_bounds(
        manager.monitor_overlays()[0].monitor.bounds);
    QCOMPARE(after.to_local_x(1000.0f), 1000.0f);
    QCOMPARE(after.to_local_y(500.0f), 500.0f);
    QCOMPARE(after.to_local_x(2500.0f), 2500.0f); // raw subtraction, no 1.5x
    QCOMPARE(after.to_local_x(2500.0f), before.to_local_x(2500.0f));
    QCOMPARE(after.to_local_y(400.0f), 400.0f);

    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::unchanged_live_dpi_refresh_is_noop() {
    // T-013R2 Phase 6: same monitor, same bounds, same live HWND DPI --
    // refresh_topology() must stay a no-op even when enumeration metadata
    // drifts; live-DPI normalization overwrites enumeration before the
    // comparison, so a WM_DISPLAYCHANGE broadcast must not recreate overlays.
    ptd::OverlayManager manager;
    std::vector<ptd::MonitorInfo> synthetic;
    ptd::MonitorInfo info{};
    info.device_name = L"\\\\.\\SYNTHDISPLAY1";
    info.bounds = make_rect(0, 0, 1920, 1080);
    info.work_area = info.bounds;
    info.is_primary = true;
    info.dpi_x = 96;
    info.dpi_y = 96;
    info.scale = 1.0f;
    synthetic.push_back(info);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    UINT live = 96;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        x = live;
        y = live;
        return true;
    });
    int attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo&, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            ++attempts;
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(attempts, 1);
    const auto* window_before = manager.monitor_overlays()[0].window.get();

    // Enumeration noise: metadata claims 168, live HWND still reports 96.
    synthetic[0].dpi_x = 168;
    synthetic[0].dpi_y = 168;
    synthetic[0].scale = 1.75f;

    QVERIFY(!manager.refresh_topology());
    QCOMPARE(attempts, 1); // no recreation
    QCOMPARE(manager.overlay_count(), std::size_t(1));
    QCOMPARE(manager.monitor_overlays()[0].window.get(), window_before);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 96u);
    QCOMPARE(manager.monitors()[0].dpi_x, 96u); // live authority won

    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::multi_monitor_partial_dpi_change_isolated() {
    // T-013R2 Phase 7: three synthetic monitors (96 / 144 / 96). Only
    // DISPLAY2's live DPI moves to 192. DISPLAY1/DISPLAY3 keep their exact
    // OverlayWindow instances, DISPLAY2 is the only recreation, and
    // monitor/window identity pairing shows no index drift. No hardware.
    ptd::OverlayManager manager;
    std::vector<ptd::MonitorInfo> synthetic;
    const wchar_t* kLeft = L"\\\\.\\SYNTHDISPLAY1";
    const wchar_t* kMain = L"\\\\.\\SYNTHDISPLAY2";
    const wchar_t* kRight = L"\\\\.\\SYNTHDISPLAY3";
    auto add = [&](const wchar_t* name, const RECT& bounds, UINT dpi, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = bounds;
        info.work_area = bounds;
        info.is_primary = primary;
        info.dpi_x = dpi;
        info.dpi_y = dpi;
        info.scale = static_cast<float>(dpi) / 96.0f;
        synthetic.push_back(std::move(info));
    };
    add(kLeft, make_rect(-1920, 0, 0, 1080), 96, false);
    add(kMain, make_rect(0, 0, 1920, 1080), 144, true);
    add(kRight, make_rect(1920, 0, 3840, 1080), 96, false);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    // The live-HWND seam is queried once per live overlay per pass, in
    // enumeration order (normalize walks discovered overlays in order; a
    // recreated overlay adds one final create_one() query).
    const std::vector<UINT> initial_live{96, 144, 96};
    const std::vector<UINT> changed_live{96, 192, 96, 192};
    const std::vector<UINT>* current = &initial_live;
    std::size_t cursor = 0;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        const UINT value = cursor < current->size() ? (*current)[cursor] : 96u;
        ++cursor;
        x = value;
        y = value;
        return true;
    });

    int left_attempts = 0;
    int main_attempts = 0;
    int right_attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo& info, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            if (info.device_name == kLeft) ++left_attempts;
            if (info.device_name == kMain) ++main_attempts;
            if (info.device_name == kRight) ++right_attempts;
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(manager.overlay_count(), std::size_t(3));
    QCOMPARE(left_attempts, 1);
    QCOMPARE(main_attempts, 1);
    QCOMPARE(right_attempts, 1);

    std::vector<const ptd::OverlayWindow*> before;
    for (const auto& overlay : manager.monitor_overlays())
        before.push_back(overlay.window.get());
    QCOMPARE(manager.monitor_overlays()[1].monitor.dpi_x, 144u);

    current = &changed_live; // only DISPLAY2's live HWND now reports 192
    cursor = 0;
    QVERIFY(manager.refresh_topology());

    // Only the changed monitor was reconciled; the others kept their
    // windows (no broadcast-driven recreation).
    QCOMPARE(left_attempts, 1);
    QCOMPARE(main_attempts, 2);
    QCOMPARE(right_attempts, 1);
    QCOMPARE(manager.overlay_count(), std::size_t(3));

    // Identity pairing intact, in order, with no index drift.
    const auto& live = manager.monitor_overlays();
    QVERIFY(live[0].monitor.device_name == kLeft);
    QVERIFY(live[1].monitor.device_name == kMain);
    QVERIFY(live[2].monitor.device_name == kRight);
    QCOMPARE(live[0].window.get(), before[0]);
    QVERIFY(live[1].window.get() != before[1]);
    QCOMPARE(live[2].window.get(), before[2]);

    // DPI state: DISPLAY2 moved to 192 (scale 2.0), others untouched;
    // canonical snapshot agrees with every live overlay.
    QCOMPARE(live[0].monitor.dpi_x, 96u);
    QCOMPARE(live[1].monitor.dpi_x, 192u);
    QCOMPARE(live[1].monitor.scale, 2.0f);
    QCOMPARE(live[2].monitor.dpi_x, 96u);
    QCOMPARE(manager.monitors().size(), std::size_t(3));
    QCOMPARE(manager.monitors()[0].dpi_x, 96u);
    QCOMPARE(manager.monitors()[1].dpi_x, 192u);
    QCOMPARE(manager.monitors()[2].dpi_x, 96u);

    // Transforms stay pure physical-origin subtraction on every monitor.
    const auto t_left = ptd::OverlayTransform::from_bounds(live[0].monitor.bounds);
    const auto t_main = ptd::OverlayTransform::from_bounds(live[1].monitor.bounds);
    const auto t_right = ptd::OverlayTransform::from_bounds(live[2].monitor.bounds);
    QCOMPARE(t_left.to_local_x(-1000.0f), 920.0f);
    // On the 192-DPI monitor the local mapping is still raw subtraction:
    // virtual 1000 stays local 1000, NOT 1000 * 2.0.
    QCOMPARE(t_main.to_local_x(1000.0f), 1000.0f);
    QCOMPARE(t_main.to_local_y(500.0f), 500.0f);
    QCOMPARE(t_right.to_local_x(2000.0f), 80.0f);

    manager.destroy();
    QCOMPARE(manager.overlay_count(), std::size_t(0));
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::transient_create_failure_is_retried_by_refresh() {
    // T-013R3 Phase 2: a transient create failure must not become permanent.
    // DISPLAY_MAIN fails on the initial create; refresh_topology() with an
    // IDENTICAL enumeration retries exactly that monitor (metadata equality
    // alone is not convergence), preserves the LEFT/RIGHT windows, gives
    // MAIN one new window, and a second identical refresh is a strict no-op.
    ptd::OverlayManager manager;
    const wchar_t* kLeft = L"\\\\.\\DISPLAY1";
    const wchar_t* kMain = L"\\\\.\\DISPLAY2";
    const wchar_t* kRight = L"\\\\.\\DISPLAY3";
    std::vector<ptd::MonitorInfo> synthetic;
    auto add = [&](const wchar_t* name, const RECT& bounds, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = bounds;
        info.work_area = bounds;
        info.is_primary = primary;
        info.dpi_x = 96;
        info.dpi_y = 96;
        info.scale = 1.0f;
        synthetic.push_back(std::move(info));
    };
    add(kLeft, make_rect(-1920, 0, 0, 1080), false);
    add(kMain, make_rect(0, 0, 1920, 1080), true);
    add(kRight, make_rect(1920, 0, 3840, 1080), false);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    int left_attempts = 0, main_attempts = 0, right_attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo& info, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            if (info.device_name == kLeft) ++left_attempts;
            if (info.device_name == kMain) ++main_attempts;
            if (info.device_name == kRight) ++right_attempts;
            // DISPLAY_MAIN fails exactly once: transient failure.
            if (info.device_name == kMain && main_attempts == 1) return nullptr;
            return std::make_unique<ptd::OverlayWindow>();
        });
    manager.set_window_dpi_for_test([](HWND, UINT& x, UINT& y) {
        x = 96;
        y = 96;
        return true;
    });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(manager.overlay_count(), std::size_t(2));
    QCOMPARE(main_attempts, 1);
    QCOMPARE(left_attempts, 1);
    QCOMPARE(right_attempts, 1);

    std::vector<const ptd::OverlayWindow*> before;
    for (const auto& overlay : manager.monitor_overlays())
        before.push_back(overlay.window.get());
    QCOMPARE(before.size(), std::size_t(2));

    QVERIFY(manager.refresh_topology()); // identical enumeration must retry
    QCOMPARE(manager.overlay_count(), std::size_t(3));
    QCOMPARE(main_attempts, 2);  // retried once
    QCOMPARE(left_attempts, 1);  // preserved, never recreated
    QCOMPARE(right_attempts, 1); // preserved, never recreated

    // Identity pairing with no index drift: [LEFT(old) MAIN(new) RIGHT(old)].
    const auto& live = manager.monitor_overlays();
    QVERIFY(live[0].monitor.device_name == kLeft);
    QVERIFY(live[1].monitor.device_name == kMain);
    QVERIFY(live[2].monitor.device_name == kRight);
    QCOMPARE(live[0].window.get(), before[0]);
    QCOMPARE(live[2].window.get(), before[1]);
    QVERIFY(live[1].window.get() != nullptr);
    QVERIFY(live[1].window.get() != before[0]);
    QVERIFY(live[1].window.get() != before[1]);
    QCOMPARE(manager.monitors().size(), std::size_t(3)); // canonical unchanged
    QCOMPARE(manager.monitors()[1].dpi_x, 96u);
    QCOMPARE(manager.monitors()[1].dpi_y, 96u);

    QVERIFY(!manager.refresh_topology()); // converged: strict no-op
    QCOMPARE(manager.overlay_count(), std::size_t(3));
    QCOMPARE(main_attempts, 2);
    QCOMPARE(left_attempts, 1);
    QCOMPARE(right_attempts, 1);

    manager.destroy();
    QCOMPARE(manager.overlay_count(), std::size_t(0));
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::changed_monitor_recreate_failure_is_retried() {
    // T-013R3 Phase 3: a bounds change whose replacement creation fails must
    // not leave the manager claiming a converged live topology. One refresh
    // gets exactly one bounded creation attempt; a later refresh with
    // identical discovered metadata retries and restores one correctly
    // paired live overlay; the refresh after that is a no-op.
    ptd::OverlayManager manager;
    const wchar_t* kMain = L"\\\\.\\SYNTHDISPLAY1";
    std::vector<ptd::MonitorInfo> synthetic;
    ptd::MonitorInfo info{};
    info.device_name = kMain;
    info.bounds = make_rect(0, 0, 1920, 1080);
    info.work_area = info.bounds;
    info.is_primary = true;
    info.dpi_x = 96;
    info.dpi_y = 96;
    info.scale = 1.0f;
    synthetic.push_back(info);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    bool fail_create = false;
    int attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo&, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            ++attempts;
            if (fail_create) return nullptr;
            return std::make_unique<ptd::OverlayWindow>();
        });
    manager.set_window_dpi_for_test([](HWND, UINT& x, UINT& y) {
        x = 96;
        y = 96;
        return true;
    });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(attempts, 1);
    QCOMPARE(manager.overlay_count(), std::size_t(1));

    // Resolution change requires recreation; force the replacement to fail.
    synthetic[0].bounds = make_rect(0, 0, 2560, 1440);
    synthetic[0].work_area = synthetic[0].bounds;
    fail_create = true;
    QVERIFY(manager.refresh_topology());
    fail_create = false;
    QCOMPARE(attempts, 2); // one bounded creation attempt in this refresh
    QCOMPARE(manager.overlay_count(), std::size_t(0));
    QVERIFY(!manager.is_valid()); // NOT converged: no live overlay for MAIN
    QCOMPARE(manager.monitors().size(), std::size_t(1));
    QCOMPARE(manager.monitors()[0].bounds.right, 2560l);

    // Identical discovered metadata: creation is retried and succeeds.
    QVERIFY(manager.refresh_topology());
    QCOMPARE(attempts, 3);
    QCOMPARE(manager.overlay_count(), std::size_t(1));
    QVERIFY(manager.is_valid());
    QVERIFY(manager.monitor_overlays()[0].monitor.device_name == kMain);
    QVERIFY(manager.monitor_overlays()[0].monitor.bounds.right == 2560l);
    QCOMPARE(manager.monitors()[0].dpi_x, 96u);

    QVERIFY(!manager.refresh_topology()); // converged: strict no-op
    QCOMPARE(attempts, 3);
    QCOMPARE(manager.overlay_count(), std::size_t(1));

    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::replacement_hwnd_dpi_difference_snapshot_regression() {
    // T-013R3 Phase 5: the replacement HWND reports 144 while enumeration and
    // the old HWND said 96. After refresh_topology() the live MonitorOverlay
    // AND the canonical monitors_ snapshot must carry 144/1.5. A second
    // refresh with the same topology and live DPI 144 must be a no-op: no
    // second recreation merely because the canonical snapshot retained 96.
    ptd::OverlayManager manager;
    std::vector<ptd::MonitorInfo> synthetic;
    ptd::MonitorInfo info{};
    info.device_name = L"\\\\.\\SYNTHDISPLAY1";
    info.bounds = make_rect(0, 0, 1920, 1080);
    info.work_area = info.bounds;
    info.is_primary = true;
    info.dpi_x = 96;
    info.dpi_y = 96;
    info.scale = 1.0f;
    synthetic.push_back(info);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    // Deterministic per-query DPI sequence (strict cursor, clamped tail):
    // [create old=96, pre-refresh old-HWND=96, replacement HWND=144,
    //  second-refresh live query=144].
    const std::vector<UINT> dpi_sequence{96, 96, 144, 144};
    std::size_t cursor = 0;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        const UINT value = dpi_sequence[std::min(cursor, dpi_sequence.size() - 1)];
        if (cursor < dpi_sequence.size()) ++cursor;
        x = value;
        y = value;
        return true;
    });
    int attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo&, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            ++attempts;
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(attempts, 1);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 96u);
    QCOMPARE(manager.monitors()[0].dpi_x, 96u);

    // Topology changes by bounds only; old HWND still reports 96 pre-refresh.
    synthetic[0].bounds = make_rect(0, 0, 2560, 1440);
    synthetic[0].work_area = synthetic[0].bounds;
    QVERIFY(manager.refresh_topology());
    QCOMPARE(attempts, 2); // exactly one recreation
    const auto* window_after = manager.monitor_overlays()[0].window.get();
    QVERIFY(window_after != nullptr);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_x, 144u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.dpi_y, 144u);
    QCOMPARE(manager.monitor_overlays()[0].monitor.scale, 1.5f);
    // Canonical snapshot agrees with the live HWND authority.
    QCOMPARE(manager.monitors()[0].dpi_x, 144u);
    QCOMPARE(manager.monitors()[0].dpi_y, 144u);
    QCOMPARE(manager.monitors()[0].scale, 1.5f);

    // Same topology, live DPI 144: no-op, no stale-snapshot recreation.
    QVERIFY(!manager.refresh_topology());
    QCOMPARE(attempts, 2);
    QCOMPARE(manager.monitor_overlays()[0].window.get(), window_after);
    QCOMPARE(manager.monitors()[0].dpi_x, 144u);

    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::newly_added_monitor_dpi_normalizes_snapshot() {
    // T-013R3 Phase 6: DISPLAY2 appears in enumeration with DPI 96, but its
    // new overlay HWND reports live DPI 144. After reconciliation DISPLAY1
    // keeps its exact window, DISPLAY2 exists at 144/1.5 in both the live
    // overlay and the canonical snapshot, and the next identical refresh is
    // a no-op. Proves monitor reconnect/add behavior.
    ptd::OverlayManager manager;
    const wchar_t* kLeft = L"\\\\.\\SYNTHDISPLAY1";
    const wchar_t* kRight = L"\\\\.\\SYNTHDISPLAY2";
    std::vector<ptd::MonitorInfo> synthetic;
    auto add = [&](const wchar_t* name, const RECT& bounds, UINT dpi, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = bounds;
        info.work_area = bounds;
        info.is_primary = primary;
        info.dpi_x = dpi;
        info.dpi_y = dpi;
        info.scale = static_cast<float>(dpi) / 96.0f;
        synthetic.push_back(std::move(info));
    };
    add(kLeft, make_rect(0, 0, 1920, 1080), 96, true);
    manager.set_enumeration_for_test([&synthetic] { return synthetic; });

    // Strict-cursor DPI sequence:
    // [create LEFT=96, refresh1 normalize LEFT=96, refresh1 create RIGHT=144,
    //  refresh2 normalize LEFT=96, refresh2 normalize RIGHT=144].
    const std::vector<UINT> dpi_sequence{96, 96, 144, 96, 144};
    std::size_t cursor = 0;
    manager.set_window_dpi_for_test([&](HWND, UINT& x, UINT& y) {
        const UINT value = dpi_sequence[std::min(cursor, dpi_sequence.size() - 1)];
        if (cursor < dpi_sequence.size()) ++cursor;
        x = value;
        y = value;
        return true;
    });
    int left_attempts = 0, right_attempts = 0;
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo& info, HINSTANCE, bool) -> std::unique_ptr<ptd::OverlayWindow> {
            if (info.device_name == kLeft) ++left_attempts;
            if (info.device_name == kRight) ++right_attempts;
            return std::make_unique<ptd::OverlayWindow>();
        });

    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(manager.overlay_count(), std::size_t(1));
    QCOMPARE(left_attempts, 1);
    const auto* left_before = manager.monitor_overlays()[0].window.get();
    QCOMPARE(manager.monitors()[0].dpi_x, 96u);

    // DISPLAY2 plugged in: enumeration adds it with DPI 96.
    add(kRight, make_rect(1920, 0, 3840, 1080), 96, false);
    QVERIFY(manager.refresh_topology());
    QCOMPARE(manager.overlay_count(), std::size_t(2));
    QCOMPARE(left_attempts, 1);  // DISPLAY1 window untouched
    QCOMPARE(right_attempts, 1); // DISPLAY2 created once
    const auto& live = manager.monitor_overlays();
    QCOMPARE(live[0].window.get(), left_before);
    QVERIFY(live[1].monitor.device_name == kRight);
    QCOMPARE(live[1].monitor.dpi_x, 144u); // live HWND authority
    QCOMPARE(live[1].monitor.dpi_y, 144u);
    QCOMPARE(live[1].monitor.scale, 1.5f);
    QCOMPARE(manager.monitors()[1].dpi_x, 144u); // canonical agrees
    QCOMPARE(manager.monitors()[1].dpi_y, 144u);
    QCOMPARE(manager.monitors()[1].scale, 1.5f);

    QVERIFY(!manager.refresh_topology()); // next identical refresh: no-op
    QCOMPARE(manager.overlay_count(), std::size_t(2));
    QCOMPARE(left_attempts, 1);
    QCOMPARE(right_attempts, 1);
    QCOMPARE(manager.monitor_overlays()[0].window.get(), left_before);

    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::deferred_refresh_coalescing_contract() {
    // T-013R1: exact deterministic contract of the PRODUCTION DeferredCoalescer.
    // One pending slot, absorbed bursts, zero work before the drain.
    ptd::DeferredCoalescer coalescer;
    int work_count = 0;
    std::vector<std::function<void()>> deferred;
    const auto post = [&deferred](std::function<void()> work) {
        deferred.push_back(std::move(work));
    };

    QVERIFY(coalescer.request(post, [&] { ++work_count; }));   // refresh A scheduled
    QVERIFY(!coalescer.request(post, [&] { ++work_count; }));  // refresh B absorbed
    QCOMPARE(deferred.size(), std::size_t(1));                 // exactly one queued item
    QCOMPARE(work_count, 0);                                   // nothing ran before the drain
    QVERIFY(coalescer.pending());

    deferred.front()();
    deferred.clear();
    QCOMPARE(work_count, 1);                                   // exactly one after the drain
    QVERIFY(!coalescer.pending());

    QVERIFY(coalescer.request(post, [&] { ++work_count; }));   // independent refresh C
    QCOMPARE(deferred.size(), std::size_t(1));
    QCOMPARE(work_count, 1);                                   // still nothing extra ran
    deferred.front()();
    deferred.clear();
    QCOMPARE(work_count, 2);
    QVERIFY(!coalescer.pending());
}

void TestMultiMonitor::dpi_awareness_query_shape() {
    const auto awareness = ptd::dpi::current_process_awareness();
    const char* name = ptd::dpi::awareness_name(awareness);
    QVERIFY(name != nullptr);
    QVERIFY(*name != '\0');
}

void TestMultiMonitor::topology_notification_requests_deferred_work_only() {
    // T-013R1: behavioral proof of the event architecture. A REAL overlay
    // WndProc receives REAL WM_DISPLAYCHANGE / WM_DPICHANGED messages; the
    // handler must only REQUEST deferred work through the production
    // DeferredCoalescer. refresh_topology-equivalent work may not execute
    // inside the native callback, and a burst must coalesce to one item.
    RECT bounds = make_rect(0, 0, 320, 240);
    ptd::OverlayWindow window;
    if (!window.create(GetModuleHandleW(nullptr), false, &bounds)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }

    int refresh_calls = 0;
    ptd::DeferredCoalescer coalescer;
    std::vector<std::function<void()>> deferred;
    ptd::OverlayWindow::set_display_change_callback([&] {
        coalescer.request(
            [&deferred](std::function<void()> work) { deferred.push_back(std::move(work)); },
            [&] { ++refresh_calls; });
    });

    // Native callback: work is requested, never executed synchronously.
    SendMessageW(window.hwnd(), WM_DISPLAYCHANGE, 0, 0);
    QCOMPARE(refresh_calls, 0);
    QCOMPARE(deferred.size(), std::size_t(1));
    QVERIFY(coalescer.pending());

    // Burst before draining (display change + DPI change): still one item.
    SendMessageW(window.hwnd(), WM_DISPLAYCHANGE, 0, 0);
    SendMessageW(window.hwnd(), WM_DPICHANGED, MAKELPARAM(120, 120), 0);
    QCOMPARE(refresh_calls, 0);
    QCOMPARE(deferred.size(), std::size_t(1));

    // Drain: exactly one reconciliation for the whole burst.
    deferred.front()();
    deferred.clear();
    QCOMPARE(refresh_calls, 1);
    QVERIFY(!coalescer.pending());
    QVERIFY(deferred.empty());

    ptd::OverlayWindow::clear_display_change_callback();
    window.destroy();
}

QTEST_MAIN(TestMultiMonitor)
#include "test_multi_monitor.moc"
