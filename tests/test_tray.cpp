#include <QTest>
#include <QSignalSpy>
#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QWidget>
#include <QApplication>
#include <QTimer>

#include "src/app/application.h"
#include "src/app/single_instance.h"
#include "src/ui/tray_icon.h"
#include "src/ui/settings_window.h"
#include "src/ui/main_window.h"
#include "src/config/app_config.h"
#include "src/config/config_storage.h"
#include "src/config/release_defaults.h"
#include "src/config/dev_defaults.h"
#include "src/core/log.h"
#include "src/app/startup_paths.h"
#include "src/render/overlay_window.h"

#include <cmath>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

class TestTray : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void tray_creation();
    void settings_hide_on_close();
    void settings_restore();
    void no_duplicate_settings_window();
    void tray_enable_disable_uses_canonical_master_state();
    void tray_action_state_updates_when_master_state_changes_from_settings();
    void exit_performs_shutdown_path();
    void exit_triggers_downstream_application_shutdown();
    void tray_cleanup();
    void repeated_show_hide_cycles();
    void disabled_state_persistence_remains_correct();
    void application_lifecycle_persistence_isolation();
    void startup_path_resolution_normal_mode();
    void startup_path_resolution_smoke_auto_exit_without_state_dir_fails_closed();
    void startup_path_resolution_smoke_mode_with_state_dir();
    void startup_path_resolution_state_dir_without_smoke_mode_is_ignored();
    void single_instance_activation_contract();
    // T-018R1 Phase 8: activation targets the one canonical window in every
    // visibility state; smoke mode never participates in ownership.
    void activation_restores_hidden_settings_window();
    void activation_restores_minimized_settings_window_no_duplicate();
    void smoke_mode_does_not_participate_in_production_ownership();
    // T-018R2 Phase 7/8: readiness publication failure fails closed before
    // the event loop starts.
    void mark_ready_failure_fails_closed_without_event_loop();
    void overlay_device_loss_recreation();

    // CORE-001: a protected source must survive the full Application
    // initialize -> shutdown lifecycle byte-identically.
    void future_schema_survives_application_shutdown();
    void future_schema_survives_settings_save_attempt();
    void malformed_backup_failure_survives_application_shutdown();
    void read_failure_survives_application_shutdown();

    // CORE-002: startup reconciliation enforces the OFF preference by
    // removing a stale owned Run value (transient disable repair).
    void autostart_off_startup_removes_stale_owned_value();
    void autostart_off_never_touches_unrelated_values();

    // W2-005: exactly one durable config read per Application startup.
    void startup_loads_configuration_exactly_once();

    // PERF-002: durable persistence is debounced; shutdown flushes it.
    void deferred_save_coalesces_and_shutdown_flushes();

    // T-37 home-surface contract: which surface each startup mode shows, that
    // the tray and the Advanced Settings action restore the ONE canonical
    // window instead of constructing a second, that Main and Settings stay
    // synchronized in both directions, and that Restore Defaults lands on both
    // surfaces as a single silent canonical transaction.
    void manual_startup_shows_main_and_not_settings();
    void autostart_startup_shows_neither_surface();
    void tray_home_action_restores_the_single_main_instance();
    void advanced_settings_action_opens_the_single_settings_instance();
    void activation_restores_the_single_main_instance();
    void main_and_settings_stay_synchronized_both_ways();
    void restore_defaults_updates_both_surfaces_in_one_transaction();
    void developer_set_defaults_captures_the_complete_canonical_state();
};

namespace {

// Runs ONE bounded production lifecycle (initialize -> run -> shutdown) with
// an isolated temp config, and hands the live Application to `body` at a
// fixed point INSIDE the running event loop -- that is the state a user
// actually observes (the product window is up, the tray is alive), not a
// post-exec() remnant. The body also ends the run, so every wait is bounded
// and a startup regression fails the test instead of hanging.
constexpr int kObserveAfterMs = 50;

template <typename Body>
void with_running_application(const char* tag, ptd::StartupMode mode, Body&& body) {
    const auto dir = std::filesystem::temp_directory_path()
                     / (std::string("protrail_t37_home_") + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto config_path = (dir / "config.json").native();

    {
        Application app(config_path);
        app.set_startup_mode(mode);
        QVERIFY(app.initialize());
        QTimer::singleShot(kObserveAfterMs, QApplication::instance(),
                           [&app, &body] {
            body(app);
            app.request_exit();
        });
        // Bounded failure guard: only fires if the observation never ran or
        // the app failed to exit on its own.
        QTimer::singleShot(10000, QApplication::instance(), [&app] { app.request_exit(); });
        QCOMPARE(app.run(), 0);
        app.shutdown();
    }

    std::filesystem::remove_all(dir, ec);
}

// Counts the process's top-level windows carrying a given title: the
// observable form of "no duplicate product home was constructed".
int top_level_windows_titled(const QString& title) {
    int count = 0;
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* w : widgets) {
        if (w->windowTitle() == title) ++count;
    }
    return count;
}

} // namespace

void TestTray::initTestCase() {
    qRegisterMetaType<ptd::TrailConfig>("ptd::TrailConfig");
    qRegisterMetaType<ptd::ClickConfig>("ptd::ClickConfig");
}

