// T-021: Trail enable-lifecycle freshness (controller level).
//
// Defect: CursorHistory is SHARED with the input layer -- MouseInput pushes
// every movement/button sample into it BEFORE it notifies the controller, so
// the history kept filling while the CHILD Trail toggle was off. Only the
// master toggle cleared on transition. Re-enabling Trail therefore woke the
// scheduler with a full history of disabled-period movement, and it rendered
// that stale path as a fresh trail.
//
// Contract proven here, through the REAL production wiring (Application::run
// creates the canonical SettingsWindow/tray/overlay and connects the settings
// signal to the controller):
//   enabled  -> disabled : the shared history is empty immediately, and the
//                          visible trail cannot stay frozen.
//   disabled -> enabled  : the history is empty again, so only movement
//                          received AFTER the re-enable can render.
//   click bubbles        : unaffected by a Trail disable (independent effect).
//   master OFF/ON        : unchanged (still clears, still never resurrects).
//   trail.enabled        : still persisted on every transition.
//
// Assertions read the controller's real CursorHistory and run the PRODUCTION
// TrailEffect geometry over it, so "no trail is emitted" is proven on
// renderable output rather than on a bookkeeping flag alone.
//
// REAL-INPUT IMMUNITY (T-021 Phase 0, E-114): this suite runs on a live
// desktop where the physical mouse keeps feeding the REAL input layer, and
// QTest::qWait pumps those WM_INPUT samples into the shared history. An
// absolute "history is empty / nothing is live" assertion after a qWait
// therefore measures the user's hand, not the contract. Every SYNTHETIC
// sample in this suite is coordinate-tagged into a band a real cursor can
// never reach (y >= kTagY on a 0..1079 desktop), and the post-wait
// assertions scan the history and the EMITTED geometry for tagged endpoints
// instead of counting untagged content: scheduler ticks and genuine
// post-re-enable movement may render, resurrected disabled-period motion
// may not. Synchronous assertions (no qWait in between) stay exact: the
// scenario lambda is one event-loop turn, so no input can interleave.

#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QTimer>

#include "../src/app/application.h"
#include "../src/config/config_storage.h"
#include "../src/config/release_defaults.h"
#include "../src/core/cursor_history.h"
#include "../src/core/log.h"
#include "../src/core/cursor_sample.h"
#include "../src/effects/trail_effect.h"
#include "../src/platform/mouse_input.h"
#include "../src/ui/settings_window.h"

#include <filesystem>
#include <functional>
#include <string>

namespace {

constexpr int64_t kMs = 1'000'000; // ns per ms

// Synthetic-sample tag band: y >= kTagY can never come from a real cursor on
// this desktop (physical y is 0..1079), so a tagged endpoint identifies this
// test's synthetic motion inside the shared history and in emitted geometry.
constexpr float kTagY = 2000.0f;

ptd::CursorSample move_sample(int64_t ts, int x, int y) {
    ptd::CursorSample s;
    s.timestamp_ns = ts;
    s.x = x;
    s.y = y;
    return s;
}

ptd::CursorSample left_down_sample(int64_t ts, int x, int y) {
    ptd::CursorSample s = move_sample(ts, x, y);
    s.button = ptd::MouseButton::Left;
    s.action = ptd::ButtonAction::Down;
    return s;
}

// Records exactly what the production trail math would stroke for the
// controller's CURRENT history (alpha > 0 counts: an invisible segment is
// not a rendered trail). Segments split by the synthetic tag band: a tagged
// endpoint can only come from this test's samples, never from the real mouse.
class CountingTrailSink : public ptd::TrailGeometrySink {
public:
    int real_segments = 0;   // alpha > 0, both endpoints below the tag band
    int tagged_segments = 0; // alpha > 0 with an endpoint at/above the tag band

    void reserve_hint(int) override {}

