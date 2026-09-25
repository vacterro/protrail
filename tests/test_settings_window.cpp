// T-010 MVP 05 Phase S: GUI binding tests (QtTest). Validates that
// SettingsWindow publishes validated configs, that master/enable toggles
// behave independently, that defaults restore exactly, and that slider
// and numeric input stay synchronized without signal storms.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../src/ui/settings_window.h"
#include "../src/ui/theme.h"
#include "../src/effects/effect_palette.h"
#include "../src/config/release_defaults.h"
#include "../src/config/config_storage.h"
#include "../src/config/dev_defaults.h"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <cmath>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QImage>
#include <QPainter>
#include <QStyleOption>
#include <QPixmap>
#include <QButtonGroup>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <filesystem>

#include <algorithm>

using ptd::ui::SettingsWindow;
using ptd::ui::SliderSpin;
using ptd::ui::ColorEditor;

Q_DECLARE_METATYPE(ptd::TrailConfig)
Q_DECLARE_METATYPE(ptd::ClickConfig)

// Percent UI values are integer-rounded, runtime values are float; use
// approximate comparison at the UI/config boundary.
namespace {
bool pct_close(double actual, double expected) {
    return qAbs(actual - expected) < 0.01;
}
}  // namespace

class TestSettingsWindow : public QObject {
    Q_OBJECT
private slots:
    void tab_construction_succeeds();
    void trail_checkbox_changes_only_trail_enabled();
    void click_checkbox_changes_only_click_enabled();
    void master_disable_emits_window_signals();
    void rgb_editor_updates_all_values();
    void slider_spin_synchronization();
    void defaults_restore_exact_defaults();
    void no_recursive_signal_storm();
    void config_bounds_are_sane();
    // T-010 repair regression coverage (Defect G).
    void construction_publishes_nothing();
    void construction_shows_default_trail_values();
    void construction_shows_supplied_trail_values();
    void construction_shows_default_click_values();
    void construction_shows_supplied_click_values();
    void percentage_mappings_publish_normalized_values();
    void trail_edit_preserves_unrelated_trail_fields();
    void trail_edit_does_not_publish_click_config();
    void click_edit_does_not_publish_trail_config();
    void click_start_end_sizes_stay_coherent();
    void click_end_size_cannot_drop_below_start();
    void color_editor_set_is_silent();
    void restore_trail_defaults_emits_once();
    void restore_click_defaults_emits_once_after_modifications();
    void restore_does_not_touch_other_effect();
    void restore_all_defaults_emits_once();
    void master_toggle_does_not_emit_configs();
    void checkbox_checked_has_non_color_cue();
    void rapid_edits_stay_bounded();
    void color_picker_swatch_properties_and_affordance();
    void color_picker_updates_numeric_inputs_and_swatch();
    void color_picker_trail_publishes_trail_config_only();
    void color_picker_click_publishes_click_config_only();
    void color_picker_cancellation_produces_zero_publications();
    void color_picker_application_publishes_exactly_once();
    // T-015 Phase 12: palette + dual trail color + presets coverage.
    void palette_surface_counts_names_and_focus();
    void palette_click_sets_exact_rgb_and_emits_once();
    void palette_click_publishes_trail_config_once();
    void direct_rgb_edit_updates_palette_selection();
    void custom_value_selects_no_palette_entry();
    void set_updates_palette_selection_silently();
    void trail_color_buttons_four_modes();
    void mode_editor_contextual_visibility();
    void hidden_editor_values_survive_mode_switch();
    void switching_modes_preserves_both_colors();
    void preset_buttons_match_kColorPresets();
    void preset_applies_colors_only_one_coherent_pair();
    void restore_trail_returns_full_yellow_yellow();
    void layout_fits_520x500();
    void selector_visual_state_contract();
    void trail_style_buttons_eight_grid();
    void style_parameter_visibility_matrix();
    void switching_styles_publishes_once_and_preserves_colors();
    // T-020R1: stable anchors, permanent SHAPE rows, explicit tab order.
    void shape_rows_are_permanent_and_stable();
    void trail_control_labels_are_user_facing();
    void trail_section_anchors_stable_across_styles();
    void click_style_options_region_reserves_space();
    void color_mode_tab_order_matches_visual_order();
    void fade_curve_buttons_three();
    // T-017 Click style & particle count GUI tests.
    void click_style_buttons_seven_grid();
    void click_parameter_visibility_matrix();
    void switching_click_styles_publishes_once_and_preserves_settings();
    void click_easing_buttons_three();
    void click_particle_amount_slider_spin_sync_and_bounds();
    void restore_click_returns_ring_and_default_particles();
    // T-018 Restore All Defaults regressions (Phase 8).
    void restore_all_defaults_from_heavily_modified_state();
    void restore_per_section_preserves_enable_checkboxes();
    // T-018R1 Phase 9: master already true -> zero master publications.
    void restore_all_with_master_already_true_emits_no_fake_master_transition();
    // T-021 sparkle decoration section.
    void sparkle_selector_six_modes_exclusive();
    void sparkle_off_hides_parameter_rows();
    void sparkle_changes_publish_once_with_fields();
    void style_change_preserves_sparkle_settings();
    void sparkle_restore_trail_defaults_resets();
    // T-021 audit additions.
    void sparkle_each_mode_publishes_exactly_once();
    void sparkle_programmatic_load_is_silent_and_off_shows_region();
    void sparkle_settings_survive_off_mode_and_color_changes();
    void sparkle_restore_all_defaults_resets();
    // T-023: the six-mode selector must hold the Trail page anchors and
    // the reserved parameter region exactly like the style selector does.
    void sparkle_mode_switching_preserves_anchors_and_region();
    // T-022 Elemental Click VFX Settings surface.
    void elemental_click_style_buttons_exclusive();
    void element_tint_row_visible_only_for_elements();
    void element_tint_slider_bounds_and_publication();
    void restore_click_defaults_resets_element_style_and_tint();
    // T-024 press-and-hold: the bounded Hold control set.
    void hold_fx_is_the_only_hold_control();
    void hold_fx_toggle_publishes_one_coherent_config();
    void hold_fx_programmatic_population_is_silent();
    void restore_click_defaults_resets_hold_fx();
    // T-027 Hold Controls / Motion Wake.
    void hold_wake_controls_gate_on_the_two_toggles();
    void hold_wake_toggle_publishes_one_coherent_config();
    void hold_sliders_publish_one_coherent_config();
    void hold_controls_programmatic_population_is_silent();
    void restore_click_defaults_resets_hold_controls();
    void hold_section_layout_is_stable_across_toggles();

    // T-40 Start with Windows GUI regressions.
    void start_with_windows_object_name_is_stable();
    void start_with_windows_programmatic_population_is_silent();
    void start_with_windows_toggle_publishes_exactly_once();
    void start_with_windows_repeated_toggle_neither_recurses_nor_double_publishes();
    void start_with_windows_restore_all_defaults_behavior();

    // T-34 Developer Release Defaults panel regressions
    void dev_tab_presence_contract();
    void dev_tab_presence_override();
    void dev_tab_capture_and_apply_flow();
    void full_config_capture_preserves_hidden_render_state();
    void dev_tab_preset_save_and_apply_roundtrip();
    void dev_tab_diff_view_reports_changes();
    void dev_tab_promote_leaves_live_config_identical();

    // T-36 Advanced Motion Wake GUI regressions.
    void motion_wake_controls_object_names_are_stable();
    void motion_wake_programmatic_population_is_silent();
    void motion_wake_sliders_publish_one_coherent_config();
    void motion_wake_gate_on_hold_and_motion_toggles();
    void restore_click_defaults_resets_motion_wake_controls();


private:
    // Shared helpers.
    QCheckBox* find_checkbox(const SettingsWindow& w, const QString& name) const;
    QPushButton* find_button(const SettingsWindow& w, const QString& text) const;
    SliderSpin* find_slider(const SettingsWindow& w, const QString& name) const;
};


// ---- T-021 sparkle decoration section ----

// Six exclusive selector buttons (never a combo box); every enum value
// loads to the right button; one user click = one publication.
void TestSettingsWindow::sparkle_selector_six_modes_exclusive() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const QList<QPushButton*> buttons{
        w.findChild<QPushButton*>("trail_sparkle_off"),
        w.findChild<QPushButton*>("trail_sparkle_stardust"),
        w.findChild<QPushButton*>("trail_sparkle_twinkle"),
        w.findChild<QPushButton*>("trail_sparkle_glitter"),
        w.findChild<QPushButton*>("trail_sparkle_firefly"),
        w.findChild<QPushButton*>("trail_sparkle_shards"),
    };
    for (auto* btn : buttons) {
        QVERIFY(btn);
        QVERIFY(btn->isCheckable());
        QCOMPARE(btn->accessibleName(), btn->text());
        QCOMPARE(btn->focusPolicy(), Qt::StrongFocus);
        // T-020 selector contract: the theme keys the styling on the
        // explicit selector property; the property must be present.
        QVERIFY(btn->property("selector").toBool());
    }
    int checked = 0;
    for (auto* b : buttons) if (b->isChecked()) ++checked;
    QCOMPARE(checked, 1);
    QVERIFY(buttons[0]->isChecked());  // Off is the product default

    // The window keeps its visible-selector contract with the new section.
    QVERIFY(w.findChild<QPushButton*>("trail_style_classic") != nullptr);

    const struct { ptd::TrailSparkleMode mode; const char* name; } modes[] = {
        {ptd::TrailSparkleMode::Off,      "trail_sparkle_off"},
        {ptd::TrailSparkleMode::Stardust, "trail_sparkle_stardust"},
        {ptd::TrailSparkleMode::Twinkle,  "trail_sparkle_twinkle"},
        {ptd::TrailSparkleMode::Glitter,  "trail_sparkle_glitter"},
        {ptd::TrailSparkleMode::Firefly,  "trail_sparkle_firefly"},
        {ptd::TrailSparkleMode::Shards,   "trail_sparkle_shards"},
    };
    for (const auto& m : modes) {
        ptd::TrailConfig c{};
        c.sparkle_mode = m.mode;
        SettingsWindow wc(c, cc);
        wc.show();
        auto* btn = wc.findChild<QPushButton*>(m.name);
        QVERIFY2(btn && btn->isChecked(), m.name);
    }

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    w.findChild<QPushButton*>("trail_sparkle_shards")->click();
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().sparkle_mode,
             ptd::TrailSparkleMode::Shards);
}

// Off hides the three parameter rows (inside the reserved region); any
// active mode shows exactly Amount/Size/Spread.
void TestSettingsWindow::sparkle_off_hides_parameter_rows() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* amount = w.findChild<SliderSpin*>("trail_sparkle_amount");
    auto* size = w.findChild<SliderSpin*>("trail_sparkle_size");
    auto* spread = w.findChild<SliderSpin*>("trail_sparkle_spread");
    auto* region = w.findChild<QWidget*>("trail_sparkle_options");
    QVERIFY(amount && size && spread && region);
    // isHidden() is the suite's visibility contract: it tracks the
    // contextual show/hide state independent of which tab is current.
    QVERIFY(amount->isHidden());
    QVERIFY(size->isHidden());
    QVERIFY(spread->isHidden());

    w.findChild<QPushButton*>("trail_sparkle_twinkle")->click();
    QVERIFY(!amount->isHidden());
    QVERIFY(!size->isHidden());
    QVERIFY(!spread->isHidden());

    w.findChild<QPushButton*>("trail_sparkle_off")->click();
    QVERIFY(amount->isHidden());
    QVERIFY(size->isHidden());
    QVERIFY(spread->isHidden());
}

// Every sparkle parameter edit publishes ONE trail config carrying the
// new sparkle fields and untouched everything else.
void TestSettingsWindow::sparkle_changes_publish_once_with_fields() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    w.findChild<QPushButton*>("trail_sparkle_glitter")->click();
    QCOMPARE(trailSpy.count(), 1);
    ptd::TrailConfig got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Glitter);
    QCOMPARE(got.sparkle_amount, ptd::TrailConfig::kDefaultSparkleAmount);
    QCOMPARE(got.sparkle_size_px, ptd::TrailConfig::kDefaultSparkleSizePx);
    QCOMPARE(got.sparkle_spread_px, ptd::TrailConfig::kDefaultSparkleSpreadPx);

    // Simulate real user edits through the inner sliders (set_value is
    // signal-silent by design; a slider setValue is a real edit).
    // Every value here must DIFFER from the shipped default, or Qt
    // suppresses the setValue as a no-op and the publication never fires
    // (T-023 rework moved kDefaultSparkleAmount to 0.70, which is exactly
    // how this test caught it).
    w.findChild<SliderSpin*>("trail_sparkle_amount")
        ->findChild<QSlider*>()->setValue(85);
    w.findChild<SliderSpin*>("trail_sparkle_size")
        ->findChild<QSlider*>()->setValue(13);   // 13 * 0.5 = 6.5 px
    w.findChild<SliderSpin*>("trail_sparkle_spread")
        ->findChild<QSlider*>()->setValue(24);
    QCOMPARE(trailSpy.count(), 3);  // one per edit, never more
    QCOMPARE(clickSpy.count(), 0);
    got = trailSpy.last().at(0).value<ptd::TrailConfig>();
    QVERIFY(pct_close(got.sparkle_amount, 0.85));
    QCOMPARE(got.sparkle_size_px, 6.5f);
    QCOMPARE(got.sparkle_spread_px, 24.0f);
}

// Changing the Trail STYLE must never reset the sparkle settings (and the
// reverse: changing the sparkle mode never touches style values).
void TestSettingsWindow::style_change_preserves_sparkle_settings() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    w.findChild<QPushButton*>("trail_sparkle_firefly")->click();
    w.findChild<SliderSpin*>("trail_sparkle_amount")->set_value(85.0);

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    w.findChild<QPushButton*>("trail_style_comet")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.style, ptd::TrailStyle::Comet);
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Firefly);
    QVERIFY(pct_close(got.sparkle_amount, 0.85));

    // Reverse direction: the sparkle click kept Comet selected.
    w.findChild<QPushButton*>("trail_sparkle_off")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto back = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(back.style, ptd::TrailStyle::Comet);
    QCOMPARE(back.sparkle_mode, ptd::TrailSparkleMode::Off);
}

// Restore Trail Defaults (and Restore All) resets the whole sparkle layer
// to Off + defaults, exactly one publication.
void TestSettingsWindow::sparkle_restore_trail_defaults_resets() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    w.findChild<QPushButton*>("trail_sparkle_glitter")->click();
    w.findChild<SliderSpin*>("trail_sparkle_size")->set_value(8.0);

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    w.findChild<QPushButton*>("btn_restore_trail")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Off);
    QVERIFY(pct_close(got.sparkle_amount,
                      ptd::TrailConfig::kDefaultSparkleAmount));
    QVERIFY(pct_close(got.sparkle_size_px,
                      ptd::TrailConfig::kDefaultSparkleSizePx));
    QVERIFY(pct_close(got.sparkle_spread_px,
                      ptd::TrailConfig::kDefaultSparkleSpreadPx));
    QVERIFY(w.findChild<QPushButton*>("trail_sparkle_off")->isChecked());
}

// T-021/T-023 audit: clicking EACH of the six selector buttons emits exactly
// one coherent TrailConfig update carrying that mode (and nothing else).
void TestSettingsWindow::sparkle_each_mode_publishes_exactly_once() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const struct { ptd::TrailSparkleMode mode; const char* name; } modes[] = {
        {ptd::TrailSparkleMode::Off,      "trail_sparkle_off"},
        {ptd::TrailSparkleMode::Stardust, "trail_sparkle_stardust"},
        {ptd::TrailSparkleMode::Twinkle,  "trail_sparkle_twinkle"},
        {ptd::TrailSparkleMode::Glitter,  "trail_sparkle_glitter"},
        {ptd::TrailSparkleMode::Firefly,  "trail_sparkle_firefly"},
        {ptd::TrailSparkleMode::Shards,   "trail_sparkle_shards"},
    };
    // First click (Off -> Stardust) actually changes the mode; the Off
    // click at the END lands on an already-selected button, which fires no
    // signal -- so assert one emission per real mode change below.
    ptd::TrailSparkleMode current = ptd::TrailSparkleMode::Off;
    for (const auto& m : modes) {
        if (m.mode == current) continue;
        QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
        QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
        w.findChild<QPushButton*>(m.name)->click();
        QCOMPARE(trailSpy.count(), 1);
        QCOMPARE(clickSpy.count(), 0);
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.sparkle_mode, m.mode);
        current = m.mode;
    }
    // Re-clicking the already-selected Shards button publishes the SAME
    // coherent config again (uniform T-020 contract: every real user
    // activation emits exactly once; programmatic selection is silent).
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    w.findChild<QPushButton*>("trail_sparkle_shards")->click();
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().sparkle_mode,
             ptd::TrailSparkleMode::Shards);
}

// T-021 audit: a programmatic config load (widget population) publishes
// nothing, and an Off default still owns its reserved parameter region.
void TestSettingsWindow::sparkle_programmatic_load_is_silent_and_off_shows_region() {
    ptd::TrailConfig tc{};  // default Off
    tc.head_thickness_px = 4.0f;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    QSignalSpy spy(&w, &SettingsWindow::trail_config_changed);
    w.show();
    QCOMPARE(spy.count(), 0);  // population from config emitted nothing
    QVERIFY(w.findChild<QPushButton*>("trail_sparkle_off")->isChecked());
    QVERIFY(w.findChild<QSlider*>("trail_sparkle_amount") == nullptr
            || w.findChild<SliderSpin*>("trail_sparkle_amount")->isHidden());
    // The reserved contextual region exists and holds its fixed height
    // (bounded anchors; Phase 13 keeps the reserved region).
    auto* region = w.findChild<QWidget*>("trail_sparkle_options");
    QVERIFY(region);
    QVERIFY(region->maximumHeight() >= region->minimumHeight());
}

// T-021 audit: stored Amount/Size/Spread survive Off -> other mode -> Off,
// and Trail color-mode changes never touch the sparkle settings.
void TestSettingsWindow::sparkle_settings_survive_off_mode_and_color_changes() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    // Store custom sparkle values while Twinkle is active.
    w.findChild<QPushButton*>("trail_sparkle_twinkle")->click();
    w.findChild<SliderSpin*>("trail_sparkle_amount")->set_value(66.0);
    w.findChild<SliderSpin*>("trail_sparkle_size")->set_value(5.5);
    w.findChild<SliderSpin*>("trail_sparkle_spread")->set_value(20.0);

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    w.findChild<QPushButton*>("trail_sparkle_off")->click();
    QCOMPARE(trailSpy.count(), 1);
    ptd::TrailConfig got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Off);
    QVERIFY(pct_close(got.sparkle_amount, 0.66));
    QVERIFY(pct_close(got.sparkle_size_px, 5.5));
    QVERIFY(pct_close(got.sparkle_spread_px, 20.0));

    // Back to a mode: the stored values reappear (never reset by Off).
    w.findChild<QPushButton*>("trail_sparkle_stardust")->click();
    QCOMPARE(trailSpy.count(), 1);
    got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Stardust);
    QVERIFY(pct_close(got.sparkle_amount, 0.66));
    QVERIFY(pct_close(got.sparkle_size_px, 5.5));
    QVERIFY(pct_close(got.sparkle_spread_px, 20.0));

    // A Trail color-mode change must not reset the sparkle settings.
    w.findChild<QPushButton*>("trail_color_gradient")->click();
    QCOMPARE(trailSpy.count(), 1);
    got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Stardust);
    QVERIFY(pct_close(got.sparkle_amount, 0.66));
}

// T-021 audit: Restore All Defaults also resets the sparkle layer to
// Off + defaults with exactly one trail publication.
void TestSettingsWindow::sparkle_restore_all_defaults_resets() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    w.findChild<QPushButton*>("trail_sparkle_firefly")->click();
    w.findChild<SliderSpin*>("trail_sparkle_spread")->set_value(30.0);

    // CORE-003 + W2-002: Restore All publishes ONE bulk AppConfig.
    QSignalSpy bulkSpy(&w, &SettingsWindow::app_config_applied);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    w.findChild<QPushButton*>("btn_restore_all")->click();
    QCOMPARE(bulkSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    const auto got = bulkSpy.takeFirst().at(0).value<ptd::AppConfig>().trail;
    QCOMPARE(got.sparkle_mode, ptd::TrailSparkleMode::Off);
    QVERIFY(pct_close(got.sparkle_amount,
                      ptd::TrailConfig::kDefaultSparkleAmount));
    QVERIFY(pct_close(got.sparkle_size_px,
                      ptd::TrailConfig::kDefaultSparkleSizePx));
    QVERIFY(pct_close(got.sparkle_spread_px,
                      ptd::TrailConfig::kDefaultSparkleSpreadPx));
    QVERIFY(w.findChild<QPushButton*>("trail_sparkle_off")->isChecked());
}

void TestSettingsWindow::tab_construction_succeeds() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    QVERIFY(w.findChild<QTabWidget*>() != nullptr);
    const int expected_tabs = ptd::is_dev_build() ? 4 : 3;
    QCOMPARE(w.findChild<QTabWidget*>()->count(), expected_tabs);
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(0), QStringLiteral("General"));
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(1), QStringLiteral("Trail"));
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(2), QStringLiteral("Click"));
    QVERIFY(w.findChildren<QComboBox*>().isEmpty());
    if (ptd::is_dev_build()) {
        QCOMPARE(w.findChild<QTabWidget*>()->tabText(3), QStringLiteral("Developer"));
    }
}

void TestSettingsWindow::trail_checkbox_changes_only_trail_enabled() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_enabled_changed);

    // General tab checkboxes
    auto boxes = w.findChildren<QCheckBox*>();
    QVERIFY(boxes.size() >= 3);
    QCheckBox* chkTrail = nullptr;
    QCheckBox* chkClick = nullptr;
    QCheckBox* chkMaster = nullptr;
    for (auto* b : boxes) {
        if (b->text() == QStringLiteral("Enable Trail")) chkTrail = b;
        if (b->text() == QStringLiteral("Enable Click Effect")) chkClick = b;
        if (b->text() == QStringLiteral("Enable ProTrail")) chkMaster = b;
    }
    QVERIFY(chkTrail); QVERIFY(chkClick); QVERIFY(chkMaster);

    chkTrail->setChecked(false);
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(trailSpy.takeFirst().at(0).toBool(), false);
    QCOMPARE(clickSpy.count(), 0);  // click unaffected

    chkTrail->setChecked(true);
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(trailSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(clickSpy.count(), 0);
}

void TestSettingsWindow::click_checkbox_changes_only_click_enabled() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_enabled_changed);

    auto boxes = w.findChildren<QCheckBox*>();
    QCheckBox* chkTrail = nullptr;
    QCheckBox* chkClick = nullptr;
    for (auto* b : boxes) {
        if (b->text() == QStringLiteral("Enable Trail")) chkTrail = b;
        if (b->text() == QStringLiteral("Enable Click Effect")) chkClick = b;
    }
    QVERIFY(chkTrail); QVERIFY(chkClick);

    chkClick->setChecked(false);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(clickSpy.takeFirst().at(0).toBool(), false);
    QCOMPARE(trailSpy.count(), 0);

    chkClick->setChecked(true);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(clickSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(trailSpy.count(), 0);  // re-enable: trail untouched
}