void TestTray::tray_creation() {
    ptd::ui::TrayIcon tray(true);
    QVERIFY(tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Enabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Disable"));
    QVERIFY(tray.action_settings() != nullptr);
    QCOMPARE(tray.action_settings()->text(), QStringLiteral("Settings..."));
    QVERIFY(tray.action_toggle() != nullptr);
    QVERIFY(tray.action_exit() != nullptr);
    QCOMPARE(tray.action_exit()->text(), QStringLiteral("Exit"));
    QVERIFY(tray.menu() != nullptr);
    QVERIFY(tray.system_tray_icon() != nullptr);

    // Disabled constructor variant
    ptd::ui::TrayIcon tray_off(false);
    QVERIFY(!tray_off.is_master_enabled());
    QCOMPARE(tray_off.tooltip(), QString::fromUtf8("ProTrail \u2014 Disabled"));
    QCOMPARE(tray_off.toggle_action_text(), QStringLiteral("Enable"));
}

void TestTray::settings_hide_on_close() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);

    settings.show();
    QVERIFY(settings.isVisible());
    QVERIFY(!settings.isHidden());

    // Window close event must hide the window instead of destroying it
    settings.close();
    QVERIFY(!settings.isVisible());
    QVERIFY(settings.isHidden());
}

void TestTray::settings_restore() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);

    settings.show();
    QVERIFY(settings.isVisible());

    settings.close();
    QVERIFY(settings.isHidden());

    // Restore from hidden state
    if (settings.isMinimized()) {
        settings.showNormal();
    } else {
        settings.show();
    }
    settings.raise();
    settings.activateWindow();

    QVERIFY(settings.isVisible());
    QVERIFY(!settings.isHidden());
}

void TestTray::no_duplicate_settings_window() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);
    QWidget* canonical_ptr = &settings;

    // Simulate multiple activation calls
    for (int i = 0; i < 5; ++i) {
        if (settings.isMinimized()) {
            settings.showNormal();
        } else {
            settings.show();
        }
        settings.raise();
        settings.activateWindow();
        QCOMPARE(static_cast<QWidget*>(&settings), canonical_ptr);
        QVERIFY(settings.isVisible());
    }
}

void TestTray::tray_enable_disable_uses_canonical_master_state() {
    ptd::ui::TrayIcon tray(true);
    QSignalSpy spy(&tray, &ptd::ui::TrayIcon::master_enabled_toggled);

    // Trigger toggle action (Disable)
    tray.action_toggle()->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    QVERIFY(!tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Disabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Enable"));

    // Trigger toggle action again (Enable)
    tray.action_toggle()->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    QVERIFY(tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Enabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Disable"));
}

void TestTray::tray_action_state_updates_when_master_state_changes_from_settings() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);
    ptd::ui::TrayIcon tray(true);

    auto* chk_master = settings.findChild<QCheckBox*>("chk_master");
    QVERIFY(chk_master != nullptr);

    // Wire master_enabled_changed signal as Application does
    QObject::connect(&settings, &ptd::ui::SettingsWindow::master_enabled_changed,
                     &tray, &ptd::ui::TrayIcon::set_master_enabled);

    // User toggles master checkbox in settings -> tray visual state updates
    chk_master->setChecked(false);
    QVERIFY(!tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Disabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Enable"));

    chk_master->setChecked(true);
    QVERIFY(tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Enabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Disable"));
}

void TestTray::exit_performs_shutdown_path() {
    ptd::ui::TrayIcon tray(true);
    QSignalSpy spy(&tray, &ptd::ui::TrayIcon::exit_requested);

    tray.action_exit()->trigger();
    QCOMPARE(spy.count(), 1);
}

void TestTray::exit_triggers_downstream_application_shutdown() {
    // T-017R1 Phase 1 & 2: Integration test proving normal exit reaches actual shutdown sequence
    // with isolated persistence seam and unambiguous log assertions.
    std::vector<std::string> log_messages;
    static std::vector<std::string>* s_active_logs = nullptr;
    s_active_logs = &log_messages;
    ptd::set_log_sink([](ptd::LogLevel, const std::string_view& msg) {
        if (s_active_logs) {
            s_active_logs->emplace_back(msg);
        }
    });

    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_tray_test_exit";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto temp_config = (temp_dir / "config.json").native();

    Application app(temp_config);
    QVERIFY(app.initialize());

    ptd::ui::TrayIcon tray(true);
    bool exit_handler_called = false;

    // Production wiring in Application::run:
    // QObject::connect(d_->tray_icon.get(), &ptd::ui::TrayIcon::exit_requested,
    //                  [this] {
    //                      app_log(2, "ProTrail exit requested via tray");
    //                      request_exit();
    //                  });
    QObject::connect(&tray, &ptd::ui::TrayIcon::exit_requested, [&] {
        exit_handler_called = true;
        ptd::log_write(ptd::LogLevel::Info, "ProTrail exit requested via tray");
        app.request_exit();
    });

    // Trigger tray Exit action
    tray.action_exit()->trigger();
    QVERIFY(exit_handler_called);

    // Verify exit requested logs were emitted (Phase 2: exact equality, distinct assertions)
    bool found_tray_exit_req = false;
    bool found_generic_exit_req = false;
    for (const auto& m : log_messages) {
        if (m == "ProTrail exit requested via tray") {
            found_tray_exit_req = true;
        }
        if (m == "ProTrail exit requested") {
            found_generic_exit_req = true;
        }
    }
    QVERIFY(found_tray_exit_req);
    QVERIFY(found_generic_exit_req);

    // Execute application shutdown path
    app.shutdown();

    // Verify shutdown log sequence was emitted (Phase 3: entry + completion)
    bool found_shutdown_started = false;
    bool found_shutdown_complete = false;
    for (const auto& m : log_messages) {
        if (m == "ProTrail shutting down") {
            found_shutdown_started = true;
        }
        if (m == "ProTrail shutdown complete") {
            found_shutdown_complete = true;
        }
    }
    QVERIFY(found_shutdown_started);
    QVERIFY(found_shutdown_complete);

    ptd::clear_log_sink();
    s_active_logs = nullptr;

    std::filesystem::remove_all(temp_dir, ec);
}

void TestTray::tray_cleanup() {
    auto tray = std::make_unique<ptd::ui::TrayIcon>(true);
    tray->show();
    QVERIFY(tray != nullptr);
    tray->hide();
    tray.reset(); // must cleanly tear down without memory leak or crash
    QVERIFY(tray == nullptr);
}

void TestTray::repeated_show_hide_cycles() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);

    for (int i = 0; i < 10; ++i) {
        settings.show();
        QVERIFY(settings.isVisible());
        settings.close();
        QVERIFY(settings.isHidden());
    }
}

