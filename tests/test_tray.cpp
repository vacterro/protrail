#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QPushButton>
#include <QMenu>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include "../src/app/application.h"
#include "../src/app/startup_mode.h"
#include "../src/app/topology_retry.h"
#include "../src/config/app_config.h"
#include "../src/config/config_storage.h"
#include "../src/config/release_defaults.h"
#include "../src/core/log.h"
#include "../src/config/dev_defaults.h"
#include "../src/platform/autostart.h"
#include "../src/ui/branding.h"
#include "../src/ui/settings_window.h"
#include "../src/ui/tray_icon.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

class TestTray : public QObject {
    Q_OBJECT
private slots:
    void application_initialize_requires_startup_log_bootstrap();
    void probe_error_blocks_application_save_and_shutdown();
    void protected_provenance_blocks_autostart_and_ui_changes();
    void protected_schema_sources_cannot_be_rewritten_by_application();
    void authoritative_config_reconciles_autostart_both_directions();
    void autostart_persistence_failure_blocks_run_key_mutation();
    void application_window_capture_retains_loaded_render_config();
    void topology_retry_converges_autonomously();
    void topology_retry_stays_bounded_on_persistent_failure();
    void topology_retry_cancel_stops_pending_work();
    void application_startup_incomplete_topology_retries_autonomously();
    void application_changed_monitor_retries_without_second_event();
    void application_shutdown_cancels_topology_retry();
    void tray_has_one_open_action();
    void product_icon_resource_matches_build_input();
    void tray_states_are_distinguishable_at_tray_sizes();
    void disabled_treatment_desaturates_and_dims();
    void manual_startup_shows_one_general_window();
    void autostart_is_tray_only();
    void close_hides_and_open_reuses_window();
    void restore_defaults_is_one_application_transaction();
    void shutdown_persistence_is_bounded_and_coalesced();
};

namespace {

int product_window_count() {
    int count = 0;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->windowTitle() == QStringLiteral("ProTrail")) ++count;
    }
    return count;
}

template <typename Body>
void with_running_application(const char* tag, ptd::StartupMode mode, Body&& body) {
    const auto dir = std::filesystem::temp_directory_path() /
                     (std::string("protrail_unified_") + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_unified_test_logs";
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    Application app((dir / "config.json").native());
    // Destroyed before `app` (reverse declaration order), so any still-armed
    // fallback watchdog single-shot is auto-cancelled and can never fire on a
    // destroyed Application in a later test sharing this process.
    QObject exit_guard;
    app.set_startup_mode(mode);
    QVERIFY(app.initialize());
    QTimer::singleShot(20, &exit_guard, [&app, &body] {
        body(app);
        app.request_exit();
    });
    QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
    QCOMPARE(app.run(), 0);
    app.shutdown();
    std::filesystem::remove_all(dir, ec);
}

struct RetryTopologyFixture {
    const std::wstring left_name = L"\\\\.\\RETRYDISPLAY-A";
    const std::wstring right_name = L"\\\\.\\RETRYDISPLAY-B";
    std::vector<ptd::MonitorInfo> monitors;
    bool fail_right_first = true;
    // W2-001 changed-monitor case: fail the NEXT right creation exactly once
    // (armed after the initial healthy topology, cleared on the failing call).
    bool fail_right_next_once = false;
    int left_attempts = 0;
    int right_attempts = 0;
    ptd::OverlayWindow* healthy_left = nullptr;
};

std::unique_ptr<ptd::OverlayManager> make_retry_topology(RetryTopologyFixture& fixture) {
    const auto add = [&](const std::wstring& name, long left, long right, bool primary) {
        ptd::MonitorInfo info{};
        info.device_name = name;
        info.bounds = RECT{left, 0, right, 100};
        info.work_area = info.bounds;
        info.is_primary = primary;
        info.dpi_x = 96;
        info.dpi_y = 96;
        info.scale = 1.0f;
        fixture.monitors.push_back(std::move(info));
    };
    add(fixture.left_name, 0, 100, true);
    add(fixture.right_name, 100, 200, false);

    auto manager = std::make_unique<ptd::OverlayManager>();
    manager->set_enumeration_for_test([&fixture] { return fixture.monitors; });
    manager->set_window_dpi_for_test([](HWND, UINT& x, UINT& y) {
        x = 96;
        y = 96;
        return true;
    });
    manager->set_create_window_for_test(
        [&fixture](const ptd::MonitorInfo& monitor, HINSTANCE, bool)
            -> std::unique_ptr<ptd::OverlayWindow> {
            if (monitor.device_name == fixture.left_name) {
                ++fixture.left_attempts;
                auto window = std::make_unique<ptd::OverlayWindow>();
                if (!fixture.healthy_left) fixture.healthy_left = window.get();
                return window;
            }
            if (monitor.device_name == fixture.right_name) {
                ++fixture.right_attempts;
                if (fixture.fail_right_first && fixture.right_attempts == 1) return nullptr;
                if (fixture.fail_right_next_once) {
                    fixture.fail_right_next_once = false;
                    return nullptr;
                }
                return std::make_unique<ptd::OverlayWindow>();
            }
            return nullptr;
        });
    return manager;
}

} // namespace

void TestTray::topology_retry_converges_autonomously() {
    int refresh_calls = 0;
    std::unique_ptr<ptd::TopologyRetry> retry;
    retry = std::make_unique<ptd::TopologyRetry>([&] {
        ++refresh_calls;
        retry->observe(ptd::OverlayManager::Convergence::Complete);
    }, ptd::TopologyRetry::Policy{5, 1, 1});

    retry->observe(ptd::OverlayManager::Convergence::Incomplete);
    QVERIFY(retry->pending());
    QTRY_COMPARE_WITH_TIMEOUT(refresh_calls, 1, 2000);
    QTest::qWait(20);
    QCOMPARE(refresh_calls, 1);
    QVERIFY(!retry->pending());
    QCOMPARE(retry->attempts(), 0);
}