// Proves SettingsWindow signal emission contract: master toggle emits master_enabled_changed only,
// and does NOT cascade-emit trail and click disable signals (preserving user preferences).
void TestSettingsWindow::master_disable_emits_window_signals() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_enabled_changed);

    auto boxes = w.findChildren<QCheckBox*>();
    QCheckBox* chkMaster = nullptr;
    for (auto* b : boxes) {
        if (b->text() == QStringLiteral("Enable ProTrail")) chkMaster = b;
    }
    QVERIFY(chkMaster);
    QVERIFY(chkMaster->isChecked());

    chkMaster->setChecked(false);
    QCOMPARE(masterSpy.count(), 1);
    QCOMPARE(masterSpy.takeFirst().at(0).toBool(), false);
    // Master OFF must NOT corrupt or cascade-emit child enables.
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);

    chkMaster->setChecked(true);
    QCOMPARE(masterSpy.count(), 1);
    QCOMPARE(masterSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
}

void TestSettingsWindow::rgb_editor_updates_all_values() {
    ColorEditor c(QStringLiteral("RGB"), 255, 255, 0);
    c.show();

    QSignalSpy spy(&c, &ColorEditor::color_changed);
    c.set(10, 20, 30);
    int r, g, b;
    c.get(r, g, b);
    QCOMPARE(r, 10);
    QCOMPARE(g, 20);
    QCOMPARE(b, 30);
    QCOMPARE(spy.count(), 0);  // set() does not emit; user input does

    // Simulate user input on R spinbox.
    auto spins = c.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 3);
    spins[0]->setValue(200);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 200);
}

void TestSettingsWindow::slider_spin_synchronization() {
    SliderSpin s(QStringLiteral("Lifetime"), 50.0, 2000.0, 10.0, 0, QStringLiteral(" ms"));
    s.show();

    QSignalSpy spy(&s, &SliderSpin::value_changed);

    s.set_value(350.0);
    QCOMPARE(s.value(), 350.0);
    QCOMPARE(spy.count(), 0);  // programmatic set does not emit

    // Slider -> spin. Note: setting the slider to the SAME value as the
    // current spin-equivalent is a no-op (no signal); use a different
    // slider value so a real change is exercised.
    auto* slider = s.findChild<QSlider*>();
    auto* spin = s.findChild<QDoubleSpinBox*>();
    QVERIFY(slider); QVERIFY(spin);
    slider->setValue(60);  // 60 * 10 = 600 (different from 350)
    QCOMPARE(spy.count(), 1);
    QCOMPARE(s.value(), 600.0);
    QCOMPARE(spy.takeFirst().at(0).toDouble(), 600.0);
    QCOMPARE(spin->value(), 600.0);

    // Setting the slider to the equivalent value again is a no-op: no emit.
    slider->setValue(60);
    QCOMPARE(spy.count(), 0);

    // Spin -> slider
    spin->setValue(800.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(s.value(), 800.0);
    QCOMPARE(slider->value(), 80);
}

void TestSettingsWindow::defaults_restore_exact_defaults() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    // Change some values first
    auto sliders = w.findChildren<SliderSpin*>();
    QVERIFY(sliders.size() >= 10);
    sliders[0]->set_value(30.0);  // head thickness max
    // Trigger restore trail defaults
    auto btns = w.findChildren<QPushButton*>();
    QPushButton* restoreTrail = nullptr;
    QPushButton* restoreClick = nullptr;
    for (auto* b : btns) {
        if (b->text() == QStringLiteral("Restore Trail Defaults")) restoreTrail = b;
        if (b->text() == QStringLiteral("Restore Click Defaults")) restoreClick = b;
    }
    QVERIFY(restoreTrail); QVERIFY(restoreClick);

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    restoreTrail->click();
    QCOMPARE(trailSpy.count(), 1);
    const ptd::TrailConfig restored = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    const ptd::TrailConfig def{};
    QCOMPARE(restored.head_thickness_px, def.head_thickness_px);
    QCOMPARE(restored.tail_thickness_px, def.tail_thickness_px);
    QCOMPARE(restored.taper_strength, def.taper_strength);
    QCOMPARE(restored.base_opacity, def.base_opacity);
    QCOMPARE(restored.smoothing, def.smoothing);
    QCOMPARE(restored.lifetime_ms, def.lifetime_ms);
    QCOMPARE(restored.fade_start, def.fade_start);
    QCOMPARE(restored.fade_curve, def.fade_curve);
    // T-015: restore returns mode Full + Start = Fade = yellow.
    QCOMPARE(restored.color_mode, def.color_mode);
    QCOMPARE(restored.start_color_r, def.start_color_r);
    QCOMPARE(restored.start_color_g, def.start_color_g);
    QCOMPARE(restored.start_color_b, def.start_color_b);
    QCOMPARE(restored.fade_color_r, def.fade_color_r);
    QCOMPARE(restored.fade_color_g, def.fade_color_g);
    QCOMPARE(restored.fade_color_b, def.fade_color_b);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    restoreClick->click();
    QCOMPARE(clickSpy.count(), 1);
    const ptd::ClickConfig rc = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    const ptd::ClickConfig cdef{};
    QCOMPARE(rc.start_radius_px, cdef.start_radius_px);
    QCOMPARE(rc.end_radius_px, cdef.end_radius_px);
    QCOMPARE(rc.duration_ms, cdef.duration_ms);
    QCOMPARE(rc.base_opacity, cdef.base_opacity);
    QCOMPARE(rc.outline_thickness_px, cdef.outline_thickness_px);
    QCOMPARE(rc.fill_opacity, cdef.fill_opacity);
    QCOMPARE(rc.easing, cdef.easing);
    QCOMPARE(rc.color_r, cdef.color_r);
    QCOMPARE(rc.trigger_left, cdef.trigger_left);
    QCOMPARE(rc.trigger_right, cdef.trigger_right);
    QCOMPARE(rc.trigger_middle, cdef.trigger_middle);
}

void TestSettingsWindow::no_recursive_signal_storm() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    // Rapid multi-control interaction: each must produce bounded signal
    // counts (no cascading re-emission loops).
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto sliders = w.findChildren<SliderSpin*>();
    for (int i = 0; i < 10 && i < static_cast<int>(sliders.size()); ++i) {
        sliders[i]->set_value(sliders[i]->value());
    }
    // set_value with same value does not emit.
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);

    // User drag on one slider emits exactly once.
    auto* slider = sliders[0]->findChild<QSlider*>();
    slider->setValue(slider->value() + 1);
    QCOMPARE(trailSpy.count() + clickSpy.count(), 1);
}

void TestSettingsWindow::config_bounds_are_sane() {
    // The GUI ranges must come from config constants (Phase Q).
    QVERIFY(ptd::TrailConfig::kMinThicknessPx < ptd::TrailConfig::kMaxThicknessPx);
    QVERIFY(ptd::TrailConfig::kMinLifetimeMs < ptd::TrailConfig::kMaxLifetimeMs);
    QVERIFY(ptd::TrailConfig::kMinFadeStart < ptd::TrailConfig::kMaxFadeStart);
    QVERIFY(ptd::ClickConfig::kMinDurationMs < ptd::ClickConfig::kMaxDurationMs);
    QVERIFY(ptd::ClickConfig::kMinOpacity < ptd::ClickConfig::kMaxOpacity);
    QVERIFY(ptd::ClickConfig::kMinOutlinePx < ptd::ClickConfig::kMaxOutlinePx);

    // Validation clamps everything into range.
    ptd::TrailConfig bad{};
    bad.head_thickness_px = 1000.0f;
    bad.lifetime_ms = -5.0f;
    bad.base_opacity = 2.0f;
    bad.taper_strength = -1.0f;
    const auto v = ptd::TrailConfig::validated(bad);
    QCOMPARE(v.head_thickness_px, ptd::TrailConfig::kMaxThicknessPx);
    QCOMPARE(v.lifetime_ms, ptd::TrailConfig::kMinLifetimeMs);
    QCOMPARE(v.base_opacity, ptd::TrailConfig::kMaxOpacity);
    QCOMPARE(v.taper_strength, 0.0f);

    ptd::ClickConfig cbad{};
    cbad.duration_ms = 99999.0f;
    cbad.fill_opacity = -1.0f;
    const auto cv = ptd::ClickConfig::validated(cbad);
    QCOMPARE(cv.duration_ms, ptd::ClickConfig::kMaxDurationMs);
    QCOMPARE(cv.fill_opacity, 0.0f);
}

// ---- Helpers ----

SliderSpin* TestSettingsWindow::find_slider(const SettingsWindow& w,
                                           const QString& name) const {
    return w.findChild<SliderSpin*>(name);
}

QCheckBox* TestSettingsWindow::find_checkbox(const SettingsWindow& w,
                                              const QString& name) const {
    return w.findChild<QCheckBox*>(name);
}

QPushButton* TestSettingsWindow::find_button(const SettingsWindow& w,
                                             const QString& text) const {
    const auto btns = w.findChildren<QPushButton*>();
    for (auto* b : btns)
        if (b->text() == text) return b;
    return nullptr;
}

// ---- T-010 repair regression coverage ----

// G1/G15: construction from default configs exposes the approved defaults
// and publishes nothing.
void TestSettingsWindow::construction_publishes_nothing() {
    // Construction must not publish: attach spies via a separate window
    // instance is impossible (signals fire during construction), so instead
    // prove the equivalent contract: a freshly constructed window has no
    // pending publications and a no-op interaction publishes nothing.
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    // Programmatic same-value set (the SliderSpin no-op path) publishes
    // nothing -- the initialization path is silent by the same mechanism.
    auto sliders = w.findChildren<SliderSpin*>();
    QVERIFY(sliders.size() >= 13);
    for (auto* s : sliders) s->set_value(s->value());
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
}

void TestSettingsWindow::construction_shows_default_trail_values() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const ptd::TrailConfig def{};
    // Trail controls initialized from config, not from widget minimums.
    QCOMPARE(w.findChild<SliderSpin*>("trail_head_thickness")->value(),
             static_cast<double>(def.head_thickness_px));
    QCOMPARE(w.findChild<SliderSpin*>("trail_tail_thickness")->value(),
             static_cast<double>(def.tail_thickness_px));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_taper")->value(),
                      static_cast<double>(def.taper_strength * 100.0)));
    QCOMPARE(w.findChild<SliderSpin*>("trail_lifetime")->value(),
             static_cast<double>(def.lifetime_ms));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_opacity")->value(),
                      static_cast<double>(def.base_opacity * 100.0)));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_smoothing")->value(),
                      static_cast<double>(def.smoothing * 100.0)));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_fade_start")->value(),
                      static_cast<double>(def.fade_start * 100.0)));
    // T-020: fade curve + color mode are exclusive button groups now.
    QVERIFY(w.findChild<QPushButton*>("trail_fade_smooth")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("trail_fade_linear")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("trail_fade_ease_out")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_color_solid")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("trail_color_head")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("trail_color_tail")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("trail_color_gradient")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_style_classic")->isChecked());
    // Solid context: the second color editor is hidden, not displayed
    // with an explanation (Phase 2: UI state communicates the mode).
    QVERIFY(w.findChild<ColorEditor*>("trail_fade_color")->isHidden());
    QVERIFY(!w.findChild<ColorEditor*>("trail_start_color")->isHidden());
    // T-015: dual trail color editors.
    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    auto* fade = w.findChild<ColorEditor*>("trail_fade_color");
    QVERIFY(start);
    QVERIFY(fade);
    int r, g, b;
    start->get(r, g, b);
    QCOMPARE(r, static_cast<int>(def.start_color_r));
    QCOMPARE(g, static_cast<int>(def.start_color_g));
    QCOMPARE(b, static_cast<int>(def.start_color_b));
    fade->get(r, g, b);
    QCOMPARE(r, static_cast<int>(def.fade_color_r));
    QCOMPARE(g, static_cast<int>(def.fade_color_g));
    QCOMPARE(b, static_cast<int>(def.fade_color_b));
    // The obsolete single-color object must be gone.
    QVERIFY(w.findChild<ColorEditor*>("trail_color") == nullptr);
    // General enable state mirrors config.
    QCheckBox* chkTrail = find_checkbox(w, "chk_trail");
    QVERIFY(chkTrail);
    QCOMPARE(chkTrail->isChecked(), def.enabled);
}

void TestSettingsWindow::construction_shows_supplied_trail_values() {
    ptd::TrailConfig tc{};
    tc.color_mode = ptd::TrailColorMode::Gradient;
    tc.start_color_r = 10; tc.start_color_g = 200; tc.start_color_b = 30;
    tc.fade_color_r = 40;  tc.fade_color_g = 50;   tc.fade_color_b = 60;
    tc.head_thickness_px = 7.5f;
    tc.tail_thickness_px = 2.5f;
    tc.taper_strength = 0.6f;
    tc.lifetime_ms = 900.0f;
    tc.base_opacity = 0.4f;
    tc.smoothing = 0.25f;
    tc.fade_start = 0.8f;
    tc.fade_curve = ptd::FadeCurve::Smooth;
    tc.enabled = false;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const auto expected = ptd::TrailConfig::validated(tc);
    QCOMPARE(w.findChild<SliderSpin*>("trail_head_thickness")->value(),
             static_cast<double>(expected.head_thickness_px));
    QCOMPARE(w.findChild<SliderSpin*>("trail_tail_thickness")->value(),
             static_cast<double>(expected.tail_thickness_px));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_taper")->value(),
                      static_cast<double>(expected.taper_strength * 100.0)));
    QCOMPARE(w.findChild<SliderSpin*>("trail_lifetime")->value(),
             static_cast<double>(expected.lifetime_ms));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_opacity")->value(),
                      static_cast<double>(expected.base_opacity * 100.0)));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_smoothing")->value(),
                      static_cast<double>(expected.smoothing * 100.0)));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("trail_fade_start")->value(),
                      static_cast<double>(expected.fade_start * 100.0)));
    // T-020: buttons reflect the supplied enums.
    QVERIFY(w.findChild<QPushButton*>("trail_fade_smooth")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_color_gradient")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_style_classic")->isChecked());
    // Gradient context: both editors visible, base (Tail) first.
    QVERIFY(!w.findChild<ColorEditor*>("trail_start_color")->isHidden());
    QVERIFY(!w.findChild<ColorEditor*>("trail_fade_color")->isHidden());
    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    auto* fade = w.findChild<ColorEditor*>("trail_fade_color");
    QVERIFY(start);
    QVERIFY(fade);
    int r, g, b;
    start->get(r, g, b);
    QCOMPARE(r, static_cast<int>(expected.start_color_r));
    QCOMPARE(g, static_cast<int>(expected.start_color_g));
    QCOMPARE(b, static_cast<int>(expected.start_color_b));
    fade->get(r, g, b);
    QCOMPARE(r, static_cast<int>(expected.fade_color_r));
    QCOMPARE(g, static_cast<int>(expected.fade_color_g));
    QCOMPARE(b, static_cast<int>(expected.fade_color_b));
    QCheckBox* chkTrail = find_checkbox(w, "chk_trail");
    QVERIFY(chkTrail);
    QCOMPARE(chkTrail->isChecked(), expected.enabled);
}

void TestSettingsWindow::construction_shows_default_click_values() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const ptd::ClickConfig def{};
    QCOMPARE(w.findChild<SliderSpin*>("click_start_size")->value(),
             static_cast<double>(def.start_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_end_size")->value(),
             static_cast<double>(def.end_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_duration")->value(),
             static_cast<double>(def.duration_ms));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("click_opacity")->value(),
                      static_cast<double>(def.base_opacity * 100.0)));
    QCOMPARE(w.findChild<SliderSpin*>("click_outline")->value(),
             static_cast<double>(def.outline_thickness_px));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("click_fill")->value(),
                      static_cast<double>(def.fill_opacity * 100.0)));
    // T-020: easing/style buttons; Ease Out + Ring visibly selected.
    QVERIFY(w.findChild<QPushButton*>("click_easing_ease_out")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("click_easing_linear")->isChecked());
    QVERIFY(!w.findChild<QPushButton*>("click_easing_smooth")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    // Ring context: fill + outline visible, particles hidden (Phase 11).
    QVERIFY(!w.findChild<SliderSpin*>("click_fill")->isHidden());
    QVERIFY(!w.findChild<SliderSpin*>("click_outline")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("click_particles")->isHidden());
    auto* color = w.findChild<ColorEditor*>("click_color");
    QVERIFY(color);
    int r, g, b;
    color->get(r, g, b);
    QCOMPARE(r, static_cast<int>(def.color_r));
    QCOMPARE(g, static_cast<int>(def.color_g));
    QCOMPARE(b, static_cast<int>(def.color_b));
    QCOMPARE(find_checkbox(w, "chk_trig_left")->isChecked(), def.trigger_left);
    QCOMPARE(find_checkbox(w, "chk_trig_right")->isChecked(), def.trigger_right);
    QCOMPARE(find_checkbox(w, "chk_trig_middle")->isChecked(), def.trigger_middle);
    QCheckBox* chkClick = find_checkbox(w, "chk_click");
    QVERIFY(chkClick);
    QCOMPARE(chkClick->isChecked(), def.enabled);
}

void TestSettingsWindow::construction_shows_supplied_click_values() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.color_r = 200; cc.color_g = 10; cc.color_b = 90;
    cc.trigger_left = false;
    cc.trigger_right = false;
    cc.trigger_middle = false;
    cc.start_radius_px = 30.0f;
    cc.end_radius_px = 90.0f;
    cc.duration_ms = 700.0f;
    cc.base_opacity = 0.6f;
    cc.outline_thickness_px = 5.5f;
    cc.fill_opacity = 0.7f;
    cc.easing = ptd::ClickEasing::Linear;
    cc.enabled = false;
    SettingsWindow w(tc, cc);
    w.show();

    const auto expected = ptd::ClickConfig::validated(cc);
    QCOMPARE(w.findChild<SliderSpin*>("click_start_size")->value(),
             static_cast<double>(expected.start_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_end_size")->value(),
             static_cast<double>(expected.end_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_duration")->value(),
             static_cast<double>(expected.duration_ms));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("click_opacity")->value(),
                      static_cast<double>(expected.base_opacity * 100.0)));
    QCOMPARE(w.findChild<SliderSpin*>("click_outline")->value(),
             static_cast<double>(expected.outline_thickness_px));
    QVERIFY(pct_close(w.findChild<SliderSpin*>("click_fill")->value(),
                      static_cast<double>(expected.fill_opacity * 100.0)));
    // T-020: supplied Linear easing lands on the Linear button.
    QVERIFY(w.findChild<QPushButton*>("click_easing_linear")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    auto* color = w.findChild<ColorEditor*>("click_color");
    QVERIFY(color);
    int r, g, b;
    color->get(r, g, b);
    QCOMPARE(r, static_cast<int>(expected.color_r));
    QCOMPARE(g, static_cast<int>(expected.color_g));
    QCOMPARE(b, static_cast<int>(expected.color_b));
    QCOMPARE(find_checkbox(w, "chk_trig_left")->isChecked(), expected.trigger_left);
    QCOMPARE(find_checkbox(w, "chk_trig_right")->isChecked(), expected.trigger_right);
    QCOMPARE(find_checkbox(w, "chk_trig_middle")->isChecked(), expected.trigger_middle);
    QCheckBox* chkClick = find_checkbox(w, "chk_click");
    QVERIFY(chkClick);
    QCOMPARE(chkClick->isChecked(), expected.enabled);
}

// G4-G7: percentage UI values publish normalized runtime values.
void TestSettingsWindow::percentage_mappings_publish_normalized_values() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto approx = [](double a, double b) {
        return qAbs(a - b) < 0.005;
    };

    // 45% trail opacity -> 0.45 runtime (default is 90%, so 45 is a real
    // change, not a no-op).
    auto* opacitySlider = w.findChild<SliderSpin*>("trail_opacity")->findChild<QSlider*>();
    opacitySlider->setValue(45);
    QCOMPARE(trailSpy.count(), 1);
    QVERIFY(approx(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().base_opacity, 0.45));

    // 25% smoothing -> 0.25 (default is 50%, so 25 is a real change).
    auto* smoothSlider = w.findChild<SliderSpin*>("trail_smoothing")->findChild<QSlider*>();
    smoothSlider->setValue(25);
    QCOMPARE(trailSpy.count(), 1);
    QVERIFY(approx(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().smoothing, 0.25));

    // 95% fade start -> 0.95.
    auto* fadeSlider = w.findChild<SliderSpin*>("trail_fade_start")->findChild<QSlider*>();
    fadeSlider->setValue(95);
    QCOMPARE(trailSpy.count(), 1);
    QVERIFY(approx(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().fade_start, 0.95));

    // 60% taper -> 0.60.
    auto* taperSlider = w.findChild<SliderSpin*>("trail_taper")->findChild<QSlider*>();
    taperSlider->setValue(60);
    QCOMPARE(trailSpy.count(), 1);
    QVERIFY(approx(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().taper_strength, 0.60));

    // Click opacity 80% -> 0.80.
    auto* clickOpSlider = w.findChild<SliderSpin*>("click_opacity")->findChild<QSlider*>();
    clickOpSlider->setValue(80);
    QCOMPARE(clickSpy.count(), 1);
    QVERIFY(approx(clickSpy.takeFirst().at(0).value<ptd::ClickConfig>().base_opacity, 0.80));

    // Click fill 40% -> 0.40.
    auto* fillSlider = w.findChild<SliderSpin*>("click_fill")->findChild<QSlider*>();
    fillSlider->setValue(40);
    QCOMPARE(clickSpy.count(), 1);
    QVERIFY(approx(clickSpy.takeFirst().at(0).value<ptd::ClickConfig>().fill_opacity, 0.40));
}

// G8: changing one Trail setting preserves every unrelated Trail field.
void TestSettingsWindow::trail_edit_preserves_unrelated_trail_fields() {
    ptd::TrailConfig tc{};
    tc.head_thickness_px = 12.0f;
    tc.tail_thickness_px = 4.0f;
    tc.taper_strength = 0.7f;
    tc.lifetime_ms = 600.0f;
    tc.base_opacity = 0.75f;
    tc.smoothing = 0.3f;
    tc.fade_start = 0.5f;
    tc.fade_curve = ptd::FadeCurve::EaseOut;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    // Edit only lifetime via the slider.
    auto* lifeSlider = w.findChild<SliderSpin*>("trail_lifetime")->findChild<QSlider*>();
    lifeSlider->setValue(80);  // 80*10 = 800 ms
    QCOMPARE(trailSpy.count(), 1);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.head_thickness_px, tc.head_thickness_px);
    QCOMPARE(got.tail_thickness_px, tc.tail_thickness_px);
    QVERIFY(qAbs(got.taper_strength - tc.taper_strength) < 0.005);
    QCOMPARE(got.base_opacity, tc.base_opacity);
    QVERIFY(qAbs(got.smoothing - tc.smoothing) < 0.005);
    QVERIFY(qAbs(got.fade_start - tc.fade_start) < 0.005);
    QCOMPARE(got.fade_curve, tc.fade_curve);
    QCOMPARE(got.color_mode, tc.color_mode);
    QCOMPARE(got.start_color_r, tc.start_color_r);
    QCOMPARE(got.start_color_g, tc.start_color_g);
    QCOMPARE(got.start_color_b, tc.start_color_b);
    QCOMPARE(got.fade_color_r, tc.fade_color_r);
    QCOMPARE(got.fade_color_g, tc.fade_color_g);
    QCOMPARE(got.fade_color_b, tc.fade_color_b);
    QCOMPARE(got.lifetime_ms, 800.0f);
}