    void add_segment(float, float y1, float, float y2, float alpha, float,
                     ptd::TrailColorF) override {
        if (alpha <= 0.0f) return;
        if (y1 >= kTagY || y2 >= kTagY) {
            ++tagged_segments;
        } else {
            ++real_segments;
        }
    }
};

struct RenderedTrail {
    int real_segments = 0;
    int tagged_segments = 0;
    int total() const { return real_segments + tagged_segments; }
};

// An isolated run has no config file, so the application starts from the
// built-in defaults; the same struct drives the geometry check.
RenderedTrail rendered_trail(const Application& app, int64_t now_ns) {
    ptd::TrailEffect effect{ptd::TrailConfig{}};
    CountingTrailSink sink;
    effect.build_geometry(app.trail_history(), now_ns, nullptr, 512, sink);
    return {sink.real_segments, sink.tagged_segments};
}

// Counts synthetic tagged samples currently present in the shared history.
int tagged_history_samples(const Application& app) {
    int tagged = 0;
    for (const ptd::CursorSample& s : app.trail_history().samples()) {
        if (static_cast<float>(s.y) >= kTagY) ++tagged;
    }
    return tagged;
}

ptd::ui::SettingsWindow* find_settings_window() {
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget* widget : widgets) {
        if (auto* settings = qobject_cast<ptd::ui::SettingsWindow*>(widget)) {
            return settings;
        }
    }
    return nullptr;
}

std::wstring isolated_config_path(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return (dir / L"config.json").native();
}

// Runs `scenario` INSIDE the production event loop. run() builds the canonical
// SettingsWindow/tray/overlay and wires the settings signals to the
// controller, so the scenario drives what actually ships. The exit guard makes
// the loop end even when a scenario assertion returns early, and a watchdog
// keeps a wedge from hanging the suite.
bool run_scenario(const std::wstring& config_path,
                  const std::function<void(Application&, ptd::ui::SettingsWindow*)>& scenario,
                  bool* scenario_ran) {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_trail_lifecycle_test_logs";
    std::error_code log_ec;
    std::filesystem::create_directories(log_dir, log_ec);
    if (!ptd::is_log_initialized()
        && !ptd::log_init((log_dir / "protrail.log").native())) {
        return false;
    }
    Application app(config_path);
    if (!app.initialize()) return false;

    QTimer::singleShot(0, QApplication::instance(), [&] {
        struct ExitGuard {
            Application& app;
            ~ExitGuard() { app.request_exit(); }
        } guard{app};
        if (auto* settings = find_settings_window()) {
            scenario(app, settings);
            if (scenario_ran) *scenario_ran = true;
        }
    });
    QTimer::singleShot(20000, QApplication::instance(), [&] { app.request_exit(); });

    const int rc = app.run();
    app.shutdown();
    return rc == 0;
}

} // namespace

class TestTrailLifecycle : public QObject {
    Q_OBJECT
private slots:
    void disabled_period_movement_never_renders();
    void click_bubbles_survive_trail_disable();
    void master_toggle_behavior_unchanged();
    void trail_enabled_persists_across_toggle();
    // CORE-003: Restore All must obey the same Trail history hygiene as the
    // direct checkbox when it re-enables Trail.
    void restore_all_reenables_trail_with_history_hygiene();
};