void TestTray::topology_retry_stays_bounded_on_persistent_failure() {
    int refresh_calls = 0;
    std::unique_ptr<ptd::TopologyRetry> retry;
    retry = std::make_unique<ptd::TopologyRetry>([&] {
        ++refresh_calls;
        retry->observe(ptd::OverlayManager::Convergence::Incomplete);
    }, ptd::TopologyRetry::Policy{3, 1, 1});

    retry->observe(ptd::OverlayManager::Convergence::Incomplete);
    QTRY_COMPARE_WITH_TIMEOUT(refresh_calls, 3, 2000);
    QTest::qWait(20);
    QCOMPARE(refresh_calls, 3);
    QVERIFY(!retry->pending());
    QCOMPARE(retry->attempts(), 3);
}

void TestTray::topology_retry_cancel_stops_pending_work() {
    int refresh_calls = 0;
    ptd::TopologyRetry retry([&] { ++refresh_calls; },
                             ptd::TopologyRetry::Policy{5, 10, 10});
    retry.observe(ptd::OverlayManager::Convergence::Incomplete);
    QVERIFY(retry.pending());
    retry.cancel();
    QTest::qWait(30);
    QCOMPARE(refresh_calls, 0);
    QVERIFY(!retry.pending());
}

void TestTray::application_startup_incomplete_topology_retries_autonomously() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_topology_retry_logs";
    const auto dir = std::filesystem::temp_directory_path() /
                     "protrail_topology_retry_startup";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    RetryTopologyFixture fixture;
    auto manager = make_retry_topology(fixture);
    auto* manager_view = manager.get();
    ptd::InMemoryAutostartBackend autostart;
    Application app((dir / "config.json").native());
    app.set_startup_mode(ptd::StartupMode::AutostartMinimized);
    app.set_autostart_backend_for_tests(&autostart);
    app.set_overlay_manager_for_tests(std::move(manager));
    app.set_topology_retry_policy_for_tests(5, 1, 1);
    QVERIFY(app.initialize());

    bool converged = false;
    QObject exit_guard;
    QTimer::singleShot(20, &exit_guard, [&] {
        QTRY_COMPARE_WITH_TIMEOUT(manager_view->overlay_count(), std::size_t(2), 2000);
        QCOMPARE(fixture.left_attempts, 1);
        QCOMPARE(fixture.right_attempts, 2);
        QCOMPARE(manager_view->monitor_overlays().size(), std::size_t(2));
        QCOMPARE(manager_view->monitor_overlays()[0].window.get(), fixture.healthy_left);
        QVERIFY(manager_view->monitor_overlays()[0].monitor.device_name == fixture.left_name);
        QVERIFY(manager_view->monitor_overlays()[1].monitor.device_name == fixture.right_name);
        converged = true;
        app.request_exit();
    });
    QTimer::singleShot(3000, &exit_guard, [&app] { app.request_exit(); });
    QCOMPARE(app.run(), 0);
    app.shutdown();
    QVERIFY(converged);
    QCOMPARE(fixture.left_attempts, 1);
    QCOMPARE(fixture.right_attempts, 2);
    std::filesystem::remove_all(dir, ec);
}

void TestTray::application_changed_monitor_retries_without_second_event() {
    // W2-001 audit contract: a changed/recreated monitor that fails once after
    // the ONLY external topology event must autonomously recover WITHOUT a
    // second WM_DISPLAYCHANGE/WM_DPICHANGED. Startup is fully healthy (both
    // overlays live); one synthetic display event triggers a refresh whose
    // right-overlay recreation fails once; the internally scheduled retry alone
    // restores full coverage, and the healthy left overlay identity survives.
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_topology_changed_logs";
    const auto dir = std::filesystem::temp_directory_path() /
                     "protrail_topology_changed_monitor";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    RetryTopologyFixture fixture;
    fixture.fail_right_first = false; // startup is fully healthy
    auto manager = make_retry_topology(fixture);
    auto* manager_view = manager.get();
    ptd::InMemoryAutostartBackend autostart;
    Application app((dir / "config.json").native());
    app.set_startup_mode(ptd::StartupMode::AutostartMinimized);
    app.set_autostart_backend_for_tests(&autostart);
    app.set_overlay_manager_for_tests(std::move(manager));
    app.set_topology_retry_policy_for_tests(5, 1, 1);
    QVERIFY(app.initialize());

    bool recovered = false;
    QObject exit_guard;
    QTimer::singleShot(20, &exit_guard, [&] {
        // Startup healthy: both overlays live, no retry pending.
        QCOMPARE(manager_view->overlay_count(), std::size_t(2));
        QVERIFY(!app.topology_retry_pending_for_tests());
        const int right_before = fixture.right_attempts;
        // Healthy left overlay identity captured AFTER startup created it.
        ptd::OverlayWindow* healthy_left_before =
            manager_view->monitor_overlays()[0].window.get();

        // Change the right monitor bounds so the ONE external event forces its
        // recreation, and arm exactly one recreation failure.
        fixture.monitors[1].bounds = RECT{100, 0, 260, 100};
        fixture.monitors[1].work_area = fixture.monitors[1].bounds;
        fixture.fail_right_next_once = true;

        // The ONLY external topology event.
        ptd::OverlayWindow::fire_display_change_for_tests();

        // The internally scheduled retry alone (no second event) restores it.
        // Converge on the RECREATED right overlay (new bounds), not merely a
        // count of 2 -- the count is already 2 and would pass before the
        // deferred refresh even runs.
        QTRY_VERIFY_WITH_TIMEOUT(
            manager_view->monitor_overlays().size() == std::size_t(2) &&
            manager_view->monitor_overlays()[1].monitor.bounds.right == 260l,
            3000);
        QVERIFY(fixture.right_attempts >= right_before + 2); // failed once, retried
        QCOMPARE(manager_view->monitor_overlays().size(), std::size_t(2));
        // Healthy left overlay identity is preserved across the retry.
        QCOMPARE(manager_view->monitor_overlays()[0].window.get(), healthy_left_before);
        QVERIFY(manager_view->monitor_overlays()[0].monitor.device_name == fixture.left_name);
        QVERIFY(manager_view->monitor_overlays()[1].monitor.device_name == fixture.right_name);
        QVERIFY(manager_view->monitor_overlays()[1].monitor.bounds.right == 260l);
        recovered = true;
        app.request_exit();
    });
    QTimer::singleShot(3000, &exit_guard, [&app] { app.request_exit(); });
    QCOMPARE(app.run(), 0);
    app.shutdown();
    QVERIFY(recovered);
    std::filesystem::remove_all(dir, ec);
}