// G9/G10: effect isolation -- one effect's edits never publish the other.
void TestSettingsWindow::trail_edit_does_not_publish_click_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    // User action 1: trail slider edit -> exactly one publication.
    w.findChild<SliderSpin*>("trail_lifetime")->findChild<QSlider*>()->setValue(50);
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    // Consume it so later assertions see only NEW publications.
    trailSpy.takeFirst();

    // User action 2: trail color edit -> exactly one NEW publication.
    auto* color = w.findChild<ColorEditor*>("trail_start_color");
    auto spins = color->findChildren<QSpinBox*>();
    spins[0]->setValue(77);
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.start_color_r, 77);

    // Zero ClickConfig publications throughout.
    QCOMPARE(clickSpy.count(), 0);
}

void TestSettingsWindow::click_edit_does_not_publish_trail_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    // User action 1: click duration edit -> exactly one publication.
    w.findChild<SliderSpin*>("click_duration")->findChild<QSlider*>()->setValue(50);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    // Consume: later takeFirst() must read the COLOR publication.
    clickSpy.takeFirst();

    // User action 2: click color edit -> exactly one NEW publication
    // carrying the edited color.
    auto* color = w.findChild<ColorEditor*>("click_color");
    auto spins = color->findChildren<QSpinBox*>();
    spins[1]->setValue(99);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got.color_g, 99);

    // User action 3: trigger edit -> one additional publication.
    find_checkbox(w, "chk_trig_left")->setChecked(false);
    QCOMPARE(clickSpy.count(), 1);
    const auto got2 = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got2.trigger_left, false);

    // TrailConfig publications remain zero throughout.
    QCOMPARE(trailSpy.count(), 0);
}

// T-010R Repair 3: GUI owns the end >= start invariant. Start raised above
// End must silently lift End to Start and publish exactly one coherent config.
void TestSettingsWindow::click_start_end_sizes_stay_coherent() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* start = w.findChild<SliderSpin*>("click_start_size");
    auto* end = w.findChild<SliderSpin*>("click_end_size");
    QVERIFY(start); QVERIFY(end);

    // 1. Initial Start=8, End=26 (approved defaults).
    QCOMPARE(start->value(), 8.0);
    QCOMPARE(end->value(), 26.0);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    // 2. User changes Start to 40 (40 > 26).
    start->findChild<QSlider*>()->setValue(40);

    // 3/4. Visible widgets reconciled: End silently raised to Start.
    QCOMPARE(start->value(), 40.0);
    QCOMPARE(end->value(), 40.0);

    // 5. Emitted config coherent; 6. exactly one publication; 7. no Trail.
    QCOMPARE(clickSpy.count(), 1);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got.start_radius_px, 40.0f);
    QCOMPARE(got.end_radius_px, 40.0f);
    QCOMPARE(trailSpy.count(), 0);

    // Later End increase works normally (End >= Start, no clamp).
    end->findChild<QSlider*>()->setValue(60);
    QCOMPARE(clickSpy.count(), 1);
    const auto grew = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(grew.start_radius_px, 40.0f);
    QCOMPARE(grew.end_radius_px, 60.0f);
    QCOMPARE(end->value(), 60.0);
}

// T-010R Repair 3: the End control cannot create End < Start. A user drag
// below Start clamps End back up to Start, publishing exactly one coherent
// config (no recursive storm, no divergent GUI/runtime values).
void TestSettingsWindow::click_end_size_cannot_drop_below_start() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* start = w.findChild<SliderSpin*>("click_start_size");
    auto* end = w.findChild<SliderSpin*>("click_end_size");
    QVERIFY(start); QVERIFY(end);

    // Raise Start to 40 first (publish consumed before the End experiment).
    QSignalSpy pre(&w, &SettingsWindow::click_config_changed);
    start->findChild<QSlider*>()->setValue(40);
    QCOMPARE(pre.count(), 1);
    pre.takeFirst();

    // Now drive End BELOW Start via the End control.
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    end->findChild<QSlider*>()->setValue(10);
    QCOMPARE(end->value(), 40.0);          // clamped up to Start, silently
    QCOMPARE(clickSpy.count(), 1);         // exactly one publication
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got.start_radius_px, 40.0f);
    QCOMPARE(got.end_radius_px, 40.0f);
}

// G11: ColorEditor::set emits zero user-change signals.
void TestSettingsWindow::color_editor_set_is_silent() {
    ColorEditor c(QStringLiteral("RGB"), 255, 255, 0);
    c.show();
    QSignalSpy spy(&c, &ColorEditor::color_changed);
    c.set(10, 20, 30);
    c.set(200, 100, 50);
    c.set(0, 0, 0);
    QCOMPARE(spy.count(), 0);
    // User edit still emits exactly one logical event.
    auto spins = c.findChildren<QSpinBox*>();
    spins[2]->setValue(120);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(2).toInt(), 120);
}

// G12: Restore Trail Defaults emits exactly one TrailConfig publication.
void TestSettingsWindow::restore_trail_defaults_emits_once() {
    ptd::TrailConfig tc{};
    tc.head_thickness_px = 20.0f;
    tc.taper_strength = 0.9f;
    tc.lifetime_ms = 1500.0f;
    tc.fade_curve = ptd::FadeCurve::Smooth;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    const ptd::TrailConfig def{};
    QCOMPARE(got.head_thickness_px, def.head_thickness_px);
    QCOMPARE(got.tail_thickness_px, def.tail_thickness_px);
    QVERIFY(qAbs(got.taper_strength - def.taper_strength) < 0.005);
    QCOMPARE(got.lifetime_ms, def.lifetime_ms);
    QVERIFY(qAbs(got.base_opacity - def.base_opacity) < 0.005);
    QVERIFY(qAbs(got.smoothing - def.smoothing) < 0.005);
    QVERIFY(qAbs(got.fade_start - def.fade_start) < 0.005);
    QCOMPARE(got.fade_curve, def.fade_curve);
    QCOMPARE(got.color_mode, def.color_mode);
    QCOMPARE(got.start_color_r, def.start_color_r);
    QCOMPARE(got.start_color_g, def.start_color_g);
    QCOMPARE(got.start_color_b, def.start_color_b);
    QCOMPARE(got.fade_color_r, def.fade_color_r);
    QCOMPARE(got.fade_color_g, def.fade_color_g);
    QCOMPARE(got.fade_color_b, def.fade_color_b);
}

// G13: Restore Click Defaults emits exactly one publication even when
// triggers/easing were previously modified.
void TestSettingsWindow::restore_click_defaults_emits_once_after_modifications() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    // Modify triggers and easing first.
    find_checkbox(w, "chk_trig_left")->setChecked(false);
    find_checkbox(w, "chk_trig_right")->setChecked(false);
    w.findChild<QPushButton*>("click_easing_linear")->click();  // Linear
    w.findChild<SliderSpin*>("click_fill")->findChild<QSlider*>()->setValue(90);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(clickSpy.count(), 1);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    const ptd::ClickConfig def{};
    QCOMPARE(got.trigger_left, def.trigger_left);
    QCOMPARE(got.trigger_right, def.trigger_right);
    QCOMPARE(got.trigger_middle, def.trigger_middle);
    QCOMPARE(got.easing, def.easing);
    QVERIFY(qAbs(got.fill_opacity - def.fill_opacity) < 0.005);
    QCOMPARE(got.start_radius_px, def.start_radius_px);
    QCOMPARE(got.end_radius_px, def.end_radius_px);
    QCOMPARE(got.duration_ms, def.duration_ms);
    QCOMPARE(got.color_r, def.color_r);
}

// G14: restore operations do not emit the other effect config.
void TestSettingsWindow::restore_does_not_touch_other_effect() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    // Consume: later count checks must see only NEW publications.
    trailSpy.takeFirst();

    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);  // no NEW trail emit after click restore
}

void TestSettingsWindow::restore_all_defaults_emits_once() {
    ptd::TrailConfig tc{};
    tc.head_thickness_px = 25.0f;
    tc.lifetime_ms = 1800.0f;
    ptd::ClickConfig cc{};
    cc.duration_ms = 500.0f;
    cc.particle_amount = 12;
    SettingsWindow w(tc, cc, false);
    w.show();

    // CORE-003 + W2-002: Restore All is ONE bulk publication.
    QSignalSpy bulkSpy(&w, &SettingsWindow::app_config_applied);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);

    find_button(w, "Restore All Defaults")->click();

    QCOMPARE(bulkSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(masterSpy.count(), 0);

    const auto got = bulkSpy.takeFirst().at(0).value<ptd::AppConfig>();
    const ptd::AppConfig def = ptd::release_defaults();
    QCOMPARE(got.trail.head_thickness_px, def.trail.head_thickness_px);
    QCOMPARE(got.trail.lifetime_ms, def.trail.lifetime_ms);
    QCOMPARE(got.click.duration_ms, def.click.duration_ms);
    QCOMPARE(got.click.particle_amount, def.click.particle_amount);
    QCOMPARE(got.master_enabled, def.master_enabled);
}

// G15 (extra guard): master toggles publish master_enabled_changed, never child configs or enables.
void TestSettingsWindow::master_toggle_does_not_emit_configs() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailCfgSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickCfgSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailEnSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickEnSpy(&w, &SettingsWindow::click_enabled_changed);

    find_checkbox(w, "chk_master")->setChecked(false);
    QCOMPARE(trailCfgSpy.count(), 0);
    QCOMPARE(clickCfgSpy.count(), 0);
    QCOMPARE(trailEnSpy.count(), 0);
    QCOMPARE(clickEnSpy.count(), 0);

    find_checkbox(w, "chk_master")->setChecked(true);
    QCOMPARE(trailCfgSpy.count(), 0);
    QCOMPARE(clickCfgSpy.count(), 0);
    QCOMPARE(trailEnSpy.count(), 0);
    QCOMPARE(clickEnSpy.count(), 0);
}

// G16: repeated rapid edits stay bounded and do not recursively re-emit.
void TestSettingsWindow::rapid_edits_stay_bounded() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto* lifeSlider = w.findChild<SliderSpin*>("trail_lifetime")->findChild<QSlider*>();
    for (int i = 0; i < 50; ++i) {
        // 36..55: default lifetime is 350 ms (slider 35), so the first edit
        // must be a real change too -- 50 user edits, 50 publications.
        lifeSlider->setValue(36 + (i % 20));
    }
    // Bounded: exactly one emit per user edit, no recursion.
    QCOMPARE(trailSpy.count(), 50);
    QCOMPARE(clickSpy.count(), 0);

    auto* durSlider = w.findChild<SliderSpin*>("click_duration")->findChild<QSlider*>();
    // Default duration is 250 ms (slider 25); 26..45 are all real changes.
    for (int i = 0; i < 50; ++i) {
        durSlider->setValue(26 + (i % 20));
    }
    QCOMPARE(clickSpy.count(), 50);
    QCOMPARE(trailSpy.count(), 50);  // unchanged by click edits
}

void TestSettingsWindow::color_picker_swatch_properties_and_affordance() {
    ColorEditor c(QStringLiteral("RGB"), 10, 20, 30);
    c.show();

    auto* btn = c.swatch_button();
    QVERIFY(btn != nullptr);
    QCOMPARE(btn->toolTip(), QStringLiteral("Choose color"));
    QCOMPARE(btn->accessibleName(), QStringLiteral("Choose color"));
    QCOMPARE(btn->focusPolicy(), Qt::StrongFocus);
    QVERIFY(btn->styleSheet().contains(QStringLiteral("rgb(10,20,30)")));
}

void TestSettingsWindow::color_picker_updates_numeric_inputs_and_swatch() {
    ColorEditor c(QStringLiteral("RGB"), 10, 20, 30);
    c.show();

    QSignalSpy spy(&c, &ColorEditor::color_changed);

    // Apply color as if accepted from color picker
    c.apply_color(111, 222, 123);

    // Numeric inputs updated
    int r, g, b;
    c.get(r, g, b);
    QCOMPARE(r, 111);
    QCOMPARE(g, 222);
    QCOMPARE(b, 123);

    auto spins = c.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 3);
    QCOMPARE(spins[0]->value(), 111);
    QCOMPARE(spins[1]->value(), 222);
    QCOMPARE(spins[2]->value(), 123);

    // Swatch updated
    QVERIFY(c.swatch_button()->styleSheet().contains(QStringLiteral("rgb(111,222,123)")));

    // Exactly one color_changed signal emitted
    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 111);
    QCOMPARE(args.at(1).toInt(), 222);
    QCOMPARE(args.at(2).toInt(), 123);

    // Manual edit on numeric spinbox also updates swatch and emits
    spins[0]->setValue(50);
    QCOMPARE(spy.count(), 1);
    QVERIFY(c.swatch_button()->styleSheet().contains(QStringLiteral("rgb(50,222,123)")));
}

void TestSettingsWindow::color_picker_trail_publishes_trail_config_only() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    QVERIFY(start != nullptr);

    // Applying color via picker path on trail start color
    start->apply_color(42, 84, 168);

    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);

    const auto cfg = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(cfg.start_color_r, 42);
    QCOMPARE(cfg.start_color_g, 84);
    QCOMPARE(cfg.start_color_b, 168);
}

void TestSettingsWindow::color_picker_click_publishes_click_config_only() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto* color = w.findChild<ColorEditor*>("click_color");
    QVERIFY(color != nullptr);

    // Applying color via picker path on click
    color->apply_color(200, 100, 50);

    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);

    const auto cfg = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(cfg.color_r, 200);
    QCOMPARE(cfg.color_g, 100);
    QCOMPARE(cfg.color_b, 50);
}

void TestSettingsWindow::color_picker_cancellation_produces_zero_publications() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    // When color dialog is cancelled, apply_color is NOT called.
    // Confirm that no spurious signals or publications occur.
    auto* trail_ed = w.findChild<ColorEditor*>("trail_start_color");
    auto* trail_fade_ed = w.findChild<ColorEditor*>("trail_fade_color");
    auto* click_ed = w.findChild<ColorEditor*>("click_color");
    QVERIFY(trail_ed && trail_fade_ed && click_ed);

    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
}

void TestSettingsWindow::color_picker_application_publishes_exactly_once() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    auto* color = w.findChild<ColorEditor*>("trail_start_color");
    // Verify that applying color results in exactly 1 publication, NOT 3
    color->apply_color(1, 2, 3);
    QCOMPARE(trailSpy.count(), 1);

    color->apply_color(4, 5, 6);
    QCOMPARE(trailSpy.count(), 2);
}

// ---- T-015 Phase 12: palette, dual trail color, quick presets ----

namespace {
bool same_rgb(const ptd::PaletteColor& c, int r, int g, int b) {
    return c.r == r && c.g == g && c.b == b;
}
} // namespace

// Every ColorEditor (trail start / trail fade / click) exposes exactly the
// 14 canonical kEffectPalette entries in canonical order, with the color
// name as tooltip AND accessible name, and keyboard-focusable buttons.
void TestSettingsWindow::palette_surface_counts_names_and_focus() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const QList<ColorEditor*> editors{
        w.findChild<ColorEditor*>("trail_start_color"),
        w.findChild<ColorEditor*>("trail_fade_color"),
        w.findChild<ColorEditor*>("click_color"),
    };
    for (auto* ed : editors) {
        QVERIFY(ed);
        QCOMPARE(ed->palette_count(), 14);
        const auto btns = ed->palette_buttons();
        QCOMPARE(btns.size(), 14);
        for (int i = 0; i < btns.size(); ++i) {
            const auto& canonical =
                ptd::kEffectPalette[static_cast<std::size_t>(i)];
            const QString name = QString::fromUtf8(canonical.name);
            QCOMPARE(btns.at(i)->toolTip(), name);
            QCOMPARE(btns.at(i)->accessibleName(), name);
            QCOMPARE(btns.at(i)->focusPolicy(), Qt::StrongFocus);
            QCOMPARE(ed->palette_button(i), btns.at(i));
        }
        QVERIFY(ed->palette_button(-1) == nullptr);
        QVERIFY(ed->palette_button(14) == nullptr);
    }
}

// One palette click = exact canonical RGB + exactly ONE color_changed +
// visibly checked (checked state) selected swatch.
void TestSettingsWindow::palette_click_sets_exact_rgb_and_emits_once() {
    ColorEditor c(QStringLiteral("RGB"), 255, 255, 0);
    c.show();

    QSignalSpy spy(&c, &ColorEditor::color_changed);
    auto* cyan = c.palette_button(8);
    QVERIFY(cyan);
    QVERIFY(cyan->isCheckable());
    cyan->click();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const auto& want = ptd::kEffectPalette[8];
    QCOMPARE(args.at(0).toInt(), static_cast<int>(want.r));
    QCOMPARE(args.at(1).toInt(), static_cast<int>(want.g));
    QCOMPARE(args.at(2).toInt(), static_cast<int>(want.b));
    int r, g, b;
    c.get(r, g, b);
    QVERIFY(same_rgb(want, r, g, b));
    QCOMPARE(c.selected_palette_index(), 8);
    QVERIFY(cyan->isChecked());
    // Exactly one swatch selected.
    int checked = 0;
    for (auto* btn : c.palette_buttons()) if (btn->isChecked()) ++checked;
    QCOMPARE(checked, 1);
}

// Palette click inside the window publishes exactly one TrailConfig
// carrying the canonical RGB and zero ClickConfig.
void TestSettingsWindow::palette_click_publishes_trail_config_once() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    QVERIFY(start);
    auto* violet = start->palette_button(11);  // Violet
    QVERIFY(violet);
    violet->click();

    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    const auto cfg = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    const auto& want = ptd::kEffectPalette[11];
    QCOMPARE(cfg.start_color_r, want.r);
    QCOMPARE(cfg.start_color_g, want.g);
    QCOMPARE(cfg.start_color_b, want.b);
    // Fade editor untouched.
    QCOMPARE(cfg.fade_color_r, tc.fade_color_r);
}

// Direct RGB edits (programmatic set + user spin edits) keep the palette
// selection in sync immediately.
void TestSettingsWindow::direct_rgb_edit_updates_palette_selection() {
    ColorEditor c(QStringLiteral("RGB"), 255, 255, 0);
    c.show();

    // Cyan is index 8.
    auto* cyan = c.palette_button(8);
    cyan->click();
    QCOMPARE(c.selected_palette_index(), 8);

    // User edits R off the canonical value -> none selected.
    auto spins = c.findChildren<QSpinBox*>();
    QCOMPARE(spins.size(), 3);
    spins[0]->setValue(1);  // 1,200,255 matches nothing
    QCOMPARE(c.selected_palette_index(), -1);

    // Back to the exact canonical RGB via spin edits -> selected again.
    spins[0]->setValue(static_cast<int>(ptd::kEffectPalette[8].r));
    QCOMPARE(c.selected_palette_index(), 8);
}

// A custom RGB that matches no palette entry leaves every swatch unchecked.
void TestSettingsWindow::custom_value_selects_no_palette_entry() {
    ColorEditor c(QStringLiteral("RGB"), 1, 2, 3);
    c.show();
    QCOMPARE(c.selected_palette_index(), -1);
    for (auto* btn : c.palette_buttons()) {
        QVERIFY(!btn->isChecked());
    }
}

// ColorEditor::set() stays signal-silent while updating the selection.
void TestSettingsWindow::set_updates_palette_selection_silently() {
    ColorEditor c(QStringLiteral("RGB"), 1, 2, 3);
    c.show();
    QSignalSpy spy(&c, &ColorEditor::color_changed);

    c.set(255, 255, 0);  // Yellow = index 4
    QCOMPARE(spy.count(), 0);
    QCOMPARE(c.selected_palette_index(), 4);

    c.set(0, 200, 255);  // Cyan = index 8
    QCOMPARE(spy.count(), 0);
    QCOMPARE(c.selected_palette_index(), 8);
    QVERIFY(c.palette_button(8)->isChecked());
    QVERIFY(!c.palette_button(4)->isChecked());
}

// T-020 Phase 1/17: four exclusive mode buttons; every enum loads to the
// correct button; each user click publishes the correct enum exactly once.
void TestSettingsWindow::trail_color_buttons_four_modes() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const QList<QPushButton*> mode_buttons{
        w.findChild<QPushButton*>("trail_color_solid"),
        w.findChild<QPushButton*>("trail_color_head"),
        w.findChild<QPushButton*>("trail_color_tail"),
        w.findChild<QPushButton*>("trail_color_gradient"),
    };
    for (auto* btn : mode_buttons) {
        QVERIFY(btn);
        QVERIFY(btn->isCheckable());
        QCOMPARE(btn->accessibleName(), btn->text());
        QCOMPARE(btn->focusPolicy(), Qt::StrongFocus);
    }
    auto checked_count = [mode_buttons] {
        int n = 0;
        for (auto* b : mode_buttons) if (b->isChecked()) ++n;
        return n;
    };
    QCOMPARE(checked_count(), 1);   // exactly one selected
    QVERIFY(mode_buttons[0]->isChecked());  // Solid = Full default

    // Every enum value loads to the correct selected button.
    const struct { ptd::TrailColorMode mode; const char* name; } modes[] = {
        {ptd::TrailColorMode::Full,        "trail_color_solid"},
        {ptd::TrailColorMode::StartAccent, "trail_color_head"},
        {ptd::TrailColorMode::FadeAccent,  "trail_color_tail"},
        {ptd::TrailColorMode::Gradient,    "trail_color_gradient"},
    };
    for (const auto& m : modes) {
        ptd::TrailConfig c{};
        c.color_mode = m.mode;
        SettingsWindow wc(c, cc);
        wc.show();
        auto* btn = wc.findChild<QPushButton*>(m.name);
        QVERIFY2(btn && btn->isChecked(), m.name);
    }

    // One user click = exactly one publication carrying the right enum.
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    for (const auto& m : modes) {
        w.findChild<QPushButton*>(m.name)->click();
        QCOMPARE(trailSpy.count(), 1);
        QCOMPARE(clickSpy.count(), 0);
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.color_mode, m.mode);
    }
    // Re-clicking the active button is one coherent (no-op) publication,
    // never a duplicate or an intermediate half-state.
    w.findChild<QPushButton*>("trail_color_tail")->click();
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().color_mode,
             ptd::TrailColorMode::FadeAccent);
}

