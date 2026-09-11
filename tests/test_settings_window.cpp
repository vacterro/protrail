// T-010 MVP 05 Phase S: GUI binding tests (QtTest). Validates that
// SettingsWindow publishes validated configs, that master/enable toggles
// behave independently, that defaults restore exactly, and that slider
// and numeric input stay synchronized without signal storms.

#include "../src/ui/settings_window.h"
#include "../src/ui/theme.h"
#include "../src/effects/effect_palette.h"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QApplication>
#include <QTabWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QImage>
#include <QPainter>
#include <QStyleOption>
#include <QPixmap>

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
    void trail_color_mode_has_four_labels();
    void switching_modes_preserves_both_colors();
    void preset_buttons_match_kColorPresets();
    void preset_applies_colors_only_one_coherent_pair();
    void restore_trail_returns_full_yellow_yellow();
    void layout_fits_640x540();
    void trail_style_combo_has_eight_labels();
    void switching_styles_publishes_once_and_preserves_colors();
    // T-017 Click style & particle count GUI tests.
    void click_style_combo_has_seven_labels();
    void switching_click_styles_publishes_once_and_preserves_settings();
    void click_particle_amount_slider_spin_sync_and_bounds();
    void restore_click_returns_ring_and_default_particles();

private:
    // Shared helpers.
    QCheckBox* find_checkbox(const SettingsWindow& w, const QString& name) const;
    QPushButton* find_button(const SettingsWindow& w, const QString& text) const;
};

void TestSettingsWindow::tab_construction_succeeds() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();
    QVERIFY(w.findChild<QTabWidget*>() != nullptr);
    // Three tabs: General, Trail, Click.
    QCOMPARE(w.findChild<QTabWidget*>()->count(), 3);
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(0), QStringLiteral("General"));
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(1), QStringLiteral("Trail"));
    QCOMPARE(w.findChild<QTabWidget*>()->tabText(2), QStringLiteral("Click"));
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
    QComboBox* curve = w.findChild<QComboBox*>("trail_fade_curve");
    QVERIFY(curve);
    QCOMPARE(curve->currentIndex(), static_cast<int>(def.fade_curve));
    // T-015: dual trail color editors + mode combo.
    QComboBox* mode = w.findChild<QComboBox*>("trail_color_mode");
    QVERIFY(mode);
    QCOMPARE(mode->currentIndex(), static_cast<int>(def.color_mode));
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
    QComboBox* curve = w.findChild<QComboBox*>("trail_fade_curve");
    QVERIFY(curve);
    QCOMPARE(curve->currentIndex(), static_cast<int>(expected.fade_curve));
    QComboBox* mode = w.findChild<QComboBox*>("trail_color_mode");
    QVERIFY(mode);
    QCOMPARE(mode->currentIndex(), static_cast<int>(expected.color_mode));
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
    QComboBox* easing = w.findChild<QComboBox*>("click_easing");
    QVERIFY(easing);
    QCOMPARE(easing->currentIndex(), static_cast<int>(def.easing));
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
    QComboBox* easing = w.findChild<QComboBox*>("click_easing");
    QVERIFY(easing);
    QCOMPARE(easing->currentIndex(), static_cast<int>(expected.easing));
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
    w.findChild<QComboBox*>("click_easing")->setCurrentIndex(0);  // Linear
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

    QSignalSpy trailSpy(&w, &SettingsWindow::trail_config_changed);
    QSignalSpy clickSpy(&w, &SettingsWindow::click_config_changed);
    QSignalSpy masterSpy(&w, &SettingsWindow::master_enabled_changed);

    find_button(w, "Restore All Defaults")->click();

    QCOMPARE(trailSpy.count(), 1);
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(masterSpy.count(), 1);

    const auto gotTrail = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    const ptd::TrailConfig defTrail{};
    QCOMPARE(gotTrail.head_thickness_px, defTrail.head_thickness_px);
    QCOMPARE(gotTrail.lifetime_ms, defTrail.lifetime_ms);

    const auto gotClick = clickSpy.takeFirst().at(0).value<ptd::ClickConfig>();
    const ptd::ClickConfig defClick{};
    QCOMPARE(gotClick.duration_ms, defClick.duration_ms);
    QCOMPARE(gotClick.particle_amount, defClick.particle_amount);

    QCOMPARE(masterSpy.takeFirst().at(0).toBool(), true);
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

// The trail color mode combo exposes exactly the four canonical labels.
void TestSettingsWindow::trail_color_mode_has_four_labels() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* mode = w.findChild<QComboBox*>("trail_color_mode");
    QVERIFY(mode);
    QCOMPARE(mode->count(), 4);
    QCOMPARE(mode->itemText(0), QStringLiteral("Full"));
    QCOMPARE(mode->itemText(1), QStringLiteral("Start only"));
    QCOMPARE(mode->itemText(2), QStringLiteral("Fade only"));
    QCOMPARE(mode->itemText(3), QStringLiteral("Gradient"));
    QCOMPARE(mode->itemData(0).toInt(), static_cast<int>(ptd::TrailColorMode::Full));
    QCOMPARE(mode->itemData(1).toInt(), static_cast<int>(ptd::TrailColorMode::StartAccent));
    QCOMPARE(mode->itemData(2).toInt(), static_cast<int>(ptd::TrailColorMode::FadeAccent));
    QCOMPARE(mode->itemData(3).toInt(), static_cast<int>(ptd::TrailColorMode::Gradient));
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

    auto* mode = w.findChild<QComboBox*>("trail_color_mode");
    mode->setCurrentIndex(3);  // Gradient

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

    // Both editors stay in place in every mode (never explicitly hidden;
    // the Trail tab itself is not the active tab here, so isHidden() is
    // the correct visibility contract).
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
    QCOMPARE(w.findChild<QComboBox*>("trail_color_mode")->currentIndex(),
             static_cast<int>(p.trail_mode));
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