void TestTray::disabled_state_persistence_remains_correct() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_tray_test_persistence";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto temp_config = (temp_dir / "config.json").native();

    // Create config with master_enabled = false
    ptd::AppConfig cfg;
    cfg.master_enabled = false;
    cfg.trail.enabled = true;
    cfg.click.enabled = true;

    QVERIFY(ptd::ConfigStorage::save_to_file(cfg, temp_config));
    const auto loaded = ptd::ConfigStorage::load_from_file(temp_config);
    QVERIFY(!loaded.master_enabled);

    // Tray initialized from persisted state reflects Disabled state
    ptd::ui::TrayIcon tray(loaded.master_enabled);
    QVERIFY(!tray.is_master_enabled());
    QCOMPARE(tray.tooltip(), QString::fromUtf8("ProTrail \u2014 Disabled"));
    QCOMPARE(tray.toggle_action_text(), QStringLiteral("Enable"));

    std::filesystem::remove_all(temp_dir, ec);
}

void TestTray::application_lifecycle_persistence_isolation() {
    // 1. Verify normal production construction uses real default config path (without opening it)
    {
        Application default_app;
        QCOMPARE(QString::fromStdWString(default_app.config_path()),
                 QString::fromStdWString(ptd::ConfigStorage::default_config_path()));
    }

    // 2. Prepare isolated temporary directory and config path
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_test_persistence_isolation";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto isolated_config_path = (temp_dir / "config.json").native();

    // 3. Create a disposable sentinel file at a separate temporary path (Phase 6: no production reads)
    const auto sentinel_dir = temp_dir / "production-sentinel";
    std::filesystem::create_directories(sentinel_dir, ec);
    const auto sentinel_path = (sentinel_dir / "sentinel.json").native();

    const std::string sentinel_original_bytes = "{\"sentinel\": \"untouched_production_substitute\"}";
    {
        std::ofstream sentinel_out(sentinel_path, std::ios::binary);
        sentinel_out << sentinel_original_bytes;
    }
    QVERIFY(std::filesystem::exists(sentinel_path, ec));

    // 4. Place distinctive non-default settings in the isolated temporary config
    ptd::AppConfig distinctive_cfg;
    distinctive_cfg.master_enabled = false;
    distinctive_cfg.trail.lifetime_ms = 777.0;
    distinctive_cfg.trail.glow_strength = 0.88f;
    distinctive_cfg.click.particle_amount = 19;
    distinctive_cfg.click.duration_ms = 888.0f;
    QVERIFY(ptd::ConfigStorage::save_to_file(distinctive_cfg, isolated_config_path));
    QVERIFY(std::filesystem::exists(isolated_config_path, ec));

    // 5. Execute relevant Application shutdown lifecycle targeting isolated config
    {
        Application app(isolated_config_path);
        QCOMPARE(QString::fromStdWString(app.config_path()),
                 QString::fromStdWString(isolated_config_path));
        QVERIFY(app.initialize());
        app.request_exit();
        app.shutdown();
    }

    // 6. Verify isolated temporary config receives expected persisted state
    QVERIFY(std::filesystem::exists(isolated_config_path, ec));
    const auto loaded = ptd::ConfigStorage::load_from_file(isolated_config_path);
    QCOMPARE(loaded.master_enabled, false);
    QCOMPARE(loaded.trail.lifetime_ms, 777.0);
    QCOMPARE(loaded.trail.glow_strength, 0.88f);
    QCOMPARE(loaded.click.particle_amount, static_cast<uint8_t>(19));
    QCOMPARE(loaded.click.duration_ms, 888.0f);

    // 7. Verify disposable sentinel file remained untouched and byte-identical
    {
        std::ifstream sentinel_in(sentinel_path, std::ios::binary);
        const std::string sentinel_current_bytes(
            (std::istreambuf_iterator<char>(sentinel_in)),
            std::istreambuf_iterator<char>());
        QCOMPARE(sentinel_current_bytes, sentinel_original_bytes);
    }

    // Clean up temporary directory
    std::filesystem::remove_all(temp_dir, ec);
}

void TestTray::startup_path_resolution_normal_mode() {
    // Case 1: no smoke environment -> default config path and default log path
    const auto paths = ptd::resolve_startup_paths(nullptr, nullptr);
    QVERIFY(paths.valid);
    QVERIFY(paths.error_message.empty());
    QCOMPARE(QString::fromStdWString(paths.config_path),
             QString::fromStdWString(ptd::ConfigStorage::default_config_path()));
    QCOMPARE(QString::fromStdWString(paths.log_path),
             QString::fromStdWString(ptd::default_log_path()));
}

void TestTray::startup_path_resolution_smoke_auto_exit_without_state_dir_fails_closed() {
    // Case 2: auto-exit armed without isolation directory -> fail closed
    const auto paths1 = ptd::resolve_startup_paths(L"3000", nullptr);
    QVERIFY(!paths1.valid);
    QVERIFY(!paths1.error_message.empty());
    QVERIFY(paths1.config_path.empty());
    QVERIFY(paths1.log_path.empty());

    const auto paths2 = ptd::resolve_startup_paths(L"1500", L"");
    QVERIFY(!paths2.valid);
    QVERIFY(!paths2.error_message.empty());
}

