// T-013 MVP 08: Multi-Monitor and DPI regression tests.
// T-013R1 hardening: the coalescing/WndProc/partial-create proofs exercise
// the production DeferredCoalescer and a synthetic monitor topology -- no
// decorative assertions, no hardware dependence for the deterministic parts.
// T-013R2 hardening: live HWND DPI is the refresh authority -- deterministic
// WindowDpiFn-seam proofs for initial snapshot normalization, DPI-change
// reconciliation, refresh no-op, and isolated multi-monitor DPI changes.

#include "../src/platform/dpi_awareness.h"
#include "../src/render/overlay_manager.h"
#include "../src/render/render_scheduler.h"
#include "../src/render/screen_map.h"
#include "../src/render/frame_geometry.h"
#include "../src/render/deferred_coalescer.h"
#include "../src/platform/mouse_input.h"
#include "../src/render/overlay_window.h"
#include "../src/core/cursor_history.h"

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
    void sparkle_transform_and_culling();
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
    void deferred_coalescer_cancel_is_real_cancellation();
    void deferred_coalescer_destroy_before_drain_is_safe();
    void dpi_awareness_query_shape();
    void topology_notification_requests_deferred_work_only();
    // T-018R1 Phase 14: deterministic bounded device-recovery tests.
    void recovery_state_ready_after_create();
    void recovery_loss_enters_pending_and_releases_resources();
    void recovery_first_failure_stays_pending();
    void recovery_no_immediate_unbounded_retry();
    void recovery_later_retry_success_returns_ready();
    void recovery_persistent_failure_stays_bounded();
    void recovery_repeated_loss_and_recovery_cycles();
    void recovery_real_forced_rebuild_restores_full_resources();
    void recovery_hresult_classification_separates_device_loss();
    void recovery_failed_attempt_exposes_no_stale_resources();
    // PERF-001: one world-space frame build per scheduler frame, per-overlay
    // dirty presentation.
    void perf001_single_effect_build_regardless_of_overlay_count();
    void perf001_dirty_overlay_presents_then_skips_when_clear();
    void perf002_sparse_overlay_buckets_preserve_seams_and_clear_once();
    void perf002_thousands_primitives_use_single_pass_and_reuse_buckets();
    void perf001_global_content_death_clears_each_dirty_overlay_once();
    void perf001_scheduler_rate_follows_populated_buckets();
    void perf001_refresh_only_metadata_updates_without_recreating_overlay();
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

// W2-003: cancel() must invalidate already-posted work, and a request accepted
// after a cancel must belong to a new generation that the stale closure cannot
// clear or execute.
void TestMultiMonitor::deferred_coalescer_cancel_is_real_cancellation() {
    ptd::DeferredCoalescer coalescer;
    int work_count = 0;
    std::vector<std::function<void()>> deferred;
    const auto post = [&deferred](std::function<void()> work) {
        deferred.push_back(std::move(work));
    };

    // request -> cancel -> drain: the queued closure executes nothing.
    QVERIFY(coalescer.request(post, [&] { ++work_count; }));
    coalescer.cancel();
    QVERIFY(!coalescer.pending());
    QCOMPARE(deferred.size(), std::size_t(1));
    deferred.front()();
    QCOMPARE(work_count, 0);
    deferred.clear();

    // request A -> cancel -> request B -> drain stale A -> B remains -> drain B.
    QVERIFY(coalescer.request(post, [&] { ++work_count; }));   // A
    coalescer.cancel();
    QVERIFY(coalescer.request(post, [&] { ++work_count; }));   // B (new generation)
    QCOMPARE(deferred.size(), std::size_t(2));                 // A still queued
    deferred.front()();                                        // drain stale A
    QCOMPARE(work_count, 0);
    QVERIFY(coalescer.pending());                              // B unaffected by A
    deferred.back()();                                         // drain B
    QCOMPARE(work_count, 1);                                   // exactly one B execution
    QVERIFY(!coalescer.pending());
}