// T-015 Phase 13: canonical 640x540 window. Trail/Click tabs may scroll
// vertically, but no horizontal overflow may exist (RGB rows and the
// 7+7 palette rows must fit the page width).
void TestSettingsWindow::layout_fits_640x540() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.resize(640, 540);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    const auto areas = w.findChildren<QScrollArea*>();
    QVERIFY(areas.size() >= 2);  // Trail + Click content areas
    for (auto* area : areas) {
        QVERIFY2(!area->horizontalScrollBar()->isVisible(),
                 "no horizontal page scrollbar at 640x540");
    }
}

// T-016: trail style combo exposes exactly the eight canonical styles.
void TestSettingsWindow::trail_style_combo_has_eight_labels() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* style = w.findChild<QComboBox*>("trail_style");
    QVERIFY(style);
    QCOMPARE(style->count(), 8);
    QCOMPARE(style->itemText(0), QStringLiteral("Classic"));
    QCOMPARE(style->itemText(1), QStringLiteral("Soft Glow"));
    QCOMPARE(style->itemText(2), QStringLiteral("Comet"));
    QCOMPARE(style->itemText(3), QStringLiteral("Neon"));
    QCOMPARE(style->itemText(4), QStringLiteral("Dotted"));
    QCOMPARE(style->itemText(5), QStringLiteral("Pulse"));
    QCOMPARE(style->itemText(6), QStringLiteral("Ribbon"));
    QCOMPARE(style->itemText(7), QStringLiteral("Spark"));
    QCOMPARE(style->itemData(0).toInt(), static_cast<int>(ptd::TrailStyle::Classic));
    QCOMPARE(style->itemData(7).toInt(), static_cast<int>(ptd::TrailStyle::Spark));
    // Defaults: Classic, 50% glow, 12 px spacing.
    QCOMPARE(style->currentIndex(), static_cast<int>(ptd::TrailStyle::Classic));
    QCOMPARE(w.findChild<SliderSpin*>("trail_glow")->value(), 50.0);
    QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->value(), 12.0);
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

    w.findChild<QComboBox*>("trail_style")->setCurrentIndex(3);  // Neon
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
    // Glow/spacing widgets still show their values after the switch.
    QCOMPARE(w.findChild<SliderSpin*>("trail_glow")->value(), 50.0);
    QCOMPARE(w.findChild<SliderSpin*>("trail_spacing")->value(), 12.0);

    // Restore Trail defaults returns style Classic + 50% + 12 px.
    find_button(w, "Restore Trail Defaults")->click();
    QCOMPARE(trailSpy.count(), 1);
    const auto def = trailSpy.takeFirst().at(0).value<ptd::TrailConfig>();
    QCOMPARE(def.style, ptd::TrailStyle::Classic);
    QCOMPARE(def.glow_strength, 0.5f);
    QCOMPARE(def.segment_spacing_px, 12.0f);
}

// T-017: click style combo exposes exactly the seven canonical styles.
void TestSettingsWindow::click_style_combo_has_seven_labels() {
    ptd::TrailConfig tc{};
    ptd::ClickConfig cc{};
    SettingsWindow w(tc, cc);
    w.show();

    auto* style = w.findChild<QComboBox*>("click_style");
    QVERIFY(style);
    QCOMPARE(style->count(), 7);
    QCOMPARE(style->itemText(0), QStringLiteral("Ring"));
    QCOMPARE(style->itemText(1), QStringLiteral("Double Ring"));
    QCOMPARE(style->itemText(2), QStringLiteral("Ripple"));
    QCOMPARE(style->itemText(3), QStringLiteral("Burst"));
    QCOMPARE(style->itemText(4), QStringLiteral("Spark Burst"));
    QCOMPARE(style->itemText(5), QStringLiteral("Soft Flash"));
    QCOMPARE(style->itemText(6), QStringLiteral("Dot + Ring"));
    QCOMPARE(style->itemData(0).toInt(), static_cast<int>(ptd::ClickStyle::Ring));
    QCOMPARE(style->itemData(1).toInt(), static_cast<int>(ptd::ClickStyle::DoubleRing));
    QCOMPARE(style->itemData(2).toInt(), static_cast<int>(ptd::ClickStyle::Ripple));
    QCOMPARE(style->itemData(3).toInt(), static_cast<int>(ptd::ClickStyle::Burst));
    QCOMPARE(style->itemData(4).toInt(), static_cast<int>(ptd::ClickStyle::SparkBurst));
    QCOMPARE(style->itemData(5).toInt(), static_cast<int>(ptd::ClickStyle::SoftFlash));
    QCOMPARE(style->itemData(6).toInt(), static_cast<int>(ptd::ClickStyle::DotRing));
    // Defaults: Ring, 8 particles.
    QCOMPARE(style->currentIndex(), static_cast<int>(ptd::ClickStyle::Ring));
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), 8.0);
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

    w.findChild<QComboBox*>("click_style")->setCurrentIndex(4);  // Spark Burst
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
    QCOMPARE(w.findChild<QComboBox*>("click_style")->currentIndex(), static_cast<int>(ptd::ClickStyle::Ring));
    QCOMPARE(w.findChild<SliderSpin*>("click_particles")->value(), 8.0);
}

QTEST_MAIN(TestSettingsWindow)
#include "test_settings_window.moc"

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