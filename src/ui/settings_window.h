#pragma once

#include "../effects/trail_config.h"
#include "../effects/click_config.h"
#include "../config/app_config.h"
#include "../config/audio_config.h"

#include <QWidget>
#include <optional>

class QPushButton;
class QCheckBox;
class QKeyEvent;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPoint;

namespace ptd {
namespace ui {

// A compact slider + numeric input pair with synchronized bidirectional
// updates (Phase Q). One valueChanged(double) signal emitted per actual
// user change; no feedback recursion. Config ranges come from validated()
// constants, not magic numbers.
class SliderSpin : public QWidget {
    Q_OBJECT
public:
    // suffix: "%", " px", " ms" etc. step: int for slider granularity
    // (e.g. 100 for 0.01 precision on 0..1).
    explicit SliderSpin(const QString& label, double min, double max,
                        double step, int decimals,
                        const QString& suffix, QWidget* parent = nullptr);
    // Defined in the .cpp: unique_ptr<Impl> needs a complete type at
    // destructor instantiation (the header only forward-declares Impl).
    ~SliderSpin() override;

    double value() const;
    void set_value(double v);

    void set_range(double min, double max, double step, int decimals);

signals:
    void value_changed(double);

private slots:
    void on_slider(int);
    void on_spin(double);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// RGB color editor: swatch + three 0..255 spinboxes (Phase I).
// Emits color_changed(r,g,b) when any component changes.
class ColorEditor : public QWidget {
    Q_OBJECT
public:
    explicit ColorEditor(const QString& label, int r, int g, int b,
                         QWidget* parent = nullptr);
    ~ColorEditor() override;

    void get(int& r, int& g, int& b) const;
    void set(int r, int g, int b);
    void apply_color(int r, int g, int b);
    QPushButton* swatch_button() const;

    // T-020: the visible meaning of this editor changes with the trail
    // color mode (Solid/Head/Tail/Gradient relabel which role each editor
    // plays). Label-only; never touches the stored RGB values.
    void set_label(const QString& text);

    // T-020R1: the editor's interactive controls in the order they are drawn
    // (swatch, R/G/B, palette row by row). Used to declare an explicit tab
    // order after the COLOR section reorders its editors.
    QList<QWidget*> focusable_children() const;

    // Palette inspection helpers (T-015 Phase 8 / 16)
    int palette_count() const;
    QPushButton* palette_button(int index) const;
    QList<QPushButton*> palette_buttons() const;
    int selected_palette_index() const;

signals:
    void color_changed(int, int, int);

public slots:
    void open_color_picker();

private slots:
    void on_spin(int);

private:
    void update_swatch();
    void update_palette_selection();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Unified ProTrail product window with General / Trail / Click tabs and an
// optional Developer tab in developer builds. The class name remains stable
// to avoid needless churn in the existing configuration/editor implementation.
// Emits validated config structs and master/enable toggles. Does not
// touch effects/rendering directly -- Application connects signals and
// publishes to effects (Phase M).
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(const ptd::AppConfig& config,
                            QWidget* parent = nullptr);
    explicit SettingsWindow(const ptd::TrailConfig& trail,
                            const ptd::ClickConfig& click,
                            bool master_enabled = true,
                            bool start_with_windows = false,
                            QWidget* parent = nullptr);
    // Defined in the .cpp after Impl is complete: unique_ptr<Impl> requires
    // a complete type at destructor instantiation, and the Q_OBJECT-generated
    // code lives in a translation unit that only sees this header.
    ~SettingsWindow() override;

signals:
    void trail_config_changed(const ptd::TrailConfig&);
    void click_config_changed(const ptd::ClickConfig&);
    void master_enabled_changed(bool);
    void trail_enabled_changed(bool);
    void click_enabled_changed(bool);
    void preset_applied(const ptd::TrailConfig&, const ptd::ClickConfig&);
    // CORE-003 + W2-002: ONE bulk publication for a whole-AppConfig operation
    // (Restore All / apply_config / dev preset). Application applies it as a
    // single transaction with one persistence commit, instead of observing a
    // sequence of unrelated partial updates.
    void app_config_applied(const ptd::AppConfig&);
    // T-032: the application-level Start with Windows preference. One
    // boolean, one owner (Application applies the registry side effect);
    // the window only reports the user's intent.
    void start_with_windows_changed(bool);
    void set_current_as_release_defaults_requested();
    // T-80: the audio assignment block. The window owns the editors and the
    // preview INTENT only; Application owns the engine, so playback and the
    // master gate never live in the UI.
    void audio_config_changed(const ptd::AudioConfig&);
    void sound_preview_requested(const std::string& sound_id);
    void sound_event_preview_requested(const ptd::EventSoundConfig& event_cfg);
    void sound_preview_stop_requested();
    // T-107: one click silences EVERY voice (runtime event voices and the
    // preview alike). Separate from sound_preview_stop_requested, which only
    // cancels the preview slot, so the UI never over-claims what Stop does.
    void sound_stop_all_requested();
    // T-120: ProTrail's own window is the ONLY keyboard event source in the
    // AppOnly scope. The window reports the physical key; Application owns the
    // passive listener, so the sound decision never lives in the UI.
    void app_key_event(int vk, int scan_code, bool extended, bool is_down);

public slots:
    // Called by Application when master disable clears visible effects;
    // keeps checkboxes in sync if needed.
    void set_master_enabled(bool);
    // T-032: silent programmatic population (no publication) for the
    // application-level startup preference.
    void set_start_with_windows(bool);

public:
    // T-032: current Start with Windows state as shown to the user.
    bool start_with_windows() const;