// Requirements 1-6, 9-of-the-toggle: populate while enabled, disable through
// the real settings checkbox, keep moving while disabled, re-enable, and prove
// nothing from the disabled period can render.
void TestTrailLifecycle::disabled_period_movement_never_renders() {
    const std::wstring config = isolated_config_path(L"protrail_t021_main");

    struct Obs {
        bool settings_found = false;
        bool trail_enabled_at_start = false;
        int history_while_enabled = 0;
        bool trail_live_while_enabled = false;
        int segments_while_enabled = 0;
        int history_after_disable = 0;
        bool trail_live_after_disable = false;
        int segments_after_disable = 0;
        bool click_enabled_after_disable = false;
        bool master_enabled_after_disable = false;
        int history_while_disabled = 0;
        int history_at_reenable = 0;
        bool trail_live_at_reenable = false;
        int segments_at_reenable = 0;
        int tagged_history_after_idle_spin = 0;
        int tagged_after_idle_spin = 0;
        int history_after_post_enable_move = 0;
        int tagged_history_after_post_enable_move = 0;
        bool trail_live_after_post_enable_move = false;
        int segments_after_post_enable_move = 0;
    } obs;
    bool ran = false;

    const bool loop_ok = run_scenario(
        config,
        [&](Application& app, ptd::ui::SettingsWindow* settings) {
            obs.settings_found = true;
            auto* chk_trail = settings->findChild<QCheckBox*>("chk_trail");
            QVERIFY(chk_trail != nullptr);
            obs.trail_enabled_at_start = chk_trail->isChecked();
            QVERIFY(chk_trail->isChecked());

            // 1. Trail active + history populated. Every sample is timestamped
            //    in the past so the lifetime window contains all of them, and
            //    tagged so later scans can tell them from real input.
            const int64_t now = ptd::now_ns();
            const int64_t base = now - 180 * kMs;
            for (int i = 0; i < 12; ++i) {
                app.record_cursor_sample(
                    move_sample(base + i * 15 * kMs, 100 + i * 12, 2000 + i * 8));
            }
            const auto live = app.lifecycle_snapshot(ptd::now_ns());
            obs.history_while_enabled = static_cast<int>(live.trail_history_size);
            obs.trail_live_while_enabled = live.trail_content_live;
            obs.segments_while_enabled =
                rendered_trail(app, ptd::now_ns()).tagged_segments;

            // 2. Disable Trail through the REAL settings path.
            chk_trail->setChecked(false);

            // 3. The shared history is empty immediately; the trail can neither
            //    render nor stay frozen, and no other effect was collateral.
            const auto disabled = app.lifecycle_snapshot(ptd::now_ns());
            obs.history_after_disable = static_cast<int>(disabled.trail_history_size);
            obs.trail_live_after_disable = disabled.trail_content_live;
            obs.segments_after_disable = rendered_trail(app, ptd::now_ns()).total();
            obs.click_enabled_after_disable = disabled.click_enabled;
            obs.master_enabled_after_disable = disabled.master_enabled;

            // 4. Movement while Trail stays disabled. The architecture keeps
            //    recording (MouseInput is not unregistered), so these samples
            //    DO land in the history -- and must be gone at re-enable.
            for (int i = 0; i < 6; ++i) {
                app.record_cursor_sample(
                    move_sample(ptd::now_ns(), 600 + i * 10, 5000 + i * 10));
            }
            obs.history_while_disabled = static_cast<int>(
                app.lifecycle_snapshot(ptd::now_ns()).trail_history_size);

            // 5. Re-enable Trail.
            chk_trail->setChecked(true);

            // 6. Empty at re-enable: nothing recorded while disabled survived.
            const auto reenabled = app.lifecycle_snapshot(ptd::now_ns());
            obs.history_at_reenable = static_cast<int>(reenabled.trail_history_size);
            obs.trail_live_at_reenable = reenabled.trail_content_live;
            obs.segments_at_reenable = rendered_trail(app, ptd::now_ns()).total();

            // ... and an idle spin (real scheduler ticks, plus any REAL input
            // the user's mouse made during the wait) cannot resurrect any of
            // the synthetic motion: no tagged sample may exist in the history
            // and no tagged endpoint may appear in emitted geometry. Genuine
            // post-re-enable movement rendering is allowed and ignored here.
            QTest::qWait(40);
            obs.tagged_history_after_idle_spin = tagged_history_samples(app);
            obs.tagged_after_idle_spin =
                rendered_trail(app, ptd::now_ns()).tagged_segments;

            // 7. A new POST-enable movement is the only thing that may render.
            //    The total history may ALSO hold real samples the user's mouse
            //    produced during the idle spin above, so the synthetic share is
            //    counted through the tag band (exactly 8), not through size.
            for (int i = 0; i < 8; ++i) {
                app.record_cursor_sample(
                    move_sample(ptd::now_ns(), 300 + i * 14, 8000 + i * 6));
            }
            const auto moved = app.lifecycle_snapshot(ptd::now_ns());
            obs.history_after_post_enable_move =
                static_cast<int>(moved.trail_history_size);
            obs.tagged_history_after_post_enable_move =
                tagged_history_samples(app);
            obs.trail_live_after_post_enable_move = moved.trail_content_live;
            obs.segments_after_post_enable_move =
                rendered_trail(app, ptd::now_ns()).tagged_segments;
        },
        &ran);

    QVERIFY(loop_ok);
    QVERIFY(ran);
    QVERIFY(obs.settings_found);

    // 1. Baseline: a live, renderable trail existed.
    QVERIFY(obs.trail_enabled_at_start);
    QCOMPARE(obs.history_while_enabled, 12);
    QVERIFY(obs.trail_live_while_enabled);
    QVERIFY(obs.segments_while_enabled > 0);

    // 2/3. Disable clears immediately -- no trail content, no geometry.
    QCOMPARE(obs.history_after_disable, 0);
    QVERIFY(!obs.trail_live_after_disable);
    QCOMPARE(obs.segments_after_disable, 0);
    QVERIFY(obs.click_enabled_after_disable);   // Trail toggle touches no other effect
    QVERIFY(obs.master_enabled_after_disable);  // ... and never the master toggle

    // 4. The disabled period really did collect movement (the recording
    //    architecture is untouched by the trail toggle).
    QCOMPARE(obs.history_while_disabled, 6);

    // 5/6. Re-enable starts from an empty Trail history: none of those six
    //      samples, and none of the twelve before them, can render -- not
    //      immediately, and not after real scheduler ticks / real input.
    QCOMPARE(obs.history_at_reenable, 0);
    QVERIFY(!obs.trail_live_at_reenable);
    QCOMPARE(obs.segments_at_reenable, 0);
    QCOMPARE(obs.tagged_history_after_idle_spin, 0);
    QCOMPARE(obs.tagged_after_idle_spin, 0);

    // 7. Only movement received after the re-enable renders again: all eight
    //    tagged synthetic samples are present (real idle-spin samples may add
    //    untagged size) and they, not the disabled-period motion, are what
    //    emits geometry.
    QVERIFY(obs.history_after_post_enable_move >= 8);
    QCOMPARE(obs.tagged_history_after_post_enable_move, 8);
    QVERIFY(obs.trail_live_after_post_enable_move);
    QVERIFY(obs.segments_after_post_enable_move > 0);
}