// W2-003: destroying the coalescer before the posted closure drains must be
// safe -- the closure holds shared state, never a raw `this`.
void TestMultiMonitor::deferred_coalescer_destroy_before_drain_is_safe() {
    int work_count = 0;
    std::vector<std::function<void()>> deferred;
    const auto post = [&deferred](std::function<void()> work) {
        deferred.push_back(std::move(work));
    };

    {
        ptd::DeferredCoalescer coalescer;
        QVERIFY(coalescer.request(post, [&] { ++work_count; }));
        QCOMPARE(deferred.size(), std::size_t(1));
        // coalescer destroyed here
    }
    deferred.front()();   // must be safe and execute no work
    QCOMPARE(work_count, 0);
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

// ---- T-018R1 Phase 14: deterministic bounded device-recovery tests ----
// The recovery clock and the recreate step are injected, so no test resets
// a real GPU and every policy boundary is exact.

namespace {
constexpr int64_t kNsPerMs = 1'000'000;

// Creates a real overlay (real HWND + real D3D/D2D/DComp chain) and then
// hands recovery control to the test through the seams.
bool create_recovery_overlay(ptd::OverlayWindow& w) {
    RECT r{0, 0, 320, 240};
    return w.create(GetModuleHandleW(nullptr), false, &r);
}
} // namespace

void TestMultiMonitor::recovery_state_ready_after_create() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::Ready);
    QVERIFY(w.has_render_resources());
    QVERIFY(w.hwnd() != nullptr);
    w.destroy();
}

void TestMultiMonitor::recovery_loss_enters_pending_and_releases_resources() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 1'000'000'000;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return true; }, [&] { return t; });

    w.note_device_loss("test", DXGI_ERROR_DEVICE_RESET);
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);
    QVERIFY(!w.has_render_resources());   // stale resources never exposed
    QVERIFY(w.hwnd() != nullptr);         // HWND survives
    QVERIFY(!w.try_recovery());           // gated: before first delay elapses
    QCOMPARE(calls, 0);
    w.destroy();
}

void TestMultiMonitor::recovery_first_failure_stays_pending() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return false; }, [&] { return t; });

    w.note_device_loss("test", D2DERR_RECREATE_TARGET);
    t += 250 * kNsPerMs;                  // initial backoff elapsed
    QVERIFY(!w.try_recovery());           // first recreate attempt fails
    QCOMPARE(calls, 1);
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);
    QVERIFY(!w.has_render_resources());
    w.destroy();
}

void TestMultiMonitor::recovery_no_immediate_unbounded_retry() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return false; }, [&] { return t; });

    w.note_device_loss("test", DXGI_ERROR_DEVICE_REMOVED);
    t += 250 * kNsPerMs;
    QVERIFY(!w.try_recovery());           // failure schedules next backoff
    QCOMPARE(calls, 1);

    // Ten "frames" later, still inside the doubled backoff window: zero
    // further attempts. Persistent failure does not spin per frame.
    for (int i = 0; i < 10; ++i) {
        t += 16 * kNsPerMs;
        QVERIFY(!w.try_recovery());
    }
    QCOMPARE(calls, 1);
    w.destroy();
}

void TestMultiMonitor::recovery_later_retry_success_returns_ready() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return false; }, [&] { return t; });

    w.note_device_loss("test", DXGI_ERROR_DEVICE_RESET);
    t += 250 * kNsPerMs;
    QVERIFY(!w.try_recovery());           // first failure
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);

    // A LATER controlled attempt succeeds (transient failure resolved).
    w.set_recovery_hooks([&] { ++calls; return true; }, [&] { return t; });
    t += 500 * kNsPerMs;                  // doubled backoff elapsed
    QVERIFY(w.try_recovery());
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::Ready);
    w.destroy();
}

void TestMultiMonitor::recovery_persistent_failure_stays_bounded() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return false; }, [&] { return t; });

    // Enter the pending state first: while Ready, try_recovery is a no-op
    // success and must never touch the recreate step.
    w.note_device_loss("test", DXGI_ERROR_DEVICE_RESET);
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);

    // ~16.7 s of virtual time, one 60 Hz frame per step: with 250 ms -> 5 s
    // doubling backoff the attempt count stays a small bounded number,
    // nowhere near the frame count. Retryable forever, never a spin.
    for (int i = 0; i < 1000; ++i) {
        t += 16'666'667; // ~16.667 ms
        w.try_recovery();
    }
    QVERIFY(calls >= 3);
    QVERIFY(calls <= 10);
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);
    w.destroy();
}