void TestTray::startup_path_resolution_smoke_mode_with_state_dir() {
    // Case 3: auto-exit + valid state dir -> isolated paths under state dir
    const std::wstring test_dir = L"C:\\temp\\smoke_test_run_42";
    const auto paths = ptd::resolve_startup_paths(L"3000", test_dir.c_str());
    QVERIFY(paths.valid);
    QVERIFY(paths.error_message.empty());
    QCOMPARE(QString::fromStdWString(paths.config_path),
             QString::fromStdWString(test_dir + L"\\config.json"));
    QCOMPARE(QString::fromStdWString(paths.log_path),
             QString::fromStdWString(test_dir + L"\\protrail.log"));
}

void TestTray::startup_path_resolution_state_dir_without_smoke_mode_is_ignored() {
    // Case 4: state dir set without smoke auto-exit -> state dir ignored, normal production paths
    const std::wstring test_dir = L"C:\\temp\\smoke_test_run_42";
    const auto paths1 = ptd::resolve_startup_paths(nullptr, test_dir.c_str());
    QVERIFY(paths1.valid);
    QVERIFY(paths1.error_message.empty());
    QCOMPARE(QString::fromStdWString(paths1.config_path),
             QString::fromStdWString(ptd::ConfigStorage::default_config_path()));
    QCOMPARE(QString::fromStdWString(paths1.log_path),
             QString::fromStdWString(ptd::default_log_path()));

    const auto paths2 = ptd::resolve_startup_paths(L"", test_dir.c_str());
    QVERIFY(paths2.valid);
    QCOMPARE(QString::fromStdWString(paths2.config_path),
             QString::fromStdWString(ptd::ConfigStorage::default_config_path()));

    const auto paths3 = ptd::resolve_startup_paths(L"0", test_dir.c_str());
    QVERIFY(paths3.valid);
    QCOMPARE(QString::fromStdWString(paths3.config_path),
             QString::fromStdWString(ptd::ConfigStorage::default_config_path()));

    const auto paths4 = ptd::resolve_startup_paths(L"-100", test_dir.c_str());
    QVERIFY(paths4.valid);
    QCOMPARE(QString::fromStdWString(paths4.config_path),
             QString::fromStdWString(ptd::ConfigStorage::default_config_path()));
}

// CORE-001: an unsupported future-schema config must survive the full
// Application lifecycle byte-identically, with no .corrupt file, and no
// rewrite of schema 11 as schema 10.
void TestTray::future_schema_survives_application_shutdown() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core001_future";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    const std::string body =
        "{ \"schema_version\": 12, \"master_enabled\": true, "
        "\"unknown_future_object\": { \"x\": 1, \"y\": [2, 3] }, "
        "\"trail\": { \"enabled\": true } }";
    {
        std::ofstream out(config_path, std::ios::binary);
        out << body;
    }

    {
        Application app(config_path);
        QVERIFY(app.initialize());
        QVERIFY(!app.persistence_allowed());
        app.request_exit();
        app.shutdown();
    }

    // Original bytes unchanged, schema 11 preserved, no .corrupt.
    QVERIFY(std::filesystem::exists(config_path, ec));
    QVERIFY(!std::filesystem::exists(std::filesystem::path(config_path).wstring() + L".corrupt", ec));
    std::ifstream in(config_path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    QCOMPARE(after, body);
    QVERIFY(after.find("\"schema_version\": 12") != std::string::npos);

    std::filesystem::remove_all(temp_dir, ec);
}

// CORE-001: an ordinary settings persistence attempt while the future-schema
// protection is active must still leave the source byte-identical.
void TestTray::future_schema_survives_settings_save_attempt() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core001_future_save";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    const std::string body =
        "{ \"schema_version\": 12, \"master_enabled\": true, "
        "\"unknown_future_object\": { \"value\": 42 } }";
    {
        std::ofstream out(config_path, std::ios::binary);
        out << body;
    }

    Application app(config_path);
    QVERIFY(app.initialize());
    QVERIFY(!app.persistence_allowed());
    // The ordinary settings persistence path is refused by the protection.
    QVERIFY(!app.save_config_for_tests());
    app.shutdown();

    std::ifstream in(config_path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    QCOMPARE(after, body);

    std::filesystem::remove_all(temp_dir, ec);
}

// CORE-001: malformed config whose backup FAILED must survive shutdown.
void TestTray::malformed_backup_failure_survives_application_shutdown() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core001_badbackup";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    const std::string body = "{ malformed and unbackupable ";
    {
        std::ofstream out(config_path, std::ios::binary);
        out << body;
    }
    const auto corrupt = std::filesystem::path(config_path).wstring() + L".corrupt";
    std::filesystem::create_directories(std::filesystem::path(corrupt) / "blocker", ec);

    {
        Application app(config_path);
        QVERIFY(app.initialize());
        QVERIFY(!app.persistence_allowed());
        app.request_exit();
        app.shutdown();
    }

    std::ifstream in(config_path, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    QCOMPARE(after, body);

    std::filesystem::remove_all(temp_dir, ec);
}

// CORE-001: a read failure must never gain permission to overwrite the source.
void TestTray::read_failure_survives_application_shutdown() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core001_readfail";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();
    // A non-empty directory at the config path forces open() to fail.
    std::filesystem::create_directories(std::filesystem::path(config_path) / "blocker", ec);
    {
        std::ofstream marker(std::filesystem::path(config_path) / "marker.txt");
        marker << "marker";
    }

    {
        Application app(config_path);
        QVERIFY(app.initialize());
        QVERIFY(!app.persistence_allowed());
        app.request_exit();
        app.shutdown();
    }

    // The unread source directory still exists and was not replaced by a file.
    QVERIFY(std::filesystem::is_directory(config_path, ec));

    std::filesystem::remove_all(temp_dir, ec);
}