// T-020 Phase 2: the COLOR section relabels the editors per mode so the
// state itself explains which color the mode uses. Solid hides the second
// editor; Head/Tail/Gradient expose both with the right roles.
void TestSettingsWindow::mode_editor_contextual_visibility() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    auto* fade = w.findChild<ColorEditor*>("trail_fade_color");
    QVERIFY(start); QVERIFY(fade);
    auto editor_label = [](ColorEditor* ed) {
        return ed->findChildren<QLabel*>().front()->text();
    };

    // Solid (default): one editor labelled Color, second hidden.
    QCOMPARE(editor_label(start), QStringLiteral("Color"));
    QVERIFY(!start->isHidden());
    QVERIFY(fade->isHidden());

    // Head: base editor first ("Base / Tail" = fade color), accent second.
    w.findChild<QPushButton*>("trail_color_head")->click();
    QVERIFY(!start->isHidden());
    QVERIFY(!fade->isHidden());
    QCOMPARE(editor_label(fade), QStringLiteral("Base / Tail"));
    QCOMPARE(editor_label(start), QStringLiteral("Head accent"));

    // Tail: base editor first ("Base / Head" = start color).
    w.findChild<QPushButton*>("trail_color_tail")->click();
    QCOMPARE(editor_label(start), QStringLiteral("Base / Head"));
    QCOMPARE(editor_label(fade), QStringLiteral("Tail accent"));

    // Gradient: Tail first, Head second, both active.
    w.findChild<QPushButton*>("trail_color_gradient")->click();
    QCOMPARE(editor_label(fade), QStringLiteral("Tail"));
    QCOMPARE(editor_label(start), QStringLiteral("Head"));
    QVERIFY(!fade->isHidden());
    QVERIFY(!start->isHidden());
}

// T-020 Phase 2/17: hidden editors keep their stored RGB values; returning
// to a mode restores exactly the colors the user selected there.
void TestSettingsWindow::hidden_editor_values_survive_mode_switch() {
    ptd::TrailConfig tc{};
    tc.start_color_r = 10; tc.start_color_g = 20; tc.start_color_b = 30;
    tc.fade_color_r = 12;  tc.fade_color_g = 34;  tc.fade_color_b = 56;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* start = w.findChild<ColorEditor*>("trail_start_color");
    auto* fade = w.findChild<ColorEditor*>("trail_fade_color");
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    // Solid hides the fade editor; editing the visible start editor must
    // not disturb the hidden fade color.
    QVERIFY(fade->isHidden());
    start->findChildren<QSpinBox*>()[2]->setValue(77);
    QCOMPARE(trailSpy.count(), 1);
    {
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.start_color_b, 77);
        QCOMPARE(got.fade_color_r, 12);  // hidden value still published
    }

    // Tail mode: fade editor visible again with its original value.
    w.findChild<QPushButton*>("trail_color_tail")->click();
    trailSpy.takeFirst();
    QVERIFY(!fade->isHidden());
    int r, g, b;
    fade->get(r, g, b);
    QCOMPARE(r, 12); QCOMPARE(g, 34); QCOMPARE(b, 56);

    // Edit fade, hide it again (Solid), come back: value intact.
    fade->findChildren<QSpinBox*>()[0]->setValue(99);
    trailSpy.takeFirst();
    w.findChild<QPushButton*>("trail_color_solid")->click();
    trailSpy.takeFirst();
    QVERIFY(fade->isHidden());
    w.findChild<QPushButton*>("trail_color_gradient")->click();
    trailSpy.takeFirst();
    fade->get(r, g, b);
    QCOMPARE(r, 99);
    start->get(r, g, b);
    QCOMPARE(r, 10); QCOMPARE(g, 20); QCOMPARE(b, 77);
}

// Switching modes preserves both colors (widget state + published config)
// and publishes exactly one coherent TrailConfig per change.
void TestSettingsWindow::switching_modes_preserves_both_colors() {
    ptd::TrailConfig tc{};
    tc.color_mode = ptd::TrailColorMode::Full;
    tc.start_color_r = 30; tc.start_color_g = 144; tc.start_color_b = 255;
    tc.fade_color_r = 255; tc.fade_color_g = 105;  tc.fade_color_b = 180;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    w.findChild<QPushButton*>("trail_color_gradient")->click();  // Gradient

    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(got.start_color_r, 30);
    QCOMPARE(got.start_color_g, 144);
    QCOMPARE(got.start_color_b, 255);
    QCOMPARE(got.fade_color_r, 255);
    QCOMPARE(got.fade_color_g, 105);
    QCOMPARE(got.fade_color_b, 180);

    // Widgets still show both colors after the switch.
    int r, g, b;
    w.findChild<ColorEditor*>("trail_start_color")->get(r, g, b);
    QCOMPARE(r, 30); QCOMPARE(g, 144); QCOMPARE(b, 255);
    w.findChild<ColorEditor*>("trail_fade_color")->get(r, g, b);
    QCOMPARE(r, 255); QCOMPARE(g, 105); QCOMPARE(b, 180);

    // Both editors stay in place in Gradient mode (the Trail tab itself is
    // not the active tab here, so isHidden() is the correct visibility
    // contract).
    QVERIFY(!w.findChild<ColorEditor*>("trail_start_color")->isHidden());
    QVERIFY(!w.findChild<ColorEditor*>("trail_fade_color")->isHidden());
}

// Six visible one-click preset buttons on General, matching kColorPresets
// exactly (order + label).
void TestSettingsWindow::preset_buttons_match_kColorPresets() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QCOMPARE(static_cast<int>(ptd::kColorPresets.size()), 6);
    for (int i = 0; i < static_cast<int>(ptd::kColorPresets.size()); ++i) {
        const QString object_name =
            QStringLiteral("preset_btn_%1").arg(
                QString::fromUtf8(ptd::kColorPresets[static_cast<std::size_t>(i)].name));
        auto* btn = w.findChild<QPushButton*>(object_name);
        QVERIFY2(btn, object_name.toUtf8().constData());
        QCOMPARE(btn->text(),
                 QString::fromUtf8(ptd::kColorPresets[static_cast<std::size_t>(i)].name));
        QVERIFY(btn->isVisible());
    }
}

void TestSettingsWindow::preset_applies_colors_only_one_coherent_pair() {
    ptd::TrailConfig tc{};
    tc.head_thickness_px = 12.0f;
    tc.tail_thickness_px = 5.0f;
    tc.taper_strength = 0.7f;
    tc.lifetime_ms = 800.0f;
    tc.base_opacity = 0.5f;
    tc.smoothing = 0.25f;
    tc.fade_start = 0.4f;
    tc.fade_curve = ptd::FadeCurve::Smooth;
    tc.enabled = true;
    ptd::ClickConfig cc{};
    cc.start_radius_px = 20.0f;
    cc.end_radius_px = 60.0f;
    cc.duration_ms = 500.0f;
    cc.base_opacity = 0.7f;
    cc.outline_thickness_px = 4.0f;
    cc.fill_opacity = 0.3f;
    cc.easing = ptd::ClickEasing::Smooth;
    cc.trigger_left = false;
    cc.enabled = true;
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy presetSpy(&w, &SettingsWindow::preset_applied);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailEnSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickEnSpy(&w, &SettingsWindow::click_enabled_changed);

    auto* fire = w.findChild<QPushButton*>("preset_btn_Fire");
    QVERIFY(fire);
    fire->click();

    // ONE coherent pair, no intermediate Trail/Click config storms.
    QCOMPARE(presetSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(trailEnSpy.count(), 0);
    QCOMPARE(clickEnSpy.count(), 0);

    const auto args = presetSpy.takeFirst();
    const auto t = args.at(0).value<ptd::TrailConfig>();
    const auto c = args.at(1).value<ptd::ClickConfig>();

    // Colors come from kColorPresets[1] (Fire) exactly.
    const auto& p = ptd::kColorPresets[1];
    QCOMPARE(t.color_mode, p.trail_mode);
    QCOMPARE(t.start_color_r, p.trail_start_r);
    QCOMPARE(t.start_color_g, p.trail_start_g);
    QCOMPARE(t.start_color_b, p.trail_start_b);
    QCOMPARE(t.fade_color_r, p.trail_fade_r);
    QCOMPARE(t.fade_color_g, p.trail_fade_g);
    QCOMPARE(t.fade_color_b, p.trail_fade_b);
    QCOMPARE(c.color_r, p.click_r);
    QCOMPARE(c.color_g, p.click_g);
    QCOMPARE(c.color_b, p.click_b);

    // Every non-color property preserved.
    QCOMPARE(t.head_thickness_px, tc.head_thickness_px);
    QCOMPARE(t.tail_thickness_px, tc.tail_thickness_px);
    QVERIFY(qAbs(t.taper_strength - tc.taper_strength) < 0.005);
    QCOMPARE(t.lifetime_ms, tc.lifetime_ms);
    QVERIFY(qAbs(t.base_opacity - tc.base_opacity) < 0.005);
    QVERIFY(qAbs(t.smoothing - tc.smoothing) < 0.005);
    QVERIFY(qAbs(t.fade_start - tc.fade_start) < 0.005);
    QCOMPARE(t.fade_curve, tc.fade_curve);
    QCOMPARE(t.enabled, tc.enabled);
    QCOMPARE(c.start_radius_px, cc.start_radius_px);
    QCOMPARE(c.end_radius_px, cc.end_radius_px);
    QCOMPARE(c.duration_ms, cc.duration_ms);
    QVERIFY(qAbs(c.base_opacity - cc.base_opacity) < 0.005);
    QCOMPARE(c.outline_thickness_px, cc.outline_thickness_px);
    QVERIFY(qAbs(c.fill_opacity - cc.fill_opacity) < 0.005);
    QCOMPARE(c.easing, cc.easing);
    QCOMPARE(c.trigger_left, cc.trigger_left);
    QCOMPARE(c.enabled, cc.enabled);

    // Widgets were updated silently to the preset colors.
    int r, g, b;
    w.findChild<ColorEditor*>("trail_start_color")->get(r, g, b);
    QCOMPARE(r, static_cast<int>(p.trail_start_r));
    QCOMPARE(g, static_cast<int>(p.trail_start_g));
    QCOMPARE(b, static_cast<int>(p.trail_start_b));
    w.findChild<ColorEditor*>("trail_fade_color")->get(r, g, b);
    QCOMPARE(r, static_cast<int>(p.trail_fade_r));
    QCOMPARE(g, static_cast<int>(p.trail_fade_g));
    QCOMPARE(b, static_cast<int>(p.trail_fade_b));
    w.findChild<ColorEditor*>("click_color")->get(r, g, b);
    QCOMPARE(r, static_cast<int>(p.click_r));
    QCOMPARE(g, static_cast<int>(p.click_g));
    QCOMPARE(b, static_cast<int>(p.click_b));
    // Preset color mode visibly selected on the mode buttons.
    const char* mode_btn_name =
        p.trail_mode == ptd::TrailColorMode::Full ? "trail_color_solid" :
        p.trail_mode == ptd::TrailColorMode::StartAccent ? "trail_color_head" :
        p.trail_mode == ptd::TrailColorMode::FadeAccent ? "trail_color_tail" :
        "trail_color_gradient";
    QVERIFY(w.findChild<QPushButton*>(mode_btn_name)->isChecked());
}

// Trail restore returns mode Full + Start = Fade = 255,255,0.
void TestSettingsWindow::restore_trail_returns_full_yellow_yellow() {
    ptd::TrailConfig tc{};
    tc.color_mode = ptd::TrailColorMode::Gradient;
    tc.start_color_r = 255; tc.start_color_g = 64;  tc.start_color_b = 24;
    tc.fade_color_r = 64;   tc.fade_color_g = 128;  tc.fade_color_b = 255;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.color_mode, ptd::TrailColorMode::Full);
    QCOMPARE(got.start_color_r, 255);
    QCOMPARE(got.start_color_g, 255);
    QCOMPARE(got.start_color_b, 0);
    QCOMPARE(got.fade_color_r, 255);
    QCOMPARE(got.fade_color_g, 255);
    QCOMPARE(got.fade_color_b, 0);
}

// T-020 Phase 14: compact 520x500 window. Trail/Click tabs may scroll
// vertically, but no horizontal overflow may exist (mode/style button
// rows, RGB rows and the 7+7 palette rows must fit the page width).
void TestSettingsWindow::layout_fits_520x500() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    QVERIFY(w.minimumSize().width() <= 520);
    QVERIFY(w.minimumSize().height() <= 500);
    QCOMPARE(w.width(), 520);
    QCOMPARE(w.height(), 500);

    const auto areas = w.findChildren<QScrollArea*>();
    QVERIFY(areas.size() >= 2);  // Trail + Click content areas
    for (auto* area : areas) {
        QVERIFY2(!area->horizontalScrollBar()->isVisible(),
                 "no horizontal page scrollbar at 520x500");
    }

    // T-020R1: no horizontal clipping in ANY contextual state either. A row
    // that appears, disappears or swaps must never widen the page past the
    // 520 px viewport.
    auto* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs);
    auto check_page = [&](int index, const QString& what) {
        tabs->setCurrentIndex(index);
        QApplication::processEvents();
        QWidget* page = tabs->widget(index);
        QVERIFY(page != nullptr);
        auto* area = page->findChild<QScrollArea*>();
        QVERIFY2(area != nullptr, what.toUtf8().constData());
        QVERIFY2(!area->horizontalScrollBar()->isVisible(),
                 qPrintable(what + QStringLiteral(": horizontal scrollbar")));
    };
    const QStringList trail_states = {
        "trail_style_classic", "trail_style_soft_glow", "trail_style_comet",
        "trail_style_neon", "trail_style_dotted", "trail_style_pulse",
        "trail_style_ribbon", "trail_style_spark",
        "trail_color_head", "trail_color_tail", "trail_color_gradient",
        "trail_color_solid"};
    for (const QString& name : trail_states) {
        w.findChild<QPushButton*>(name)->click();
        check_page(1, name);
    }
    const QStringList click_states = {
        "click_style_ring", "click_style_double_ring", "click_style_ripple",
        "click_style_burst", "click_style_spark_burst",
        "click_style_soft_flash", "click_style_dot_ring"};
    for (const QString& name : click_states) {
        w.findChild<QPushButton*>(name)->click();
        check_page(2, name);
    }
}

// T-020R1 TARGET 1/2: the selector contract is a VISUAL contract, so it is
// proven by pixels: a selected and an unselected selector button are drawn
// through the widget's own stylesheet-aware style, with and without focus,
// and the resulting bevel is inspected. String checks alone could not tell
// an inside-out state (selected reading as an accidentally pressed button)
// from a correct one.
void TestSettingsWindow::selector_visual_state_contract() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    // (a) the property exists on every selector button (they are the whole
    // family of exclusive button groups), and it is what the theme keys on.
    int selector_count = 0;
    for (auto* button : w.findChildren<QPushButton*>()) {
        if (!button->property("selector").toBool()) continue;
        ++selector_count;
        QVERIFY(button->isCheckable());
        QVERIFY(button->focusPolicy() & Qt::TabFocus);
    }
    QVERIFY(selector_count > 0);

    // (b) exactly one selected button per exclusive group, and every member
    // of every group is a selector (never a plain button that would fall
    // through to the generic button rules).
    const QStringList groups = {"trail_color_mode", "trail_style",
                                "trail_fade_curve", "click_style",
                                "click_easing"};
    for (const QString& name : groups) {
        auto* group = w.findChild<QButtonGroup*>(name);
        QVERIFY2(group != nullptr, name.toUtf8().constData());
        int checked = 0;
        QVERIFY(group->buttons().size() >= 3);
        for (auto* button : group->buttons()) {
            QVERIFY2(button->property("selector").toBool(),
                     button->objectName().toUtf8().constData());
            if (button->isChecked()) ++checked;
        }
        QCOMPARE(checked, 1);
    }

    // (c) the contract lives on the property, not on the generic checked rule
    // that produced the inside-out first pass.
    const QString sheet = ptd::theme::golden_stylesheet();
    QVERIFY(sheet.contains(QStringLiteral("QPushButton[selector=\"true\"]")));
    QVERIFY(sheet.contains(QStringLiteral("QPushButton[selector=\"true\"]:checked")));
    QVERIFY(sheet.contains(QStringLiteral("QPushButton[selector=\"true\"]:focus")));
    QVERIFY(!sheet.contains(QStringLiteral("QPushButton:checked")));

    // (d) palette swatches (checkable too!) never inherit selector styling.
    const auto editors = w.findChildren<ColorEditor*>();
    QVERIFY(!editors.isEmpty());
    for (auto* editor : editors) {
        QVERIFY(editor->palette_count() > 0);
        for (auto* swatch : editor->palette_buttons()) {
            QVERIFY(!swatch->property("selector").toBool());
            QVERIFY(swatch->isCheckable());
        }
    }

    // (e) pixels: draw the real buttons through their own style.
    auto* selected = w.findChild<QPushButton*>("trail_color_solid");
    auto* unselected = w.findChild<QPushButton*>("trail_color_head");
    QVERIFY(selected && unselected);
    QVERIFY(selected->isChecked());
    QVERIFY(!unselected->isChecked());

    auto render = [](QPushButton* button, bool focused, const QSize& size) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QStyleOptionButton opt;
        opt.initFrom(button);
        opt.rect = QRect(QPoint(0, 0), size);
        opt.text = button->text();
        // QWidget::initFrom does not carry the check state: set it here so
        // the stylesheet's :checked pseudo-class is the state under test.
        opt.state |= button->isChecked() ? QStyle::State_On : QStyle::State_Off;
        if (focused) opt.state |= QStyle::State_HasFocus;
        QPainter painter(&image);
        button->style()->drawControl(QStyle::CE_PushButton, &opt, &painter, button);
        return image;
    };
    // Both probes are drawn into the same box so the comparison is about the
    // state, not about the label's width.
    const QSize probe(qMax(selected->sizeHint().width(),
                           unselected->sizeHint().width()),
                      qMax(selected->sizeHint().height(),
                           unselected->sizeHint().height()));
    auto count = [](const QImage& image, const QColor& target, const QRect& area) {
        int n = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (image.pixelColor(x, y) == target) ++n;
        return n;
    };

    const QColor lit = ptd::theme::borderHighlight();   // #F0D060
    const QColor bevel = ptd::theme::bevelLight();      // #75663D
    const QColor dark = ptd::theme::borderDark();       // #100E08
    const QColor raised = ptd::theme::surfaceRaised();  // #3D372A
    const QColor chosen = ptd::theme::surfaceAlt();     // #453D30

    const QImage sel = render(selected, false, probe);
    const QImage unsel = render(unselected, false, probe);
    const QImage sel_focus = render(selected, true, probe);
    const QImage unsel_focus = render(unselected, true, probe);
    QVERIFY(sel.width() > 8 && sel.height() > 8);
    QVERIFY(sel.size() == unsel.size());
    const QRect whole(0, 0, sel.width(), sel.height());
    const QRect top_edge(0, 0, sel.width(), 2);
    const QRect bottom_edge(0, sel.height() - 2, sel.width(), 2);

    // Unselected: neutral raised Golden button with the standard bevel and
    // no selection highlight at all.
    QVERIFY(count(unsel, bevel, top_edge) > 0);
    QVERIFY(count(unsel, dark, bottom_edge) > 0);
    QVERIFY(count(unsel, raised, whole) > 0);
    QCOMPARE(count(unsel, lit, top_edge), 0);
    QCOMPARE(count(unsel, chosen, whole), 0);

    // Selected: the lit (top/left) edge is borderHighlight and the shadow
    // (bottom/right) edge is still borderDark -- the bevel DIRECTION is
    // unchanged, so the control still reads raised, never pressed.
    QVERIFY(count(sel, lit, top_edge) > 0);
    QVERIFY(count(sel, dark, bottom_edge) > 0);
    QVERIFY(count(sel, chosen, whole) > 0);
    QCOMPARE(count(sel, bevel, top_edge), 0);

    // The two states genuinely differ (lit bevel + selected surface).
    QVERIFY(sel != unsel);

    // Focus is additive: it must not rewrite the bevel colours or the
    // surface, so the selected/unselected distinction survives focus and the
    // bevel geometry is never flattened into one gold rectangle.
    QCOMPARE(count(sel_focus, lit, top_edge), count(sel, lit, top_edge));
    QCOMPARE(count(sel_focus, dark, bottom_edge), count(sel, dark, bottom_edge));
    QCOMPARE(count(sel_focus, chosen, whole), count(sel, chosen, whole));
    QCOMPARE(count(unsel_focus, lit, top_edge), count(unsel, lit, top_edge));
    QCOMPARE(count(unsel_focus, bevel, top_edge), count(unsel, bevel, top_edge));
    QCOMPARE(count(unsel_focus, chosen, whole), count(unsel, chosen, whole));

    // Selected + focused still reads primarily as selected.
    QVERIFY(sel_focus != unsel_focus);
    QVERIFY(count(sel_focus, lit, top_edge) > 0);
    QVERIFY(count(sel_focus, chosen, whole) > 0);
    QCOMPARE(count(unsel_focus, lit, top_edge), 0);
    QCOMPARE(count(unsel_focus, chosen, whole), 0);
    // ... and the focus cue itself is visible in both states.
    QVERIFY(sel_focus != sel);
    QVERIFY(unsel_focus != unsel);
}

// T-020 Phase 3/17: eight checkable style buttons in a 4x2 grid; exactly
// one active; every enum loads to the correct button.
void TestSettingsWindow::trail_style_buttons_eight_grid() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const QList<QPushButton*> style_buttons{
        w.findChild<QPushButton*>("trail_style_classic"),
        w.findChild<QPushButton*>("trail_style_soft_glow"),
        w.findChild<QPushButton*>("trail_style_comet"),
        w.findChild<QPushButton*>("trail_style_neon"),
        w.findChild<QPushButton*>("trail_style_dotted"),
        w.findChild<QPushButton*>("trail_style_pulse"),
        w.findChild<QPushButton*>("trail_style_ribbon"),
        w.findChild<QPushButton*>("trail_style_spark"),
    };
    const QStringList labels = {
        "Classic", "Soft Glow", "Comet", "Neon",
        "Dotted", "Pulse", "Ribbon", "Spark"};
    for (int i = 0; i < style_buttons.size(); ++i) {
        QVERIFY2(style_buttons[i], style_buttons[i]
            ? style_buttons[i]->objectName().toUtf8().constData()
            : labels.at(i).toUtf8().constData());
        QVERIFY(style_buttons[i]->isCheckable());
        QCOMPARE(style_buttons[i]->accessibleName(), labels.at(i));
        QCOMPARE(style_buttons[i]->focusPolicy(), Qt::StrongFocus);
    }
    int checked = 0;
    for (auto* b : style_buttons) if (b->isChecked()) ++checked;
    QCOMPARE(checked, 1);
    QVERIFY(style_buttons[0]->isChecked());  // Classic default

    // Defaults: 50% glow, 12 px spacing (values live even while hidden).
    QCOMPARE(w.findChild<SliderSpin*>("trail_glow")->value(), 50.0);
    QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->value(), 12.0);

    // Every enum loads to the correct selected button.
    const ptd::TrailStyle styles[8] = {
        ptd::TrailStyle::Classic, ptd::TrailStyle::SoftGlow,
        ptd::TrailStyle::Comet,   ptd::TrailStyle::Neon,
        ptd::TrailStyle::Dotted,  ptd::TrailStyle::Pulse,
        ptd::TrailStyle::Ribbon,  ptd::TrailStyle::Spark};
    for (int i = 0; i < 8; ++i) {
        ptd::TrailConfig c{};
        c.style = styles[i];
        SettingsWindow wc(c, cc);
        wc.show();
        auto* btn = wc.findChild<QPushButton*>(style_buttons[i]->objectName());
        QVERIFY2(btn && btn->isChecked(), labels.at(i).toUtf8().constData());
    }
}