void TestMultiMonitor::recovery_repeated_loss_and_recovery_cycles() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    bool succeed = false;
    w.set_recovery_hooks([&] { return succeed; }, [&] { return t; });

    for (int cycle = 0; cycle < 3; ++cycle) {
        w.note_device_loss("test", D2DERR_RECREATE_TARGET);
        QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);
        succeed = true;
        t += 250 * kNsPerMs;
        QVERIFY(w.try_recovery());
        QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::Ready);
        succeed = false;
    }
    w.destroy();
}

void TestMultiMonitor::recovery_real_forced_rebuild_restores_full_resources() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    w.note_device_loss("test", DXGI_ERROR_DEVICE_RESET);
    QVERIFY(!w.has_render_resources());

    // Forced synchronous rebuild with the REAL default step: the controlled
    // attempt recreates the whole chain and returns to Ready.
    QVERIFY(w.recreate_render_resources());
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::Ready);
    QVERIFY(w.has_render_resources());
    QVERIFY(w.hwnd() != nullptr);
    w.destroy();
}

void TestMultiMonitor::recovery_hresult_classification_separates_device_loss() {
    QVERIFY(ptd::OverlayWindow::is_device_loss_hresult(D2DERR_RECREATE_TARGET));
    QVERIFY(ptd::OverlayWindow::is_device_loss_hresult(DXGI_ERROR_DEVICE_REMOVED));
    QVERIFY(ptd::OverlayWindow::is_device_loss_hresult(DXGI_ERROR_DEVICE_RESET));
    // Non-device results must NOT request a full device-chain rebuild.
    QVERIFY(!ptd::OverlayWindow::is_device_loss_hresult(S_OK));
    QVERIFY(!ptd::OverlayWindow::is_device_loss_hresult(E_FAIL));
    QVERIFY(!ptd::OverlayWindow::is_device_loss_hresult(E_INVALIDARG));
    QVERIFY(!ptd::OverlayWindow::is_device_loss_hresult(DXGI_ERROR_INVALID_CALL));
}

void TestMultiMonitor::recovery_failed_attempt_exposes_no_stale_resources() {
    ptd::OverlayWindow w;
    if (!create_recovery_overlay(w)) {
        QSKIP("overlay HWND/D3D device unavailable in this session (environment)");
    }
    int64_t t = 0;
    int calls = 0;
    w.set_recovery_hooks([&] { ++calls; return false; }, [&] { return t; });

    w.note_device_loss("test", DXGI_ERROR_DEVICE_REMOVED);
    t += 250 * kNsPerMs;
    QVERIFY(!w.try_recovery());

    // The frame path must refuse to render (no partial/stale chain) and may
    // only touch the bounded policy gate.
    ptd::TrailEffect trail;
    ptd::ClickBubbleEffect click;
    ptd::TrailConfig tc{};
    ptd::CursorHistory history;
    const int64_t now = 123'456'789;
    ptd::FrameGeometry frame;
    frame.build(trail, history, tc, click, now);
    frame.append_segment(100.0f, 100.0f, 120.0f, 100.0f,
                         0.9f, 3.0f, ptd::TrailColorF{1.0f, 1.0f, 0.0f});
    const std::vector<ptd::FrameGeometry::OverlayInput> bounds{
        ptd::FrameGeometry::OverlayInput{RECT{0, 0, 1000, 1000}, 60}};
    const auto& partitions = frame.partition_for_overlays(bounds);
    w.render_frame(frame, partitions[0].primitive_indices, now);
    QCOMPARE(calls, 1);                    // exactly the gated attempt
    QCOMPARE(w.recovery_phase(), ptd::OverlayWindow::RecoveryPhase::RecoveryPending);
    w.destroy();
}