// Requirement 7: click bubbles are independent of the Trail toggle.
void TestTrailLifecycle::click_bubbles_survive_trail_disable() {
    const std::wstring config = isolated_config_path(L"protrail_t021_click");

    struct Obs {
        bool bubble_live_before_disable = false;
        bool bubble_live_after_trail_disable = false;
        bool bubble_live_after_idle_spin = false;
        bool trail_live_after_trail_disable = false;
        int history_after_trail_disable = 0;
        bool click_enabled_after_trail_disable = false;
    } obs;
    bool ran = false;

    const bool loop_ok = run_scenario(
        config,
        [&](Application& app, ptd::ui::SettingsWindow* settings) {
            auto* chk_trail = settings->findChild<QCheckBox*>("chk_trail");
            QVERIFY(chk_trail != nullptr);

            // A left-button Down sample spawns one bubble (default triggers).
            app.record_cursor_sample(left_down_sample(ptd::now_ns(), 400, 300));
            obs.bubble_live_before_disable =
                app.lifecycle_snapshot(ptd::now_ns()).click_content_live;

            chk_trail->setChecked(false);

            const auto disabled = app.lifecycle_snapshot(ptd::now_ns());
            obs.bubble_live_after_trail_disable = disabled.click_content_live;
            obs.trail_live_after_trail_disable = disabled.trail_content_live;
            obs.history_after_trail_disable =
                static_cast<int>(disabled.trail_history_size);
            obs.click_enabled_after_trail_disable = disabled.click_enabled;

            // The bubble keeps animating through real scheduler ticks even
            // though the trail is off (the same content gate drives both).
            QTest::qWait(30);
            obs.bubble_live_after_idle_spin =
                app.lifecycle_snapshot(ptd::now_ns()).click_content_live;
        },
        &ran);

    QVERIFY(loop_ok);
    QVERIFY(ran);
    QVERIFY(obs.bubble_live_before_disable);
    QVERIFY(obs.bubble_live_after_trail_disable);
    QVERIFY(!obs.trail_live_after_trail_disable);
    QCOMPARE(obs.history_after_trail_disable, 0);
    QVERIFY(obs.click_enabled_after_trail_disable);
    QVERIFY(obs.bubble_live_after_idle_spin);
}