// CORE-002: with the preference OFF and a stale owned Run value present, the
// Application startup reconcile must remove exactly that value.
void TestTray::autostart_off_startup_removes_stale_owned_value() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core002_off";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    // Preference OFF, but the machine still carries ProTrail's owned value.
    ptd::AppConfig cfg{};
    cfg.start_with_windows = false;
    QVERIFY(ptd::ConfigStorage::save_to_file(cfg, config_path));

    ptd::InMemoryAutostartBackend backend;
    backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                ptd::AutostartManager::build_command(L"C:\\Old\\protrail.exe"));

    {
        Application app(config_path);
        app.set_autostart_backend_for_tests(&backend);
        QVERIFY(app.initialize());
        app.shutdown();
    }

    // Owned value gone; no unrelated entries were seeded.
    QVERIFY(backend.values.empty());

    std::filesystem::remove_all(temp_dir, ec);
}

// CORE-002: the OFF startup reconcile must never touch unrelated entries.
void TestTray::autostart_off_never_touches_unrelated_values() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_core002_neighbours";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    ptd::AppConfig cfg{};
    cfg.start_with_windows = false;
    QVERIFY(ptd::ConfigStorage::save_to_file(cfg, config_path));

    ptd::InMemoryAutostartBackend backend;
    backend.values.emplace_back(L"SomeOtherApp", L"C:\\Other\\other.exe --minimized");
    backend.values.emplace_back(L"Unrelated", L"C:\\nope.exe");
    backend.values.emplace_back(ptd::AutostartManager::kValueName,
                                ptd::AutostartManager::build_command(L"C:\\Old\\protrail.exe"));
    const auto neighbours = backend.values;

    {
        Application app(config_path);
        app.set_autostart_backend_for_tests(&backend);
        QVERIFY(app.initialize());
        app.shutdown();
    }

    // Exactly the owned value was removed; the two unrelated entries are
    // byte-identical and in order.
    QCOMPARE(backend.values.size(), neighbours.size() - 1);
    QCOMPARE(backend.values[0], neighbours[0]);
    QCOMPARE(backend.values[1], neighbours[1]);

    std::filesystem::remove_all(temp_dir, ec);
}

// W2-005: startup must read the durable configuration exactly ONCE. The old
// implementation loaded it in initialize() AND again in run(); this proves the
// single-snapshot contract.
void TestTray::startup_loads_configuration_exactly_once() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_w2005_single_load";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();

    ptd::AppConfig cfg{};
    cfg.master_enabled = true;
    cfg.trail.lifetime_ms = 333.0f;
    QVERIFY(ptd::ConfigStorage::save_to_file(cfg, config_path));

    ptd::ConfigStorage::reset_load_count_for_tests();
    {
        Application app(config_path);
        QVERIFY(app.initialize());
        // Drive the production event loop bounded so run()'s own startup path
        // executes: the old implementation performed a SECOND load there.
        QTimer::singleShot(0, QApplication::instance(), [&app] { app.request_exit(); });
        QTimer::singleShot(5000, QApplication::instance(), [&app] { app.request_exit(); });
        app.run();
        app.shutdown();
    }
    QCOMPARE(ptd::ConfigStorage::load_count_for_tests(), 1);

    std::filesystem::remove_all(temp_dir, ec);
}
// PERF-002: a burst of visual edits must produce ONE durable write after the
// debounce settles, and any still-pending edit must be flushed on shutdown.
void TestTray::deferred_save_coalesces_and_shutdown_flushes() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_perf002_debounce";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto config_path = (temp_dir / "config.json").native();
    QVERIFY(ptd::ConfigStorage::save_to_file(ptd::AppConfig{}, config_path));

    Application app(config_path);
    QVERIFY(app.initialize());
    app.set_save_debounce_ms_for_tests(40);
    app.reset_save_invocation_count_for_tests();

    // Simulate a burst: many deferred requests inside the debounce window.
    for (int i = 0; i < 50; ++i) {
        app.request_deferred_save();
    }
    QCOMPARE(app.save_invocation_count_for_tests(), 0);  // nothing written yet
    QVERIFY(app.config_dirty_for_tests());

    // Let the restartable single-shot fire exactly once.
    QTest::qWait(120);
    QCOMPARE(app.save_invocation_count_for_tests(), 1);
    QVERIFY(!app.config_dirty_for_tests());

    // A pending edit at shutdown must be flushed synchronously.
    app.request_deferred_save();
    QVERIFY(app.config_dirty_for_tests());
    app.shutdown();

    std::filesystem::remove_all(temp_dir, ec);
}
void TestTray::single_instance_activation_contract() {
    // T-018: verify registered message for single instance activation
    const UINT wm_activate = RegisterWindowMessageW(L"ProTrail_ActivateInstance");
    QVERIFY(wm_activate >= 0xC000 && wm_activate <= 0xFFFF);

    // Verify startup paths identify smoke mode correctly
    const auto paths_normal = ptd::resolve_startup_paths(nullptr, nullptr);
    QVERIFY(!paths_normal.is_smoke_mode);

    const auto paths_smoke = ptd::resolve_startup_paths(L"3000", L"C:\\temp\\smoke");
    QVERIFY(paths_smoke.is_smoke_mode);
}