void TestTray::application_shutdown_cancels_topology_retry() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_topology_cancel_logs";
    const auto dir = std::filesystem::temp_directory_path() /
                     "protrail_topology_cancel_shutdown";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    RetryTopologyFixture fixture;
    auto manager = make_retry_topology(fixture);
    auto* manager_view = manager.get();
    ptd::InMemoryAutostartBackend autostart;
    Application app((dir / "config.json").native());
    app.set_startup_mode(ptd::StartupMode::AutostartMinimized);
    app.set_autostart_backend_for_tests(&autostart);
    app.set_overlay_manager_for_tests(std::move(manager));
    app.set_topology_retry_policy_for_tests(5, 500, 500);
    QVERIFY(app.initialize());

    bool had_live_overlay_at_exit = false;
    QObject exit_guard;
    QTimer::singleShot(20, &exit_guard, [&] {
        QCOMPARE(manager_view->overlay_count(), std::size_t(1));
        QCOMPARE(fixture.right_attempts, 1);
        QVERIFY(app.topology_retry_pending_for_tests());
        had_live_overlay_at_exit = true;
        app.request_exit();
    });
    QTimer::singleShot(3000, &exit_guard, [&app] { app.request_exit(); });
    QCOMPARE(app.run(), 0);
    QVERIFY(app.topology_retry_pending_for_tests());
    app.shutdown();
    QVERIFY(had_live_overlay_at_exit);
    QVERIFY(!app.topology_retry_pending_for_tests());
    QTest::qWait(600);
    QCOMPARE(fixture.right_attempts, 1);
    std::filesystem::remove_all(dir, ec);
}

void TestTray::application_initialize_requires_startup_log_bootstrap() {
    ptd::reset_log_for_tests();
    const auto blocker = std::filesystem::temp_directory_path() /
                         "protrail_log_guard_blocker";
    {
        std::ofstream out(blocker, std::ios::trunc);
        QVERIFY(out.good());
        out << "parent path blocker";
    }
    const auto config_path = (blocker / "config.json").native();
    {
        Application app(config_path);
        QVERIFY(!app.initialize());
        QVERIFY(!ptd::is_log_initialized());
    }
    QVERIFY(!std::filesystem::exists(config_path));
    std::error_code ec;
    std::filesystem::remove(blocker, ec);
}

void TestTray::probe_error_blocks_application_save_and_shutdown() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_probe_error_test_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    const auto dir = std::filesystem::temp_directory_path() /
                     "protrail_core001_probe_error_application";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path config_path = dir / "config.json";
    ptd::AppConfig original = ptd::release_defaults();
    original.master_enabled = false;
    original.click.duration_ms = 2345;
    QVERIFY(ptd::ConfigStorage::save_to_file(original, config_path.native()));

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };
    const std::string before = read_bytes(config_path);
    QVERIFY(!before.empty());

    ptd::InMemoryAutostartBackend backend;
    backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                L"preserved existing command");
    backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
    const auto backend_before = backend.values;

    const std::wstring protected_path = config_path.native();
    ptd::ConfigStorage::set_filesystem_probe_for_tests(
        [protected_path](const std::wstring& path) {
            if (path == protected_path) {
                return std::tuple<bool, std::error_code>{
                    false, std::make_error_code(std::errc::permission_denied)};
            }
            std::error_code probe_ec;
            const bool exists = std::filesystem::exists(path, probe_ec);
            return std::tuple<bool, std::error_code>{exists, probe_ec};
        });
    struct ProbeReset {
        ~ProbeReset() { ptd::ConfigStorage::clear_filesystem_probe_for_tests(); }
    } reset_probe;

    {
        Application app(config_path.native());
        app.set_startup_mode(ptd::StartupMode::AutostartMinimized);
        app.set_autostart_backend_for_tests(&backend);
        QVERIFY(app.initialize());
        QVERIFY(!app.persistence_allowed());
        QVERIFY(!app.save_config_for_tests());
        QCOMPARE(backend.write_calls, 0);
        QCOMPARE(backend.remove_calls, 0);
        QVERIFY(backend.values == backend_before);
        app.shutdown();
    }

    QVERIFY(std::filesystem::exists(config_path));
    QCOMPARE(read_bytes(config_path), before);
    QCOMPARE(backend.write_calls, 0);
    QCOMPARE(backend.remove_calls, 0);
    QVERIFY(backend.values == backend_before);
    std::filesystem::remove_all(dir, ec);
}