// T-021 Phase 11 (audit): sparkle primitives share the production
// OverlayTransform contract -- no DPI multiplication anywhere, virtual-
// screen physical pixels in, overlay-local physical pixels out, culling
// envelope = the conservative outer radius used by add_sparkle.
void TestMultiMonitor::sparkle_transform_and_culling() {
    // Left/top monitor with negative coordinates.
    RECT left_rect{0, 0, 1920, 1080};
    RECT top_rect{-1920, -1080, 0, 0};
    RECT right_rect{1920, 0, 3840, 1080};
    const auto left = ptd::OverlayTransform::from_bounds(left_rect);
    const auto top = ptd::OverlayTransform::from_bounds(top_rect);
    const auto right = ptd::OverlayTransform::from_bounds(right_rect);

    // Negative virtual coordinates transform without any scaling: a
    // sparkle at (-960, -540) is the exact overlay center of the top
    // monitor (no DPI multiplication, pure translation).
    QCOMPARE(top.to_local_x(-960.0f), 960.0f);
    QCOMPARE(top.to_local_y(-540.0f), 540.0f);
    QCOMPARE(left.to_local_x(100.0f), 100.0f);

    // Seam: a sparkle just inside the seam is visible on both overlays.
    QVERIFY(!left.cull_circle(1915.0f, 500.0f, 8.0f));
    QVERIFY(!right.cull_circle(1925.0f, 500.0f, 8.0f));
    // Culling: far outside each overlay.
    QVERIFY(left.cull_circle(3000.0f, 500.0f, 8.0f));
    QVERIFY(right.cull_circle(100.0f, 500.0f, 8.0f));
    QVERIFY(top.cull_circle(500.0f, 500.0f, 8.0f));
    // Edge-crossing sparkle: center outside but envelope inside -> drawn.
    QVERIFY(!right.cull_circle(3845.0f, 500.0f, 8.0f));
    QVERIFY(right.cull_circle(3855.0f, 500.0f, 8.0f));

    // Mixed-DPI invariant: the transform is per-monitor physical px, so
    // the same virtual point maps consistently regardless of DPI, exactly
    // like the trail segments (no per-sparkle special case exists).
    RECT lo_rect{0, 0, 3840, 2160};   // 150% scaled virtual geometry
    const auto lo = ptd::OverlayTransform::from_bounds(lo_rect);
    QCOMPARE(lo.to_local_x(1000.0f), 1000.0f);
    QCOMPARE(lo.to_local_y(1000.0f), 1000.0f);
}

// ---- PERF-001: one world-space build per frame + dirty overlays ----

namespace {

std::vector<ptd::MonitorInfo> perf001_row() {
    auto add = [](const wchar_t* name, const RECT& b) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = b;
        info.work_area = b;
        info.dpi_x = 96;
        info.dpi_y = 96;
        info.scale = 1.0f;
        return info;
    };
    return {
        add(L"\\\\.\\DISPLAY_A", RECT{-1920, 0, 0, 1080}),
        add(L"\\\\.\\DISPLAY_B", RECT{0, 0, 1920, 1080}),
        add(L"\\\\.\\DISPLAY_C", RECT{1920, 0, 3840, 1080}),
    };
}

void perf001_push_span(ptd::CursorHistory& history, int32_t x0, int32_t x1,
                       int32_t y, int64_t base_ns) {
    for (int32_t x = x0, i = 0; x <= x1; ++x, ++i) {
        ptd::CursorSample s{};
        s.timestamp_ns = base_ns + i * 1'000'000;  // 1 ms apart
        s.x = x;
        s.y = y;
        history.push(s);
    }
}

} // namespace