    // T-107: add/remove a sound id in the curated favorites list, republish,
    // and refresh the Favorites column. Public because it is the ONE membership
    // path both right-click menus route through, so the catalog and the
    // Favorites column can never disagree about a sound.
    void toggle_sound_favorite(const QString& id);
    // T-107: current favorites exactly as shown in the Favorites column.
    QStringList favorites_for_tests() const;

    // T-120: Sound & Keyboard UI helpers for deterministic testing and programmatic control.
    void select_sound_event_for_editing(const QString& canonical_event_id);
    QString selected_sound_event_id() const;
    void reset_sound_category(ptd::SoundEventCategory cat);
    void reset_selected_sound_event();
    void apply_sound_profile_preset(ptd::SoundProfilePreset preset);
    bool import_audio_profile_json(const QByteArray& json, QString* error_msg = nullptr);
    QByteArray export_audio_profile_json() const;
    // Returns true while the user is actively scrolling/navigating the sound browser
    // so Application can suppress wheel/arrow sound effects during catalog audition.
    bool is_browsing_sound_catalog() const;

    // T-34: Developer defaults & presets interface
     ptd::AppConfig capture_current_settings() const;
    void apply_config(const ptd::AppConfig& cfg);
    // CORE-004: seed the retained non-UI baseline for whole-config captures.
    void set_full_snapshot_for_tests(const ptd::AppConfig& cfg);
    bool has_dev_tab() const;
    QWidget* dev_tab() const;
    std::optional<ptd::AppConfig> captured_config() const;
    void show_dev_status(const QString& text);
    void set_developer_defaults_controller(bool enabled);

private slots:
    void on_master_toggled(bool);
    void on_trail_toggled(bool);
    void on_click_toggled(bool);
    // T-032: the user turned the Start with Windows control.
    void on_start_with_windows_toggled(bool);
    // T-027: Hold FX / Motion Wake toggles re-evaluate the Hold block's
    // interactive state and publish one coherent ClickConfig.
    void on_hold_toggled(bool);
    void on_trail_slider(double);
    void on_click_slider(double);
    // T-36: Turn/Stop Accent toggles publish one coherent ClickConfig.
    void on_motion_accent_toggled(bool);
    void on_trail_color(int, int, int);
    void on_click_color(int, int, int);
    void on_preset_clicked(int index);
    void on_restore_all();
    void on_restore_trail();
    void on_restore_click();
    // T-80: Sound tab. emit_audio_config() is the single publication path;
    // the preview slots only forward intent to Application.
    void emit_audio_config();
    void on_sound_enabled_toggled();
    void on_sound_mute_toggled();
    void on_sound_volume(double);
    void on_sound_assignment_changed(int index);
    void on_sound_catalog_selection_changed();
    void on_sound_replay_clicked();
    void on_sound_stop_clicked();
    // T-107: stop every voice, not just the preview slot.
    void on_sound_stop_all_clicked();
    // T-107: right-click on a catalog row toggles its favorite membership.
    void on_sound_catalog_context_menu(const QPoint& pos);
    // T-107: right-click on a favorites row removes it (same membership
    // toggle, so the two lists can never disagree about a sound).
    void on_sound_favorites_context_menu(const QPoint& pos);
    void emit_trail_config();
    void emit_click_config();
    // T-020: selector buttons (one user click = one coherent publication).
    // idClicked fires only on real user activation, never on programmatic
    // selection, so config loading stays silent.
    void on_trail_mode_clicked(int id);
    void on_trail_style_clicked(int id);
    void on_trail_fade_clicked(int id);
    // T-021: sparkle mode selector (user click = visibility + one publish).
    void on_trail_sparkle_clicked(int id);
    void on_click_style_clicked(int id);
    void on_click_easing_clicked(int id);