void TestTray::protected_provenance_blocks_autostart_and_ui_changes() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_protected_autostart_test_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    auto exercise_protected_source = [&](const char* tag, bool probe_error) {
        const auto dir = std::filesystem::temp_directory_path() /
                         (std::string("protrail_core002_") + tag);
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path config_path = dir / "config.json";
        if (probe_error) {
            ptd::AppConfig source = ptd::release_defaults();
            source.start_with_windows = true;
            QVERIFY(ptd::ConfigStorage::save_to_file(source, config_path.native()));
        } else {
            std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
            QVERIFY(out.good());
            out << "{\"schema_version\":"
                << (ptd::AppConfig::kCurrentSchemaVersion + 1)
                << ",\"start_with_windows\":true}";
            out.close();
            QVERIFY(out.good());
        }

        const auto read_bytes = [](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>());
        };
        const std::string before = read_bytes(config_path);
        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                    L"old registered command");
        backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
        const auto backend_before = backend.values;

        if (probe_error) {
            const std::wstring protected_path = config_path.native();
            ptd::ConfigStorage::set_filesystem_probe_for_tests(
                [protected_path](const std::wstring& path) {
                    if (path == protected_path) {
                        return std::tuple<bool, std::error_code>{
                            false, std::make_error_code(std::errc::permission_denied)};
                    }
                    std::error_code probe_ec;
                    const bool exists = std::filesystem::exists(path, probe_ec);
                    return std::tuple<bool, std::error_code>{exists, probe_ec};
                });
        }
        struct ProbeReset {
            ~ProbeReset() { ptd::ConfigStorage::clear_filesystem_probe_for_tests(); }
        } reset_probe;

        bool toggle_found = false;
        bool initially_off = false;
        bool finally_off = false;
        bool backend_unchanged_during_toggle = false;
        {
            Application app(config_path.native());
            QObject exit_guard;
            app.set_startup_mode(ptd::StartupMode::Normal);
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            QVERIFY(!app.persistence_allowed());
            QCOMPARE(backend.write_calls, 0);
            QCOMPARE(backend.remove_calls, 0);
            QVERIFY(backend.values == backend_before);

            QTimer::singleShot(20, &exit_guard, [&] {
                auto* product = app.product_window_for_tests();
                auto* toggle = product
                    ? product->findChild<QCheckBox*>(QStringLiteral("chk_start_with_windows"))
                    : nullptr;
                toggle_found = toggle != nullptr;
                if (toggle) {
                    initially_off = !toggle->isChecked();
                    toggle->click();
                    finally_off = !toggle->isChecked();
                }
                backend_unchanged_during_toggle =
                    backend.write_calls == 0 && backend.remove_calls == 0
                    && backend.values == backend_before;
                app.request_exit();
            });
            QTimer::singleShot(5000, &exit_guard, [&app] {
                app.request_exit();
            });
            QCOMPARE(app.run(), 0);
            app.shutdown();
        }

        QVERIFY(toggle_found);
        QVERIFY(initially_off);
        QVERIFY(finally_off);
        QVERIFY(backend_unchanged_during_toggle);
        QCOMPARE(backend.write_calls, 0);
        QCOMPARE(backend.remove_calls, 0);
        QVERIFY(backend.values == backend_before);
        QVERIFY(std::filesystem::exists(config_path));
        QCOMPARE(read_bytes(config_path), before);
        std::filesystem::remove_all(dir, ec);
    };

    exercise_protected_source("future_schema", false);
    exercise_protected_source("read_failure", true);
}

void TestTray::authoritative_config_reconciles_autostart_both_directions() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_trusted_autostart_test_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    const std::wstring executable = ptd::current_executable_path();
    QVERIFY(!executable.empty());
    const std::wstring expected_command =
        ptd::AutostartManager::build_command(executable);

    auto exercise_trusted_source = [&](bool desired) {
        const auto tag = desired ? "enabled" : "disabled";
        const auto dir = std::filesystem::temp_directory_path() /
                         (std::string("protrail_core002_trusted_") + tag);
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        ptd::AppConfig config = ptd::release_defaults();
        config.start_with_windows = desired;
        QVERIFY(ptd::ConfigStorage::save_to_file(config, config_path.native()));

        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                    L"stale registered command");
        backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
        {
            Application app(config_path.native());
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            QVERIFY(app.persistence_allowed());
            if (desired) {
                QCOMPARE(backend.write_calls, 1);
                QCOMPARE(backend.remove_calls, 0);
                std::wstring actual;
                QVERIFY(backend.read(ptd::AutostartManager::kValueName, actual));
                QCOMPARE(QString::fromStdWString(actual),
                         QString::fromStdWString(expected_command));
            } else {
                QCOMPARE(backend.write_calls, 0);
                QCOMPARE(backend.remove_calls, 1);
                std::wstring actual;
                QVERIFY(!backend.read(ptd::AutostartManager::kValueName, actual));
            }
            std::wstring unrelated;
            QVERIFY(backend.read(L"UnrelatedApp", unrelated));
            QCOMPARE(unrelated, std::wstring(L"unrelated command"));
            app.shutdown();
        }
        std::filesystem::remove_all(dir, ec);
    };

    exercise_trusted_source(false);
    exercise_trusted_source(true);
}

