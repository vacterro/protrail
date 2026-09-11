#pragma once

#include "../effects/trail_config.h"
#include "../effects/click_config.h"

#include <QWidget>

class QPushButton;
class QCheckBox;
class QComboBox;

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

// Main Settings window with three tabs: General / Trail / Click.
// Emits validated config structs and master/enable toggles. Does not
// touch effects/rendering directly -- Application connects signals and
// publishes to effects (Phase M).
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(const ptd::TrailConfig& trail,
                            const ptd::ClickConfig& click,
                            bool master_enabled = true,
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

public slots:
    // Called by Application when master disable clears visible effects;
    // keeps checkboxes in sync if needed.
    void set_master_enabled(bool);

private slots:
    void on_master_toggled(bool);
    void on_trail_toggled(bool);
    void on_click_toggled(bool);
    void on_trail_slider(double);
    void on_click_slider(double);
    void on_trail_color(int, int, int);
    void on_click_color(int, int, int);
    void on_preset_clicked(int index);
    void on_restore_all();
    void on_restore_trail();
    void on_restore_click();
    void emit_trail_config();
    void emit_click_config();

private:
    void apply_preset(int index);
    void build_general_tab();
    void build_trail_tab();
    void build_click_tab();
    void setup_connections();
    void set_object_names();

    // Single explicit initialization path: copy validated config -> widgets
    // while blocking widget signals, so programmatic setup never publishes.
    void populate_from_config();
    void set_trail_widgets(const ptd::TrailConfig&);
    void set_click_widgets(const ptd::ClickConfig&);
    void apply_enable_states();

    bool master_enabled_ = true;
    ptd::TrailConfig trail_cfg_;
    ptd::ClickConfig click_cfg_;

    struct Impl;
    std::unique_ptr<Impl> d_;

protected:
    void closeEvent(QCloseEvent* event) override;
};

} // namespace ui
} // namespace ptd