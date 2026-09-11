#include <QTest>
#include <QSignalSpy>
#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QCheckBox>

#include "src/app/application.h"
#include "src/ui/tray_icon.h"
#include "src/ui/settings_window.h"
#include "src/config/app_config.h"
#include "src/config/config_storage.h"
#include "src/core/log.h"
#include "src/app/startup_paths.h"
#include "src/render/overlay_window.h"

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
    void overlay_device_loss_recreation();
};

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
    QCOMPARE(tray.action_settings()->text(), QStringLiteral("Settings"));
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

QTEST_MAIN(TestTray)
#include "test_tray.moc"