void TestTray::autostart_persistence_failure_blocks_run_key_mutation() {
    // W2-002: durable desired preference is the commit authority for autostart.
    // A forced config-save failure must leave the on-disk preference unchanged
    // and the Run key untouched; a successful save followed by an injected
    // registry failure must still leave the desired preference durable. Proven
    // through BOTH the checkbox path (apply_start_with_windows) and the whole-
    // AppConfig transaction (Restore All / apply_app_config_transaction).
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_w2002_persist_fail_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    const std::wstring executable = ptd::current_executable_path();
    QVERIFY(!executable.empty());
    const std::wstring expected_command =
        ptd::AutostartManager::build_command(executable);

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };

    struct SaveSeamReset {
        ~SaveSeamReset() { ptd::ConfigStorage::clear_save_failure_path_for_tests(); }
    } reset_save_seam;

    // Case A: durable ON + registered Run entry, request OFF via the checkbox
    // path while config save is forced to fail -> disk stays ON, backend is
    // never asked to remove, controller/UI desired value returns to ON.
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2002_checkbox_off";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        ptd::AppConfig on_config = ptd::release_defaults();
        on_config.start_with_windows = true;
        QVERIFY(ptd::ConfigStorage::save_to_file(on_config, config_path.native()));
        const std::string before = read_bytes(config_path);

        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(ptd::AutostartManager::kValueName, expected_command);
        backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
        const auto backend_before = backend.values;

        bool toggle_found = false;
        bool initially_on = false;
        bool ui_returned_on = false;
        {
            Application app(config_path.native());
            QObject exit_guard;
            app.set_startup_mode(ptd::StartupMode::Normal);
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            QVERIFY(app.persistence_allowed());
            const int writes_after_init = backend.write_calls;
            const int removes_after_init = backend.remove_calls;

            QTimer::singleShot(20, &exit_guard, [&] {
                auto* product = app.product_window_for_tests();
                auto* toggle = product
                    ? product->findChild<QCheckBox*>(QStringLiteral("chk_start_with_windows"))
                    : nullptr;
                toggle_found = toggle != nullptr;
                if (toggle) {
                    initially_on = toggle->isChecked();
                    // Force the durable write to fail for exactly this config.
                    ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
                    toggle->click(); // request OFF
                    ptd::ConfigStorage::clear_save_failure_path_for_tests();
                    ui_returned_on = toggle->isChecked();
                }
                // The Run key was never mutated by the blocked OFF request.
                QCOMPARE(backend.remove_calls, removes_after_init);
                QCOMPARE(backend.write_calls, writes_after_init);
                app.request_exit();
            });
            QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
            QCOMPARE(app.run(), 0);
            // Shutdown save must also be blocked for this protected-from-write
            // test path; arm the seam again so the final save cannot rewrite it.
            ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
            app.shutdown();
            ptd::ConfigStorage::clear_save_failure_path_for_tests();
        }

        QVERIFY(toggle_found);
        QVERIFY(initially_on);
        QVERIFY(ui_returned_on); // desired preference resynced to durable ON
        QCOMPARE(backend.remove_calls, 0);
        QVERIFY(backend.values == backend_before);
        QCOMPARE(read_bytes(config_path), before); // disk still ON, byte-identical
        std::filesystem::remove_all(dir, ec);
    }

    // Case B: durable OFF + no Run entry, request ON via the checkbox path with
    // forced save failure -> disk stays OFF, backend receives no write.
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2002_checkbox_on";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        ptd::AppConfig off_config = ptd::release_defaults();
        off_config.start_with_windows = false;
        QVERIFY(ptd::ConfigStorage::save_to_file(off_config, config_path.native()));
        const std::string before = read_bytes(config_path);

        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
        const auto backend_before = backend.values;

        bool toggle_found = false;
        bool initially_off = false;
        bool ui_returned_off = false;
        {
            Application app(config_path.native());
            QObject exit_guard;
            app.set_startup_mode(ptd::StartupMode::Normal);
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            const int writes_after_init = backend.write_calls;

            QTimer::singleShot(20, &exit_guard, [&] {
                auto* product = app.product_window_for_tests();
                auto* toggle = product
                    ? product->findChild<QCheckBox*>(QStringLiteral("chk_start_with_windows"))
                    : nullptr;
                toggle_found = toggle != nullptr;
                if (toggle) {
                    initially_off = !toggle->isChecked();
                    ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
                    toggle->click(); // request ON
                    ptd::ConfigStorage::clear_save_failure_path_for_tests();
                    ui_returned_off = !toggle->isChecked();
                }
                QCOMPARE(backend.write_calls, writes_after_init);
                app.request_exit();
            });
            QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
            QCOMPARE(app.run(), 0);
            ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
            app.shutdown();
            ptd::ConfigStorage::clear_save_failure_path_for_tests();
        }

        QVERIFY(toggle_found);
        QVERIFY(initially_off);
        QVERIFY(ui_returned_off); // desired preference resynced to durable OFF
        QCOMPARE(backend.write_calls, 0);
        std::wstring value;
        QVERIFY(!backend.read(ptd::AutostartManager::kValueName, value));
        QVERIFY(backend.values == backend_before);
        QCOMPARE(read_bytes(config_path), before); // disk still OFF
        std::filesystem::remove_all(dir, ec);
    }

    // Case C: successful persistence followed by an injected registry
    // reconciliation failure -> the desired preference stays durable ON so a
    // later startup reconciliation can converge machine state.
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2002_registry_fail";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        ptd::AppConfig off_config = ptd::release_defaults();
        off_config.start_with_windows = false;
        QVERIFY(ptd::ConfigStorage::save_to_file(off_config, config_path.native()));

        ptd::InMemoryAutostartBackend backend;
        backend.fail_writes = true; // registry write fails AFTER durable commit
        {
            Application app(config_path.native());
            QObject exit_guard;
            app.set_startup_mode(ptd::StartupMode::Normal);
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());

            QTimer::singleShot(20, &exit_guard, [&] {
                auto* product = app.product_window_for_tests();
                auto* toggle = product
                    ? product->findChild<QCheckBox*>(QStringLiteral("chk_start_with_windows"))
                    : nullptr;
                QVERIFY(toggle != nullptr);
                toggle->click(); // request ON; save succeeds, registry write fails
                app.request_exit();
            });
            QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
            QCOMPARE(app.run(), 0);
            app.shutdown();
        }

        // The durable preference committed to disk as ON despite registry fail.
        const auto reloaded = ptd::ConfigStorage::load_from_file_result(config_path.native());
        QVERIFY(reloaded.config.start_with_windows);
        std::filesystem::remove_all(dir, ec);
    }

    // Case D: the SAME persistence-first rule holds through the whole-AppConfig
    // Restore-All transaction, not only the narrow checkbox path. Start durable
    // ON, drive a whole-config apply that flips Start-with-Windows OFF while
    // save is forced to fail -> disk stays ON, backend never removes.
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2002_whole_config";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        ptd::AppConfig on_config = ptd::release_defaults();
        on_config.start_with_windows = true;
        QVERIFY(ptd::ConfigStorage::save_to_file(on_config, config_path.native()));
        const std::string before = read_bytes(config_path);

        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(ptd::AutostartManager::kValueName, expected_command);
        const auto backend_before = backend.values;

        {
            Application app(config_path.native());
            QObject exit_guard;
            app.set_startup_mode(ptd::StartupMode::Normal);
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            const int removes_after_init = backend.remove_calls;

            QTimer::singleShot(20, &exit_guard, [&] {
                auto* product = app.product_window_for_tests();
                QVERIFY(product != nullptr);
                // Emit a whole-config transaction that turns Start-with-Windows
                // OFF while the durable write is forced to fail.
                ptd::AppConfig next = on_config;
                next.start_with_windows = false;
                ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
                emit product->app_config_applied(next);
                ptd::ConfigStorage::clear_save_failure_path_for_tests();
                // Run key untouched by the blocked whole-config OFF.
                QCOMPARE(backend.remove_calls, removes_after_init);
                app.request_exit();
            });
            QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
            QCOMPARE(app.run(), 0);
            ptd::ConfigStorage::set_save_failure_path_for_tests(config_path.native());
            app.shutdown();
            ptd::ConfigStorage::clear_save_failure_path_for_tests();
        }

        QCOMPARE(backend.remove_calls, 0);
        QVERIFY(backend.values == backend_before);
        QCOMPARE(read_bytes(config_path), before); // disk still ON
        std::filesystem::remove_all(dir, ec);
    }
}