// T-020 Phase 4/17: only style-relevant parameter rows exist. Irrelevant
// rows are HIDDEN (panel physically shorter), never merely disabled.
void TestSettingsWindow::style_parameter_visibility_matrix() {
    ptd::ClickConfig cc{};
    const struct { ptd::TrailStyle style; bool glow; bool dots; } rows[] = {
        {ptd::TrailStyle::Classic,  false, false},
        {ptd::TrailStyle::SoftGlow, true,  false},
        {ptd::TrailStyle::Comet,    false, false},
        {ptd::TrailStyle::Neon,     true,  false},
        {ptd::TrailStyle::Dotted,   false, true},
        {ptd::TrailStyle::Pulse,    false, false},
        {ptd::TrailStyle::Ribbon,   false, false},
        {ptd::TrailStyle::Spark,    false, true},
    };
    for (const auto& row : rows) {
        ptd::TrailConfig tc{};
        tc.style = row.style;
        SettingsWindow w(tc, cc);
        w.show();
        QCOMPARE(w.findChild<SliderSpin*>("trail_glow")->isHidden(), !row.glow);
        QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->isHidden(), !row.dots);
    }
}

// A style switch publishes exactly one coherent TrailConfig and never
// touches colors or geometry fields.
void TestSettingsWindow::switching_styles_publishes_once_and_preserves_colors() {
    ptd::TrailConfig tc{};
    tc.style = ptd::TrailStyle::Classic;
    tc.glow_strength = 0.5f;
    tc.segment_spacing_px = 12.0f;
    tc.start_color_r = 10; tc.start_color_g = 20; tc.start_color_b = 30;
    tc.fade_color_r = 200; tc.fade_color_g = 210; tc.fade_color_b = 220;
    tc.color_mode = ptd::TrailColorMode::Gradient;
    tc.head_thickness_px = 6.0f;
    tc.lifetime_ms = 400.0f;
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    w.findChild<QPushButton*>("trail_style_neon")->click();  // Neon
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 0);
    const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(got.style, ptd::TrailStyle::Neon);
    QCOMPARE(got.color_mode, ptd::TrailColorMode::Gradient);
    QCOMPARE(got.start_color_r, 10);
    QCOMPARE(got.start_color_g, 20);
    QCOMPARE(got.start_color_b, 30);
    QCOMPARE(got.fade_color_r, 200);
    QCOMPARE(got.fade_color_g, 210);
    QCOMPARE(got.fade_color_b, 220);
    QCOMPARE(got.head_thickness_px, 6.0f);
    QCOMPARE(got.lifetime_ms, 400.0f);
    // Visibility updated immediately: Neon shows Glow, hides Dot spacing.
    QVERIFY(!w.findChild<SliderSpin*>("trail_glow")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("trail_spacing")->isHidden());
    // Glow/spacing widgets still show their values after the switch
    // (hidden controls keep their stored values).
    QCOMPARE(w.findChild<SliderSpin*>("trail_glow")->value(), 50.0);
    QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->value(), 12.0);

    // Restore Trail defaults returns style Classic + 50% + 12 px and the
    // Classic contextual surface (both parameter rows hidden again).
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto def = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(def.style, ptd::TrailStyle::Classic);
    QCOMPARE(def.glow_strength, 0.5f);
    QCOMPARE(def.segment_spacing_px, 12.0f);
    QVERIFY(w.findChild<SliderSpin*>("trail_glow")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("trail_spacing")->isHidden());
    QVERIFY(w.findChild<QPushButton*>("trail_style_classic")->isChecked());
}

// T-020R1 TARGET 3: Width / Tail width / Taper are PERMANENT SHAPE rows.
// The first T-020 pass hid Tail width at taper 0 and hid both it and Taper
// for Ribbon, which reflowed SHAPE and dragged the TRAIL anchor up and down
// while the user was still dragging the taper slider. A style or a taper
// value that ignores a row now leaves the live row in place.
void TestSettingsWindow::shape_rows_are_permanent_and_stable() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();

    auto* head = w.findChild<SliderSpin*>("trail_head_thickness");
    auto* tail = w.findChild<SliderSpin*>("trail_tail_thickness");
    auto* taper = w.findChild<SliderSpin*>("trail_taper");
    QVERIFY(head); QVERIFY(tail); QVERIFY(taper);

    // Default taper 0: every SHAPE row is present and live.
    QVERIFY(!head->isHidden());
    QVERIFY(!tail->isHidden());
    QVERIFY(!taper->isHidden());

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    // Taper 0 -> 60 -> 0: nothing appears, disappears or resets.
    taper->findChild<QSlider*>()->setValue(60);
    QCOMPARE(trailSpy.count(), 1);
    trailSpy.takeFirst();
    QVERIFY(!tail->isHidden());

    tail->findChild<QSlider*>()->setValue(120);  // 120 * 0.1 = 12 px
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(tail->value(), 12.0);
    trailSpy.takeFirst();

    taper->findChild<QSlider*>()->setValue(0);
    QCOMPARE(trailSpy.count(), 1);
    trailSpy.takeFirst();
    QVERIFY(!tail->isHidden());
    QCOMPARE(tail->value(), 12.0);

    // Ribbon owns its own forced width profile in the renderer, but the rows
    // stay visible and keep their stored values: no reflow, nothing disabled.
    w.findChild<QPushButton*>("trail_style_ribbon")->click();
    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(trailSpy.takeFirst().at(0).value<ptd::TrailConfig>().style,
             ptd::TrailStyle::Ribbon);
    QVERIFY(!head->isHidden());
    QVERIFY(!tail->isHidden());
    QVERIFY(!taper->isHidden());
    QVERIFY(head->isEnabled());
    QVERIFY(tail->isEnabled());
    QVERIFY(taper->isEnabled());
    QCOMPARE(tail->value(), 12.0);

    // Back to Classic: same rows, same values, one coherent publication.
    w.findChild<QPushButton*>("trail_style_classic")->click();
    QCOMPARE(trailSpy.count(), 1);
    trailSpy.takeFirst();
    QVERIFY(!tail->isHidden());
    QCOMPARE(tail->value(), 12.0);
}

// T-020R1 TARGET 5: the Trail controls speak the user's language. Internal
// terminology (head thickness / tail thickness / lifetime) never reaches a
// label, and the controls replaced by T-020 button selectors stay buttons --
// no combo box may come back anywhere in the window.
void TestSettingsWindow::trail_control_labels_are_user_facing() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto label_of = [&](const char* name) {
        auto* s = w.findChild<SliderSpin*>(name);
        if (s == nullptr) return QStringLiteral("<missing>");
        auto* l = s->findChild<QLabel*>();
        return l == nullptr ? QStringLiteral("<no label>") : l->text();
    };
    QCOMPARE(label_of("trail_head_thickness"), QStringLiteral("Width"));
    QCOMPARE(label_of("trail_taper"), QStringLiteral("Taper"));
    QCOMPARE(label_of("trail_tail_thickness"), QStringLiteral("Tail width"));
    QCOMPARE(label_of("trail_lifetime"), QStringLiteral("Duration"));
    QCOMPARE(label_of("trail_opacity"), QStringLiteral("Opacity"));
    QCOMPARE(label_of("trail_smoothing"), QStringLiteral("Smoothness"));
    QCOMPARE(label_of("trail_fade_start"), QStringLiteral("Fade start"));

    // Fade curve stays an exclusive button selector.
    QVERIFY(w.findChild<QPushButton*>("trail_fade_linear"));
    QVERIFY(w.findChild<QPushButton*>("trail_fade_smooth"));
    QVERIFY(w.findChild<QPushButton*>("trail_fade_ease_out"));
    QVERIFY(w.findChildren<QComboBox*>().isEmpty());
}

// T-020R1 TARGET 3: the STYLE / COLOR / SHAPE / TRAIL anchors must not move
// when the Trail style changes. The style-specific controls live in one
// bounded region inside STYLE, so every later section keeps its exact
// vertical position for all eight styles.
void TestSettingsWindow::trail_section_anchors_stable_across_styles() {
    const QStringList style_names = {
        "trail_style_classic", "trail_style_soft_glow", "trail_style_comet",
        "trail_style_neon", "trail_style_dotted", "trail_style_pulse",
        "trail_style_ribbon", "trail_style_spark"};
    const QStringList anchor_titles = {"STYLE", "COLOR", "SHAPE", "TRAIL"};

    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);  // Trail
    QWidget* page = tabs->widget(1);
    QVERIFY(page);
    // The Trail page was not the current tab when the window was shown; let
    // its scroll area settle before any anchor is measured.
    QApplication::processEvents();
    QTest::qWait(50);
    QApplication::processEvents();

    auto anchor_label = [&](const QString& title) -> QLabel* {
        for (auto* l : page->findChildren<QLabel*>()) {
            if (l->objectName() == QStringLiteral("sectionLabel")
                && l->text() == title) {
                return l;
            }
        }
        return nullptr;
    };
    QList<QLabel*> anchors;
    for (const QString& title : anchor_titles) {
        auto* label = anchor_label(title);
        QVERIFY2(label != nullptr, title.toUtf8().constData());
        anchors.append(label);
    }
    auto anchor_points = [&]() {
        QList<QPoint> points;
        for (auto* label : anchors) {
            points.append(label->mapTo(page, QPoint(0, 0)));
        }
        return points;
    };

    auto* options = w.findChild<QWidget*>("trail_style_options");
    QVERIFY(options != nullptr);
    const int reserved = options->height();
    QVERIFY(reserved > 0);

    const QList<QPoint> baseline = anchor_points();
    QVERIFY(baseline.size() == anchor_titles.size());

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    for (const QString& name : style_names) {
        w.findChild<QPushButton*>(name)->click();
        QCOMPARE(trailSpy.count(), 1);
        trailSpy.takeFirst();
        QApplication::processEvents();
        QTest::qWait(20);
        QApplication::processEvents();
        QVERIFY2(anchor_points() == baseline, name.toUtf8().constData());
        QCOMPARE(options->height(), reserved);
        QVERIFY(!options->isHidden());
    }

    // The style-specific control itself swaps inside the reserved region:
    // Glow for the glow styles, Dot spacing for the dot styles, nothing for
    // Classic / Comet / Pulse / Ribbon.
    auto inside_options = [&](const char* name) {
        auto* s = w.findChild<SliderSpin*>(name);
        return s != nullptr && !s->isHidden()
            && options->isAncestorOf(s);
    };
    w.findChild<QPushButton*>("trail_style_neon")->click();
    QVERIFY(inside_options("trail_glow"));
    QVERIFY(!inside_options("trail_spacing"));
    w.findChild<QPushButton*>("trail_style_spark")->click();
    QVERIFY(inside_options("trail_spacing"));
    QVERIFY(!inside_options("trail_glow"));
    w.findChild<QPushButton*>("trail_style_ribbon")->click();
    QVERIFY(!inside_options("trail_glow"));
    QVERIFY(!inside_options("trail_spacing"));
    QVERIFY2(anchor_points() == baseline, "ribbon");
}

// T-020R1 TARGET 3 (Click): the Particle row is style-specific (Burst /
// Spark Burst) and swaps inside a bounded region, so the TRIGGERS / COLOR /
// ANIMATION anchors keep their positions across every click style.
void TestSettingsWindow::click_style_options_region_reserves_space() {
    const QStringList style_names = {
        "click_style_ring", "click_style_double_ring", "click_style_ripple",
        "click_style_burst", "click_style_spark_burst",
        "click_style_soft_flash", "click_style_dot_ring",
        // T-022: the elemental styles must hold the anchors too -- they show
        // an extra Element tint row, and Fire hides Outline.
        "click_style_air", "click_style_fire", "click_style_water",
        "click_style_earth"};
    const QStringList anchor_titles = {
        "STYLE", "TRIGGERS", "COLOR", "ANIMATION"};

    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(2);  // Click
    QWidget* page = tabs->widget(2);
    QVERIFY(page);
    QApplication::processEvents();
    QTest::qWait(50);
    QApplication::processEvents();

    auto anchor_label = [&](const QString& title) -> QLabel* {
        for (auto* l : page->findChildren<QLabel*>()) {
            if (l->objectName() == QStringLiteral("sectionLabel")
                && l->text() == title) {
                return l;
            }
        }
        return nullptr;
    };
    QList<QLabel*> anchors;
    for (const QString& title : anchor_titles) {
        auto* label = anchor_label(title);
        QVERIFY2(label != nullptr, title.toUtf8().constData());
        anchors.append(label);
    }
    // T-021 audit repair (same flake class as the E-115 lifecycle repair):
    // measure anchors relative to their SHARED content ancestor, not to
    // the tab page -- mapTo(page) includes the scroll viewport offset, so
    // a click that triggers scroll-into-view shifts every measured anchor
    // without any layout change. Content-relative measurement keeps the
    // T-020R1 invariant (sections never move relative to each other) and
    // removes the viewport term that made this test scroll-sensitive.
    QWidget* content_parent = anchors.first()->parentWidget();
    QVERIFY(content_parent != nullptr);
    // Walk to a stable ancestor that contains ALL anchor labels (section
    // labels may be reparented by the scroll-area layout, so the direct
    // parent is not guaranteed to be the shared one).
    while (content_parent->parentWidget() != nullptr) {
        bool all_inside = true;
        for (const auto* label : anchors) {
            if (!content_parent->isAncestorOf(label)) {
                all_inside = false;
                break;
            }
        }
        if (all_inside) break;
        content_parent = content_parent->parentWidget();
    }
    auto anchor_points = [&]() {
        QList<QPoint> points;
        for (auto* label : anchors) {
            points.append(label->mapTo(content_parent, QPoint(0, 0)));
        }
        return points;
    };

    auto* options = w.findChild<QWidget*>("click_style_options");
    auto* particles = w.findChild<SliderSpin*>("click_particles");
    QVERIFY(options != nullptr);
    QVERIFY(particles != nullptr);
    QVERIFY(options->isAncestorOf(particles));
    const int reserved = options->height();
    QVERIFY(reserved > 0);
    const QList<QPoint> baseline = anchor_points();

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    for (const QString& name : style_names) {
        w.findChild<QPushButton*>(name)->click();
        QCOMPARE(clickSpy.count(), 1);
        clickSpy.takeFirst();
        QApplication::processEvents();
        QTest::qWait(20);
        QApplication::processEvents();
        const QList<QPoint> after = anchor_points();
        if (after != baseline) {
            for (int k = 0; k < after.size() && k < baseline.size(); ++k) {
                qWarning("anchor '%s' moved by (%d, %d)",
                         qPrintable(anchor_titles.at(k)),
                         after.at(k).x() - baseline.at(k).x(),
                         after.at(k).y() - baseline.at(k).y());
            }
        }
        QVERIFY2(after == baseline, name.toUtf8().constData());
        QCOMPARE(options->height(), reserved);
        QVERIFY(!options->isHidden());
    }
}

// T-020R1 TARGET 4: the COLOR section swaps its two editors at runtime, and
// Qt's focus chain is built in widget-creation order -- so Tab used to keep
// walking into whichever editor had MOVED. Repeated
// Solid -> Head -> Tail -> Gradient -> Solid must leave the keyboard path
// matching what is drawn, every time.
void TestSettingsWindow::color_mode_tab_order_matches_visual_order() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);  // Trail
    QWidget* page = tabs->widget(1);
    // The Trail page was not the current tab when the window was shown, so
    // let its scroll area settle before measuring anything.
    QApplication::processEvents();
    QTest::qWait(50);
    QApplication::processEvents();
    auto* area = page->findChild<QScrollArea*>();
    QVERIFY(area != nullptr);
    QWidget* content = area->widget();
    QVERIFY(content != nullptr);

    auto* start_ed = w.findChild<ColorEditor*>("trail_start_color");
    auto* fade_ed = w.findChild<ColorEditor*>("trail_fade_color");
    auto* mode_group = w.findChild<QButtonGroup*>("trail_color_mode");
    QVERIFY(start_ed && fade_ed && mode_group);

    // The COLOR section's own interactive widgets: the four mode buttons and
    // everything inside both editors.
    auto in_color_section = [&](QWidget* x) {
        if (x == start_ed || x == fade_ed
            || start_ed->isAncestorOf(x) || fade_ed->isAncestorOf(x)) {
            return true;
        }
        auto* as_button = qobject_cast<QAbstractButton*>(x);
        return as_button != nullptr && mode_group->buttons().contains(as_button);
    };
    auto color_section_in_chain_order = [&]() {
        QList<QWidget*> out;
        QWidget* cur = content->nextInFocusChain();
        while (cur != content) {
            if (content->isAncestorOf(cur) && cur->isVisible()
                && (cur->focusPolicy() & Qt::TabFocus)
                && in_color_section(cur)) {
                out.append(cur);
            }
            cur = cur->nextInFocusChain();
        }
        return out;
    };
    auto describe = [content](const QList<QWidget*>& widgets) {
        QString out;
        for (auto* x : widgets) {
            const QPoint p = x->mapTo(content, QPoint(0, 0));
            out += QStringLiteral("[%1 %2,%3]")
                       .arg(x->objectName().isEmpty()
                                ? QStringLiteral("<unnamed>")
                                : x->objectName())
                       .arg(p.x())
                       .arg(p.y());
        }
        return out;
    };

    QSignalSpy spy(&w, &SettingsWindow::trail_config_changed);
    const QStringList modes = {"trail_color_solid", "trail_color_head",
                               "trail_color_tail", "trail_color_gradient",
                               "trail_color_solid", "trail_color_head",
                               "trail_color_tail", "trail_color_gradient",
                               "trail_color_solid"};
    for (const QString& mode : modes) {
        w.findChild<QPushButton*>(mode)->click();
        QCOMPARE(spy.count(), 1);
        spy.takeFirst();
        QApplication::processEvents();
        QTest::qWait(20);
        QApplication::processEvents();

        const auto chain = color_section_in_chain_order();
        QVERIFY2(!chain.isEmpty(), mode.toUtf8().constData());

        // The mode buttons are drawn above both editors, so they come first.
        const auto start_children = start_ed->focusable_children();
        const auto fade_children = fade_ed->focusable_children();
        const bool fade_visible = !fade_ed->isHidden();
        QVERIFY(!start_ed->isHidden());
        const int start_first_index = chain.indexOf(start_children.first());
        const int start_last_index = chain.indexOf(start_children.last());
        const int fade_first_index =
            fade_visible ? chain.indexOf(fade_children.first()) : -1;
        const int fade_last_index =
            fade_visible ? chain.indexOf(fade_children.last()) : -1;
        QVERIFY(start_first_index >= 0);
        if (fade_visible) {
            QVERIFY2(fade_first_index >= 0,
                     qPrintable(QStringLiteral("%1 chain=%2")
                                    .arg(mode, describe(chain))));
        }
        const int editor_top = fade_visible
            ? qMin(start_first_index, fade_first_index)
            : start_first_index;
        for (auto* button : mode_group->buttons()) {
            const int index = chain.indexOf(button);
            QVERIFY2(index >= 0, button->objectName().toUtf8().constData());
            QVERIFY2(index < editor_top,
                     qPrintable(QStringLiteral("%1 chain=%2")
                                    .arg(mode, describe(chain))));
        }

        // Key order of the two mode-button rows by x (the row itself cannot
        // move): Solid, Head, Tail, Gradient.
        QList<QAbstractButton*> mode_row = mode_group->buttons();
        std::stable_sort(mode_row.begin(), mode_row.end(),
                         [content](QAbstractButton* a, QAbstractButton* b) {
                             return a->mapTo(content, QPoint(0, 0)).x()
                                  < b->mapTo(content, QPoint(0, 0)).x();
                         });
        for (int i = 1; i < mode_row.size(); ++i) {
            QVERIFY(chain.indexOf(mode_row.at(i - 1))
                    < chain.indexOf(mode_row.at(i)));
        }

        // Both editors visible (Head/Tail/Gradient): the editor drawn first
        // must own the earlier block of the focus chain, with no interleaving
        // and nothing of the other editor in between.
        if (fade_visible) {
            QVERIFY(start_last_index >= 0);
            QVERIFY(fade_last_index >= 0);
            QVERIFY2(start_last_index < fade_first_index
                         || fade_last_index < start_first_index,
                     qPrintable(QStringLiteral("%1 chain=%2")
                                    .arg(mode, describe(chain))));
            const int start_top = start_ed->mapTo(content, QPoint(0, 0)).y();
            const int fade_top = fade_ed->mapTo(content, QPoint(0, 0)).y();
            QVERIFY(start_top != fade_top);
            if (start_top < fade_top) QVERIFY(start_last_index < fade_first_index);
            else QVERIFY(fade_last_index < start_first_index);
        }
    }
}

// T-020 Phase 7/17: three curve buttons; Smooth (T-019 default) visibly
// selected; each click publishes the right enum exactly once.
void TestSettingsWindow::fade_curve_buttons_three() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* linear = w.findChild<QPushButton*>("trail_fade_linear");
    auto* smooth = w.findChild<QPushButton*>("trail_fade_smooth");
    auto* ease = w.findChild<QPushButton*>("trail_fade_ease_out");
    QVERIFY(linear); QVERIFY(smooth); QVERIFY(ease);
    QVERIFY(linear->isCheckable()); QVERIFY(smooth->isCheckable());
    QVERIFY(ease->isCheckable());
    QVERIFY(smooth->isChecked());
    QVERIFY(!linear->isChecked());
    QVERIFY(!ease->isChecked());

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    linear->click();
    QCOMPARE(trailSpy.count(), 1);
    {
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.fade_curve, ptd::FadeCurve::Linear);
    }
    QVERIFY(linear->isChecked());
    QVERIFY(!smooth->isChecked());

    ease->click();
    QCOMPARE(trailSpy.count(), 1);
    {
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.fade_curve, ptd::FadeCurve::EaseOut);
    }

    // Restore Trail Defaults visibly re-selects Smooth.
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    {
        const auto got = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
        QCOMPARE(got.fade_curve, ptd::FadeCurve::Smooth);
    }
    QVERIFY(smooth->isChecked());
}

// T-020 Phase 10/17: seven checkable style buttons; exactly one active;
// every enum loads to the correct button.
void TestSettingsWindow::click_style_buttons_seven_grid() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    const QList<QPushButton*> style_buttons{
        w.findChild<QPushButton*>("click_style_ring"),
        w.findChild<QPushButton*>("click_style_double_ring"),
        w.findChild<QPushButton*>("click_style_ripple"),
        w.findChild<QPushButton*>("click_style_burst"),
        w.findChild<QPushButton*>("click_style_spark_burst"),
        w.findChild<QPushButton*>("click_style_soft_flash"),
        w.findChild<QPushButton*>("click_style_dot_ring"),
    };
    const QStringList labels = {
        "Ring", "Double Ring", "Ripple", "Burst",
        "Spark Burst", "Soft Flash", "Dot + Ring"};
    for (int i = 0; i < style_buttons.size(); ++i) {
        QVERIFY2(style_buttons[i], labels.at(i).toUtf8().constData());
        QVERIFY(style_buttons[i]->isCheckable());
        QCOMPARE(style_buttons[i]->accessibleName(), labels.at(i));
        QCOMPARE(style_buttons[i]->focusPolicy(), Qt::StrongFocus);
    }
    int checked = 0;
    for (auto* b : style_buttons) if (b->isChecked()) ++checked;
    QCOMPARE(checked, 1);
    QVERIFY(style_buttons[0]->isChecked());  // Ring default
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), 8.0);

    // Every enum loads to the correct selected button.
    const ptd::ClickStyle styles[7] = {
        ptd::ClickStyle::Ring,      ptd::ClickStyle::DoubleRing,
        ptd::ClickStyle::Ripple,    ptd::ClickStyle::Burst,
        ptd::ClickStyle::SparkBurst, ptd::ClickStyle::SoftFlash,
        ptd::ClickStyle::DotRing};
    for (int i = 0; i < 7; ++i) {
        ptd::ClickConfig c{};
        c.style = styles[i];
        SettingsWindow wc(tc, c);
        wc.show();
        auto* btn = wc.findChild<QPushButton*>(style_buttons[i]->objectName());
        QVERIFY2(btn && btn->isChecked(), labels.at(i).toUtf8().constData());
    }
}

