// T-37 Main Essentials surface regression.
//
// These are DIRECT, deterministic regressions for the compact product-home
// surface itself: widget identity, silent population, one-publication-per-
// action, the semantic field each control owns, the contextual enable states
// and the tray-shaped close contract. The cross-surface and startup
// contracts (which surface a startup mode shows, Main <-> Settings
// synchronization, Restore Defaults) live in protrail_tray_tests, where the
// production Application wiring exists.
#include "../src/ui/main_window.h"
#include "../src/config/dev_defaults.h"

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QSignalSpy>

Q_DECLARE_METATYPE(ptd::TrailConfig)
Q_DECLARE_METATYPE(ptd::ClickConfig)

namespace {

// A legal, fully-enabled, deliberately non-default snapshot: enabled children
// are required for the contextual enable-state contract to be observable, and
// non-default values make an accidental cross-field write visible instead of
// hiding behind a default that happens to equal it.
ptd::AppConfig enabled_base_config() {
    ptd::AppConfig base = ptd::AppConfig::validated(ptd::AppConfig{});
    base.master_enabled = true;
    base.start_with_windows = false;
    base.trail.enabled = true;
    base.trail.style = ptd::TrailStyle::Classic;
    base.trail.sparkle_mode = ptd::TrailSparkleMode::Off;
    base.click.enabled = true;
    base.click.style = ptd::ClickStyle::Ring;
    base.click.hold_enabled = true;
    base.click.hold_wake_enabled = true;
    base.click.hold_intensity = 1.0f;
    base.click.hold_wake_density = 1.0f;
    // Fields the Main surface never edits, moved off their defaults so a
    // control that writes a neighbour is caught by the whole-struct
    // comparison instead of passing on a publication count alone.
    base.trail.lifetime_ms = 480.0f;
    base.trail.glow_strength = 0.31f;
    base.trail.sparkle_amount = 0.33f;
    base.click.particle_amount = 11;
    base.click.duration_ms = 275.0f;
    return ptd::AppConfig::validated(base);
}

} // namespace

class TestMainWindow final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void controls_have_stable_names();
    void developer_action_follows_the_dev_build_authority();
    void population_is_silent();
    void each_user_control_publishes_once();
    void each_control_changes_only_its_intended_field();
    void master_hold_and_motion_wake_enable_states();
    void navigation_actions_are_explicit();
    void close_hides_instead_of_destroying();
};

void TestMainWindow::initTestCase() {
    qRegisterMetaType<ptd::TrailConfig>("ptd::TrailConfig");
    qRegisterMetaType<ptd::ClickConfig>("ptd::ClickConfig");
}

void TestMainWindow::cleanupTestCase() {
    // Never leave a build-mode override behind for another test file/target.
    ptd::set_dev_build_override_for_tests(std::nullopt);
}

// Clause 1: every Essential widget exists under a stable object name, so a
// later refactor cannot silently rename a control out from under the
// controller wiring or an accessibility/automation layer.
void TestMainWindow::controls_have_stable_names() {
    ptd::ui::MainWindow w;
    const QStringList names = {
        "main_chk_master", "main_chk_trail", "main_cmb_trail_style",
        "main_cmb_sparkle_mode", "main_chk_click", "main_cmb_click_style",
        "main_chk_hold", "main_chk_motion_wake", "main_sld_wake_density",
        "main_sld_hold_intensity", "main_chk_start_with_windows",
        "main_btn_advanced_settings", "main_btn_restore_defaults"};
    for (const auto& name : names) {
        QVERIFY2(w.findChild<QWidget*>(name) != nullptr, qPrintable(name));
    }
}

// Target D: the developer-only defaults action is present exactly when the
// product's ONE dev-build authority says so. A production Release build must
// not carry it at all -- not merely have it disabled.
void TestMainWindow::developer_action_follows_the_dev_build_authority() {
    const auto action = [](ptd::ui::MainWindow& w) {
        return w.findChild<QPushButton*>(QStringLiteral("main_btn_set_defaults"));
    };

    ptd::set_dev_build_override_for_tests(true);
    {
        ptd::ui::MainWindow w;
        QVERIFY(action(w) != nullptr);
        QSignalSpy requested(&w, &ptd::ui::MainWindow::set_current_as_release_defaults_requested);
        action(w)->click();
        QCOMPARE(requested.count(), 1);
        // The visible label is short by design; the tooltip must disambiguate.
        QVERIFY(action(w)->toolTip().contains(QStringLiteral("Release Defaults")));
    }

    ptd::set_dev_build_override_for_tests(false);
    {
        ptd::ui::MainWindow w;
        QVERIFY(action(w) == nullptr);
    }

    ptd::set_dev_build_override_for_tests(std::nullopt);
}