void TestTray::protected_schema_sources_cannot_be_rewritten_by_application() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_protected_schema_test_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    std::vector<std::string> documents = {
        R"({"master_enabled":true})",
        R"({"schema_version":"11","master_enabled":true})",
        R"({"schema_version":{"value":11},"master_enabled":true})",
        R"({"schema_version":true,"master_enabled":true})",
        R"({"schema_version":10.5,"master_enabled":true})",
        "{\"schema_version\":"
            + std::to_string(ptd::AppConfig::kCurrentSchemaVersion + 1)
            + ",\"master_enabled\":true}",
    };
    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    };

    for (std::size_t index = 0; index < documents.size(); ++index) {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("protrail_core003_protected_" + std::to_string(index));
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";
        {
            std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
            QVERIFY(out.good());
            out.write(documents[index].data(),
                      static_cast<std::streamsize>(documents[index].size()));
            out.close();
            QVERIFY(out.good());
        }
        const std::string before = read_bytes(config_path);

        ptd::InMemoryAutostartBackend backend;
        backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                    L"preserved command");
        backend.values.emplace_back(L"UnrelatedApp", L"unrelated command");
        const auto backend_before = backend.values;
        {
            Application app(config_path.native());
            app.set_autostart_backend_for_tests(&backend);
            QVERIFY(app.initialize());
            QVERIFY(!app.persistence_allowed());
            QVERIFY(!app.save_config_for_tests());
            app.shutdown();
        }

        QVERIFY(std::filesystem::exists(config_path));
        QCOMPARE(read_bytes(config_path), before);
        QVERIFY(!std::filesystem::exists(config_path.native() + L".corrupt"));
        QCOMPARE(backend.write_calls, 0);
        QCOMPARE(backend.remove_calls, 0);
        QVERIFY(backend.values == backend_before);
        std::filesystem::remove_all(dir, ec);
    }
}

void TestTray::application_window_capture_retains_loaded_render_config() {
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_render_baseline_test_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    const auto dir = std::filesystem::temp_directory_path() /
                     "protrail_core004_application_capture";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto config_path = dir / "config.json";
    ptd::AppConfig source = ptd::release_defaults();
    source.render.diagnostic_primitives = true;
    source.trail.lifetime_ms = 850.0f;
    source.click.duration_ms = 450.0f;
    QVERIFY(ptd::ConfigStorage::save_to_file(source, config_path.native()));

    ptd::InMemoryAutostartBackend backend;
    ptd::AppConfig captured;
    bool captured_window = false;
    {
        Application app(config_path.native());
        QObject exit_guard;
        app.set_startup_mode(ptd::StartupMode::Normal);
        app.set_autostart_backend_for_tests(&backend);
        QVERIFY(app.initialize());
        QTimer::singleShot(20, &exit_guard, [&] {
            auto* product = app.product_window_for_tests();
            if (product) {
                captured = product->capture_current_settings();
                captured_window = true;
            }
            app.request_exit();
        });
        QTimer::singleShot(5000, &exit_guard, [&app] {
            app.request_exit();
        });
        QCOMPARE(app.run(), 0);
        app.shutdown();
    }

    QVERIFY(captured_window);
    QCOMPARE(captured, source);
    QVERIFY(captured.render.diagnostic_primitives);
    std::filesystem::remove_all(dir, ec);
}