    // T-34: Developer tab slots
    void on_dev_capture();
    void on_dev_save_preset();
    void on_dev_apply_preset();
    void on_dev_apply_defaults();
    void on_dev_show_diff();
    void on_dev_promote();

private:
    void apply_preset(int index);
    void build_general_tab();
    void build_trail_tab();
    void build_click_tab();
    void build_sound_tab();
    void build_dev_tab();
    void refresh_dev_presets();
    std::optional<ptd::AppConfig> resolve_dev_selected_config() const;
    void setup_connections();
    void set_object_names();

    // T-020 contextual surfaces: silent visibility/label updates driven by
    // the selected mode/style. Never publish, never reset stored values.
    void update_trail_visibility();
    void update_color_section();
    void update_click_visibility();

    // Single explicit initialization path: copy validated config -> widgets
    // while blocking widget signals, so programmatic setup never publishes.
    void populate_from_config();
    void set_trail_widgets(const ptd::TrailConfig&);
    void set_click_widgets(const ptd::ClickConfig&);
    // T-80: silent population. Never publishes and never previews, so
    // opening/loading the window produces no audio.
    void set_audio_widgets(const ptd::AudioConfig&);
    // Rebuilds the assignment combo contents from the current catalog.
    void refresh_sound_catalog();
    void refresh_sound_favorites();
    void refresh_sound_recent();
    void refresh_keyboard_event_list();
    void populate_event_editor_for(const std::string& canonical_id);
    void record_recent_sound(const std::string& sound_id);
    void schedule_catalog_audition(const std::string& sound_id);
    void apply_enable_states();

     bool master_enabled_ = true;
    // T-032: persisted application preference; not part of TrailConfig or
    // ClickConfig because it is not an effect setting.
    bool start_with_windows_ = false;
    ptd::TrailConfig trail_cfg_;
    ptd::ClickConfig click_cfg_;
    // T-80: eight independent sound assignments plus enable/mute/volume.
    ptd::AudioConfig audio_cfg_{};
    // CORE-004: complete canonical baseline so non-UI fields survive capture.
    ptd::RenderConfig render_baseline_{};
    std::optional<ptd::AppConfig> captured_config_;
    bool developer_defaults_controller_ = false;

    struct Impl;
    std::unique_ptr<Impl> d_;

protected:
    // T-120 / T-121: report physical keys to the passive router without double-dispatch.
    // Catches key input across all child widgets (e.g. focused QLineEdit, QComboBox).
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void closeEvent(QCloseEvent* event) override;
};

} // namespace ui
} // namespace ptd
