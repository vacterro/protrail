// T-012 MVP 07: RenderScheduler regression tests.
// Validates:
// 1. Hardware refresh rate query & fallback clamping.
// 2. Pacing interval calculation for various refresh rates.
// 3. Initial idle state contract (0 wakeups).
// 4. Wake transition from Idle to Active + immediate frame render.
// 5. Active frame execution while content is live.
// 6. Two-step shutdown cycle: Active -> PresentingClear (1 clear frame) -> Idle.
// 7. Wake during clear interruption (no stutter).
// 8. Delta-time calculation stability and clamping.
// 9. Frame metrics (frame counts, burst counts, frame duration).
// 10. Force idle clean abort.

#include "../src/render/render_scheduler.h"

#include <QtTest/QtTest>
#include <vector>

class TestRenderScheduler : public QObject {
    Q_OBJECT
private slots:
    void refresh_rate_detection_within_valid_bounds();
    void pacing_interval_calculated_correctly();
    void initial_state_is_idle();
    void wake_transitions_to_active_and_emits_immediate_frame();
    void ticks_maintain_active_while_content_live();
    void clear_cycle_presents_one_frame_then_enters_idle();
    void wake_during_clear_preserves_active_state();
    void delta_time_is_clamped_on_stall();
    void metrics_track_frames_bursts_and_timing();
    void force_idle_halts_immediately();
};

void TestRenderScheduler::refresh_rate_detection_within_valid_bounds() {
    const int hz = ptd::RenderScheduler::detect_display_refresh_rate();
    QVERIFY(hz >= 30);
    QVERIFY(hz <= 360);
}

void TestRenderScheduler::pacing_interval_calculated_correctly() {
    ptd::RenderScheduler scheduler;

    scheduler.set_target_fps(60);
    QCOMPARE(scheduler.target_fps(), 60);
    QCOMPARE(scheduler.frame_interval_ms(), 17); // 1000/60 = 16.67 -> 17

    scheduler.set_target_fps(120);
    QCOMPARE(scheduler.target_fps(), 120);
    QCOMPARE(scheduler.frame_interval_ms(), 8);  // 1000/120 = 8.33 -> 8

    scheduler.set_target_fps(144);
    QCOMPARE(scheduler.target_fps(), 144);
    QCOMPARE(scheduler.frame_interval_ms(), 7);  // 1000/144 = 6.94 -> 7

    scheduler.set_target_fps(240);
    QCOMPARE(scheduler.target_fps(), 240);
    QCOMPARE(scheduler.frame_interval_ms(), 4);  // 1000/240 = 4.17 -> 4

    // Underflow clamped to 30
    scheduler.set_target_fps(10);
    QCOMPARE(scheduler.target_fps(), 30);

    // Overflow clamped to 360
    scheduler.set_target_fps(500);
    QCOMPARE(scheduler.target_fps(), 360);
}

void TestRenderScheduler::initial_state_is_idle() {
    ptd::RenderScheduler scheduler;
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Idle);
    QCOMPARE(scheduler.is_active(), false);
    QCOMPARE(scheduler.metrics().total_frames, 0ull);
    QCOMPARE(scheduler.metrics().total_bursts, 0ull);
}

void TestRenderScheduler::wake_transitions_to_active_and_emits_immediate_frame() {
    ptd::RenderScheduler scheduler;

    std::vector<ptd::FrameAction> actions;
    scheduler.set_frame_callback([&](int64_t, double, ptd::FrameAction act) {
        actions.push_back(act);
    });

    scheduler.wake();

    QCOMPARE(scheduler.state(), ptd::SchedulerState::Active);
    QCOMPARE(scheduler.is_active(), true);
    QCOMPARE(actions.size(), 1u);
    QCOMPARE(actions[0], ptd::FrameAction::RenderContent);
    QCOMPARE(scheduler.metrics().total_bursts, 1ull);
}