// Clause 2: programmatic population emits NOTHING. A config load, a Settings
// edit or a Restore Defaults must not loop back into a publication, a save or
// a callback chain.
void TestMainWindow::population_is_silent() {
    ptd::ui::MainWindow w;
    QSignalSpy master(&w, &ptd::ui::MainWindow::master_enabled_changed);
    QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
    QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
    QSignalSpy startup(&w, &ptd::ui::MainWindow::start_with_windows_changed);
    QSignalSpy advanced(&w, &ptd::ui::MainWindow::advanced_settings_requested);
    QSignalSpy restore(&w, &ptd::ui::MainWindow::restore_defaults_requested);

    ptd::AppConfig cfg = enabled_base_config();
    cfg.trail.style = ptd::TrailStyle::Neon;
    cfg.click.style = ptd::ClickStyle::Fire;
    cfg.click.hold_wake_density = 0.45f;
    w.set_from_app_config(cfg);

    QCOMPARE(master.count(), 0);
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 0);
    QCOMPARE(startup.count(), 0);
    QCOMPARE(advanced.count(), 0);
    QCOMPARE(restore.count(), 0);

    // Re-populating with the SAME snapshot is equally silent (no edge fires).
    w.set_from_app_config(cfg);
    QCOMPARE(master.count(), 0);
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 0);
    QCOMPARE(startup.count(), 0);
}

// Clauses 4: one user operation produces exactly one publication on the
// surface, and on no other signal family.
void TestMainWindow::each_user_control_publishes_once() {
    ptd::ui::MainWindow w;
    w.set_from_app_config(enabled_base_config());
    QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
    QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
    QSignalSpy master(&w, &ptd::ui::MainWindow::master_enabled_changed);
    QSignalSpy startup(&w, &ptd::ui::MainWindow::start_with_windows_changed);

    w.findChild<QComboBox*>("main_cmb_trail_style")->setCurrentIndex(3);
    QCOMPARE(trail.count(), 1);
    QCOMPARE(click.count(), 0);
    w.findChild<QComboBox*>("main_cmb_sparkle_mode")->setCurrentIndex(2);
    QCOMPARE(trail.count(), 2);
    w.findChild<QComboBox*>("main_cmb_click_style")->setCurrentIndex(1);
    QCOMPARE(click.count(), 1);
    QCOMPARE(trail.count(), 2);
    w.findChild<QCheckBox*>("main_chk_trail")->setChecked(false);
    QCOMPARE(trail.count(), 3);
    w.findChild<QCheckBox*>("main_chk_click")->setChecked(false);
    QCOMPARE(click.count(), 2);
    w.findChild<QCheckBox*>("main_chk_hold")->setChecked(false);
    QCOMPARE(click.count(), 3);
    w.findChild<QCheckBox*>("main_chk_motion_wake")->setChecked(false);
    QCOMPARE(click.count(), 4);
    w.findChild<QSlider*>("main_sld_wake_density")->setValue(45);
    QCOMPARE(click.count(), 5);
    w.findChild<QSlider*>("main_sld_hold_intensity")->setValue(55);
    QCOMPARE(click.count(), 6);
    w.findChild<QCheckBox*>("main_chk_master")->setChecked(false);
    QCOMPARE(master.count(), 1);
    w.findChild<QCheckBox*>("main_chk_start_with_windows")->setChecked(true);
    QCOMPARE(startup.count(), 1);

    // Repeating the same value is NOT a change: no second publication.
    w.findChild<QComboBox*>("main_cmb_trail_style")->setCurrentIndex(3);
    QCOMPARE(trail.count(), 3);
    w.findChild<QSlider*>("main_sld_wake_density")->setValue(45);
    QCOMPARE(click.count(), 6);
}