void TestMultiMonitor::perf001_single_effect_build_regardless_of_overlay_count() {
    // PERF-001: OverlayManager::render_frame must run the world-space effect
    // models exactly ONCE, no matter how many overlays are live. Before the
    // repair each overlay ran TrailEffect::build_geometry + Click draw, so the
    // build count scaled with monitor count.
    auto synthetic = perf001_row();
    synthetic[0].refresh_rate_hz = 60;
    synthetic[1].refresh_rate_hz = 120;
    synthetic[2].refresh_rate_hz = 240;
    ptd::OverlayManager manager;
    manager.set_enumeration_for_test([synthetic] { return synthetic; });
    manager.set_create_window_for_test(
        [](const ptd::MonitorInfo&, HINSTANCE, bool)
            -> std::unique_ptr<ptd::OverlayWindow> {
            return std::make_unique<ptd::OverlayWindow>();
        });
    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(manager.overlay_count(), std::size_t(3));

    const int64_t base = 1'000'000'000LL;
    const int64_t now = base + 500 * 1'000'000;

    ptd::TrailEffect trail;
    ptd::TrailConfig tc{};
    tc.enabled = true;
    tc.lifetime_ms = 3000.0f;  // hold the whole span inside the window
    trail.set_config(tc);
    ptd::ClickBubbleEffect click;
    ptd::ClickConfig cc{};

    auto render_span = [&](int32_t x0, int32_t x1, int expected_hz,
                           std::size_t expected_overlays) {
        ptd::CursorHistory history;
        if (x0 <= x1) perf001_push_span(history, x0, x1, 300, base);
        manager.render_frame(trail, history, tc, click, cc, now);
        QCOMPARE(manager.content_refresh_rate_hz(), expected_hz);
        QCOMPARE(manager.last_presenting_overlays(), expected_overlays);
    };

    render_span(-1500, -1400, 60, 1);  // content only on A
    render_span(100, 200, 120, 1);     // content only on B
    render_span(2500, 2600, 240, 1);   // content only on C
    render_span(1850, 1990, 240, 2);  // seam B/C selects the faster panel
    render_span(1, 0, 0, 0);           // empty frame has no content source
    // One world-frame build per application frame, independent of three
    // live overlays and sparse bucket membership.
    QCOMPARE(manager.frame_build_count(), 5ULL);

    ptd::OverlayWindow::clear_display_change_callback();
}