// T-020 Phase 11/17: only style-relevant click rows exist; irrelevant rows
// are hidden, never merely disabled, and never reset.
void TestSettingsWindow::click_parameter_visibility_matrix() {
    ptd::TrailConfig tc{};
    const struct { ptd::ClickStyle style; bool particles; bool fill;
                   bool outline; } rows[] = {
        {ptd::ClickStyle::Ring,       false, true,  true},
        {ptd::ClickStyle::DoubleRing, false, false, true},
        {ptd::ClickStyle::Ripple,     false, false, true},
        {ptd::ClickStyle::Burst,      true,  false, true},
        {ptd::ClickStyle::SparkBurst, true,  false, false},
        {ptd::ClickStyle::SoftFlash,  false, false, false},
        {ptd::ClickStyle::DotRing,    false, true,  true},
    };
    for (const auto& row : rows) {
        ptd::ClickConfig cc{};
        cc.style = row.style;
        SettingsWindow w(tc, cc);
        w.show();
        QCOMPARE(w.findChild<SliderSpin*>("click_particles")->isHidden(),
                 !row.particles);
        QCOMPARE(w.findChild<SliderSpin*>("click_fill")->isHidden(),
                 !row.fill);
        QCOMPARE(w.findChild<SliderSpin*>("click_outline")->isHidden(),
                 !row.outline);
        // Always-visible animation contract rows.
        QVERIFY(!w.findChild<SliderSpin*>("click_start_size")->isHidden());
        QVERIFY(!w.findChild<SliderSpin*>("click_end_size")->isHidden());
        QVERIFY(!w.findChild<SliderSpin*>("click_duration")->isHidden());
        QVERIFY(!w.findChild<SliderSpin*>("click_opacity")->isHidden());
    }
}

// T-017: switching click style publishes exactly one coherent ClickConfig
// and does not publish trail config or corrupt other click fields.
void TestSettingsWindow::switching_click_styles_publishes_once_and_preserves_settings() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.style = ptd::ClickStyle::Ring;
    cc.particle_amount = 8;
    cc.color_r = 15; cc.color_g = 25; cc.color_b = 35;
    cc.start_radius_px = 12.0f;
    cc.end_radius_px = 40.0f;
    cc.duration_ms = 350.0f;
    cc.base_opacity = 0.85f;
    cc.outline_thickness_px = 2.5f;
    cc.fill_opacity = 0.15f;
    cc.easing = ptd::ClickEasing::Smooth;

    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    w.findChild<QPushButton*>("click_style_spark_burst")->click();  // Spark Burst
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);

    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got.style, ptd::ClickStyle::SparkBurst);
    QCOMPARE(got.particle_amount, uint8_t(8));
    QCOMPARE(got.color_r, uint8_t(15));
    QCOMPARE(got.color_g, uint8_t(25));
    QCOMPARE(got.color_b, uint8_t(35));
    QCOMPARE(got.start_radius_px, 12.0f);
    QCOMPARE(got.end_radius_px, 40.0f);
    QCOMPARE(got.duration_ms, 350.0f);
    QCOMPARE(got.base_opacity, 0.85f);
    QCOMPARE(got.outline_thickness_px, 2.5f);
    QCOMPARE(got.fill_opacity, 0.15f);
    QCOMPARE(got.easing, ptd::ClickEasing::Smooth);
    // Visibility followed immediately: particles shown, fill + outline
    // hidden; hidden controls keep their stored values.
    QVERIFY(!w.findChild<SliderSpin*>("click_particles")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("click_fill")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("click_outline")->isHidden());
    QCOMPARE(w.findChild<SliderSpin*>("click_fill")->value(), 15.0);
    QCOMPARE(w.findChild<SliderSpin*>("click_outline")->value(), 2.5);
}

// T-017: click particle amount slider and spin synchronize and emit once per change.
void TestSettingsWindow::click_particle_amount_slider_spin_sync_and_bounds() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* particles = w.findChild<SliderSpin*>("click_particles");
    QVERIFY(particles);
    QCOMPARE(particles->value(), 8.0);
    auto* slider = particles->findChild<QSlider*>();
    auto* spin = particles->findChild<QDoubleSpinBox*>();
    QVERIFY(slider); QVERIFY(spin);

    // Initial bounds
    QCOMPARE(spin->minimum(), static_cast<double>(ptd::ClickConfig::kMinParticleAmount));
    QCOMPARE(spin->maximum(), static_cast<double>(ptd::ClickConfig::kMaxParticleAmount));

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    // User edits slider -> updates spin and publishes once
    slider->setValue(20);
    QCOMPARE(clickSpy.count(), 1);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got.particle_amount, uint8_t(20));
    QCOMPARE(spin->value(), 20.0);

    // User edits spin -> updates slider and publishes once
    spin->setValue(12.0);
    QCOMPARE(clickSpy.count(), 1);
    const auto got2 = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(got2.particle_amount, uint8_t(12));
    QCOMPARE(slider->value(), 12);
}

// T-017: restore click defaults resets style to Ring and particles to 8.
void TestSettingsWindow::restore_click_returns_ring_and_default_particles() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.style = ptd::ClickStyle::SoftFlash;
    cc.particle_amount = 16;
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 1);

    const auto def = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(def.style, ptd::ClickStyle::Ring);
    QCOMPARE(def.particle_amount, uint8_t(8));
    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("click_easing_ease_out")->isChecked());
    // Default visibility restored: particles hidden, fill + outline shown.
    QVERIFY(w.findChild<SliderSpin*>("click_particles")->isHidden());
    QVERIFY(!w.findChild<SliderSpin*>("click_fill")->isHidden());
    QVERIFY(!w.findChild<SliderSpin*>("click_outline")->isHidden());
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), 8.0);
}

// T-020 Phase 12/17: three easing buttons; Ease Out default visibly
// selected; each click publishes the right enum exactly once.
void TestSettingsWindow::click_easing_buttons_three() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* linear = w.findChild<QPushButton*>("click_easing_linear");
    auto* smooth = w.findChild<QPushButton*>("click_easing_smooth");
    auto* ease = w.findChild<QPushButton*>("click_easing_ease_out");
    QVERIFY(linear); QVERIFY(smooth); QVERIFY(ease);
    QVERIFY(linear->isCheckable()); QVERIFY(smooth->isCheckable());
    QVERIFY(ease->isCheckable());
    QVERIFY(ease->isChecked());
    QVERIFY(!linear->isChecked());
    QVERIFY(!smooth->isChecked());

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    linear->click();
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    {
        const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
        QCOMPARE(got.easing, ptd::ClickEasing::Linear);
    }
    smooth->click();
    QCOMPARE(clickSpy.count(), 1);
    {
        const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
        QCOMPARE(got.easing, ptd::ClickEasing::Smooth);
    }
    QVERIFY(smooth->isChecked());
    QVERIFY(!ease->isChecked());
}

// T-018 Phase 8: Restore All Defaults from heavily modified state.
void TestSettingsWindow::restore_all_defaults_from_heavily_modified_state() {
    ptd::TrailConfig modified_trail{};
    modified_trail.enabled = false;
    modified_trail.style = ptd::TrailStyle::Neon;
    modified_trail.head_thickness_px = 24.0f;
    modified_trail.tail_thickness_px = 12.0f;
    modified_trail.taper_strength = 0.80f;
    modified_trail.lifetime_ms = 850.0f;
    modified_trail.base_opacity = 0.60f;
    modified_trail.smoothing = 0.85f;
    modified_trail.fade_start = 0.40f;
    modified_trail.fade_curve = ptd::FadeCurve::EaseOut;
    modified_trail.glow_strength = 0.75f;
    modified_trail.segment_spacing_px = 16.0f;
    modified_trail.color_mode = ptd::TrailColorMode::Gradient;
    modified_trail.start_color_r = 10;
    modified_trail.start_color_g = 200;
    modified_trail.start_color_b = 50;
    modified_trail.fade_color_r = 250;
    modified_trail.fade_color_g = 10;
    modified_trail.fade_color_b = 100;

    ptd::ClickConfig modified_click{};
    modified_click.enabled = false;
    modified_click.style = ptd::ClickStyle::Burst;
    modified_click.particle_amount = 20;
    modified_click.trigger_left = false;
    modified_click.trigger_right = true;
    modified_click.trigger_middle = true;
    modified_click.color_r = 120;
    modified_click.color_g = 40;
    modified_click.color_b = 210;
    modified_click.start_radius_px = 16.0f;
    modified_click.end_radius_px = 55.0f;
    modified_click.duration_ms = 450.0f;
    modified_click.base_opacity = 0.50f;
    modified_click.outline_thickness_px = 4.0f;
    modified_click.fill_opacity = 0.35f;
    modified_click.easing = ptd::ClickEasing::Linear;

    // Master is OFF initially
    const bool master_start = false;
    SettingsWindow w(modified_trail, modified_click, master_start);
    w.show();

    auto* chkMaster = w.findChild<QCheckBox*>("chk_master");
    auto* chkTrail = w.findChild<QCheckBox*>("chk_trail");
    auto* chkClick = w.findChild<QCheckBox*>("chk_click");
    QVERIFY(chkMaster && chkTrail && chkClick);
    QCOMPARE(chkMaster->isChecked(), false);
    QCOMPARE(chkTrail->isChecked(), false);
    QCOMPARE(chkClick->isChecked(), false);

    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailEnableSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickEnableSpy(&w, &SettingsWindow::click_enabled_changed);
    QSignalSpy presetSpy(&w, &SettingsWindow::preset_applied);
    QSignalSpy bulkSpy(&w, &SettingsWindow::app_config_applied);

    auto* btn_restore_all = w.findChild<QPushButton*>("btn_restore_all");
    QVERIFY(btn_restore_all != nullptr);
    btn_restore_all->click();

    // CORE-003 + W2-002: ONE bulk publication replaces the narrow fan-out.
    QCOMPARE(bulkSpy.count(), 1);
    QCOMPARE(masterSpy.count(), 0);
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(trailEnableSpy.count(), 0);
    QCOMPARE(clickEnableSpy.count(), 0);
    QCOMPARE(presetSpy.count(), 0);

    const auto bulkCfg = bulkSpy.takeFirst().at(0).value<ptd::AppConfig>();
    QCOMPARE(bulkCfg.master_enabled, true);
    QCOMPARE(chkMaster->isChecked(), true);

    // Checkboxes match defaults
    QCOMPARE(chkTrail->isChecked(), true);
    QCOMPARE(chkTrail->isEnabled(), true);
    QCOMPARE(chkClick->isChecked(), true);
    QCOMPARE(chkClick->isEnabled(), true);

    // Exact TrailConfig and ClickConfig defaults
    const ptd::TrailConfig def_trail{};
    const ptd::ClickConfig def_click{};
    const auto gotTrail = bulkCfg.trail;
    const auto gotClick = bulkCfg.click;

    QCOMPARE(gotTrail.enabled, def_trail.enabled);
    QCOMPARE(gotTrail.style, def_trail.style);
    QCOMPARE(gotTrail.head_thickness_px, def_trail.head_thickness_px);
    QCOMPARE(gotTrail.tail_thickness_px, def_trail.tail_thickness_px);
    QCOMPARE(gotTrail.taper_strength, def_trail.taper_strength);
    QCOMPARE(gotTrail.lifetime_ms, def_trail.lifetime_ms);
    QCOMPARE(gotTrail.base_opacity, def_trail.base_opacity);
    QCOMPARE(gotTrail.smoothing, def_trail.smoothing);
    QCOMPARE(gotTrail.fade_start, def_trail.fade_start);
    QCOMPARE(gotTrail.fade_curve, def_trail.fade_curve);
    QCOMPARE(gotTrail.glow_strength, def_trail.glow_strength);
    QCOMPARE(gotTrail.segment_spacing_px, def_trail.segment_spacing_px);
    QCOMPARE(gotTrail.color_mode, def_trail.color_mode);
    QCOMPARE(gotTrail.start_color_r, def_trail.start_color_r);
    QCOMPARE(gotTrail.start_color_g, def_trail.start_color_g);
    QCOMPARE(gotTrail.start_color_b, def_trail.start_color_b);
    QCOMPARE(gotTrail.fade_color_r, def_trail.fade_color_r);
    QCOMPARE(gotTrail.fade_color_g, def_trail.fade_color_g);
    QCOMPARE(gotTrail.fade_color_b, def_trail.fade_color_b);

    QCOMPARE(gotClick.enabled, def_click.enabled);
    QCOMPARE(gotClick.style, def_click.style);
    QCOMPARE(gotClick.particle_amount, def_click.particle_amount);
    QCOMPARE(gotClick.trigger_left, def_click.trigger_left);
    QCOMPARE(gotClick.trigger_right, def_click.trigger_right);
    QCOMPARE(gotClick.trigger_middle, def_click.trigger_middle);
    QCOMPARE(gotClick.color_r, def_click.color_r);
    QCOMPARE(gotClick.color_g, def_click.color_g);
    QCOMPARE(gotClick.color_b, def_click.color_b);
    QCOMPARE(gotClick.start_radius_px, def_click.start_radius_px);
    QCOMPARE(gotClick.end_radius_px, def_click.end_radius_px);
    QCOMPARE(gotClick.duration_ms, def_click.duration_ms);
    QCOMPARE(gotClick.base_opacity, def_click.base_opacity);
    QCOMPARE(gotClick.outline_thickness_px, def_click.outline_thickness_px);
    QCOMPARE(gotClick.fill_opacity, def_click.fill_opacity);
    QCOMPARE(gotClick.easing, def_click.easing);

    // Every visible widget matches emitted config (T-020: selectors are
    // buttons now; Classic/Solid/Smooth + Ring/Ease Out visibly active).
    QVERIFY(w.findChild<QPushButton*>("trail_style_classic")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_fade_smooth")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("trail_color_solid")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    QVERIFY(w.findChild<QPushButton*>("click_easing_ease_out")->isChecked());
    // Default contextual surfaces restored: the style options region is
    // empty for Classic, and the SHAPE rows are permanent (T-020R1).
    QVERIFY(w.findChild<SliderSpin*>("trail_glow")->isHidden());
    QVERIFY(w.findChild<SliderSpin*>("trail_spacing")->isHidden());
    QVERIFY(!w.findChild<SliderSpin*>("trail_tail_thickness")->isHidden());
    QCOMPARE(w.findChild<SliderSpin*>("trail_head_thickness")->value(), static_cast<double>(def_trail.head_thickness_px));
    QCOMPARE(w.findChild<SliderSpin*>("trail_tail_thickness")->value(), static_cast<double>(def_trail.tail_thickness_px));
    QVERIFY(qAbs(w.findChild<SliderSpin*>("trail_taper")->value() - def_trail.taper_strength * 100.0) < 0.01);
    QCOMPARE(w.findChild<SliderSpin*>("trail_lifetime")->value(), static_cast<double>(def_trail.lifetime_ms));
    QVERIFY(qAbs(w.findChild<SliderSpin*>("trail_opacity")->value() - def_trail.base_opacity * 100.0) < 0.01);
    QVERIFY(qAbs(w.findChild<SliderSpin*>("trail_smoothing")->value() - def_trail.smoothing * 100.0) < 0.01);
    QVERIFY(qAbs(w.findChild<SliderSpin*>("trail_fade_start")->value() - def_trail.fade_start * 100.0) < 0.01);
    QVERIFY(qAbs(w.findChild<SliderSpin*>("trail_glow")->value() - def_trail.glow_strength * 100.0) < 0.01);
    QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->value(), static_cast<double>(def_trail.segment_spacing_px));

    int tr, tg, tb;
    w.findChild<ColorEditor*>("trail_start_color")->get(tr, tg, tb);
    QCOMPARE(tr, static_cast<int>(def_trail.start_color_r));
    QCOMPARE(tg, static_cast<int>(def_trail.start_color_g));
    QCOMPARE(tb, static_cast<int>(def_trail.start_color_b));

    w.findChild<ColorEditor*>("trail_fade_color")->get(tr, tg, tb);
    QCOMPARE(tr, static_cast<int>(def_trail.fade_color_r));
    QCOMPARE(tg, static_cast<int>(def_trail.fade_color_g));
    QCOMPARE(tb, static_cast<int>(def_trail.fade_color_b));

    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), static_cast<double>(def_click.particle_amount));
    QCOMPARE(w.findChild<QCheckBox*>("chk_trig_left")->isChecked(), def_click.trigger_left);
    QCOMPARE(w.findChild<QCheckBox*>("chk_trig_right")->isChecked(), def_click.trigger_right);
    QCOMPARE(w.findChild<QCheckBox*>("chk_trig_middle")->isChecked(), def_click.trigger_middle);
    QCOMPARE(w.findChild<SliderSpin*>("click_start_size")->value(), static_cast<double>(def_click.start_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_end_size")->value(), static_cast<double>(def_click.end_radius_px));
    QCOMPARE(w.findChild<SliderSpin*>("click_duration")->value(), static_cast<double>(def_click.duration_ms));
    QVERIFY(qAbs(w.findChild<SliderSpin*>("click_opacity")->value() - def_click.base_opacity * 100.0) < 0.01);
    QCOMPARE(w.findChild<SliderSpin*>("click_outline")->value(), static_cast<double>(def_click.outline_thickness_px));
    QVERIFY(qAbs(w.findChild<SliderSpin*>("click_fill")->value() - def_click.fill_opacity * 100.0) < 0.01);

    int cr, cg, cb;
    w.findChild<ColorEditor*>("click_color")->get(cr, cg, cb);
    QCOMPARE(cr, static_cast<int>(def_click.color_r));
    QCOMPARE(cg, static_cast<int>(def_click.color_g));
    QCOMPARE(cb, static_cast<int>(def_click.color_b));
}

// T-018 Phase 8: Existing per-section Restore Trail and Restore Click behavior remains unchanged.
void TestSettingsWindow::restore_per_section_preserves_enable_checkboxes() {
    ptd::TrailConfig tc{};
    tc.enabled = false;
    tc.head_thickness_px = 25.0f;
    ptd::ClickConfig cc{};
    cc.enabled = false;
    cc.duration_ms = 500.0f;

    SettingsWindow w(tc, cc, true);
    w.show();

    auto* chkTrail = w.findChild<QCheckBox*>("chk_trail");
    auto* chkClick = w.findChild<QCheckBox*>("chk_click");
    QVERIFY(chkTrail && chkClick);
    QCOMPARE(chkTrail->isChecked(), false);
    QCOMPARE(chkClick->isChecked(), false);

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    // Restore Trail defaults: preserves disabled state of chk_trail
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto rtrail = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(rtrail.enabled, false);
    QCOMPARE(chkTrail->isChecked(), false);

    // Restore Click defaults: preserves disabled state of chk_click
    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(clickSpy.count(), 1);
    const auto rclick = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(rclick.enabled, false);
    QCOMPARE(chkClick->isChecked(), false);
}

// T-018R1 Phase 9: Restore All while master is ALREADY true must not emit
// a fake master transition. Contract: zero master publications, exactly one
// Trail config, exactly one Click config, zero duplicate enabled
// publications, zero preset publications.
void TestSettingsWindow::restore_all_with_master_already_true_emits_no_fake_master_transition() {
    ptd::TrailConfig tc{};
    tc.head_thickness_px = 30.0f;
    tc.lifetime_ms = 1500.0f;
    ptd::ClickConfig cc{};
    cc.duration_ms = 600.0f;
    cc.particle_amount = 10;

    SettingsWindow w(tc, cc, true); // master ON from the start
    w.show();

    auto* chkMaster = w.findChild<QCheckBox*>("chk_master");
    QVERIFY(chkMaster);
    QCOMPARE(chkMaster->isChecked(), true);

    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailEnableSpy(&w, &SettingsWindow::trail_enabled_changed);
    QSignalSpy clickEnableSpy(&w, &SettingsWindow::click_enabled_changed);
    QSignalSpy presetSpy(&w, &SettingsWindow::preset_applied);
    QSignalSpy bulkSpy(&w, &SettingsWindow::app_config_applied);

    auto* btn_restore_all = w.findChild<QPushButton*>("btn_restore_all");
    QVERIFY(btn_restore_all != nullptr);
    btn_restore_all->click();

    // CORE-003 + W2-002: one bulk publication, no narrow fan-out.
    QCOMPARE(bulkSpy.count(), 1);
    QCOMPARE(masterSpy.count(), 0);   // no fake master transition
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(trailEnableSpy.count(), 0);
    QCOMPARE(clickEnableSpy.count(), 0);
    QCOMPARE(presetSpy.count(), 0);

    const auto bulkCfg = bulkSpy.takeFirst().at(0).value<ptd::AppConfig>();
    const ptd::TrailConfig def_trail{};
    const ptd::ClickConfig def_click{};
    const auto gotTrail = bulkCfg.trail;
    const auto gotClick = bulkCfg.click;
    QCOMPARE(gotTrail.head_thickness_px, def_trail.head_thickness_px);
    QCOMPARE(gotTrail.lifetime_ms, def_trail.lifetime_ms);
    QCOMPARE(gotClick.duration_ms, def_click.duration_ms);
    QCOMPARE(gotClick.particle_amount, def_click.particle_amount);
    QCOMPARE(chkMaster->isChecked(), true);
}

// ---- T-022 Elemental Click VFX: Settings surface ----

// Four more exclusive selector buttons in the click STYLE grid (never a
// combo box), every elemental enum value loads to its own button, and one
// user click publishes exactly one config carrying that style.
void TestSettingsWindow::elemental_click_style_buttons_exclusive() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    struct Case { const char* name; const char* label; ptd::ClickStyle style; };
    const Case cases[4] = {
        {"click_style_air",   "Air",   ptd::ClickStyle::Air},
        {"click_style_fire",  "Fire",  ptd::ClickStyle::Fire},
        {"click_style_water", "Water", ptd::ClickStyle::Water},
        {"click_style_earth", "Earth", ptd::ClickStyle::Earth},
    };

    // The grid now holds eleven buttons and exactly one is ever checked.
    const QStringList all_names = {
        "click_style_ring", "click_style_double_ring", "click_style_ripple",
        "click_style_burst", "click_style_spark_burst", "click_style_soft_flash",
        "click_style_dot_ring", "click_style_air", "click_style_fire",
        "click_style_water", "click_style_earth"};
    QList<QPushButton*> all;
    for (const QString& n : all_names) {
        auto* b = w.findChild<QPushButton*>(n);
        QVERIFY2(b != nullptr, n.toUtf8().constData());
        QVERIFY(b->isCheckable());
        QCOMPARE(b->focusPolicy(), Qt::StrongFocus);
        all.append(b);
    }
    QCOMPARE(all.size(), 11);

    for (const Case& c : cases) {
        auto* btn = w.findChild<QPushButton*>(QString::fromLatin1(c.name));
        QVERIFY(btn);
        QCOMPARE(btn->accessibleName(), QString::fromLatin1(c.label));

        QSignalSpy spy(&w, &SettingsWindow::click_config_changed);
        btn->click();
        QCOMPARE(spy.count(), 1);
        const auto published = spy.takeFirst().at(0).value<ptd::ClickConfig>();
        QCOMPARE(published.style, c.style);

        int checked = 0;
        for (auto* b : all) if (b->isChecked()) ++checked;
        QCOMPARE(checked, 1);
        QVERIFY(btn->isChecked());
    }

    // Every elemental enum value loads back onto its own button (the
    // constructor is the load path: a stored config must select it).
    for (const Case& c : cases) {
        ptd::TrailConfig tc2{};
        ptd::ClickConfig loaded{};
        loaded.style = c.style;
        SettingsWindow w2(tc2, loaded);
        w2.show();
        QVERIFY2(w2.findChild<QPushButton*>(QString::fromLatin1(c.name))->isChecked(),
                 c.name);
    }
}