// Requirement 8: master OFF/ON keeps its own documented behavior.
void TestTrailLifecycle::master_toggle_behavior_unchanged() {
    const std::wstring config = isolated_config_path(L"protrail_t021_master");

    struct Obs {
        int history_before_master_off = 0;
        bool master_after_off = true;
        int history_after_master_off = 0;
        int history_while_master_off = 0;
        bool trail_live_while_master_off = true;
        bool master_after_on = false;
        int history_after_master_on = 0;
        bool trail_live_after_master_on = true;
        bool trail_enabled_after_master_cycle = false;
        bool trail_live_after_new_movement = false;
    } obs;
    bool ran = false;

    const bool loop_ok = run_scenario(
        config,
        [&](Application& app, ptd::ui::SettingsWindow* settings) {
            auto* chk_master = settings->findChild<QCheckBox*>("chk_master");
            QVERIFY(chk_master != nullptr);
            QVERIFY(chk_master->isChecked());

            const int64_t now = ptd::now_ns();
            for (int i = 0; i < 8; ++i) {
                app.record_cursor_sample(
                    move_sample(now - (120 - i * 15) * kMs, 200 + i * 12, 200 + i * 8));
            }
            obs.history_before_master_off = static_cast<int>(
                app.lifecycle_snapshot(ptd::now_ns()).trail_history_size);

            chk_master->setChecked(false);
            const auto off = app.lifecycle_snapshot(ptd::now_ns());
            obs.master_after_off = off.master_enabled;
            obs.history_after_master_off = static_cast<int>(off.trail_history_size);

            // Input keeps being recorded while the master toggle is off; the
            // master gate must still make it unrenderable.
            app.record_cursor_sample(move_sample(ptd::now_ns(), 700, 700));
            const auto off_move = app.lifecycle_snapshot(ptd::now_ns());
            obs.history_while_master_off = static_cast<int>(off_move.trail_history_size);
            obs.trail_live_while_master_off = off_move.trail_content_live;

            chk_master->setChecked(true);
            const auto on = app.lifecycle_snapshot(ptd::now_ns());
            obs.master_after_on = on.master_enabled;
            obs.history_after_master_on = static_cast<int>(on.trail_history_size);
            obs.trail_live_after_master_on = on.trail_content_live;
            obs.trail_enabled_after_master_cycle = on.trail_enabled;

            app.record_cursor_sample(move_sample(ptd::now_ns(), 800, 620));
            app.record_cursor_sample(move_sample(ptd::now_ns(), 820, 640));
            obs.trail_live_after_new_movement =
                app.lifecycle_snapshot(ptd::now_ns()).trail_content_live;
        },
        &ran);

    QVERIFY(loop_ok);
    QVERIFY(ran);

    // The master toggle's established behavior: OFF clears and refuses new
    // rendering, ON starts from an empty history, and an OFF/ON cycle never
    // resurrects disabled-period movement.
    QCOMPARE(obs.history_before_master_off, 8);
    QVERIFY(!obs.master_after_off);
    QCOMPARE(obs.history_after_master_off, 0);
    QCOMPARE(obs.history_while_master_off, 1);   // still recorded by the input layer
    QVERIFY(!obs.trail_live_while_master_off);   // ... but the master gate refuses it
    QVERIFY(obs.master_after_on);
    QCOMPARE(obs.history_after_master_on, 0);
    QVERIFY(!obs.trail_live_after_master_on);
    QVERIFY(obs.trail_enabled_after_master_cycle);  // the child toggle is untouched
    QVERIFY(obs.trail_live_after_new_movement);
}

// Requirement 9 (the T-020 store's file-digest check also depends on this):
// the child toggle still persists trail.enabled, once per transition.
void TestTrailLifecycle::trail_enabled_persists_across_toggle() {
    const std::wstring config = isolated_config_path(L"protrail_t021_persist");

    ptd::AppConfig initial;
    QVERIFY(ptd::ConfigStorage::save_to_file(initial, config));
    QVERIFY(ptd::ConfigStorage::load_from_file(config).trail.enabled);

    struct Obs {
        bool disabled_persisted = false;
        bool controller_reports_disabled = false;
        bool enabled_persisted = false;
        bool controller_reports_enabled = false;
        bool click_untouched = false;
        bool master_untouched = false;
    } obs;
    bool ran = false;

    const bool loop_ok = run_scenario(
        config,
        [&](Application& app, ptd::ui::SettingsWindow* settings) {
            auto* chk_trail = settings->findChild<QCheckBox*>("chk_trail");
            QVERIFY(chk_trail != nullptr);

            chk_trail->setChecked(false);
            obs.disabled_persisted = !ptd::ConfigStorage::load_from_file(config).trail.enabled;
            obs.controller_reports_disabled = !app.trail_enabled();

            chk_trail->setChecked(true);
            const ptd::AppConfig reloaded = ptd::ConfigStorage::load_from_file(config);
            obs.enabled_persisted = reloaded.trail.enabled;
            obs.controller_reports_enabled = app.trail_enabled();
            obs.click_untouched = reloaded.click.enabled;
            obs.master_untouched = reloaded.master_enabled;
        },
        &ran);

    QVERIFY(loop_ok);
    QVERIFY(ran);
    QVERIFY(obs.disabled_persisted);
    QVERIFY(obs.controller_reports_disabled);
    QVERIFY(obs.enabled_persisted);
    QVERIFY(obs.controller_reports_enabled);
    QVERIFY(obs.click_untouched);
    QVERIFY(obs.master_untouched);

    const auto final_cfg = ptd::ConfigStorage::load_from_file(config);
    QVERIFY(final_cfg.trail.enabled);
    QVERIFY(final_cfg.click.enabled);
    QVERIFY(final_cfg.master_enabled);

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(config).parent_path(), ec);
}

