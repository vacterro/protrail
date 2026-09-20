#pragma once

#include "../config/app_config.h"

#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QCloseEvent;

namespace ptd {
namespace ui {

// T-37 Main UI Essentials: a compact quick-control surface for frequent
// tuning, NOT a second Settings window. It exposes exactly eleven controls --
// Master FX, Trail FX, Trail Style, Sparkle Mode, Click FX, Click Style,
// Hold FX, Motion Wake, Wake Density, Hold Intensity, Start with Windows --
// arranged in a compact two-column grid.
//
// ONE authority: this window holds no config of its own beyond the last
// canonical snapshot pushed into it. Every user change publishes the SAME
// signals SettingsWindow uses, so Application applies ONE coherent update
// and pushes canonical state back to BOTH surfaces. Advanced controls stay
// in Settings.
//
// DEVELOPER ACTION: a build in which is_dev_build() is true additionally
// carries one compact developer-only action, "Set Defaults", which emits
// set_current_as_release_defaults_requested(). It invokes the SAME
// controller operation the Settings developer panel exposes -- the button is
// absent entirely from a production Release build, so it can never become a
// second defaults authority or ship to an ordinary user.
//
// SILENT POPULATION: set_from_app_config() blocks every widget signal and
// emits nothing, so loading config, a Settings edit, Restore Defaults or a
// DEV preset can never loop back into a publication, a duplicate save or a
// callback recursion.
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Silent: no signal is emitted, no save is requested.
    void set_from_app_config(const ptd::AppConfig& cfg);

signals:
    void advanced_settings_requested();
    void restore_defaults_requested();
    // Developer builds only (the button does not exist otherwise).
    void set_current_as_release_defaults_requested();
    void master_enabled_changed(bool);
    void trail_enabled_changed(bool);
    void click_enabled_changed(bool);
    void start_with_windows_changed(bool);
    void trail_config_changed(const ptd::TrailConfig&);
    void click_config_changed(const ptd::ClickConfig&);

private slots:
    void on_master_toggled(bool);
    void on_child_toggled(bool);
    void on_trail_style_changed(int index);
    void on_sparkle_mode_changed(int index);
    void on_click_style_changed(int index);
    void on_wake_density_changed(double);
    void on_hold_intensity_changed(double);
    void on_motion_wake_toggled(bool);
    void on_start_with_windows_toggled(bool);
    void on_advanced_settings();
    void on_restore_defaults();
    void on_set_current_as_release_defaults();

private:
    void emit_trail();
    void emit_click();
    void update_enable_states();

    struct Impl;
    std::unique_ptr<Impl> d_;

protected:
    void closeEvent(QCloseEvent* event) override;
};

} // namespace ui
} // namespace ptd