// Element tint is an elemental-only control: visible for the four elements,
// hidden for all seven T-017 styles, and Fire additionally hides the Outline
// row because Fire draws no outlined ring at all.
void TestSettingsWindow::element_tint_row_visible_only_for_elements() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto* tint = w.findChild<SliderSpin*>("click_element_tint");
    auto* particles = w.findChild<SliderSpin*>("click_particles");
    auto* outline = w.findChild<SliderSpin*>("click_outline");
    QVERIFY(tint && particles && outline);

    const QStringList non_elemental = {
        "click_style_ring", "click_style_double_ring", "click_style_ripple",
        "click_style_burst", "click_style_spark_burst", "click_style_soft_flash",
        "click_style_dot_ring"};
    for (const QString& n : non_elemental) {
        w.findChild<QPushButton*>(n)->click();
        QApplication::processEvents();
        QVERIFY2(tint->isHidden(), n.toUtf8().constData());
    }

    const QStringList elemental = {
        "click_style_air", "click_style_fire", "click_style_water",
        "click_style_earth"};
    for (const QString& n : elemental) {
        w.findChild<QPushButton*>(n)->click();
        QApplication::processEvents();
        QVERIFY2(!tint->isHidden(), n.toUtf8().constData());
        // Every element spawns particles, so Particles applies to them too.
        QVERIFY2(!particles->isHidden(), n.toUtf8().constData());
    }

    w.findChild<QPushButton*>("click_style_fire")->click();
    QApplication::processEvents();
    QVERIFY(outline->isHidden());   // Fire has no outlined ring
    w.findChild<QPushButton*>("click_style_water")->click();
    QApplication::processEvents();
    QVERIFY(!outline->isHidden());  // Water's ripples are outlined rings
}

// The tint slider is bounded by the config constants, its spin box stays in
// sync, and its value reaches the published config.
void TestSettingsWindow::element_tint_slider_bounds_and_publication() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.style = ptd::ClickStyle::Water;
    SettingsWindow w(tc, cc);
    w.show();

    auto* tint = w.findChild<SliderSpin*>("click_element_tint");
    QVERIFY(tint);
    QCOMPARE(tint->value(), 0.65);   // documented default

    // set_value is signal-silent by design (it IS the load path), so a real
    // user edit is driven through the inner slider: 0.01 step -> 25 = 0.25.
    QSignalSpy spy(&w, &SettingsWindow::click_config_changed);
    tint->findChild<QSlider*>()->setValue(25);
    QApplication::processEvents();
    QCOMPARE(spy.count(), 1);   // one edit, exactly one publication
    const auto published = spy.takeLast().at(0).value<ptd::ClickConfig>();
    QVERIFY(std::fabs(published.element_tint - 0.25f) < 1e-4f);

    // Out-of-range requests are clamped by the widget range, never published raw.
    tint->set_value(5.0);
    QApplication::processEvents();
    QVERIFY(tint->value() <= ptd::ClickConfig::kMaxElementTint + 1e-9);
    tint->set_value(-5.0);
    QApplication::processEvents();
    QVERIFY(tint->value() >= ptd::ClickConfig::kMinElementTint - 1e-9);
}

// Restore Click Defaults must return the section to Ring + the documented
// tint default, exactly like it already returns particles to 8.
void TestSettingsWindow::restore_click_defaults_resets_element_style_and_tint() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.style = ptd::ClickStyle::Earth;
    cc.element_tint = 0.05f;
    SettingsWindow w(tc, cc);
    w.show();

    QVERIFY(w.findChild<QPushButton*>("click_style_earth")->isChecked());
    QCOMPARE(w.findChild<SliderSpin*>("click_element_tint")->value(), 0.05);

    auto* restore = find_button(w, "Restore Click Defaults");
    QVERIFY(restore);
    restore->click();
    QApplication::processEvents();

    QVERIFY(w.findChild<QPushButton*>("click_style_ring")->isChecked());
    QCOMPARE(w.findChild<SliderSpin*>("click_element_tint")->value(), 0.65);
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), 8.0);
}

// T-023: switching sparkle mode -- including to and from the new Shards
// mode -- must not move a single Trail-page section anchor, and the
// reserved Amount/Size/Spread region must keep one constant height while
// the rows appear and disappear INSIDE it.
void TestSettingsWindow::sparkle_mode_switching_preserves_anchors_and_region() {
    const QStringList mode_names = {
        "trail_sparkle_off", "trail_sparkle_stardust", "trail_sparkle_twinkle",
        "trail_sparkle_glitter", "trail_sparkle_firefly", "trail_sparkle_shards"};
    const QStringList anchor_titles = {"STYLE", "COLOR", "SHAPE", "TRAIL",
                                       "SPARKLES"};

    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(520, 500);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);  // Trail
    QWidget* page = tabs->widget(1);
    QVERIFY(page);
    QApplication::processEvents();
    QTest::qWait(50);
    QApplication::processEvents();

    QList<QLabel*> anchors;
    for (const QString& title : anchor_titles) {
        QLabel* found = nullptr;
        for (auto* l : page->findChildren<QLabel*>()) {
            if (l->objectName() == QStringLiteral("sectionLabel")
                && l->text() == title) {
                found = l;
                break;
            }
        }
        QVERIFY2(found != nullptr, title.toUtf8().constData());
        anchors.append(found);
    }
    auto anchor_points = [&]() {
        QList<QPoint> points;
        for (auto* label : anchors) points.append(label->mapTo(page, QPoint(0, 0)));
        return points;
    };

    auto* region = w.findChild<QWidget*>("trail_sparkle_options");
    QVERIFY(region != nullptr);
    const int reserved = region->height();
    QVERIFY(reserved > 0);
    const QList<QPoint> baseline = anchor_points();

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    for (const QString& name : mode_names) {
        auto* button = w.findChild<QPushButton*>(name);
        QVERIFY2(button != nullptr, name.toUtf8().constData());
        button->click();
        QCOMPARE(trailSpy.count(), 1);
        trailSpy.takeFirst();
        QApplication::processEvents();
        QTest::qWait(20);
        QApplication::processEvents();
        QVERIFY2(anchor_points() == baseline, name.toUtf8().constData());
        QCOMPARE(region->height(), reserved);
        QVERIFY(!region->isHidden());
    }

    // Shards specifically: the three rows are visible inside the region and
    // the region has not grown to fit them.
    w.findChild<QPushButton*>("trail_sparkle_shards")->click();
    QApplication::processEvents();
    QTest::qWait(20);
    QApplication::processEvents();
    for (const char* row : {"trail_sparkle_amount", "trail_sparkle_size",
                            "trail_sparkle_spread"}) {
        auto* slider = w.findChild<SliderSpin*>(QString::fromLatin1(row));
        QVERIFY2(slider != nullptr, row);
        QVERIFY2(!slider->isHidden(), row);
        QVERIFY2(region->isAncestorOf(slider), row);
    }
    QCOMPARE(region->height(), reserved);
    QVERIFY2(anchor_points() == baseline, "shards");
}


// ---- T-024 press-and-hold Settings surface ----
//
// The milestone allows exactly ONE user control for the whole gesture:
// a Hold FX toggle. No threshold slider, no charge slider, no intensity
// slider, and no second HOLD style selector -- the current Click style
// already determines the hold style.
void TestSettingsWindow::hold_fx_is_the_only_hold_control() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QCheckBox* hold = find_checkbox(w, "chk_hold_fx");
    QVERIFY2(hold != nullptr, "the Hold FX toggle must exist");
    QCOMPARE(hold->text(), QStringLiteral("Hold FX"));
    // Fresh defaults: Hold FX follows the ClickConfig default (fresh ON).
    QCOMPARE(hold->isChecked(), ptd::ClickConfig{}.hold_enabled);

    // T-027 retired the "one control only" limitation, but the things it was
    // protecting against stay forbidden: no activation-threshold knob, no
    // charge-duration knob, and no hidden second style selector.
    for (const char* forbidden : {"click_hold_charge", "click_hold_threshold",
                                  "click_hold_ms", "hold_threshold",
                                  "hold_charge_ms", "hold_activation"}) {
        QVERIFY2(w.findChild<SliderSpin*>(QString::fromLatin1(forbidden)) == nullptr,
                 forbidden);
    }
    // Exactly the four approved Hold sliders, no more.
    const QStringList approved{"hold_intensity", "hold_wake_density",
                               "hold_wake_lifetime", "hold_release_strength"};
    for (const QString& name : approved) {
        QVERIFY2(find_slider(w, name) != nullptr, qPrintable(name));
    }
    const QList<SliderSpin*> hold_sliders =
        w.findChildren<SliderSpin*>(QRegularExpression("^hold_"));
    QCOMPARE(hold_sliders.size(), approved.size());
    // And no second style selector: the eleven Click style buttons are the
    // only style buttons on the page.
    QVERIFY(w.findChild<QPushButton*>("hold_style_ring") == nullptr);
    QVERIFY(w.findChild<QPushButton*>("click_hold_style_ring") == nullptr);
}

void TestSettingsWindow::hold_fx_toggle_publishes_one_coherent_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.end_radius_px = 33.0f;
    cc.particle_amount = 11;
    SettingsWindow w(tc, cc);
    w.show();

    QCheckBox* hold = find_checkbox(w, "chk_hold_fx");
    QVERIFY(hold != nullptr);
    QCOMPARE(hold->isChecked(), true);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    hold->setChecked(false);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    auto off = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(off.hold_enabled, false);
    // ONE coherent config: every unrelated field travels with it untouched.
    QCOMPARE(off.end_radius_px, 33.0f);
    QCOMPARE(off.particle_amount, static_cast<uint8_t>(11));
    QCOMPARE(off.enabled, cc.enabled);

    hold->setChecked(true);
    QCOMPARE(clickSpy.count(), 1);
    auto on = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(on.hold_enabled, true);
    QCOMPARE(on.end_radius_px, 33.0f);
    QCOMPARE(trailSpy.count(), 0);
}

void TestSettingsWindow::hold_fx_programmatic_population_is_silent() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = false;
    SettingsWindow w(tc, cc);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    w.show();
    // Construction from a supplied config publishes NOTHING (Defect B),
    // and the supplied hold state is reflected in the widget.
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(find_checkbox(w, "chk_hold_fx")->isChecked(), false);
}

void TestSettingsWindow::restore_click_defaults_resets_hold_fx() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = false;
    SettingsWindow w(tc, cc);
    w.show();
    QCOMPARE(find_checkbox(w, "chk_hold_fx")->isChecked(), false);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(clickSpy.count(), 1);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    const ptd::ClickConfig def{};
    // Restore returns Hold FX to the FRESH-INSTALL default, like every
    // other Click field, and leaves the widget agreeing with it.
    QCOMPARE(got.hold_enabled, def.hold_enabled);
    QCOMPARE(find_checkbox(w, "chk_hold_fx")->isChecked(), def.hold_enabled);
}

// ---- T-027 Hold Controls / Motion Wake ----

// The Hold block is always laid out (no surprise re-flow), but its
// interactive state follows the two toggles logically:
//   Hold FX off    -> the whole block is inert
//   Motion Wake off -> only the wake-specific rows are inert, because
//                      Intensity and Release Strength still apply to the aura
//                      and the release payoff.
void TestSettingsWindow::hold_wake_controls_gate_on_the_two_toggles() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = true;
    SettingsWindow w(tc, cc);
    w.show();

    QCheckBox* hold = find_checkbox(w, "chk_hold_fx");
    QCheckBox* wake = find_checkbox(w, "chk_hold_wake");
    QVERIFY(hold && wake);
    QCOMPARE(wake->text(), QStringLiteral("Motion Wake"));

    SliderSpin* intensity = find_slider(w, "hold_intensity");
    SliderSpin* density = find_slider(w, "hold_wake_density");
    SliderSpin* life = find_slider(w, "hold_wake_lifetime");
    SliderSpin* release = find_slider(w, "hold_release_strength");
    QVERIFY(intensity && density && life && release);

    // Everything on.
    QVERIFY(hold->isEnabled() && wake->isEnabled());
    QVERIFY(intensity->isEnabled() && density->isEnabled());
    QVERIFY(life->isEnabled() && release->isEnabled());

    // Motion Wake off: the wake rows go inert, the aura/payoff rows do not.
    wake->setChecked(false);
    QVERIFY(density->isEnabled() == false);
    QVERIFY(life->isEnabled() == false);
    QVERIFY(intensity->isEnabled() == true);
    QVERIFY(release->isEnabled() == true);
    // The rows stay LAID OUT and visible: only ENABLED state may follow the
    // toggles, never visibility (isHidden() is the toggle-independent test --
    // isVisible() is also false for any widget on a background tab).
    QVERIFY(!density->isHidden() && !life->isHidden());

    // Hold FX off: the whole block is inert, including the wake toggle.
    wake->setChecked(true);
    hold->setChecked(false);
    QVERIFY(wake->isEnabled() == false);
    QVERIFY(intensity->isEnabled() == false);
    QVERIFY(density->isEnabled() == false);
    QVERIFY(life->isEnabled() == false);
    QVERIFY(release->isEnabled() == false);
    QVERIFY(!intensity->isHidden() && !density->isHidden() && !life->isHidden());

    // And back on: every control recovers (no unrecoverable dead state).
    hold->setChecked(true);
    QVERIFY(wake->isEnabled() && intensity->isEnabled());
    QVERIFY(density->isEnabled() && life->isEnabled() && release->isEnabled());
}

void TestSettingsWindow::hold_wake_toggle_publishes_one_coherent_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = true;
    cc.hold_intensity = 1.4f;
    cc.hold_wake_lifetime_ms = 1200.0f;
    cc.end_radius_px = 33.0f;
    SettingsWindow w(tc, cc);
    w.show();

    QCheckBox* wake = find_checkbox(w, "chk_hold_wake");
    QVERIFY(wake);
    QCOMPARE(wake->isChecked(), true);

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);

    wake->setChecked(false);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(trailSpy.count(), 0);
    const auto off = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(off.hold_wake_enabled, false);
    // ONE coherent config: the untouched Hold values travel with it.
    QCOMPARE(off.hold_enabled, true);
    QCOMPARE(off.hold_intensity, 1.4f);
    QCOMPARE(off.hold_wake_lifetime_ms, 1200.0f);
    QCOMPARE(off.end_radius_px, 33.0f);
}

void TestSettingsWindow::hold_sliders_publish_one_coherent_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    SettingsWindow w(tc, cc);
    w.show();

    // set_value IS the load path and is signal-silent by design, so a real
    // user edit is driven through the inner slider (SliderSpin maps slider int
    // v to value v * step, so the ints below are value/step).
    struct Case {
        const char* name;
        int slider_int;    // raw inner-slider position (user edit)
        float expected;    // value the published config must carry
    };
    const Case cases[] = {
        {"hold_intensity", 150, 1.5f},
        {"hold_wake_density", 50, 0.5f},
        // Wake Life is a SECONDS widget over a MILLISECONDS field:
        // 30 * 0.05 s = 1.5 s must publish 1500 ms.
        {"hold_wake_lifetime", 30, 1500.0f},
        {"hold_release_strength", 175, 1.75f},
    };
    for (const Case& c : cases) {
        SliderSpin* s = find_slider(w, QString::fromLatin1(c.name));
        QVERIFY2(s != nullptr, c.name);
        QSlider* inner = s->findChild<QSlider*>();
        QVERIFY2(inner != nullptr, c.name);
        QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
        inner->setValue(c.slider_int);
        QApplication::processEvents();
        // One user action, exactly one coherent publication.
        QCOMPARE(clickSpy.count(), 1);
        const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
        const float actual = std::strcmp(c.name, "hold_intensity") == 0
                                 ? got.hold_intensity
                             : std::strcmp(c.name, "hold_wake_density") == 0
                                 ? got.hold_wake_density
                             : std::strcmp(c.name, "hold_wake_lifetime") == 0
                                 ? got.hold_wake_lifetime_ms
                                 : got.hold_release_strength;
        QCOMPARE(actual, c.expected);
        // The other three fields travelled untouched, and the published
        // config is already inside the hard bounds (nothing to repair).
        QCOMPARE(got.hold_intensity, ptd::ClickConfig::validated(got).hold_intensity);
        QCOMPARE(got.hold_wake_density,
                 ptd::ClickConfig::validated(got).hold_wake_density);
        QCOMPARE(got.hold_wake_lifetime_ms,
                 ptd::ClickConfig::validated(got).hold_wake_lifetime_ms);
        QCOMPARE(got.hold_release_strength,
                 ptd::ClickConfig::validated(got).hold_release_strength);
    }
}

void TestSettingsWindow::hold_controls_programmatic_population_is_silent() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = false;
    cc.hold_intensity = 0.4f;
    cc.hold_wake_density = 1.8f;
    cc.hold_wake_lifetime_ms = 300.0f;
    cc.hold_release_strength = 2.0f;
    SettingsWindow w(tc, cc);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    w.show();

    // Construction from a supplied config publishes NOTHING (Defect B) and
    // every control reflects exactly the supplied value.
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(find_checkbox(w, "chk_hold_wake")->isChecked(), false);
    QCOMPARE(find_slider(w, "hold_intensity")->value(), 40.0);
    QCOMPARE(find_slider(w, "hold_wake_density")->value(), 180.0);
    QCOMPARE(find_slider(w, "hold_wake_lifetime")->value(), 0.30);
    QCOMPARE(find_slider(w, "hold_release_strength")->value(), 200.0);
}

void TestSettingsWindow::restore_click_defaults_resets_hold_controls() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = false;
    cc.hold_intensity = 0.3f;
    cc.hold_wake_density = 2.0f;
    cc.hold_wake_lifetime_ms = 2500.0f;
    cc.hold_release_strength = 0.5f;
    SettingsWindow w(tc, cc);
    w.show();

    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    find_button(w, "Restore Click Defaults")->click();
    QCOMPARE(clickSpy.count(), 1);
    const auto got = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    const ptd::ClickConfig def{};
    QCOMPARE(got.hold_wake_enabled, def.hold_wake_enabled);
    QCOMPARE(got.hold_intensity, def.hold_intensity);
    QCOMPARE(got.hold_wake_density, def.hold_wake_density);
    QCOMPARE(got.hold_wake_lifetime_ms, def.hold_wake_lifetime_ms);
    QCOMPARE(got.hold_release_strength, def.hold_release_strength);
    // The widgets agree with what was published.
    QCOMPARE(find_checkbox(w, "chk_hold_wake")->isChecked(), def.hold_wake_enabled);
    QCOMPARE(find_slider(w, "hold_intensity")->value(),
             static_cast<double>(def.hold_intensity) * 100.0);
    QCOMPARE(find_slider(w, "hold_wake_density")->value(),
             static_cast<double>(def.hold_wake_density) * 100.0);
    QCOMPARE(find_slider(w, "hold_wake_lifetime")->value(),
             static_cast<double>(def.hold_wake_lifetime_ms) / 1000.0);
    QCOMPARE(find_slider(w, "hold_release_strength")->value(),
             static_cast<double>(def.hold_release_strength) * 100.0);
}

// Toggling Hold FX / Motion Wake must not re-flow the tab: every control
// changes ENABLED state only, so nothing below the Hold section can move.
void TestSettingsWindow::hold_section_layout_is_stable_across_toggles() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = true;
    SettingsWindow w(tc, cc);
    w.show();
    QCoreApplication::processEvents();

    SliderSpin* intensity = find_slider(w, "hold_intensity");
    SliderSpin* density = find_slider(w, "hold_wake_density");
    SliderSpin* life = find_slider(w, "hold_wake_lifetime");
    SliderSpin* release = find_slider(w, "hold_release_strength");
    QPushButton* restore = find_button(w, "Restore Click Defaults");
    QVERIFY(intensity && density && life && release && restore);

    const QRect before_i = intensity->geometry();
    const QRect before_d = density->geometry();
    const QRect before_l = life->geometry();
    const QRect before_r = release->geometry();
    const QRect before_restore = restore->geometry();

    QCheckBox* wake = find_checkbox(w, "chk_hold_wake");
    QCheckBox* hold = find_checkbox(w, "chk_hold_fx");
    wake->setChecked(false);
    hold->setChecked(false);
    hold->setChecked(true);
    wake->setChecked(true);
    QCoreApplication::processEvents();

    QCOMPARE(intensity->geometry(), before_i);
    QCOMPARE(density->geometry(), before_d);
    QCOMPARE(life->geometry(), before_l);
    QCOMPARE(release->geometry(), before_r);
    // The anchor BELOW the Hold section is what a re-flow would move.
    QCOMPARE(restore->geometry(), before_restore);
}

// T-40: Start with Windows GUI regressions.
void TestSettingsWindow::start_with_windows_object_name_is_stable() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
    QVERIFY2(chk != nullptr, "chk_start_with_windows must exist in General tab");
    QCOMPARE(chk->objectName(), QStringLiteral("chk_start_with_windows"));
    QCOMPARE(chk->text(), QStringLiteral("Start with Windows"));
}

void TestSettingsWindow::start_with_windows_programmatic_population_is_silent() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};

    // Initial true: silent on load
    {
        SettingsWindow w(tc, cc, true, true);
        QSignalSpy spy(&w, &SettingsWindow::start_with_windows_changed);
        w.show();
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.start_with_windows(), true);
        QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
        QVERIFY(chk && chk->isChecked());

        // set_start_with_windows(true) when already true -> silent
        w.set_start_with_windows(true);
        QCOMPARE(spy.count(), 0);

        // set_start_with_windows(false) -> silent programmatic update
        w.set_start_with_windows(false);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.start_with_windows(), false);
        QVERIFY(chk && !chk->isChecked());
    }

    // Initial false: silent on load
    {
        SettingsWindow w(tc, cc, true, false);
        QSignalSpy spy(&w, &SettingsWindow::start_with_windows_changed);
        w.show();
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.start_with_windows(), false);
        QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
        QVERIFY(chk && !chk->isChecked());
    }
}