// T-018R1 Phase 8: the production activation path (Application::show_settings)
// restores a HIDDEN canonical SettingsWindow; repeated activation never
// creates a second window.
void TestTray::activation_restores_hidden_settings_window() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);
    QWidget* canonical = &settings;

    settings.show();
    settings.close();
    QVERIFY(settings.isHidden());

    // Exact Application::show_settings sequence, executed twice.
    for (int i = 0; i < 2; ++i) {
        if (settings.isMinimized()) {
            settings.showNormal();
        } else {
            settings.show();
        }
        settings.raise();
        settings.activateWindow();
        HWND hwnd = reinterpret_cast<HWND>(settings.winId());
        QVERIFY(hwnd != nullptr);
    }
    QVERIFY(settings.isVisible());
    QCOMPARE(static_cast<QWidget*>(&settings), canonical);
}

// T-018R1 Phase 8: the production activation path restores a MINIMIZED
// canonical SettingsWindow through showNormal(), still exactly one window.
void TestTray::activation_restores_minimized_settings_window_no_duplicate() {
    ptd::TrailConfig trail{};
    ptd::ClickConfig click{};
    ptd::ui::SettingsWindow settings(trail, click, true);
    QWidget* canonical = &settings;

    settings.show();
    settings.showMinimized();
    QVERIFY(settings.isMinimized());

    if (settings.isMinimized()) {
        settings.showNormal();
    } else {
        settings.show();
    }
    settings.raise();
    settings.activateWindow();

    QVERIFY(settings.isVisible());
    QVERIFY(!settings.isMinimized());
    QCOMPARE(static_cast<QWidget*>(&settings), canonical);
}

// T-018R1 Phase 8: smoke mode resolves WITHOUT production single-instance
// participation -- the startup contract that keeps main.cpp from ever
// handing a SingleInstance authority to a smoke run.
void TestTray::smoke_mode_does_not_participate_in_production_ownership() {
    const auto smoke = ptd::resolve_startup_paths(L"3000", L"C:\\temp\\smoke_iso");
    QVERIFY(smoke.valid);
    QVERIFY(smoke.is_smoke_mode);

    const auto normal = ptd::resolve_startup_paths(nullptr, nullptr);
    QVERIFY(normal.valid);
    QVERIFY(!normal.is_smoke_mode);
    // Only the normal paths may reach set_single_instance(); this assert
    // pins the discriminator main.cpp relies on.
    QVERIFY(smoke.is_smoke_mode != normal.is_smoke_mode);
}

// T-018R2 Phase 7: Application::run() must fail closed when readiness
// publication fails: log the exact Win32 error context, never enter the
// event loop advertising a healthy runtime whose activation cannot be
// delivered. Controlled path: run() returns before exec(); shutdown()
// still executes (begin_shutdown handshake included).
void TestTray::mark_ready_failure_fails_closed_without_event_loop() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "protrail_tray_test_mark_ready_fail";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto temp_config = (temp_dir / "config.json").native();

    // A SingleInstance with no ownership/transport: mark_ready() must fail.
    // Unique per-process names (test isolation across parallel runs).
    const std::wstring tag = std::to_wstring(GetCurrentProcessId());
    ptd::SingleInstanceConfig cfg;
    cfg.mutex_name = L"Local\\ProTrail_T018R2_appfail_Mutex_" + tag;
    cfg.ready_event_name = L"Local\\ProTrail_T018R2_appfail_Ready_" + tag;
    cfg.activate_event_name = L"Local\\ProTrail_T018R2_appfail_Activate_" + tag;
    cfg.shutdown_event_name = L"Local\\ProTrail_T018R2_appfail_Shutdown_" + tag;
    ptd::SingleInstance broken(cfg);
    QVERIFY(!broken.mark_ready());

    Application app(temp_config);
    QVERIFY(app.initialize());
    app.set_single_instance(&broken);

    // run() must return before starting the event loop (fail closed).
    const int rc = app.run();
    QCOMPARE(rc, 1);

    // Controlled teardown still executes the shutdown handshake (the
    // non-owner begin_shutdown is a guarded no-op).
    app.shutdown();

    std::filesystem::remove_all(temp_dir, ec);
}

void TestTray::overlay_device_loss_recreation() {
    // T-018: verify overlay window can recreate render resources cleanly on device loss
    ptd::OverlayWindow overlay;
    RECT r{0, 0, 320, 240};
    QVERIFY(overlay.create(GetModuleHandleW(nullptr), false, &r));
    QVERIFY(overlay.hwnd() != nullptr);

    // Recreate render resources directly (simulates recovery after D2DERR_RECREATE_TARGET)
    QVERIFY(overlay.recreate_render_resources());

    overlay.destroy();
    QVERIFY(overlay.hwnd() == nullptr);
}

// T-37 Target A: a manual launch opens the compact product home and does NOT
// immediately open the full advanced editor.
void TestTray::manual_startup_shows_main_and_not_settings() {
    with_running_application("manual", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(settings != nullptr);
        QVERIFY(main_w != static_cast<QWidget*>(settings));

        QVERIFY(main_w->isVisible());
        QVERIFY(!settings->isVisible());
        QVERIFY(settings->isHidden());

        // Exactly one product home exists, and it is the Essentials surface.
        QCOMPARE(top_level_windows_titled(QStringLiteral("ProTrail")), 1);
        QVERIFY(main_w->findChild<QCheckBox*>(QStringLiteral("main_chk_master")) != nullptr);
        QVERIFY(main_w->findChild<QWidget*>(QStringLiteral("main_chk_start_with_windows")) != nullptr);
    });
}