// Clause 3: each user control changes EXACTLY its intended semantic field.
// The emitted payload is compared against the pre-action snapshot with only
// that one field moved, so a control that quietly rewrites a neighbour fails
// instead of passing on a publication count alone.
void TestMainWindow::each_control_changes_only_its_intended_field() {
    const ptd::AppConfig base = enabled_base_config();

    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
        w.findChild<QComboBox*>("main_cmb_trail_style")->setCurrentIndex(3);
        QCOMPARE(trail.count(), 1);
        ptd::AppConfig expect = base;
        expect.trail.style = ptd::TrailStyle::Neon;
        QVERIFY(trail.at(0).at(0).value<ptd::TrailConfig>() == expect.trail);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
        w.findChild<QComboBox*>("main_cmb_sparkle_mode")->setCurrentIndex(2);
        QCOMPARE(trail.count(), 1);
        ptd::AppConfig expect = base;
        expect.trail.sparkle_mode = ptd::TrailSparkleMode::Twinkle;
        QVERIFY(trail.at(0).at(0).value<ptd::TrailConfig>() == expect.trail);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        // Index 10 == Earth: the highest legal ClickStyle, i.e. the case a
        // stale combo population would get wrong.
        w.findChild<QComboBox*>("main_cmb_click_style")->setCurrentIndex(10);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.style = ptd::ClickStyle::Earth;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
        w.findChild<QCheckBox*>("main_chk_trail")->setChecked(false);
        QCOMPARE(trail.count(), 1);
        ptd::AppConfig expect = base;
        expect.trail.enabled = false;
        QVERIFY(trail.at(0).at(0).value<ptd::TrailConfig>() == expect.trail);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        w.findChild<QCheckBox*>("main_chk_click")->setChecked(false);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.enabled = false;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        w.findChild<QCheckBox*>("main_chk_hold")->setChecked(false);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.hold_enabled = false;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        w.findChild<QCheckBox*>("main_chk_motion_wake")->setChecked(false);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.hold_wake_enabled = false;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        w.findChild<QSlider*>("main_sld_wake_density")->setValue(45);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.hold_wake_density = 0.45f;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
        w.findChild<QSlider*>("main_sld_hold_intensity")->setValue(175);
        QCOMPARE(click.count(), 1);
        ptd::AppConfig expect = base;
        expect.click.hold_intensity = 1.75f;
        QVERIFY(click.at(0).at(0).value<ptd::ClickConfig>() == expect.click);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy master(&w, &ptd::ui::MainWindow::master_enabled_changed);
        w.findChild<QCheckBox*>("main_chk_master")->setChecked(false);
        QCOMPARE(master.count(), 1);
        QCOMPARE(master.at(0).at(0).toBool(), false);
    }
    {
        ptd::ui::MainWindow w;
        w.set_from_app_config(base);
        QSignalSpy startup(&w, &ptd::ui::MainWindow::start_with_windows_changed);
        w.findChild<QCheckBox*>("main_chk_start_with_windows")->setChecked(true);
        QCOMPARE(startup.count(), 1);
        QCOMPARE(startup.at(0).at(0).toBool(), true);
    }
}