void TestSettingsWindow::start_with_windows_toggle_publishes_exactly_once() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    QSignalSpy spy(&w, &SettingsWindow::start_with_windows_changed);
    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);

    QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
    QVERIFY(chk);

    // One user toggle: false -> true
    chk->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    QCOMPARE(w.start_with_windows(), true);
    // Unrelated signals never published
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(masterSpy.count(), 0);

    // One user toggle: true -> false
    chk->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    QCOMPARE(w.start_with_windows(), false);
    QCOMPARE(trailSpy.count(), 0);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(masterSpy.count(), 0);
}

void TestSettingsWindow::start_with_windows_repeated_toggle_neither_recurses_nor_double_publishes() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    QSignalSpy spy(&w, &SettingsWindow::start_with_windows_changed);
    QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
    QVERIFY(chk);

    // Connect a consumer that re-queries and re-sets to prove no recursion
    int callbacks = 0;
    QObject::connect(&w, &SettingsWindow::start_with_windows_changed,
                     [&](bool val) {
                         ++callbacks;
                         QCOMPARE(w.start_with_windows(), val);
                         w.set_start_with_windows(val);
                     });

    // Rapid repeated clicks: exactly one signal per click
    for (int i = 0; i < 6; ++i) {
        chk->click();
        const bool expected = (i % 2 == 0);
        QCOMPARE(spy.count(), i + 1);
        QCOMPARE(callbacks, i + 1);
        QCOMPARE(w.start_with_windows(), expected);
        QCOMPARE(chk->isChecked(), expected);
    }
}

void TestSettingsWindow::start_with_windows_restore_all_defaults_behavior() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};

    // Case 1: Value differs from default (start_with_windows = true).
    // Restore All Defaults must emit exactly ONE false publication.
    {
        SettingsWindow w(tc, cc, true, true);
        w.show();
        // CORE-003 + W2-002: the whole-config change is ONE bulk publication;
        // Application owns the desired-state reconcile from cfg.start_with_windows.
        QSignalSpy bulkSpy(&w, &SettingsWindow::app_config_applied);

        find_button(w, "Restore All Defaults")->click();

        QCOMPARE(bulkSpy.count(), 1);
        QCOMPARE(bulkSpy.takeFirst().at(0).value<ptd::AppConfig>().start_with_windows, false);
        QCOMPARE(w.start_with_windows(), false);
        QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
        QVERIFY(chk && !chk->isChecked());
    }

    // Case 2: Value already equals default (start_with_windows = false).
    // Restore All Defaults must emit ZERO start_with_windows_changed publications.
    {
        SettingsWindow w(tc, cc, true, false);
        w.show();
        QSignalSpy spy(&w, &SettingsWindow::start_with_windows_changed);

        find_button(w, "Restore All Defaults")->click();

        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.start_with_windows(), false);
        QCheckBox* chk = find_checkbox(w, "chk_start_with_windows");
        QVERIFY(chk && !chk->isChecked());
    }
}

void TestSettingsWindow::dev_tab_presence_contract() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);

    QTabWidget* tabs = w.findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);

    if (ptd::is_dev_build()) {
        QVERIFY(w.has_dev_tab());
        QVERIFY(w.dev_tab() != nullptr);
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(3), QStringLiteral("Developer"));
    } else {
        QVERIFY(!w.has_dev_tab());
        QVERIFY(w.dev_tab() == nullptr);
        QCOMPARE(tabs->count(), 3);
    }
}

void TestSettingsWindow::dev_tab_presence_override() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};

    // 1. Force disabled
    ptd::set_dev_build_override_for_tests(false);
    {
        SettingsWindow w(tc, cc, true, false);
        QVERIFY(!w.has_dev_tab());
        QVERIFY(w.dev_tab() == nullptr);
        QCOMPARE(w.findChild<QTabWidget*>()->count(), 3);
    }

    // 2. Force enabled
    ptd::set_dev_build_override_for_tests(true);
    {
        SettingsWindow w(tc, cc, true, false);
        QVERIFY(w.has_dev_tab());
        QVERIFY(w.dev_tab() != nullptr);
        QCOMPARE(w.findChild<QTabWidget*>()->count(), 4);
        QCOMPARE(w.findChild<QTabWidget*>()->tabText(3), QStringLiteral("Developer"));
    }

    ptd::set_dev_build_override_for_tests(std::nullopt);
}

void TestSettingsWindow::dev_tab_capture_and_apply_flow() {
    ptd::set_dev_build_override_for_tests(true);
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    QVERIFY(!w.captured_config().has_value());

    QPushButton* btn_cap = w.findChild<QPushButton*>("btn_dev_capture");
    QVERIFY(btn_cap != nullptr);
    btn_cap->click();

    QVERIFY(w.captured_config().has_value());
    const ptd::AppConfig captured = *w.captured_config();
    QCOMPARE(captured, w.capture_current_settings());

    // Modify a live setting (e.g. uncheck master)
    QCheckBox* chk_master = find_checkbox(w, "chk_master");
    QVERIFY(chk_master != nullptr);
    chk_master->click();
    QCOMPARE(w.capture_current_settings().master_enabled, false);
    // Captured snapshot should NOT have changed
    QCOMPARE(w.captured_config()->master_enabled, true);

    // Apply release defaults
    QPushButton* btn_defs = w.findChild<QPushButton*>("btn_dev_apply_defaults");
    QVERIFY(btn_defs != nullptr);
    btn_defs->click();

    QCOMPARE(w.capture_current_settings().master_enabled, ptd::release_defaults().master_enabled);
    QCOMPARE(chk_master->isChecked(), ptd::release_defaults().master_enabled);

    ptd::set_dev_build_override_for_tests(std::nullopt);
}

void TestSettingsWindow::full_config_capture_preserves_hidden_render_state() {
    ptd::set_dev_build_override_for_tests(true);
    ptd::AppConfig initial = ptd::release_defaults();
    initial.render.diagnostic_primitives = true;
    initial.start_with_windows = true;
    initial.trail.lifetime_ms = 850.0f;
    initial.click.duration_ms = 450.0f;

    SettingsWindow w(initial);
    QSignalSpy whole_config(&w, &SettingsWindow::app_config_applied);
    QCOMPARE(w.capture_current_settings(), initial);
    QCOMPARE(whole_config.count(), 0);

    SliderSpin* life = find_slider(w, "trail_lifetime");
    QVERIFY(life != nullptr);
    QSlider* inner_life = life->findChild<QSlider*>();
    QVERIFY(inner_life != nullptr);
    inner_life->setValue(90);
    const ptd::AppConfig edited = w.capture_current_settings();
    QCOMPARE(edited.trail.lifetime_ms, 900.0f);
    QVERIFY(edited.render.diagnostic_primitives);
    QCOMPARE(edited.click.duration_ms, initial.click.duration_ms);
    QCOMPARE(whole_config.count(), 0);

    QPushButton* capture = w.findChild<QPushButton*>("btn_dev_capture");
    QVERIFY(capture != nullptr);
    capture->click();
    QVERIFY(w.captured_config().has_value());
    QCOMPARE(*w.captured_config(), edited);

    w.apply_config(edited);
    QCOMPARE(whole_config.count(), 1);
    QCOMPARE(whole_config.takeFirst().at(0).value<ptd::AppConfig>(), edited);

    QPushButton* defaults = w.findChild<QPushButton*>("btn_dev_apply_defaults");
    QVERIFY(defaults != nullptr);
    defaults->click();
    QCOMPARE(w.capture_current_settings().render.diagnostic_primitives,
             ptd::release_defaults().render.diagnostic_primitives);
    QCOMPARE(whole_config.count(), 1);
    QCOMPARE(whole_config.takeFirst().at(0).value<ptd::AppConfig>(),
             ptd::release_defaults());

    ptd::set_dev_build_override_for_tests(std::nullopt);
}

void TestSettingsWindow::dev_tab_preset_save_and_apply_roundtrip() {
    ptd::set_dev_build_override_for_tests(true);
    const auto temp_dir = std::filesystem::temp_directory_path() / ("protrail_test_presets_gui_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    ptd::set_dev_presets_dir_for_tests(temp_dir);

    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    // Prepare distinct settings
    ptd::AppConfig custom = ptd::release_defaults();
    custom.trail.lifetime_ms = 850.0f;
    custom.click.duration_ms = 450.0f;
    custom.render.diagnostic_primitives = true;
    w.apply_config(custom);

    // Save as preset "gui_test_preset"
    QLineEdit* edit_name = w.findChild<QLineEdit*>("edit_dev_preset_name");
    QPushButton* btn_save = w.findChild<QPushButton*>("btn_dev_save_preset");
    QVERIFY(edit_name != nullptr && btn_save != nullptr);

    edit_name->setText(QStringLiteral("gui_test_preset"));
    btn_save->click();

    QVERIFY(std::filesystem::exists(temp_dir / "gui_test_preset.json"));
    const auto saved_preset = ptd::load_dev_preset("gui_test_preset");
    QVERIFY(saved_preset.has_value());
    QCOMPARE(*saved_preset, custom);

    // Reset window to defaults
    w.apply_config(ptd::release_defaults());
    QCOMPARE(w.capture_current_settings().trail.lifetime_ms, ptd::release_defaults().trail.lifetime_ms);

    // Select preset and apply
    QListWidget* list_w = w.findChild<QListWidget*>("list_dev_presets");
    QPushButton* btn_apply = w.findChild<QPushButton*>("btn_dev_apply_preset");
    QVERIFY(list_w != nullptr && btn_apply != nullptr);

    auto items = list_w->findItems(QStringLiteral("gui_test_preset"), Qt::MatchExactly);
    QVERIFY(!items.isEmpty());
    list_w->setCurrentItem(items.first());
    btn_apply->click();

    QCOMPARE(w.capture_current_settings(), custom);
    QVERIFY(w.capture_current_settings().render.diagnostic_primitives);

    SliderSpin* life = find_slider(w, "trail_lifetime");
    QVERIFY(life != nullptr);
    QSlider* inner_life = life->findChild<QSlider*>();
    QVERIFY(inner_life != nullptr);
    inner_life->setValue(90);
    QCOMPARE(w.capture_current_settings().trail.lifetime_ms, 900.0f);
    QVERIFY(w.capture_current_settings().render.diagnostic_primitives);
    QPushButton* capture = w.findChild<QPushButton*>("btn_dev_capture");
    QVERIFY(capture != nullptr);
    capture->click();
    QVERIFY(w.captured_config().has_value());
    QVERIFY(w.captured_config()->render.diagnostic_primitives);

    ptd::reset_dev_presets_dir_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);
    std::filesystem::remove_all(temp_dir, ec);
}

void TestSettingsWindow::dev_tab_diff_view_reports_changes() {
    ptd::set_dev_build_override_for_tests(true);
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    // With release defaults, diff shows no differences
    w.apply_config(ptd::release_defaults());
    QPushButton* btn_diff = w.findChild<QPushButton*>("btn_dev_show_diff");
    QPlainTextEdit* txt_diff = w.findChild<QPlainTextEdit*>("txt_dev_diff");
    QVERIFY(btn_diff != nullptr && txt_diff != nullptr);

    btn_diff->click();
    QVERIFY(txt_diff->toPlainText().contains(QStringLiteral("No differences from Release Defaults.")));

    // Modify a field
    ptd::AppConfig mod = ptd::release_defaults();
    mod.trail.glow_strength = 0.93f;
    mod.render.diagnostic_primitives = true;
    w.apply_config(mod);

    btn_diff->click();
    QVERIFY(txt_diff->toPlainText().contains(QStringLiteral("trail.glow_strength")));
    QVERIFY(txt_diff->toPlainText().contains(QStringLiteral("render.diagnostic_primitives")));

    ptd::set_dev_build_override_for_tests(std::nullopt);
}

void TestSettingsWindow::dev_tab_promote_leaves_live_config_identical() {
    ptd::set_dev_build_override_for_tests(true);
    const auto temp_file = std::filesystem::temp_directory_path() / ("rel_defs_gui_test_" + std::to_string(GetCurrentProcessId()) + ".json");
    std::error_code ec;
    std::filesystem::remove(temp_file, ec);
    ptd::set_canonical_release_defaults_path_for_tests(temp_file);

    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc, true, false);
    w.show();

    ptd::AppConfig custom = ptd::release_defaults();
    custom.trail.lifetime_ms = 711.0f;
    custom.start_with_windows = true;
    w.apply_config(custom);

    const ptd::AppConfig before = w.capture_current_settings();

    QPushButton* btn_promote = w.findChild<QPushButton*>("btn_dev_promote");
    QVERIFY(btn_promote != nullptr);
    btn_promote->click();

    // Verify canonical target file received the promoted settings
    QVERIFY(std::filesystem::exists(temp_file));
    auto reloaded = ptd::ConfigStorage::load_from_file(temp_file.wstring());
    QCOMPARE(reloaded.trail.lifetime_ms, 711.0f);
    QCOMPARE(reloaded.start_with_windows, true);

    // Verify live settings remain byte-identical
    const ptd::AppConfig after = w.capture_current_settings();
    QCOMPARE(after, before);

    ptd::reset_canonical_release_defaults_path_for_tests();
    ptd::set_dev_build_override_for_tests(std::nullopt);
    std::filesystem::remove(temp_file, ec);
}

// T-010R Repair 4: the checked indicator must carry a non-color state cue.
// The Golden Default theme draws a tick mark (borderDark pixels) inside the
// teal checked fill. Verified by painting real checkboxes with QStyle and
// scanning exact palette values; grayscale difference is asserted separately.
// The style-drawn path renders the widget's actual stylesheet state
// machine, including focus and disabled flags, without relying on the
// offscreen platform to deliver window focus.
void TestSettingsWindow::checkbox_checked_has_non_color_cue() {
    const QRgb teal        = qRgb(0x00, 0x80, 0x80); // accentTeal fill
    const QRgb tick_dark   = qRgb(0x10, 0x0E, 0x08); // borderDark mark
    const QRgb focus_gold  = qRgb(0xF0, 0xD0, 0x60); // borderHighlight
    const QColor teal_c(teal), dark_c(tick_dark), gold_c(focus_gold);

    auto paint_checkbox = [](bool checked, bool disabled, bool focused) {
        QCheckBox box;
        box.setStyleSheet(ptd::theme::golden_stylesheet());
        QFont f(QStringLiteral("Verdana"), 12);
        f.setStyleStrategy(QFont::NoAntialias);
        box.setFont(f);
        box.setText(QStringLiteral("Probe"));
        box.setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        box.setEnabled(!disabled);

        QImage canvas(64, 24, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        QStyleOptionButton opt;
        opt.initFrom(&box);
        opt.rect = QRect(0, 0, 64, 24);
        opt.text = box.text();
        opt.state.setFlag(QStyle::State_On, checked);
        opt.state.setFlag(QStyle::State_FocusAtBorder, focused);
        opt.state.setFlag(QStyle::State_Enabled, box.isEnabled());
        if (focused) opt.state |= QStyle::State_HasFocus;
        box.style()->drawControl(QStyle::CE_CheckBox, &opt, &painter, &box);
        return canvas;
    };

    auto count_color = [](const QImage& img, const QColor& target) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) == target) ++n;
        return n;
    };

    auto find_teal_box = [&](const QImage& img, const QColor& c) {
        QRect r;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) == c) r |= QRect(x, y, 1, 1);
        return r;
    };
    // Tick mark sits inside the teal fill; the indicator border (borderDark
    // bottom-right) is a 2px frame at the edges, so count the dark mark only
    // within the inner region (teal bbox shrunk by 4px per side).
    auto tick_via = [&](const QImage& img) {
        const QRect b = find_teal_box(img, teal_c);
        if (b.isEmpty()) return int(0);
        const QRect inner(b.x() + 4, b.y() + 4,
                          qMax(1, b.width() - 8),
                          qMax(1, b.height() - 8));
        int n = 0;
        for (int y = inner.top(); y <= inner.bottom() && y < img.height(); ++y)
            for (int x = inner.left(); x <= inner.right() && x < img.width(); ++x)
                if (img.pixelColor(x, y) == dark_c) ++n;
        return n;
    };

    const QImage unchecked = paint_checkbox(false, false, false);
    const QImage checked   = paint_checkbox(true, false, false);
    const QImage focused   = paint_checkbox(true, false, true);
    const QImage dis_un    = paint_checkbox(false, true, false);
    const QImage dis_ck    = paint_checkbox(true, true, false);

    // Unchecked: no teal fill, no tick inside.
    QCOMPARE(count_color(unchecked, teal_c), 0);
    QCOMPARE(tick_via(unchecked), 0);

    // Checked: teal fill present AND the dark tick mark drawn inside it.
    QVERIFY(count_color(checked, teal_c) > 16);
    QVERIFY(tick_via(checked) > 8);  // the non-color state cue exists

    // Color-stripped distinguishability: checked vs unchecked should still
    // differ after grayscale conversion (the tick shape remains, the hue
    // is gone).
    const QImage cg = checked.convertToFormat(QImage::Format_Grayscale8);
    const QImage ug = unchecked.convertToFormat(QImage::Format_Grayscale8);
    QVERIFY(cg.size() == ug.size());
    int gray_diff = 0;
    for (int y = 0; y < cg.height(); ++y)
        for (int x = 0; x < cg.width(); ++x)
            if (cg.pixel(x, y) != ug.pixel(x, y)) ++gray_diff;
    QVERIFY(gray_diff > 32);

    // Disabled checked keeps the same cue; disabled unchecked keeps none.
    QVERIFY(count_color(dis_ck, teal_c) > 16);
    QVERIFY(tick_via(dis_ck) > 8);
    QCOMPARE(count_color(dis_un, teal_c), 0);
    QCOMPARE(tick_via(dis_un), 0);

    // Keyboard focus recolors the indicator border to borderHighlight
    // (2px width preserved: no layout shift) -- a visible focus cue.
    QVERIFY(count_color(focused, gold_c) >
            count_color(checked, gold_c));
}

// ---- T-36 Advanced Motion Wake GUI regressions ----

void TestSettingsWindow::motion_wake_controls_object_names_are_stable() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    QVERIFY(w.findChild<SliderSpin*>("wake_strength") != nullptr);
    QVERIFY(w.findChild<SliderSpin*>("wake_size") != nullptr);
    QVERIFY(w.findChild<SliderSpin*>("wake_spread") != nullptr);
    QVERIFY(w.findChild<SliderSpin*>("speed_response") != nullptr);
    QVERIFY(w.findChild<SliderSpin*>("min_motion_speed") != nullptr);
    QVERIFY(w.findChild<QCheckBox*>("chk_turn_accent") != nullptr);
    QVERIFY(w.findChild<QCheckBox*>("chk_stop_accent") != nullptr);
}

void TestSettingsWindow::motion_wake_programmatic_population_is_silent() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = true;
    cc.wake_strength = 1.5f;
    cc.wake_size = 1.3f;
    cc.wake_spread = 0.4f;
    cc.speed_response = 1.7f;
    cc.min_motion_speed_px_s = 250.0f;
    cc.turn_accent = true;
    cc.stop_accent = true;

    // Constructor population is the only canonical silent load path after
    // the two-window synchronization layer was removed.
    SettingsWindow w(tc, cc);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QCOMPARE(clickSpy.count(), 0);

    QCOMPARE(find_slider(w, "wake_strength")->value(), 150.0);
    QCOMPARE(find_slider(w, "wake_size")->value(), 130.0);
    QCOMPARE(find_slider(w, "wake_spread")->value(), 40.0);
    QCOMPARE(find_slider(w, "speed_response")->value(), 170.0);
    QCOMPARE(find_slider(w, "min_motion_speed")->value(), 250.0);
    QVERIFY(find_checkbox(w, "chk_turn_accent")->isChecked());
    QVERIFY(find_checkbox(w, "chk_stop_accent")->isChecked());
}

void TestSettingsWindow::motion_wake_sliders_publish_one_coherent_config() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    // set_value IS the load path and is signal-silent by design; a real user
    // edit is driven through the inner slider.
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);

    SliderSpin* ws = find_slider(w, "wake_strength");
    QVERIFY(ws != nullptr);
    QSlider* inner_ws = ws->findChild<QSlider*>();
    QVERIFY(inner_ws != nullptr);
    inner_ws->setValue(175);
    QApplication::processEvents();
    QCOMPARE(clickSpy.count(), 1);
    auto emitted = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(emitted.wake_strength, 1.75f);

    SliderSpin* ms = find_slider(w, "min_motion_speed");
    QVERIFY(ms != nullptr);
    QSlider* inner_ms = ms->findChild<QSlider*>();
    QVERIFY(inner_ms != nullptr);
    inner_ms->setValue(40);  // step 10 px/s -> 400 px/s
    QApplication::processEvents();
    QCOMPARE(clickSpy.count(), 1);
    emitted = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QCOMPARE(emitted.min_motion_speed_px_s, 400.0f);

    find_checkbox(w, "chk_turn_accent")->click();
    QApplication::processEvents();
    QCOMPARE(clickSpy.count(), 1);
    emitted = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    QVERIFY(emitted.turn_accent);
}

void TestSettingsWindow::motion_wake_gate_on_hold_and_motion_toggles() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.hold_enabled = true;
    cc.hold_wake_enabled = true;
    SettingsWindow w(tc, cc);
    QVERIFY(find_slider(w, "wake_strength")->isEnabled());
    QVERIFY(find_checkbox(w, "chk_turn_accent")->isEnabled());

    // Hold FX off disables the whole block's advanced rows.
    find_checkbox(w, "chk_hold_fx")->click();
    QVERIFY(!find_slider(w, "wake_strength")->isEnabled());
    QVERIFY(!find_slider(w, "min_motion_speed")->isEnabled());
    QVERIFY(!find_checkbox(w, "chk_stop_accent")->isEnabled());

    // Hold FX back on, Motion Wake off: advanced rows stay disabled.
    find_checkbox(w, "chk_hold_fx")->click();
    find_checkbox(w, "chk_hold_wake")->click();
    QVERIFY(!find_slider(w, "wake_size")->isEnabled());
}

void TestSettingsWindow::restore_click_defaults_resets_motion_wake_controls() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    cc.wake_strength = 1.9f;
    cc.wake_size = 1.8f;
    cc.wake_spread = 1.7f;
    cc.speed_response = 1.6f;
    cc.min_motion_speed_px_s = 900.0f;
    cc.turn_accent = true;
    cc.stop_accent = true;
    SettingsWindow w(tc, cc);
    w.apply_config(ptd::release_defaults());
    const ptd::AppConfig d = ptd::release_defaults();
    QCOMPARE(find_slider(w, "wake_strength")->value(), d.click.wake_strength * 100.0);
    QCOMPARE(find_slider(w, "wake_size")->value(), d.click.wake_size * 100.0);
    QCOMPARE(find_slider(w, "wake_spread")->value(), d.click.wake_spread * 100.0);
    QCOMPARE(find_slider(w, "speed_response")->value(), d.click.speed_response * 100.0);
    QCOMPARE(find_slider(w, "min_motion_speed")->value(), d.click.min_motion_speed_px_s);
    QCOMPARE(find_checkbox(w, "chk_turn_accent")->isChecked(), d.click.turn_accent);
    QCOMPARE(find_checkbox(w, "chk_stop_accent")->isChecked(), d.click.stop_accent);
}

QTEST_MAIN(TestSettingsWindow)
#include "test_settings_window.moc"