void TestTray::tray_has_one_open_action() {
    ptd::ui::TrayIcon tray(true);
    QCOMPARE(tray.action_home()->text(), QStringLiteral("Open ProTrail"));
    QCOMPARE(tray.action_toggle()->text(), QStringLiteral("Disable"));
    QCOMPARE(tray.action_exit()->text(), QStringLiteral("Exit"));

    int open_actions = 0;
    for (QAction* action : tray.menu()->actions()) {
        if (action->text() == QStringLiteral("Open ProTrail")) ++open_actions;
        QVERIFY(action->text() != QStringLiteral("Settings..."));
    }
    QCOMPARE(open_actions, 1);
}

// The approved icon's one authority is the IDI_ICON1 PE resource. This test
// binary links the same generated protrail.rc, so resource availability must
// agree with what configure saw; with the resource present every shell size
// resolves and the enabled tray state IS the product icon.
void TestTray::product_icon_resource_matches_build_input() {
    QCOMPARE(ptd::ui::branding::has_product_icon(), PROTRAIL_EXPECT_FINAL_ICON != 0);
    const QIcon product = ptd::ui::branding::product_icon();
    if (!ptd::ui::branding::has_product_icon()) {
        QVERIFY(product.isNull());
        const QImage tray = ptd::ui::TrayIcon::create_icon(true).pixmap(16, 16).toImage();
        const QImage fallback =
            ptd::ui::branding::development_fallback_icon(true).pixmap(16, 16).toImage();
        QCOMPARE(tray, fallback);
        return;
    }
    QVERIFY(!product.isNull());
    for (int size : ptd::ui::branding::kProductIconSizes) {
        const QPixmap pm = product.pixmap(size, size);
        QVERIFY2(pm.width() == size && pm.height() == size,
                 qPrintable(QStringLiteral("product icon lacks a %1px entry").arg(size)));
    }
    QCOMPARE(ptd::ui::TrayIcon::create_icon(true).pixmap(32, 32).toImage(),
             product.pixmap(32, 32).toImage());
}

// Enabled and disabled must differ visibly at the real tray sizes: the mean
// premultiplied RGBA difference over the drawn pixels has to exceed a
// generous floor, so a near-identical tint cannot pass.
void TestTray::tray_states_are_distinguishable_at_tray_sizes() {
    for (int size : {16, 20, 24, 32}) {
        const QImage on = ptd::ui::TrayIcon::create_icon(true).pixmap(size, size)
                              .toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage off = ptd::ui::TrayIcon::create_icon(false).pixmap(size, size)
                               .toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(on.size(), off.size());
        double diff = 0.0;
        int drawn = 0;
        for (int y = 0; y < on.height(); ++y) {
            for (int x = 0; x < on.width(); ++x) {
                const QRgb a = on.pixel(x, y);
                const QRgb b = off.pixel(x, y);
                if (qAlpha(a) == 0 && qAlpha(b) == 0) continue;
                ++drawn;
                diff += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
                        std::abs(qBlue(a) - qBlue(b)) + std::abs(qAlpha(a) - qAlpha(b));
            }
        }
        QVERIFY2(drawn > 0, qPrintable(QStringLiteral("empty tray icon at %1px").arg(size)));
        const double mean = diff / (drawn * 4.0);
        QVERIFY2(mean >= 24.0,
                 qPrintable(QStringLiteral("tray states too similar at %1px: mean diff %2")
                                .arg(size).arg(mean)));
    }
}

void TestTray::disabled_treatment_desaturates_and_dims() {
    QImage src(2, 1, QImage::Format_ARGB32);
    src.setPixel(0, 0, qRgba(220, 40, 10, 255));
    src.setPixel(1, 0, qRgba(0, 0, 0, 0));
    const QImage out = ptd::ui::branding::disabled_treatment(src);
    const QRgb px = out.pixel(0, 0);
    QCOMPARE(qRed(px), qGray(qRgb(220, 40, 10)));
    QCOMPARE(qGreen(px), qRed(px));
    QCOMPARE(qBlue(px), qRed(px));
    QCOMPARE(qAlpha(px), 115);
    QCOMPARE(qAlpha(out.pixel(1, 0)), 0);
}

void TestTray::manual_startup_shows_one_general_window() {
    with_running_application("manual", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* product = app.product_window_for_tests();
        QVERIFY(product != nullptr);
        QCOMPARE(static_cast<QWidget*>(product), static_cast<QWidget*>(app.product_window_for_tests()));
        QVERIFY(product->isVisible());
        QCOMPARE(product->windowTitle(), QStringLiteral("ProTrail"));
        QCOMPARE(product_window_count(), 1);
        auto* tabs = product->findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), ptd::is_dev_build() ? 4 : 3);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(product->findChild<QPushButton*>(QStringLiteral("btn_restore_all")) != nullptr);
    });
}

void TestTray::autostart_is_tray_only() {
    with_running_application("autostart", ptd::StartupMode::AutostartMinimized,
                             [](Application& app) {
        auto* product = app.product_window_for_tests();
        QVERIFY(product != nullptr);
        QVERIFY(product->isHidden());
        QVERIFY(!product->isVisible());
        QCOMPARE(product_window_count(), 1);
        QVERIFY(app.tray_icon_for_tests() != nullptr);
        QVERIFY(app.tray_icon_for_tests()->is_visible());
    });
}

void TestTray::close_hides_and_open_reuses_window() {
    with_running_application("reuse", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* product = app.product_window_for_tests();
        auto* tray = app.tray_icon_for_tests();
        QVERIFY(product != nullptr);
        QVERIFY(tray != nullptr);
        product->close();
        QVERIFY(product->isHidden());
        QCOMPARE(product_window_count(), 1);
        app.show_main(); // second-instance interactive activation path
        QVERIFY(product->isVisible());
        QCOMPARE(product_window_count(), 1);
        QCOMPARE(app.product_window_for_tests(), product);
        product->close();
        tray->action_home()->trigger();
        QVERIFY(product->isVisible());
        QCOMPARE(product_window_count(), 1);
        QCOMPARE(app.product_window_for_tests(), product);
        tray->action_home()->trigger();
        QCOMPARE(app.product_window_for_tests(), product);
    });
}