void TestMultiMonitor::perf001_dirty_overlay_presents_then_skips_when_clear() {
    ptd::OverlayDirtyState dirty;

    // Known clear, no intersection: nothing to do.
    QCOMPARE(dirty.decide(false), ptd::OverlayDirtyState::Decision::Skip);

    // Intersected: present content, overlay becomes dirty.
    QCOMPARE(dirty.decide(true), ptd::OverlayDirtyState::Decision::PresentContent);
    QVERIFY(!dirty.is_clear());

    // Content gone: exactly ONE transparent clear.
    QCOMPARE(dirty.decide(false), ptd::OverlayDirtyState::Decision::PresentClear);
    QVERIFY(dirty.is_clear());

    // Already clear and still empty: skip forever until intersected again.
    QCOMPARE(dirty.decide(false), ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(dirty.decide(false), ptd::OverlayDirtyState::Decision::Skip);

    // Re-intersected: content again.
    QCOMPARE(dirty.decide(true), ptd::OverlayDirtyState::Decision::PresentContent);

    // peek() does not mutate; commit() only after a real present.
    ptd::OverlayDirtyState fresh;  // starts clear
    QCOMPARE(fresh.peek(false), ptd::OverlayDirtyState::Decision::Skip);
    QVERIFY(fresh.is_clear());
    QCOMPARE(fresh.peek(true), ptd::OverlayDirtyState::Decision::PresentContent);
    QVERIFY(fresh.is_clear());
    fresh.commit(true);
    QVERIFY(!fresh.is_clear());
}

void TestMultiMonitor::perf002_sparse_overlay_buckets_preserve_seams_and_clear_once() {
    ptd::FrameGeometry frame;
    ptd::TrailColorF color{1.0f, 1.0f, 0.0f};
    using OverlayInput = ptd::FrameGeometry::OverlayInput;
    const std::vector<OverlayInput> three_monitors{
        OverlayInput{RECT{-1920, 0, 0, 1080}, 60},
        OverlayInput{RECT{0, 0, 1920, 1080}, 120},
        OverlayInput{RECT{1920, 0, 3840, 1080}, 240}};
    frame.append_segment(-1700.0f, 300.0f, -1500.0f, 300.0f,
                         0.9f, 3.0f, color);
    frame.append_segment(2800.0f, 300.0f, 3000.0f, 300.0f,
                         0.9f, 3.0f, color);
    frame.append_segment(-1200.0f, 400.0f, -1100.0f, 400.0f,
                         0.9f, 3.0f, color);
    const auto& sparse = frame.partition_for_overlays(three_monitors);
    QCOMPARE(sparse.size(), std::size_t(3));
    QCOMPARE(sparse[0].primitive_indices.size(), std::size_t(2));
    QCOMPARE(sparse[0].primitive_indices[0], std::size_t(0));
    QCOMPARE(sparse[0].primitive_indices[1], std::size_t(2));
    QVERIFY(sparse[1].primitive_indices.empty());
    QVERIFY(!sparse[1].intersects());
    QCOMPARE(sparse[2].primitive_indices.size(), std::size_t(1));
    QCOMPARE(sparse[2].primitive_indices[0], std::size_t(1));
    // OverlayWindow receives exactly these indices, so the empty middle
    // display has no unrelated primitive to visit.
    std::size_t middle_overlay_visits = 0;
    for (const std::size_t index : sparse[1].primitive_indices) {
        Q_UNUSED(index);
        ++middle_overlay_visits;
    }
    QCOMPARE(middle_overlay_visits, std::size_t(0));

    // Physical seam overlap assigns a crossing primitive to both neighbours.
    ptd::FrameGeometry seam;
    seam.append_segment(1850.0f, 500.0f, 1990.0f, 500.0f,
                        0.9f, 3.0f, color);
    const auto& seam_buckets = seam.partition_for_overlays(three_monitors);
    QVERIFY(seam_buckets[0].primitive_indices.empty());
    QCOMPARE(seam_buckets[1].primitive_indices.size(), std::size_t(1));
    QCOMPARE(seam_buckets[2].primitive_indices.size(), std::size_t(1));

    // One shared conservative-bound function covers stroke width and sparkle
    // extent, so both primitives survive partitioning at the physical seam.
    const std::vector<OverlayInput> seam_monitors{
        OverlayInput{RECT{-100, 0, 0, 100}, 60},
        OverlayInput{RECT{0, 0, 100, 100}, 240}};
    ptd::FrameGeometry margins;
    margins.append_segment(-8.0f, 40.0f, -4.0f, 40.0f,
                           0.9f, 3.0f, color);
    margins.append_sparkle(-4.0f, 60.0f, 8.0f, 0.0f,
                           0.9f, color, ptd::TrailSparkleShape::Cross);
    const auto& margin_buckets = margins.partition_for_overlays(seam_monitors);
    QCOMPARE(margin_buckets[0].primitive_indices.size(), std::size_t(2));
    QCOMPARE(margin_buckets[1].primitive_indices.size(), std::size_t(2));

    ptd::OverlayDirtyState a;
    ptd::OverlayDirtyState b;
    ptd::OverlayDirtyState c;
    QCOMPARE(a.decide(sparse[0].intersects()),
             ptd::OverlayDirtyState::Decision::PresentContent);
    QCOMPARE(b.decide(sparse[1].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(c.decide(sparse[2].intersects()),
             ptd::OverlayDirtyState::Decision::PresentContent);
    frame.clear();
    const auto& cleared = frame.partition_for_overlays(three_monitors);
    QCOMPARE(a.decide(cleared[0].intersects()),
             ptd::OverlayDirtyState::Decision::PresentClear);
    QCOMPARE(b.decide(cleared[1].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(c.decide(cleared[2].intersects()),
             ptd::OverlayDirtyState::Decision::PresentClear);
    QCOMPARE(a.decide(cleared[0].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(b.decide(cleared[1].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(c.decide(cleared[2].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
}

void TestMultiMonitor::perf002_thousands_primitives_use_single_pass_and_reuse_buckets() {
    constexpr std::size_t kPerSide = 3000;
    constexpr std::size_t kTotal = kPerSide * 2;
    using OverlayInput = ptd::FrameGeometry::OverlayInput;
    const std::vector<OverlayInput> monitors{
        OverlayInput{RECT{-1920, 0, 0, 1080}, 60},
        OverlayInput{RECT{0, 0, 1920, 1080}, 120},
        OverlayInput{RECT{1920, 0, 3840, 1080}, 240}};
    const ptd::TrailColorF color{0.2f, 0.8f, 1.0f};
    ptd::FrameGeometry frame;
    auto fill = [&]() {
        for (std::size_t i = 0; i < kPerSide; ++i) {
            const float y = 20.0f + static_cast<float>(i % 1000);
            const float left_x = -1800.0f + static_cast<float>(i % 100);
            const float right_x = 2800.0f + static_cast<float>(i % 100);
            frame.append_segment(left_x, y, left_x + 10.0f, y,
                                 0.8f, 2.0f, color);
            frame.append_segment(right_x, y, right_x + 10.0f, y,
                                 0.8f, 2.0f, color);
        }
    };
    fill();
    const auto& first = frame.partition_for_overlays(monitors);
    QCOMPARE(frame.primitive_partition_visits_for_tests(), kTotal);
    QCOMPARE(frame.overlay_candidate_checks_for_tests(), kTotal);
    QCOMPARE(first[0].primitive_indices.size(), kPerSide);
    QVERIFY(first[1].primitive_indices.empty());
    QCOMPARE(first[2].primitive_indices.size(), kPerSide);
    const std::size_t a_capacity = first[0].primitive_indices.capacity();
    const std::size_t b_capacity = first[1].primitive_indices.capacity();
    const std::size_t c_capacity = first[2].primitive_indices.capacity();

    frame.clear();
    fill();
    const auto& second = frame.partition_for_overlays(monitors);
    QCOMPARE(frame.primitive_partition_visits_for_tests(), kTotal);
    QCOMPARE(frame.overlay_candidate_checks_for_tests(), kTotal);
    QCOMPARE(second[0].primitive_indices.capacity(), a_capacity);
    QCOMPARE(second[1].primitive_indices.capacity(), b_capacity);
    QCOMPARE(second[2].primitive_indices.capacity(), c_capacity);
}

void TestMultiMonitor::perf001_global_content_death_clears_each_dirty_overlay_once() {
    // Global content death: an empty world-space frame must make every
    // previously-visible overlay present ONE clear and then stop presenting.
    ptd::FrameGeometry frame;
    const std::vector<ptd::FrameGeometry::OverlayInput> monitors{
        ptd::FrameGeometry::OverlayInput{RECT{-1920, 0, 0, 1080}, 60},
        ptd::FrameGeometry::OverlayInput{RECT{0, 0, 1920, 1080}, 240}};
    const auto& partitions = frame.partition_for_overlays(monitors);
    QVERIFY(!partitions[0].intersects());
    QVERIFY(!partitions[1].intersects());

    ptd::OverlayDirtyState a;
    ptd::OverlayDirtyState b;
    a.decide(true);   // both were visible last frame
    b.decide(true);
    QVERIFY(!a.is_clear());
    QVERIFY(!b.is_clear());

    // Frame with no content: each dirty overlay clears exactly once...
    QCOMPARE(a.decide(partitions[0].intersects()),
             ptd::OverlayDirtyState::Decision::PresentClear);
    QCOMPARE(b.decide(partitions[1].intersects()),
             ptd::OverlayDirtyState::Decision::PresentClear);
    QVERIFY(a.is_clear());
    QVERIFY(b.is_clear());

    // ...and from then on both skip: the scheduler can reach Idle without a
    // transparent-frame storm on unrelated monitors.
    QCOMPARE(a.decide(partitions[0].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
    QCOMPARE(b.decide(partitions[1].intersects()),
             ptd::OverlayDirtyState::Decision::Skip);
}

void TestMultiMonitor::perf001_scheduler_rate_follows_populated_buckets() {
    using OverlayInput = ptd::FrameGeometry::OverlayInput;
    const std::vector<OverlayInput> monitors{
        OverlayInput{RECT{0, 0, 1920, 1080}, 60},
        OverlayInput{RECT{1920, 0, 3840, 1080}, 240}};
    const ptd::TrailColorF color{1.0f, 1.0f, 1.0f};
    ptd::FrameGeometry frame;

    // Only the 60 Hz display contains current content.
    frame.append_segment(400.0f, 200.0f, 500.0f, 200.0f,
                         0.8f, 3.0f, color);
    const auto& low_only = frame.partition_for_overlays(monitors);
    QVERIFY(low_only[0].intersects());
    QVERIFY(!low_only[1].intersects());
    QCOMPARE(frame.content_refresh_rate_hz(), 60);
    QCOMPARE(ptd::RenderScheduler::resolve_target_fps(
                 frame.content_refresh_rate_hz(), 240), 60);

    // Content only on the 240 Hz display follows 240 Hz despite the lower-rate
    // neighbouring overlay.
    frame.clear();
    frame.append_segment(2400.0f, 200.0f, 2500.0f, 200.0f,
                         0.8f, 3.0f, color);
    const auto& high_only = frame.partition_for_overlays(monitors);
    QVERIFY(!high_only[0].intersects());
    QVERIFY(high_only[1].intersects());
    QCOMPARE(frame.content_refresh_rate_hz(), 240);
    QCOMPARE(ptd::RenderScheduler::resolve_target_fps(
                 frame.content_refresh_rate_hz(), 60), 240);

    // A physical seam crossing belongs to both buckets and uses the faster
    // of the displays that actually receive it.
    frame.clear();
    frame.append_segment(1900.0f, 500.0f, 1940.0f, 500.0f,
                         0.8f, 3.0f, color);
    const auto& seam = frame.partition_for_overlays(monitors);
    QVERIFY(seam[0].intersects());
    QVERIFY(seam[1].intersects());
    QCOMPARE(frame.content_refresh_rate_hz(), 240);

    // Empty frame has no content-derived display and uses the bounded live
    // monitor fallback. Refresh-only metadata updates flow through the same
    // bucket inputs without changing physical geometry.
    frame.clear();
    const auto& empty = frame.partition_for_overlays(monitors);
    QVERIFY(!empty[0].intersects());
    QVERIFY(!empty[1].intersects());
    QCOMPARE(frame.content_refresh_rate_hz(), 0);
    QCOMPARE(ptd::RenderScheduler::resolve_target_fps(
                 frame.content_refresh_rate_hz(), 240), 240);

    auto changed_refresh = monitors;
    changed_refresh[1].refresh_rate_hz = 144;
    frame.clear();
    frame.append_segment(2400.0f, 200.0f, 2500.0f, 200.0f,
                         0.8f, 3.0f, color);
    const auto& after_refresh_change =
        frame.partition_for_overlays(changed_refresh);
    QVERIFY(!after_refresh_change[0].intersects());
    QVERIFY(after_refresh_change[1].intersects());
    QCOMPARE(frame.content_refresh_rate_hz(), 144);
}

void TestMultiMonitor::perf001_refresh_only_metadata_updates_without_recreating_overlay() {
    int live_refresh_hz = 240;
    int create_calls = 0;
    ptd::MonitorInfo monitor{};
    monitor.bounds = make_rect(0, 0, 1920, 1080);
    monitor.work_area = monitor.bounds;
    monitor.is_primary = true;
    monitor.refresh_rate_hz = live_refresh_hz;

    ptd::OverlayManager manager;
    manager.set_enumeration_for_test([&] {
        monitor.refresh_rate_hz = live_refresh_hz;
        return std::vector<ptd::MonitorInfo>{monitor};
    });
    manager.set_create_window_for_test(
        [&](const ptd::MonitorInfo&, HINSTANCE, bool)
            -> std::unique_ptr<ptd::OverlayWindow> {
            ++create_calls;
            return std::make_unique<ptd::OverlayWindow>();
        });
    QVERIFY(manager.create(GetModuleHandleW(nullptr), false));
    QCOMPARE(create_calls, 1);
    QCOMPARE(manager.monitor_overlays().size(), std::size_t(1));
    ptd::OverlayWindow* const window_owner =
        manager.monitor_overlays()[0].window.get();

    // Same identity, bounds, work area, DPI and primary status; only refresh
    // metadata changes. The coalesced topology owner updates it in place.
    live_refresh_hz = 144;
    QCOMPARE(manager.refresh_topology_ex(),
             ptd::OverlayManager::Convergence::Unchanged);
    QCOMPARE(create_calls, 1);
    QCOMPARE(manager.monitor_overlays()[0].window.get(), window_owner);
    QCOMPARE(manager.monitor_overlays()[0].monitor.refresh_rate_hz, 144);
    QCOMPARE(manager.monitors()[0].refresh_rate_hz, 144);
    manager.destroy();
    ptd::OverlayWindow::clear_display_change_callback();
}

QTEST_MAIN(TestMultiMonitor)
#include "test_multi_monitor.moc"