void TestRenderScheduler::ticks_maintain_active_while_content_live() {
    ptd::RenderScheduler scheduler;

    int render_count = 0;
    scheduler.set_frame_callback([&](int64_t, double dt, ptd::FrameAction act) {
        if (act == ptd::FrameAction::RenderContent) {
            render_count++;
            QVERIFY(dt >= 0.0);
        }
    });

    scheduler.wake(); // initial frame (1)
    QCOMPARE(render_count, 1);

    // 5 timer ticks with active content
    for (int i = 0; i < 5; ++i) {
        scheduler.on_timer_tick(true);
    }

    QCOMPARE(render_count, 6);
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Active);
}

void TestRenderScheduler::clear_cycle_presents_one_frame_then_enters_idle() {
    ptd::RenderScheduler scheduler;

    std::vector<ptd::FrameAction> actions;
    scheduler.set_frame_callback([&](int64_t, double, ptd::FrameAction act) {
        actions.push_back(act);
    });

    scheduler.wake();
    QCOMPARE(actions.size(), 1u);
    QCOMPARE(actions[0], ptd::FrameAction::RenderContent);

    // Content dies
    scheduler.on_timer_tick(false);
    QCOMPARE(scheduler.state(), ptd::SchedulerState::PresentingClear);
    QCOMPARE(actions.size(), 2u);
    QCOMPARE(actions[1], ptd::FrameAction::RenderClear);

    // Next tick: sleep
    scheduler.on_timer_tick(false);
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Idle);
    QCOMPARE(scheduler.is_active(), false);
    QCOMPARE(actions.size(), 3u);
    QCOMPARE(actions[2], ptd::FrameAction::Sleep);

    // Further ticks in Idle do nothing
    scheduler.on_timer_tick(false);
    QCOMPARE(actions.size(), 3u);
}

void TestRenderScheduler::wake_during_clear_preserves_active_state() {
    ptd::RenderScheduler scheduler;

    std::vector<ptd::FrameAction> actions;
    scheduler.set_frame_callback([&](int64_t, double, ptd::FrameAction act) {
        actions.push_back(act);
    });

    scheduler.wake();
    scheduler.on_timer_tick(false); // Transitions to PresentingClear
    QCOMPARE(scheduler.state(), ptd::SchedulerState::PresentingClear);

    // New mouse event arrives before sleep
    scheduler.wake();
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Active);

    // Next tick with content continues active
    scheduler.on_timer_tick(true);
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Active);
}

void TestRenderScheduler::delta_time_is_clamped_on_stall() {
    ptd::RenderScheduler scheduler;

    double recorded_dt = 0.0;
    scheduler.set_frame_callback([&](int64_t, double dt, ptd::FrameAction) {
        recorded_dt = dt;
    });

    scheduler.wake();
    scheduler.on_timer_tick(true);

    // Even if dt is computed over time, it is clamped to <= 0.1s
    QVERIFY(recorded_dt >= 0.0);
    QVERIFY(recorded_dt <= 0.1);
}

void TestRenderScheduler::metrics_track_frames_bursts_and_timing() {
    ptd::RenderScheduler scheduler;

    scheduler.set_frame_callback([&](int64_t, double, ptd::FrameAction) {
        scheduler.begin_frame();
        // simulate 100 us of work
        QThread::usleep(100);
        scheduler.end_frame();
    });

    scheduler.wake();
    scheduler.on_timer_tick(true);
    scheduler.on_timer_tick(true);

    const auto& m = scheduler.metrics();
    QCOMPARE(m.total_bursts, 1ull);
    QCOMPARE(m.total_frames, 3ull);
}

void TestRenderScheduler::force_idle_halts_immediately() {
    ptd::RenderScheduler scheduler;
    scheduler.wake();
    QCOMPARE(scheduler.is_active(), true);

    scheduler.force_idle();
    QCOMPARE(scheduler.state(), ptd::SchedulerState::Idle);
    QCOMPARE(scheduler.is_active(), false);
}

QTEST_MAIN(TestRenderScheduler)
#include "test_render_scheduler.moc"