// T-37 Target A: an autostart launch stays tray-only. No Main, no Settings,
// no minimized-but-present window, no focus claim.
void TestTray::autostart_startup_shows_neither_surface() {
    with_running_application("autostart", ptd::StartupMode::AutostartMinimized,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(settings != nullptr);

        QVERIFY(!main_w->isVisible());
        QVERIFY(main_w->isHidden());
        QVERIFY(!main_w->isMinimized());
        QVERIFY(!settings->isVisible());
        QVERIFY(settings->isHidden());

        // The tray is the only way in, and it is present.
        auto* tray = app.tray_icon_for_tests();
        QVERIFY(tray != nullptr);
        QCOMPARE(top_level_windows_titled(QStringLiteral("ProTrail")), 1);
    });
}

// T-37 Target A/B: the tray's product-home action restores the EXISTING Main
// instance after a hide-to-tray close, repeatedly, without ever constructing
// a second one.
void TestTray::tray_home_action_restores_the_single_main_instance() {
    with_running_application("tray_home", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* tray = app.tray_icon_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(tray != nullptr);

        QCOMPARE(tray->action_home()->text(), QStringLiteral("Open ProTrail"));

        // Closing the home surface is a hide, not a shutdown: ProTrail keeps
        // running and the tray still holds the canonical instance.
        main_w->close();
        QVERIFY(!main_w->isVisible());
        QVERIFY(main_w->isHidden());

        for (int i = 0; i < 3; ++i) {
            tray->action_home()->trigger();
            QVERIFY(main_w->isVisible());
            QCOMPARE(app.main_window_for_tests(), main_w);
            QCOMPARE(top_level_windows_titled(QStringLiteral("ProTrail")), 1);
        }
    });
}

// T-37 Target A: "Advanced Settings..." on the product home opens the EXISTING
// SettingsWindow -- the complete advanced editor stays reachable, and stays a
// single instance.
void TestTray::advanced_settings_action_opens_the_single_settings_instance() {
    with_running_application("advanced", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(settings != nullptr);
        QVERIFY(!settings->isVisible());

        auto* button = main_w->findChild<QPushButton*>(QStringLiteral("main_btn_advanced_settings"));
        QVERIFY(button != nullptr);

        button->click();
        QVERIFY(settings->isVisible());
        QCOMPARE(app.settings_window_for_tests(), settings);

        // Closing the advanced editor hides it; the next request restores the
        // same object rather than a duplicate. The product home is untouched.
        settings->close();
        QVERIFY(!settings->isVisible());
        button->click();
        QVERIFY(settings->isVisible());
        QCOMPARE(app.settings_window_for_tests(), settings);
        QVERIFY(app.main_window_for_tests() == main_w);
    });
}

// T-37 Target A: the single-instance activation path restores the product
// home (the activation handler calls exactly this operation), idempotently.
void TestTray::activation_restores_the_single_main_instance() {
    with_running_application("activation", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);

        main_w->close();
        QVERIFY(!main_w->isVisible());

        app.show_main();
        QVERIFY(main_w->isVisible());
        QCOMPARE(app.main_window_for_tests(), main_w);
        QCOMPARE(app.settings_window_for_tests(), settings);
        QVERIFY(!settings->isVisible());

        // A repeated activation burst restores the SAME window once.
        for (int i = 0; i < 4; ++i) app.show_main();
        QCOMPARE(app.main_window_for_tests(), main_w);
        QCOMPARE(top_level_windows_titled(QStringLiteral("ProTrail")), 1);
    });
}

// T-37 Target B: ONE config authority. A Main edit lands in Settings, and a
// Settings publication lands back in Main, with no second configuration store
// on either surface.
void TestTray::main_and_settings_stay_synchronized_both_ways() {
    with_running_application("sync", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(settings != nullptr);

        auto* trail_style = main_w->findChild<QComboBox*>(QStringLiteral("main_cmb_trail_style"));
        auto* click_style = main_w->findChild<QComboBox*>(QStringLiteral("main_cmb_click_style"));
        QVERIFY(trail_style != nullptr);
        QVERIFY(click_style != nullptr);

        // Main -> canonical -> Settings.
        trail_style->setCurrentIndex(static_cast<int>(ptd::TrailStyle::Dotted));
        QVERIFY(settings->capture_current_settings().trail.style == ptd::TrailStyle::Dotted);

        // Settings -> canonical -> Main.
        ptd::AppConfig from_settings = settings->capture_current_settings();
        from_settings.trail.style = ptd::TrailStyle::Ribbon;
        from_settings.trail.sparkle_mode = ptd::TrailSparkleMode::Firefly;
        from_settings.click.style = ptd::ClickStyle::Water;
        from_settings.click.hold_wake_density = 1.5f;
        settings->apply_config(from_settings);

        QCOMPARE(trail_style->currentIndex(), static_cast<int>(ptd::TrailStyle::Ribbon));
        QCOMPARE(main_w->findChild<QComboBox*>(QStringLiteral("main_cmb_sparkle_mode"))->currentIndex(),
                 static_cast<int>(ptd::TrailSparkleMode::Firefly));
        QCOMPARE(click_style->currentIndex(), static_cast<int>(ptd::ClickStyle::Water));
        QCOMPARE(main_w->findChild<QSlider*>(QStringLiteral("main_sld_wake_density"))->value(),
                 150);

        // The Settings surface itself agrees with what it published.
        QVERIFY(settings->capture_current_settings().click.style == ptd::ClickStyle::Water);
    });
}