// CORE-003: Restore All Defaults re-enables Trail through the bulk-config
// transaction. The disabled-period movement must be cleared by the SAME
// history hygiene the direct checkbox uses -- the enable edge must never
// resurrect samples collected while Trail was off.
void TestTrailLifecycle::restore_all_reenables_trail_with_history_hygiene() {
    const std::wstring config = isolated_config_path(L"protrail_core003_restore");

    struct Obs {
        int history_after_disable = 0;
        int tagged_while_disabled = 0;
        bool trail_enabled_after_restore = false;
        int history_at_restore = 0;
        bool trail_live_at_restore = true;
        int tagged_after_restore = 0;
        int tagged_after_idle_spin = 0;
    } obs;
    bool ran = false;

    const bool loop_ok = run_scenario(
        config,
        [&](Application& app, ptd::ui::SettingsWindow* settings) {
            auto* chk_trail = settings->findChild<QCheckBox*>("chk_trail");
            QVERIFY(chk_trail != nullptr);
            QVERIFY(chk_trail->isChecked());

            // 1. Populate while enabled, then disable Trail via the checkbox.
            const int64_t now = ptd::now_ns();
            for (int i = 0; i < 8; ++i) {
                app.record_cursor_sample(
                    move_sample(now - (120 - i * 15) * kMs, 100 + i * 10, 2000 + i * 8));
            }
            chk_trail->setChecked(false);
            obs.history_after_disable =
                static_cast<int>(app.lifecycle_snapshot(ptd::now_ns()).trail_history_size);

            // 2. Movement while disabled: it lands in the shared history.
            for (int i = 0; i < 6; ++i) {
                app.record_cursor_sample(
                    move_sample(ptd::now_ns(), 600 + i * 10, 5000 + i * 10));
            }
            obs.tagged_while_disabled = tagged_history_samples(app);
            QVERIFY(obs.tagged_while_disabled == 6);

            // 3. Restore All Defaults: canonical defaults have Trail enabled,
            //    so this is a disabled->enabled edge through the bulk path.
            settings->apply_config(ptd::release_defaults());

            const auto restored = app.lifecycle_snapshot(ptd::now_ns());
            obs.trail_enabled_after_restore = restored.trail_enabled;
            obs.history_at_restore = static_cast<int>(restored.trail_history_size);
            obs.trail_live_at_restore = restored.trail_content_live;
            obs.tagged_after_restore = tagged_history_samples(app);

            QTest::qWait(30);
            obs.tagged_after_idle_spin = tagged_history_samples(app);
        },
        &ran);

    QVERIFY(loop_ok);
    QVERIFY(ran);

    QCOMPARE(obs.history_after_disable, 0);
    QCOMPARE(obs.tagged_while_disabled, 6);
    QVERIFY(obs.trail_enabled_after_restore);
    // The disabled-period samples are gone immediately at the re-enable.
    QCOMPARE(obs.history_at_restore, 0);
    QVERIFY(!obs.trail_live_at_restore);
    QCOMPARE(obs.tagged_after_restore, 0);
    QCOMPARE(obs.tagged_after_idle_spin, 0);

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(config).parent_path(), ec);
}
QTEST_MAIN(TestTrailLifecycle)
#include "test_trail_lifecycle.moc"