// Clause 7: the contextual enable states are the product's own contract --
// a child control that cannot affect anything must be visibly unavailable,
// and re-enabling the parent must restore it without a publication.
void TestMainWindow::master_hold_and_motion_wake_enable_states() {
    auto child = [](ptd::ui::MainWindow& w, const char* name) {
        auto* c = w.findChild<QWidget*>(QString::fromLatin1(name));
        Q_ASSERT(c != nullptr);
        return c;
    };

    ptd::ui::MainWindow w;
    QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
    QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);
    QSignalSpy master_spy(&w, &ptd::ui::MainWindow::master_enabled_changed);

    // Master OFF: every child is unavailable.
    ptd::AppConfig off = enabled_base_config();
    off.master_enabled = false;
    w.set_from_app_config(off);
    QVERIFY(!child(w, "main_chk_trail")->isEnabled());
    QVERIFY(!child(w, "main_chk_click")->isEnabled());
    QVERIFY(!child(w, "main_chk_hold")->isEnabled());
    QVERIFY(!child(w, "main_chk_motion_wake")->isEnabled());
    QVERIFY(!child(w, "main_sld_hold_intensity")->isEnabled());
    QVERIFY(!child(w, "main_sld_wake_density")->isEnabled());
    QVERIFY(!child(w, "main_cmb_trail_style")->isEnabled());
    QVERIFY(!child(w, "main_cmb_sparkle_mode")->isEnabled());
    QVERIFY(!child(w, "main_cmb_click_style")->isEnabled());
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 0);

    // Master ON, Click ON, Hold ON, Motion Wake ON: the whole chain is live.
    ptd::AppConfig on = enabled_base_config();
    on.click.hold_wake_enabled = true;
    w.set_from_app_config(on);
    QVERIFY(child(w, "main_chk_trail")->isEnabled());
    QVERIFY(child(w, "main_chk_click")->isEnabled());
    QVERIFY(child(w, "main_chk_hold")->isEnabled());
    QVERIFY(child(w, "main_chk_motion_wake")->isEnabled());
    QVERIFY(child(w, "main_sld_hold_intensity")->isEnabled());
    QVERIFY(child(w, "main_sld_wake_density")->isEnabled());

    // Motion Wake OFF: only the wake-density control goes dark; Hold
    // Intensity belongs to the Hold layer and must stay usable.
    w.findChild<QCheckBox*>("main_chk_motion_wake")->setChecked(false);
    QCOMPARE(click.count(), 1);
    QVERIFY(!child(w, "main_sld_wake_density")->isEnabled());
    QVERIFY(child(w, "main_sld_hold_intensity")->isEnabled());
    QVERIFY(child(w, "main_chk_motion_wake")->isEnabled());

    // Hold OFF: both Hold-layer sliders go dark, Click itself stays usable.
    w.findChild<QCheckBox*>("main_chk_hold")->setChecked(false);
    QCOMPARE(click.count(), 2);
    QVERIFY(!child(w, "main_sld_hold_intensity")->isEnabled());
    QVERIFY(!child(w, "main_sld_wake_density")->isEnabled());
    QVERIFY(child(w, "main_chk_click")->isEnabled());

    // Click OFF: its style selector and the Hold layer go dark; Trail stays on.
    w.findChild<QCheckBox*>("main_chk_click")->setChecked(false);
    QCOMPARE(click.count(), 3);
    QVERIFY(!child(w, "main_cmb_click_style")->isEnabled());
    QVERIFY(!child(w, "main_chk_hold")->isEnabled());
    QVERIFY(child(w, "main_chk_trail")->isEnabled());
    QVERIFY(child(w, "main_cmb_trail_style")->isEnabled());

    // Master OFF through the user path clears the whole surface.
    w.findChild<QCheckBox*>("main_chk_master")->setChecked(false);
    QCOMPARE(master_spy.count(), 1);
    QVERIFY(!child(w, "main_chk_trail")->isEnabled());
    QVERIFY(!child(w, "main_chk_click")->isEnabled());

    // Master ON through the user path restores it. The live path only
    // re-evaluates states; it must not publish a config.
    w.findChild<QCheckBox*>("main_chk_master")->setChecked(true);
    QCOMPARE(master_spy.count(), 2);
    QVERIFY(child(w, "main_chk_trail")->isEnabled());
    QVERIFY(child(w, "main_chk_click")->isEnabled());
    QVERIFY(w.findChild<QCheckBox*>("main_chk_trail")->isChecked());
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 3);
}

// The two navigation actions and the developer action each publish exactly
// once, and nothing else fires.
void TestMainWindow::navigation_actions_are_explicit() {
    ptd::ui::MainWindow w;
    QSignalSpy advanced(&w, &ptd::ui::MainWindow::advanced_settings_requested);
    QSignalSpy restore(&w, &ptd::ui::MainWindow::restore_defaults_requested);
    QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
    QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);

    w.findChild<QPushButton*>("main_btn_advanced_settings")->click();
    w.findChild<QPushButton*>("main_btn_restore_defaults")->click();
    QCOMPARE(advanced.count(), 1);
    QCOMPARE(restore.count(), 1);
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 0);
}

// Clause 8: closing the product home hides it and keeps ProTrail (and its
// effects) alive -- the tray application model, never an exit.
void TestMainWindow::close_hides_instead_of_destroying() {
    ptd::ui::MainWindow w;
    QSignalSpy master(&w, &ptd::ui::MainWindow::master_enabled_changed);
    QSignalSpy trail(&w, &ptd::ui::MainWindow::trail_config_changed);
    QSignalSpy click(&w, &ptd::ui::MainWindow::click_config_changed);

    w.show();
    QVERIFY(w.isVisible());
    w.close();
    QVERIFY(!w.isVisible());
    QVERIFY(w.isHidden());

    // Still the same live object with its canonical snapshot intact.
    QCOMPARE(master.count(), 0);
    QCOMPARE(trail.count(), 0);
    QCOMPARE(click.count(), 0);
    w.show();
    QVERIFY(w.isVisible());
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