void TestTray::restore_defaults_is_one_application_transaction() {
    with_running_application("restore", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* product = app.product_window_for_tests();
        QVERIFY(product != nullptr);
        auto* restore = product->findChild<QPushButton*>(QStringLiteral("btn_restore_all"));
        QVERIFY(restore != nullptr);
        QSignalSpy bulk(product, &ptd::ui::SettingsWindow::app_config_applied);
        app.reset_save_invocation_count_for_tests();
        restore->click();
        QCOMPARE(bulk.count(), 1);
        QCOMPARE(app.save_invocation_count_for_tests(), 1);
    });
}

void TestTray::shutdown_persistence_is_bounded_and_coalesced() {
    // W2-005: a last-moment debounced visual edit flushed successfully during
    // shutdown must produce EXACTLY ONE durable write, not the flush + an
    // unconditional second identical shutdown save. With no pending dirty edit
    // shutdown still performs its one intended final save. And the final
    // on-disk bytes equal the final in-memory visual state.
    const auto log_dir = std::filesystem::temp_directory_path() /
                         "protrail_w2005_shutdown_logs";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ptd::is_log_initialized()) {
        QVERIFY(ptd::log_init((log_dir / "protrail.log").native()));
    }

    const auto read_config = [](const std::filesystem::path& path) {
        return ptd::ConfigStorage::load_from_file(path.wstring());
    };

    // Case A: pending dirty debounced edit + shutdown before the debounce
    // fires -> exactly one successful durable write, and the on-disk trail
    // lifetime equals the last in-memory edit.
    float final_lifetime = 0.0f;
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2005_dirty_flush";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";

        Application app(config_path.native());
        QObject exit_guard;
        app.set_startup_mode(ptd::StartupMode::Normal);
        // Long debounce so the timer cannot fire before shutdown.
        app.set_save_debounce_ms_for_tests(100000);
        QVERIFY(app.initialize());

        QTimer::singleShot(20, &exit_guard, [&] {
            auto* product = app.product_window_for_tests();
            QVERIFY(product != nullptr);
            ptd::TrailConfig edited = ptd::release_defaults().trail;
            edited.lifetime_ms = 1234.0f;
            final_lifetime = edited.lifetime_ms;
            app.reset_save_invocation_count_for_tests();
            emit product->trail_config_changed(edited); // arms the debounce
            QVERIFY(app.config_dirty_for_tests());
            QCOMPARE(app.save_invocation_count_for_tests(), 0); // not yet written
            app.request_exit();
        });
        QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
        QCOMPARE(app.run(), 0);
        app.shutdown(); // one flush; NO unconditional duplicate write
        QCOMPARE(app.save_invocation_count_for_tests(), 1);
        QVERIFY(!app.config_dirty_for_tests());

        const ptd::AppConfig on_disk = read_config(config_path);
        QCOMPARE(on_disk.trail.lifetime_ms, final_lifetime);
        std::filesystem::remove_all(dir, ec);
    }

    // Case B: no pending dirty edit -> shutdown still performs exactly one
    // intended final save (the documented product contract).
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2005_no_pending";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";

        Application app(config_path.native());
        QObject exit_guard;
        app.set_startup_mode(ptd::StartupMode::Normal);
        QVERIFY(app.initialize());

        QTimer::singleShot(20, &exit_guard, [&] {
            QVERIFY(!app.config_dirty_for_tests());
            app.reset_save_invocation_count_for_tests();
            app.request_exit();
        });
        QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
        QCOMPARE(app.run(), 0);
        app.shutdown();
        QCOMPARE(app.save_invocation_count_for_tests(), 1); // one final save
        std::filesystem::remove_all(dir, ec);
    }

    // Case C: the first dirty flush fails -> shutdown performs exactly one
    // bounded fallback save attempt, never an unbounded/duplicate sequence.
    {
        const auto dir = std::filesystem::temp_directory_path() /
                         "protrail_w2005_flush_fail";
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const auto config_path = dir / "config.json";

        struct SaveSeamReset {
            ~SaveSeamReset() { ptd::ConfigStorage::clear_save_failure_path_for_tests(); }
        } reset_save_seam;

        Application app(config_path.native());
        QObject exit_guard;
        app.set_startup_mode(ptd::StartupMode::Normal);
        app.set_save_debounce_ms_for_tests(100000);
        QVERIFY(app.initialize());

        QTimer::singleShot(20, &exit_guard, [&] {
            auto* product = app.product_window_for_tests();
            QVERIFY(product != nullptr);
            ptd::TrailConfig edited = ptd::release_defaults().trail;
            edited.lifetime_ms = 777.0f;
            emit product->trail_config_changed(edited);
            QVERIFY(app.config_dirty_for_tests());
            app.reset_save_invocation_count_for_tests();
            // Force EVERY save to fail so the shutdown flush fails and the
            // one bounded fallback save also fails: exactly two attempts, then
            // stop -- never an unbounded retry storm.
            ptd::ConfigStorage::set_save_failure_path_for_tests(std::wstring());
            app.request_exit();
        });
        QTimer::singleShot(5000, &exit_guard, [&app] { app.request_exit(); });
        QCOMPARE(app.run(), 0);
        app.shutdown(); // failed flush (1) + bounded fallback save (1)
        QCOMPARE(app.save_invocation_count_for_tests(), 2);
        ptd::ConfigStorage::clear_save_failure_path_for_tests();
        std::filesystem::remove_all(dir, ec);
    }
}

QTEST_MAIN(TestTray)
#include "test_tray.moc"