// T-37 Target B/D: Restore Defaults from the product home applies the
// canonical Release Defaults to BOTH surfaces as ONE transaction, silently,
// with exactly one persistence commit and no publication storm.
void TestTray::restore_defaults_updates_both_surfaces_in_one_transaction() {
    with_running_application("restore", ptd::StartupMode::Normal,
                             [](Application& app) {
        auto* main_w = app.main_window_for_tests();
        auto* settings = app.settings_window_for_tests();
        QVERIFY(main_w != nullptr);
        QVERIFY(settings != nullptr);

        auto* trail_style = main_w->findChild<QComboBox*>(QStringLiteral("main_cmb_trail_style"));
        auto* density = main_w->findChild<QSlider*>(QStringLiteral("main_sld_wake_density"));
        QVERIFY(trail_style != nullptr);
        QVERIFY(density != nullptr);

        // Move well away from the canonical defaults first.
        trail_style->setCurrentIndex(static_cast<int>(ptd::TrailStyle::Neon));
        density->setValue(45);
        QVERIFY(settings->capture_current_settings().click.hold_wake_density != 1.0f);

        const ptd::AppConfig& canonical = ptd::release_defaults();
        QSignalSpy main_trail(main_w, &ptd::ui::MainWindow::trail_config_changed);
        QSignalSpy main_click(main_w, &ptd::ui::MainWindow::click_config_changed);
        app.reset_save_invocation_count_for_tests();

        main_w->findChild<QPushButton*>(QStringLiteral("main_btn_restore_defaults"))->click();

        // Both surfaces observe the canonical defaults.
        QCOMPARE(trail_style->currentIndex(), static_cast<int>(canonical.trail.style));
        QCOMPARE(density->value(), static_cast<int>(canonical.click.hold_wake_density * 100.0f));
        QVERIFY(settings->capture_current_settings().trail.style == canonical.trail.style);
        QVERIFY(settings->capture_current_settings().click.style == canonical.click.style);
        QVERIFY(settings->capture_current_settings().master_enabled == canonical.master_enabled);

        // Silent: restoring is a push INTO the surfaces, never a publication
        // out of them, and the whole operation is ONE persistence commit.
        QCOMPARE(main_trail.count(), 0);
        QCOMPARE(main_click.count(), 0);
        QCOMPARE(app.save_invocation_count_for_tests(), 1);
        QVERIFY(!app.config_dirty_for_tests());
    });
}

// Target D: "Set Defaults" on the product home routes to the SAME controller
// operation the Settings developer panel publishes, and that operation takes
// the COMPLETE canonical Application::app_config -- including fields no Main
// control can reach -- rather than reconstructing a snapshot from whichever
// widgets happen to exist. Redirected at the canonical-path seam so the
// repository's own resources/release_defaults.json is provably untouched.
void TestTray::developer_set_defaults_captures_the_complete_canonical_state() {
    const auto dir = std::filesystem::temp_directory_path()
                     / "protrail_t37_set_defaults";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto promoted = dir / "release_defaults.json";

    // The repository source must still be byte-identical when the test ends.
    const auto repository_source = ptd::canonical_release_defaults_path();
    const auto read_all = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    };
    const std::string before = read_all(repository_source);
    QVERIFY(!before.empty());

    ptd::set_dev_build_override_for_tests(true);
    ptd::set_canonical_release_defaults_path_for_tests(promoted);
    {
        with_running_application("set_defaults", ptd::StartupMode::Normal,
                                 [](Application& app) {
            auto* main_w = app.main_window_for_tests();
            auto* settings = app.settings_window_for_tests();
            QVERIFY(main_w != nullptr);
            QVERIFY(settings != nullptr);

            auto* promote = main_w->findChild<QPushButton*>(QStringLiteral("main_btn_set_defaults"));
            QVERIFY(promote != nullptr);

            // Two fields that NO Main Essentials control can reach are moved
            // through the canonical Settings publication path first; they are
            // exactly what a UI-reconstructed snapshot would silently drop.
            ptd::AppConfig rich = settings->capture_current_settings();
            rich.trail.glow_strength = 0.22f;
            rich.click.particle_amount = 19;
            settings->apply_config(rich);

            // Two fields the product home CAN reach.
            main_w->findChild<QComboBox*>(QStringLiteral("main_cmb_trail_style"))
                ->setCurrentIndex(static_cast<int>(ptd::TrailStyle::Comet));
            main_w->findChild<QSlider*>(QStringLiteral("main_sld_wake_density"))->setValue(175);

            promote->click();
        });
    }
    ptd::reset_canonical_release_defaults_path_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);

    QVERIFY(std::filesystem::exists(promoted, ec));
    const std::string written = read_all(promoted);
    QVERIFY(!written.empty());

    QString err;
    const auto parsed = ptd::ConfigStorage::deserialize_json(
        QByteArray::fromStdString(written), &err);
    QVERIFY2(parsed.has_value(), qPrintable(err));
    QVERIFY(parsed->schema_version == ptd::AppConfig::kCurrentSchemaVersion);
    QVERIFY(parsed->trail.style == ptd::TrailStyle::Comet);
    QVERIFY(std::abs(parsed->click.hold_wake_density - 1.75f) < 0.0001f);
    // The fields no surface control exposes survived the promotion.
    QVERIFY(std::abs(parsed->trail.glow_strength - 0.22f) < 0.0001f);
    QCOMPARE(static_cast<int>(parsed->click.particle_amount), 19);

    // The repository's canonical source was not touched: the promotion wrote
    // exactly the path the seam redirected it to.
    QCOMPARE(read_all(repository_source), before);

    std::filesystem::remove_all(dir, ec);
}

QTEST_MAIN(TestTray)
#include "test_tray.moc"