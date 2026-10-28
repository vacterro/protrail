#include "settings_window.h"
#include "theme.h"
#include "../config/release_defaults.h"
#include "../config/dev_defaults.h"
#include "../config/config_storage.h"
#include "../audio/sound_engine.h"
#include "../platform/keyboard_input.h"
#include "../effects/effect_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QTabWidget>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QCloseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QFileDialog>
#include <QFile>
#include <QScrollBar>
#include <QApplication>

#include <algorithm>
#include <cstddef>

namespace ptd {
namespace ui {

namespace {

// T-020 Phase 14: compact column geometry. The shared label column and
// numeric box shrink so the whole window fits a practical 520x500 minimum
// without truncating labels (longest: "Glow strength" / "2000 ms").
constexpr int kRowLabelPx = 110;
constexpr int kSpinWidthPx = 72;

QLabel* make_row_label(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setFixedWidth(kRowLabelPx);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return lbl;
}

// One checkable selector button. It is explicitly marked as a selector so
// the theme's dedicated contract applies (neutral raised when unselected, a
// stable lighter Golden surface with a gold lit bevel when selected) instead
// of the ordinary-button rules, which made a selected control read as an
// accidentally depressed button. Keyboard focusable, named for accessibility.
QPushButton* make_check(const QString& text, const QString& name) {
    auto* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setProperty("selector", true);
    btn->setObjectName(name);
    btn->setAccessibleName(text);
    btn->setFocusPolicy(Qt::StrongFocus);
    return btn;
}

// T-020: a combo box replacement -- one compact row of mutually exclusive
// checkable buttons. Button ids are the persisted enum values in the
// given order. idClicked drives publications, so programmatic selection
// during config loading never emits.
QWidget* make_selector_row(const QStringList& labels, const QStringList& names,
                           QButtonGroup** group_out, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme::controlPadPx());
    auto* grp = new QButtonGroup(row);
    grp->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* btn = make_check(labels.at(i), names.at(i));
        grp->addButton(btn, i);
        lay->addWidget(btn, 1);
    }
    *group_out = grp;
    return row;
}

// T-020R1: Qt keeps its focus chain in widget-CREATION order, so after the
// COLOR section swaps its two editors at runtime, Tab kept walking into the
// editor that had MOVED -- the keyboard path stopped matching what was on
// screen. The section therefore declares its own tab order after every
// contextual update: mode buttons in visual order, then the editor drawn
// first top-to-bottom, then the one drawn second. The order is stated in
// code rather than measured from geometry, so it cannot depend on when Qt
// happens to activate the layout. Hidden editors are included on purpose:
// Tab traversal skips hidden widgets anyway, and their internal order stays
// correct for the moment the mode shows them again.
void sync_tab_order(QButtonGroup* modes, ColorEditor* first,
                    ColorEditor* second) {
    QList<QWidget*> ordered;
    if (modes != nullptr) {
        auto buttons = modes->buttons();
        std::stable_sort(buttons.begin(), buttons.end(),
                         [modes](QAbstractButton* a, QAbstractButton* b) {
                             return modes->id(a) < modes->id(b);
                         });
        for (auto* b : buttons) ordered.append(b);
    }
    if (first != nullptr)  ordered += first->focusable_children();
    if (second != nullptr) ordered += second->focusable_children();
    for (int i = 1; i < ordered.size(); ++i) {
        QWidget::setTabOrder(ordered.at(i - 1), ordered.at(i));
    }
}

// T-020 Phase 13: lightweight section header (label + 1px separator)
// instead of one framed QGroupBox per 2-3 controls. The caller keeps
// appending rows into `content`, which continues inside the same box.
QWidget* begin_section(const QString& title, QVBoxLayout*& content) {
    auto* box = new QWidget();
    auto* outer = new QVBoxLayout(box);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::controlPadPx());
    auto* head = new QLabel(title);
    head->setObjectName(QStringLiteral("sectionLabel"));
    auto* line = new QFrame();
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: %1; border: none;")
                            .arg(theme::borderMuted().name()));
    outer->addWidget(head);
    outer->addWidget(line);
    content = outer;
    return box;
}

} // namespace

// ---- SliderSpin ----

struct SliderSpin::Impl {
    QHBoxLayout* lay = nullptr;
    QLabel* lbl = nullptr;
    QSlider* slider = nullptr;
    QDoubleSpinBox* spin = nullptr;
    double min_ = 0.0, max_ = 1.0, step_ = 1.0;
    int decimals_ = 0;
    bool updating_ = false;
};

SliderSpin::SliderSpin(const QString& label, double min, double max,
                       double step, int decimals, const QString& suffix,
                       QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>()) {
    d_->min_ = min; d_->max_ = max; d_->step_ = step; d_->decimals_ = decimals;

    d_->lay = new QHBoxLayout(this);
    d_->lay->setContentsMargins(0, 0, 0, 0);
    d_->lay->setSpacing(theme::controlPadPx());

    d_->lbl = new QLabel(label);
    d_->lbl->setFixedWidth(kRowLabelPx);    // shared compact label column
    d_->lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    d_->slider = new QSlider(Qt::Horizontal);
    d_->slider->setMinimum(static_cast<int>(min / step + 0.5));
    d_->slider->setMaximum(static_cast<int>(max / step + 0.5));
    d_->slider->setSingleStep(1);
    d_->slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    d_->spin = new QDoubleSpinBox();
    d_->spin->setRange(min, max);
    d_->spin->setSingleStep(step);
    d_->spin->setDecimals(decimals);
    d_->spin->setSuffix(suffix);
    d_->spin->setFixedWidth(kSpinWidthPx);  // fixed spin width (no reflow)

    d_->lay->addWidget(d_->lbl);
    d_->lay->addWidget(d_->slider, 1);
    d_->lay->addWidget(d_->spin);

    connect(d_->slider, &QSlider::valueChanged, this, &SliderSpin::on_slider);
    connect(d_->spin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SliderSpin::on_spin);
}

SliderSpin::~SliderSpin() = default;

double SliderSpin::value() const { return d_->spin->value(); }

void SliderSpin::set_value(double v) {
    v = std::clamp(v, d_->min_, d_->max_);
    if (d_->updating_) return;
    d_->updating_ = true;
    d_->slider->setValue(static_cast<int>(v / d_->step_ + 0.5));
    d_->spin->setValue(v);
    d_->updating_ = false;
}

void SliderSpin::set_range(double min, double max, double step, int decimals) {
    d_->min_ = min; d_->max_ = max; d_->step_ = step; d_->decimals_ = decimals;
    const int smin = static_cast<int>(min / step + 0.5);
    const int smax = static_cast<int>(max / step + 0.5);
    d_->slider->setRange(smin, smax);
    d_->spin->setRange(min, max);
    d_->spin->setSingleStep(step);
    d_->spin->setDecimals(decimals);
}

void SliderSpin::on_slider(int v) {
    if (d_->updating_) return;
    const double dv = v * d_->step_;
    d_->updating_ = true;
    d_->spin->setValue(dv);
    d_->updating_ = false;
    emit value_changed(dv);
}

void SliderSpin::on_spin(double v) {
    if (d_->updating_) return;
    const int sv = static_cast<int>(v / d_->step_ + 0.5);
    d_->updating_ = true;
    d_->slider->setValue(sv);
    d_->updating_ = false;
    emit value_changed(v);
}

// ---- ColorEditor ----

// T-015: the palette is laid out as deterministic 7 + 7 wrapping rows.
constexpr int kPaletteColumns = 7;

struct ColorEditor::Impl {
    QGridLayout* lay = nullptr;
    QLabel* lbl = nullptr;
    QPushButton* swatch = nullptr;
    QSpinBox* spin_r = nullptr;
    QSpinBox* spin_g = nullptr;
    QSpinBox* spin_b = nullptr;
    // 14 canonical palette swatches (kEffectPalette order).
    QList<QPushButton*> palette_btns;
};

// One palette swatch: effect-domain face color (the ONLY allowed arbitrary
// RGB), Golden Default bevel chrome, checkable so the selected entry is
// visibly sunken (state never conveyed by color alone).
static void style_palette_button(QPushButton* btn, int r, int g, int b) {
    const QString face = QStringLiteral("rgb(%1,%2,%3)").arg(r).arg(g).arg(b);
    const QString dark = theme::borderDark().name();
    const QString light = theme::bevelLight().name();
    const QString hl = theme::borderHighlight().name();
    btn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  border: 2px solid;"
        "  border-top-color: %2;"
        "  border-left-color: %2;"
        "  border-bottom-color: %3;"
        "  border-right-color: %3;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  border-top-color: %4;"
        "  border-left-color: %4;"
        "  border-bottom-color: %4;"
        "  border-right-color: %4;"
        "}"
        "QPushButton:focus {"
        "  border-top-color: %4;"
        "  border-left-color: %4;"
        "  border-bottom-color: %4;"
        "  border-right-color: %4;"
        "}"
        // Checked/pressed = sunken bevel (inverted): the non-color
        // selected-state cue required by T-015.
        "QPushButton:checked {"
        "  background-color: %1;"
        "  border-top-color: %3;"
        "  border-left-color: %3;"
        "  border-bottom-color: %2;"
        "  border-right-color: %2;"
        "}"
    ).arg(face, light, dark, hl));
}

ColorEditor::ColorEditor(const QString& label, int r, int g, int b,
                         QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>()) {
    d_->lay = new QGridLayout(this);
    d_->lay->setContentsMargins(0, 0, 0, 0);
    d_->lay->setHorizontalSpacing(theme::controlPadPx());
    d_->lay->setVerticalSpacing(theme::controlPadPx());

    d_->lbl = new QLabel(label);
    d_->lbl->setFixedWidth(kRowLabelPx);
    d_->lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    d_->swatch = new QPushButton();
    d_->swatch->setObjectName(QStringLiteral("swatchButton"));
    d_->swatch->setFixedSize(24, 16);
    d_->swatch->setToolTip(QStringLiteral("Choose color"));
    d_->swatch->setAccessibleName(QStringLiteral("Choose color"));
    d_->swatch->setFocusPolicy(Qt::StrongFocus);
    d_->swatch->setCursor(Qt::PointingHandCursor);

    d_->spin_r = new QSpinBox(); d_->spin_r->setRange(0, 255); d_->spin_r->setValue(r);
    d_->spin_g = new QSpinBox(); d_->spin_g->setRange(0, 255); d_->spin_g->setValue(g);
    d_->spin_b = new QSpinBox(); d_->spin_b->setRange(0, 255); d_->spin_b->setValue(b);
    for (auto* s : {d_->spin_r, d_->spin_g, d_->spin_b}) {
        s->setFixedWidth(55);
        s->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    }

    d_->lay->addWidget(d_->lbl,     0, 0);
    d_->lay->addWidget(d_->swatch,  0, 1);
    d_->lay->addWidget(new QLabel("R"), 0, 2);
    d_->lay->addWidget(d_->spin_r,  0, 3);
    d_->lay->addWidget(new QLabel("G"), 0, 4);
    d_->lay->addWidget(d_->spin_g,  0, 5);
    d_->lay->addWidget(new QLabel("B"), 0, 6);
    d_->lay->addWidget(d_->spin_b,  0, 7);

    // T-015: 14-swatch palette under the RGB row. One click applies the
    // exact canonical RGB and emits exactly one color_changed (via
    // apply_color). 7 + 7 wrapping, canonical order, named tooltips.
    for (int i = 0; i < static_cast<int>(ptd::kEffectPalette.size()); ++i) {
        const ptd::PaletteColor& pc = ptd::kEffectPalette[static_cast<std::size_t>(i)];
        const QString name = QString::fromUtf8(pc.name);
        auto* btn = new QPushButton();
        btn->setFixedSize(22, 16);
        btn->setCheckable(true);
        btn->setToolTip(name);
        btn->setAccessibleName(name);
        btn->setFocusPolicy(Qt::StrongFocus);
        btn->setCursor(Qt::PointingHandCursor);
        style_palette_button(btn, pc.r, pc.g, pc.b);
        const int row = 1 + i / kPaletteColumns;
        const int col = 1 + i % kPaletteColumns;
        d_->lay->addWidget(btn, row, col);
        connect(btn, &QPushButton::clicked, this, [this, i] {
            const ptd::PaletteColor& c =
                ptd::kEffectPalette[static_cast<std::size_t>(i)];
            apply_color(c.r, c.g, c.b);
        });
        d_->palette_btns.append(btn);
    }

    update_swatch();
    update_palette_selection();

    connect(d_->swatch, &QPushButton::clicked,
            this, &ColorEditor::open_color_picker);
    connect(d_->spin_r, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ColorEditor::on_spin);
    connect(d_->spin_g, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ColorEditor::on_spin);
    connect(d_->spin_b, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ColorEditor::on_spin);
}

ColorEditor::~ColorEditor() = default;

void ColorEditor::set_label(const QString& text) {
    d_->lbl->setText(text);
}

QPushButton* ColorEditor::swatch_button() const {
    return d_->swatch;
}

void ColorEditor::get(int& r, int& g, int& b) const {
    r = d_->spin_r->value();
    g = d_->spin_g->value();
    b = d_->spin_b->value();
}

void ColorEditor::set(int r, int g, int b) {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    QSignalBlocker br(d_->spin_r), bg(d_->spin_g), bb(d_->spin_b);
    d_->spin_r->setValue(r);
    d_->spin_g->setValue(g);
    d_->spin_b->setValue(b);
    update_swatch();
    update_palette_selection();
}

void ColorEditor::apply_color(int r, int g, int b) {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    {
        QSignalBlocker br(d_->spin_r);
        QSignalBlocker bg(d_->spin_g);
        QSignalBlocker bb(d_->spin_b);
        d_->spin_r->setValue(r);
        d_->spin_g->setValue(g);
        d_->spin_b->setValue(b);
    }
    update_swatch();
    update_palette_selection();
    emit color_changed(r, g, b);
}

void ColorEditor::open_color_picker() {
    int r, g, b;
    get(r, g, b);
    const QColor initial(r, g, b);
    const QColor chosen = QColorDialog::getColor(
        initial, this, QStringLiteral("Choose color"),
        QColorDialog::ColorDialogOptions());
    if (chosen.isValid()) {
        apply_color(chosen.red(), chosen.green(), chosen.blue());
    }
}

void ColorEditor::on_spin(int) {
    int r = d_->spin_r->value();
    int g = d_->spin_g->value();
    int b = d_->spin_b->value();
    update_swatch();
    update_palette_selection();
    emit color_changed(r, g, b);
}

// ---- ColorEditor palette surface (T-015 Phase 8) ----

int ColorEditor::palette_count() const {
    return static_cast<int>(d_->palette_btns.size());
}

QPushButton* ColorEditor::palette_button(int index) const {
    if (index < 0 || index >= d_->palette_btns.size()) return nullptr;
    return d_->palette_btns.at(index);
}

QList<QPushButton*> ColorEditor::palette_buttons() const {
    return d_->palette_btns;
}

QList<QWidget*> ColorEditor::focusable_children() const {
    // The interactive controls in the order they are drawn: swatch, R, G, B,
    // then the palette grid row by row (creation order == visual order).
    QList<QWidget*> out;
    out << d_->swatch << d_->spin_r << d_->spin_g << d_->spin_b;
    for (auto* b : d_->palette_btns) out << b;
    return out;
}

int ColorEditor::selected_palette_index() const {
    int r, g, b;
    get(r, g, b);
    return ptd::find_palette_index(static_cast<uint8_t>(r),
                                   static_cast<uint8_t>(g),
                                   static_cast<uint8_t>(b));
}

void ColorEditor::update_palette_selection() {
    const int selected = selected_palette_index();
    for (int i = 0; i < d_->palette_btns.size(); ++i) {
        QSignalBlocker block(d_->palette_btns.at(i));
        d_->palette_btns.at(i)->setChecked(i == selected);
    }
}

void ColorEditor::update_swatch() {
    int r = d_->spin_r->value();
    int g = d_->spin_g->value();
    int b = d_->spin_b->value();
    const QString bg = QStringLiteral("rgb(%1,%2,%3)").arg(r).arg(g).arg(b);
    const QString dark = theme::borderDark().name();
    const QString light = theme::bevelLight().name();
    const QString hl = theme::borderHighlight().name();

    d_->swatch->setStyleSheet(QStringLiteral(
        "QPushButton#swatchButton {"
        "  background-color: %1;"
        "  border: 2px solid;"
        "  border-top-color: %2;"
        "  border-left-color: %2;"
        "  border-bottom-color: %3;"
        "  border-right-color: %3;"
        "  padding: 0px;"
        "}"
        "QPushButton#swatchButton:hover {"
        "  border-top-color: %4;"
        "  border-left-color: %4;"
        "  border-bottom-color: %4;"
        "  border-right-color: %4;"
        "}"
        "QPushButton#swatchButton:focus {"
        "  border-top-color: %4;"
        "  border-left-color: %4;"
        "  border-bottom-color: %4;"
        "  border-right-color: %4;"
        "}"
        "QPushButton#swatchButton:pressed {"
        "  border-top-color: %3;"
        "  border-left-color: %3;"
        "  border-bottom-color: %2;"
        "  border-right-color: %2;"
        "}"
    ).arg(bg, light, dark, hl));
}

// ---- SettingsWindow ----

struct SettingsWindow::Impl {
    QTabWidget* tabs = nullptr;
    QWidget* general_tab = nullptr;
    QWidget* trail_tab = nullptr;
    QWidget* click_tab = nullptr;

    // General
    QCheckBox* chk_master;
    QCheckBox* chk_trail;
    QCheckBox* chk_click;
    // T-032: application-level autostart preference.
    QCheckBox* chk_start_with_windows = nullptr;
    // T-015: six one-click color presets (kColorPresets order).
    QList<QPushButton*> preset_buttons;
    QPushButton* btn_restore_all;

    // Trail (T-020: combo boxes replaced by exclusive button groups)
    QButtonGroup* trail_color_group = nullptr;   // Solid/Head/Tail/Gradient
    QButtonGroup* trail_style_group = nullptr;   // 8 styles, 4x2 grid
    ColorEditor* trail_start_color = nullptr;
    ColorEditor* trail_fade_color = nullptr;
    QVBoxLayout* trail_editors_lay = nullptr;
    QWidget* trail_style_options = nullptr;
    SliderSpin* trail_glow = nullptr;
    SliderSpin* trail_spacing = nullptr;
    SliderSpin* trail_head_thickness = nullptr;
    SliderSpin* trail_tail_thickness = nullptr;
    SliderSpin* trail_taper = nullptr;
    SliderSpin* trail_lifetime = nullptr;
    SliderSpin* trail_opacity = nullptr;
    SliderSpin* trail_smoothing = nullptr;
    SliderSpin* trail_fade_start = nullptr;
    QButtonGroup* trail_fade_group = nullptr;    // Linear/Smooth/Ease Out
    // T-021: sparkle decoration selector + bounded parameter region.
    QButtonGroup* trail_sparkle_group = nullptr; // six modes, 3x2 grid
    QWidget* trail_sparkle_options = nullptr;    // bounded contextual region
    SliderSpin* trail_sparkle_amount = nullptr;
    SliderSpin* trail_sparkle_size = nullptr;
    SliderSpin* trail_sparkle_spread = nullptr;
    QPushButton* btn_restore_trail = nullptr;

    // Click
    QCheckBox* chk_trig_left = nullptr;
    QCheckBox* chk_trig_right = nullptr;
    QCheckBox* chk_trig_middle = nullptr;
    QCheckBox* chk_hold_fx = nullptr;            // T-024 hold gesture
    // T-027 Hold FX controls. The wake block is a fixed-height region of
    // always-present rows that are only ENABLED/DISABLED, never hidden: a
    // toggle must not re-flow the tab and move every anchor below it.
    QCheckBox* chk_hold_wake = nullptr;
    SliderSpin* hold_intensity = nullptr;
    SliderSpin* hold_wake_density = nullptr;
    SliderSpin* hold_wake_lifetime = nullptr;
    SliderSpin* hold_release_strength = nullptr;
    // T-36 Advanced Motion Wake.
    SliderSpin* wake_strength = nullptr;
    SliderSpin* wake_size = nullptr;
    SliderSpin* wake_spread = nullptr;
    SliderSpin* speed_response = nullptr;
    SliderSpin* min_motion_speed = nullptr;
    QCheckBox* chk_turn_accent = nullptr;
    QCheckBox* chk_stop_accent = nullptr;
    QButtonGroup* click_style_group = nullptr;   // T-022: 11 styles, 4-col grid
    QWidget* click_style_options = nullptr;      // bounded contextual region
    SliderSpin* click_particles = nullptr;
    SliderSpin* click_element_tint = nullptr;    // T-022, elemental styles only
    ColorEditor* click_color = nullptr;
    SliderSpin* click_start_size = nullptr;
    SliderSpin* click_end_size = nullptr;
    SliderSpin* click_duration = nullptr;
    SliderSpin* click_opacity = nullptr;
    SliderSpin* click_outline = nullptr;
    SliderSpin* click_fill = nullptr;
    QWidget* click_param_options = nullptr;   // T-022 bounded outline/fill host
    QButtonGroup* click_easing_group = nullptr;  // Linear/Smooth/Ease Out
    QPushButton* btn_restore_click = nullptr;

    // T-80 / T-120: Sound tab.
    QWidget* sound_tab = nullptr;
    QCheckBox* chk_sound_enabled = nullptr;
    QCheckBox* chk_sound_muted = nullptr;
    SliderSpin* sound_volume = nullptr;
    QLineEdit* sound_search = nullptr;
    QListWidget* sound_catalog = nullptr;
    QPushButton* btn_sound_replay = nullptr;
    QPushButton* btn_sound_stop = nullptr;
    QPushButton* btn_sound_stop_all = nullptr;  // T-107: silence every voice
    QListWidget* sound_favorites = nullptr;
    QListWidget* sound_recent = nullptr;
    QCheckBox* chk_preview_on_select = nullptr;
    QComboBox* sound_assign[ptd::kAudioEventCount] = {};

    // T-120: Keyboard, Scroll, Presets, Event Editor & Random Pool widgets
    QCheckBox* chk_keyboard_enabled = nullptr;
    QComboBox* combo_keyboard_scope = nullptr;
    QCheckBox* chk_typing_sounds = nullptr;
    QCheckBox* chk_modifier_tap_sounds = nullptr;
    QCheckBox* chk_suppress_repeat_typing = nullptr;
    QCheckBox* chk_suppress_sensitive = nullptr;
    QLabel* lbl_keyboard_scope_info = nullptr;
    QLabel* lbl_keyboard_scope_hint = nullptr;

    QLineEdit* edit_excluded_proc = nullptr;
    QPushButton* btn_add_excluded_proc = nullptr;
    QPushButton* btn_remove_excluded_proc = nullptr;
    QListWidget* list_excluded_procs = nullptr;

    QCheckBox* chk_horizontal_scroll = nullptr;
    SliderSpin* scroll_sensitivity = nullptr;
    SliderSpin* scroll_cooldown = nullptr;
    SliderSpin* scroll_max_rate = nullptr;
    SliderSpin* scroll_pitch_var = nullptr;

    QComboBox* combo_sound_profile = nullptr;
    QPushButton* btn_apply_sound_profile = nullptr;
    QPushButton* btn_export_audio_profile = nullptr;
    QPushButton* btn_import_audio_profile = nullptr;

    QComboBox* combo_event_category = nullptr;
    QLineEdit* edit_event_search = nullptr;
    QPushButton* btn_reset_category = nullptr;
    QListWidget* list_sound_events = nullptr;

    // Selected event editor
    std::string selected_event_id = "typing.generic";
    QLabel* lbl_selected_event_title = nullptr;
    QLabel* lbl_selected_event_note = nullptr;
    QCheckBox* chk_event_enabled = nullptr;
    QComboBox* combo_event_sound = nullptr;
    QPushButton* btn_event_fav_toggle = nullptr;
    QComboBox* combo_event_policy = nullptr;
    QComboBox* combo_event_scope = nullptr;
    SliderSpin* event_gain_db = nullptr;
    SliderSpin* event_cooldown_ms = nullptr;
    SliderSpin* event_priority = nullptr;
    QPushButton* btn_event_preview = nullptr;
    QPushButton* btn_event_reset = nullptr;

    // Collapsible advanced event controls
    QCheckBox* chk_show_advanced_event = nullptr;
    QWidget* advanced_event_panel = nullptr;
    SliderSpin* event_fade_in_ms = nullptr;
    SliderSpin* event_fade_out_ms = nullptr;
    SliderSpin* event_min_retrigger_ms = nullptr;
    SliderSpin* event_polyphony = nullptr;
    SliderSpin* event_pitch_var = nullptr;
    QCheckBox* chk_event_no_repeat = nullptr;
    QCheckBox* chk_event_norm_override = nullptr;
    QCheckBox* chk_event_norm_enabled = nullptr;

    // Random Pool editor
    QCheckBox* chk_event_use_pool = nullptr;
    QWidget* pool_editor_panel = nullptr;
    QListWidget* list_pool_entries = nullptr;
    QComboBox* combo_pool_add_sound = nullptr;
    QPushButton* btn_pool_add = nullptr;
    QPushButton* btn_pool_remove = nullptr;
    QPushButton* btn_pool_up = nullptr;
    QPushButton* btn_pool_down = nullptr;
    QCheckBox* chk_pool_entry_enabled = nullptr;
    SliderSpin* pool_entry_weight = nullptr;
    SliderSpin* pool_entry_gain_db = nullptr;
    QPushButton* btn_pool_preview_entry = nullptr;
    QPushButton* btn_pool_preview_random = nullptr;

    // Custom shortcuts editor
    QLineEdit* edit_custom_sc_name = nullptr;
    QLineEdit* edit_custom_sc_chord = nullptr;
    QCheckBox* chk_custom_sc_app = nullptr;
    QPushButton* btn_custom_sc_add = nullptr;
    QPushButton* btn_custom_sc_remove = nullptr;
    QListWidget* list_custom_shortcuts = nullptr;

    // Debounce timer for rapid wheel/scroll audition
    QTimer* preview_debounce_timer = nullptr;
    std::string pending_preview_sound_id;
    QStringList cached_catalog_ids;

    // T-34: Developer Tab (dev builds only)
    QWidget* dev_tab = nullptr;
    QPushButton* btn_dev_capture = nullptr;
    QLabel* lbl_dev_captured = nullptr;
    QLineEdit* edit_dev_preset_name = nullptr;
    QPushButton* btn_dev_save_preset = nullptr;
    QListWidget* list_dev_presets = nullptr;
    QPushButton* btn_dev_apply_preset = nullptr;
    QPushButton* btn_dev_apply_defaults = nullptr;
    QButtonGroup* dev_source_group = nullptr;
    QPushButton* btn_dev_show_diff = nullptr;
    QPushButton* btn_dev_promote = nullptr;
    QPlainTextEdit* txt_dev_diff = nullptr;
    QLabel* lbl_dev_status = nullptr;
};

SettingsWindow::SettingsWindow(const ptd::TrailConfig& trail,
                               const ptd::ClickConfig& click,
                               bool master_enabled,
                               bool start_with_windows,
                               QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>()),
      master_enabled_(master_enabled),
      start_with_windows_(start_with_windows),
      trail_cfg_(trail), click_cfg_(click) {
    setWindowTitle(QStringLiteral("ProTrail"));
    // Unified product surface: one resizable window, with vertical scrolling
    // reserved for the content-heavy Trail and Click tabs.
     setMinimumSize(520, 500);
    resize(520, 500);

    // Fresh launches always start at General. The tab index is deliberately
    // session-local; it is not part of AppConfig.


    // Apply Golden Default theme
    setStyleSheet(theme::golden_stylesheet());
    QFont f(theme::fontFamily(), theme::fontBodyPx());
    f.setStyleStrategy(QFont::NoAntialias);
    setFont(f);

    build_general_tab();
    build_trail_tab();
    build_click_tab();
    build_sound_tab();
    if (ptd::is_dev_build()) {
        build_dev_tab();
    }

    d_->tabs = new QTabWidget();
    d_->tabs->addTab(d_->general_tab, "General");
    d_->tabs->addTab(d_->trail_tab, "Trail");
    d_->tabs->addTab(d_->click_tab, "Click");
    d_->tabs->addTab(d_->sound_tab, "Sound");
    if (d_->dev_tab != nullptr) {
        d_->tabs->addTab(d_->dev_tab, "Developer");
    }

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(theme::outerMarginPx(), theme::outerMarginPx(),
                             theme::outerMarginPx(), theme::outerMarginPx());
    root->setSpacing(theme::sectionGapPx());
    root->addWidget(d_->tabs);

    setup_connections();
    set_object_names();

    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }

    // Single explicit initialization: copy validated config -> widgets
    // with all signals blocked; no publications during construction.
    populate_from_config();
    apply_enable_states();
}

SettingsWindow::SettingsWindow(const ptd::AppConfig& config, QWidget* parent)
    : SettingsWindow(config.trail, config.click, config.master_enabled,
                     config.start_with_windows, parent) {
    const ptd::AppConfig validated = ptd::AppConfig::validated(config);
    master_enabled_ = validated.master_enabled;
    start_with_windows_ = validated.start_with_windows;
    trail_cfg_ = validated.trail;
    click_cfg_ = validated.click;
    audio_cfg_ = validated.audio;
    render_baseline_ = validated.render;
    populate_from_config();
    apply_enable_states();
}

SettingsWindow::~SettingsWindow() {
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
}

// T-120 / T-121: the AppOnly keyboard scope listens across all child widgets
// (including focused QLineEdit, QComboBox, QListWidget), so typing or navigating
// in ProTrail's own window feeds the passive router without double-dispatch.
bool SettingsWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event != nullptr && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        QWidget* w = qobject_cast<QWidget*>(watched);
        if (w != nullptr && (w == this || this->isAncestorOf(w))) {
            QWidget* fw = QApplication::focusWidget();
            if (fw == nullptr || w == fw || (w == this && !this->isAncestorOf(fw))) {
                auto* key_event = static_cast<QKeyEvent*>(event);
                const int scan = key_event->nativeScanCode();
                emit app_key_event(key_event->nativeVirtualKey(), scan, (scan >> 8) == 0xE0,
                                   event->type() == QEvent::KeyPress);
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsWindow::keyPressEvent(QKeyEvent* event) {
    QWidget::keyPressEvent(event);
}

void SettingsWindow::keyReleaseEvent(QKeyEvent* event) {
    QWidget::keyReleaseEvent(event);
}

void SettingsWindow::build_general_tab() {
    d_->general_tab = new QWidget();
    d_->general_tab->setObjectName(QStringLiteral("tab_general"));
    auto* lay = new QVBoxLayout(d_->general_tab);
    lay->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                            theme::groupPadPx(), theme::groupPadPx());
    lay->setSpacing(theme::sectionGapPx());

    // ENABLE (T-020 Phase 13: section header instead of a group box)
    QVBoxLayout* en = nullptr;
    lay->addWidget(begin_section(QStringLiteral("ENABLE"), en));
    d_->chk_master = new QCheckBox("Enable ProTrail");
    d_->chk_trail = new QCheckBox("Enable Trail");
    d_->chk_click = new QCheckBox("Enable Click Effect");
    en->addWidget(d_->chk_master);
    en->addWidget(d_->chk_trail);
    en->addWidget(d_->chk_click);

    // STARTUP (T-032). An OS side effect deserves its own section rather
    // than a fourth line under ENABLE: it is not an effect toggle and it is
    // the one control here that changes what Windows does at sign-in.
    QVBoxLayout* su = nullptr;
    lay->addWidget(begin_section(QStringLiteral("STARTUP"), su));
    d_->chk_start_with_windows = new QCheckBox("Start with Windows");
    su->addWidget(d_->chk_start_with_windows);
    auto* startup_note = new QLabel(
        "Launches ProTrail minimized to the tray when you sign in.");
    startup_note->setObjectName(QStringLiteral("hintLabel"));
    su->addWidget(startup_note);

    // T-015: quick color presets, T-020 Phase 15 reflowed as a compact
    // 3x2 grid. kColorPresets is the ONLY authority; a preset changes
    // Trail color_mode/Start/Fade and Click RGB, nothing else.
    QVBoxLayout* pr = nullptr;
    lay->addWidget(begin_section(QStringLiteral("QUICK COLOR PRESETS"), pr));
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(theme::controlPadPx());
    grid->setVerticalSpacing(theme::controlPadPx());
    for (int i = 0; i < static_cast<int>(ptd::kColorPresets.size()); ++i) {
        auto* btn = new QPushButton(
            QString::fromUtf8(ptd::kColorPresets[static_cast<std::size_t>(i)].name));
        const int idx = i;
        connect(btn, &QPushButton::clicked,
                this, [this, idx] { on_preset_clicked(idx); });
        grid->addWidget(btn, i / 3, i % 3);
        grid->setColumnStretch(i % 3, 1);
        d_->preset_buttons.append(btn);
    }
    pr->addLayout(grid);

    auto* note = new QLabel("Applies Trail + Click colors only.");
    note->setObjectName(QStringLiteral("hintLabel"));
    pr->addWidget(note);

    // T-018: restore all defaults
    d_->btn_restore_all = new QPushButton("Restore All Defaults");
    lay->addWidget(d_->btn_restore_all);
    lay->addStretch();
}

void SettingsWindow::build_trail_tab() {
    d_->trail_tab = new QWidget();
    d_->trail_tab->setObjectName(QStringLiteral("tab_trail"));
    auto* root = new QVBoxLayout(d_->trail_tab);
    root->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                             theme::groupPadPx(), theme::groupPadPx());
    root->setSpacing(theme::sectionGapPx());

    auto scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget();
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme::sectionGapPx());
    scroll->setWidget(content);

    // STYLE (T-020 Phase 3): 8 styles as a 4x2 checkable grid; exactly one
    // active. Style-relevant parameter rows appear/disappear below.
    QVBoxLayout* st = nullptr;
    lay->addWidget(begin_section(QStringLiteral("STYLE"), st));
    auto* style_grid_widget = new QWidget();
    auto* sgrid = new QGridLayout(style_grid_widget);
    sgrid->setContentsMargins(0, 0, 0, 0);
    sgrid->setHorizontalSpacing(theme::controlPadPx());
    sgrid->setVerticalSpacing(theme::controlPadPx());
    const QStringList style_texts = {
        "Classic", "Soft Glow", "Comet", "Neon",
        "Dotted", "Pulse", "Ribbon", "Spark"};
    const QStringList style_names = {
        "trail_style_classic", "trail_style_soft_glow", "trail_style_comet",
        "trail_style_neon", "trail_style_dotted", "trail_style_pulse",
        "trail_style_ribbon", "trail_style_spark"};
    d_->trail_style_group = new QButtonGroup(this);
    d_->trail_style_group->setExclusive(true);
    for (int i = 0; i < style_texts.size(); ++i) {
        auto* btn = make_check(style_texts.at(i), style_names.at(i));
        d_->trail_style_group->addButton(btn, i);
        sgrid->addWidget(btn, i / 4, i % 4);
        sgrid->setColumnStretch(i % 4, 1);
    }
    st->addWidget(style_grid_widget);

    // T-020R1: the bounded style-options region lives INSIDE the STYLE
    // section and is the only thing a style switch may add or remove, so
    // COLOR / SHAPE / TRAIL keep their exact vertical positions for every
    // style. Dotted/Spark use it for Dot spacing.
    d_->trail_style_options = new QWidget();
    auto* style_options_lay = new QVBoxLayout(d_->trail_style_options);
    style_options_lay->setContentsMargins(0, 0, 0, 0);
    style_options_lay->setSpacing(0);
    d_->trail_glow = new SliderSpin(
        "Glow", ptd::TrailConfig::kMinGlowStrength * 100.0,
        ptd::TrailConfig::kMaxGlowStrength * 100.0, 1.0, 0, "%");
    d_->trail_spacing = new SliderSpin(
        "Dot spacing", ptd::TrailConfig::kMinSegmentSpacingPx,
        ptd::TrailConfig::kMaxSegmentSpacingPx, 1.0, 0, " px");
    style_options_lay->addWidget(d_->trail_glow);
    style_options_lay->addWidget(d_->trail_spacing);
    d_->trail_style_options->setFixedHeight(d_->trail_glow->sizeHint().height());
    st->addWidget(d_->trail_style_options);

    // COLOR (T-020 Phases 1-2): Solid/Head/Tail/Gradient mode buttons +
    // contextual editors. Both stored RGB values survive every switch; the
    // selected mode is visible without opening any drop-down.
    QVBoxLayout* co = nullptr;
    lay->addWidget(begin_section(QStringLiteral("COLOR"), co));
    auto* mode_row = new QHBoxLayout();
    mode_row->setSpacing(theme::controlPadPx());
    mode_row->addWidget(make_row_label(QStringLiteral("Mode")));
    auto* mode_widget = make_selector_row(
        {"Solid", "Head", "Tail", "Gradient"},
        {"trail_color_solid", "trail_color_head", "trail_color_tail",
         "trail_color_gradient"},
        &d_->trail_color_group, this);
    mode_row->addWidget(mode_widget, 1);
    co->addLayout(mode_row);

    d_->trail_editors_lay = new QVBoxLayout();
    d_->trail_editors_lay->setContentsMargins(0, 0, 0, 0);
    d_->trail_editors_lay->setSpacing(theme::controlPadPx());
    d_->trail_start_color = new ColorEditor("Color",
        trail_cfg_.start_color_r, trail_cfg_.start_color_g,
        trail_cfg_.start_color_b);
    d_->trail_fade_color = new ColorEditor("Fade / Tail",
        trail_cfg_.fade_color_r, trail_cfg_.fade_color_g,
        trail_cfg_.fade_color_b);
    d_->trail_editors_lay->addWidget(d_->trail_start_color);
    d_->trail_editors_lay->addWidget(d_->trail_fade_color);
    co->addLayout(d_->trail_editors_lay);

    // SHAPE (T-020 Phase 5): width + tail width + taper. All three rows are
    // permanent SHAPE controls (T-020R1): a style that ignores one of them
    // (Ribbon ignores taper/tail, taper 0 ignores tail width) keeps the live
    // row in place instead of hiding it, because hiding it reflows SHAPE and
    // drags the TRAIL anchor up and down with the selection.
    QVBoxLayout* sh = nullptr;
    lay->addWidget(begin_section(QStringLiteral("SHAPE"), sh));
    d_->trail_head_thickness = new SliderSpin(
        "Width", ptd::TrailConfig::kMinThicknessPx,
        ptd::TrailConfig::kMaxThicknessPx, 0.1, 1, " px");
    d_->trail_tail_thickness = new SliderSpin(
        "Tail width", ptd::TrailConfig::kMinThicknessPx,
        ptd::TrailConfig::kMaxThicknessPx, 0.1, 1, " px");
    // Percentage controls use UI percent units 0..100 (runtime keeps
    // normalized 0..1; conversion happens in emit_trail_config).
    d_->trail_taper = new SliderSpin(
        "Taper", 0.0, 100.0, 1.0, 0, "%");
    sh->addWidget(d_->trail_head_thickness);
    sh->addWidget(d_->trail_tail_thickness);
    sh->addWidget(d_->trail_taper);

    // TRAIL (T-020 Phase 6): motion + fade merged into one section.
    QVBoxLayout* tr = nullptr;
    lay->addWidget(begin_section(QStringLiteral("TRAIL"), tr));
    d_->trail_lifetime = new SliderSpin(
        "Duration", ptd::TrailConfig::kMinLifetimeMs,
        ptd::TrailConfig::kMaxLifetimeMs, 10.0, 0, " ms");
    d_->trail_opacity = new SliderSpin(
        "Opacity", ptd::TrailConfig::kMinOpacity * 100.0,
        ptd::TrailConfig::kMaxOpacity * 100.0, 1.0, 0, "%");
    d_->trail_smoothing = new SliderSpin(
        "Smoothness", ptd::TrailConfig::kMinSmoothing * 100.0,
        ptd::TrailConfig::kMaxSmoothing * 100.0, 1.0, 0, "%");
    d_->trail_fade_start = new SliderSpin(
        "Fade start", ptd::TrailConfig::kMinFadeStart * 100.0,
        ptd::TrailConfig::kMaxFadeStart * 100.0, 1.0, 0, "%");
    tr->addWidget(d_->trail_lifetime);
    tr->addWidget(d_->trail_opacity);
    tr->addWidget(d_->trail_smoothing);
    tr->addWidget(d_->trail_fade_start);

    // Fade curve (T-020 Phase 7): three mutually exclusive buttons.
    auto* fade_row = new QHBoxLayout();
    fade_row->setSpacing(theme::controlPadPx());
    fade_row->addWidget(make_row_label(QStringLiteral("Fade curve")));
    auto* fade_widget = make_selector_row(
        {"Linear", "Smooth", "Ease Out"},
        {"trail_fade_linear", "trail_fade_smooth", "trail_fade_ease_out"},
        &d_->trail_fade_group, this);
    fade_row->addWidget(fade_widget, 1);
    tr->addLayout(fade_row);

    // SPARKLES (T-021 Phase 12): optional decoration overlay on top of the
    // selected trail style. Selector buttons, never a combo box, under the
    // accepted T-020 selector contract; compact 3x2 grid. The three
    // parameter rows live in a bounded reserved region (the same principle
    // as the STYLE options region above), so switching the mode -- or
    // toggling it Off -- never moves any other section.
    QVBoxLayout* sp = nullptr;
    lay->addWidget(begin_section(QStringLiteral("SPARKLES"), sp));
    auto* sparkle_grid_widget = new QWidget();
    auto* spgrid = new QGridLayout(sparkle_grid_widget);
    spgrid->setContentsMargins(0, 0, 0, 0);
    spgrid->setHorizontalSpacing(theme::controlPadPx());
    spgrid->setVerticalSpacing(theme::controlPadPx());
    const QStringList sparkle_texts = {
        "Off", "Stardust", "Twinkle", "Glitter", "Firefly", "Shards"};
    const QStringList sparkle_names = {
        "trail_sparkle_off", "trail_sparkle_stardust", "trail_sparkle_twinkle",
        "trail_sparkle_glitter", "trail_sparkle_firefly", "trail_sparkle_shards"};
    d_->trail_sparkle_group = new QButtonGroup(this);
    d_->trail_sparkle_group->setExclusive(true);
    for (int i = 0; i < sparkle_texts.size(); ++i) {
        auto* btn = make_check(sparkle_texts.at(i), sparkle_names.at(i));
        d_->trail_sparkle_group->addButton(btn, i);
        spgrid->addWidget(btn, i / 3, i % 3);
        spgrid->setColumnStretch(i % 3, 1);
    }
    sp->addWidget(sparkle_grid_widget);

    d_->trail_sparkle_options = new QWidget();
    auto* sparkle_options_lay = new QVBoxLayout(d_->trail_sparkle_options);
    sparkle_options_lay->setContentsMargins(0, 0, 0, 0);
    sparkle_options_lay->setSpacing(0);
    // Percentage control: UI shows 0..100, the runtime keeps 0..1
    // (conversion in emit_trail_config, exactly like Taper/Opacity).
    d_->trail_sparkle_amount = new SliderSpin(
        "Amount", ptd::TrailConfig::kMinSparkleAmount * 100.0,
        ptd::TrailConfig::kMaxSparkleAmount * 100.0, 1.0, 0, "%");
    d_->trail_sparkle_size = new SliderSpin(
        "Size", ptd::TrailConfig::kMinSparkleSizePx,
        ptd::TrailConfig::kMaxSparkleSizePx, 0.5, 1, " px");
    d_->trail_sparkle_spread = new SliderSpin(
        "Spread", ptd::TrailConfig::kMinSparkleSpreadPx,
        ptd::TrailConfig::kMaxSparkleSpreadPx, 1.0, 0, " px");
    sparkle_options_lay->addWidget(d_->trail_sparkle_amount);
    sparkle_options_lay->addWidget(d_->trail_sparkle_size);
    sparkle_options_lay->addWidget(d_->trail_sparkle_spread);
    // Reserved three-row height: rows appear and disappear INSIDE it, so
    // nothing below the section ever reflows.
    d_->trail_sparkle_options->setFixedHeight(
        d_->trail_sparkle_amount->sizeHint().height() * 3);
    sp->addWidget(d_->trail_sparkle_options);

    // Restore
    d_->btn_restore_trail = new QPushButton("Restore Trail Defaults");
    lay->addWidget(d_->btn_restore_trail);
    lay->addStretch();

    root->addWidget(scroll);
}

void SettingsWindow::build_sound_tab() {
    d_->sound_tab = new QWidget();
    auto* root = new QVBoxLayout(d_->sound_tab);
    root->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                             theme::groupPadPx(), theme::groupPadPx());
    root->setSpacing(theme::sectionGapPx());

    auto scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget();
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme::sectionGapPx());
    scroll->setWidget(content);
    root->addWidget(scroll);

    // Debounce timer for rapid scrolling / wheel audition in the sound catalog
    d_->preview_debounce_timer = new QTimer(this);
    d_->preview_debounce_timer->setSingleShot(true);
    connect(d_->preview_debounce_timer, &QTimer::timeout, this, [this] {
        if (!d_->pending_preview_sound_id.empty()) {
            const std::string id = d_->pending_preview_sound_id;
            d_->pending_preview_sound_id.clear();
            emit sound_preview_requested(id);
        }
    });

    // 1. ENABLE & MASTER VOLUME
    QVBoxLayout* en = nullptr;
    lay->addWidget(begin_section(QStringLiteral("ENABLE"), en));
    auto* en_top = new QHBoxLayout();
    en_top->setSpacing(theme::sectionGapPx());
    d_->chk_sound_enabled = new QCheckBox("Enable Sound");
    d_->chk_sound_muted = new QCheckBox("Mute");
    d_->chk_preview_on_select = new QCheckBox("Audition on Select");
    d_->chk_preview_on_select->setChecked(true);
    d_->chk_preview_on_select->setToolTip(
        QStringLiteral("Automatically preview WAV sounds when navigating the catalog."));
    en_top->addWidget(d_->chk_sound_enabled);
    en_top->addWidget(d_->chk_sound_muted);
    en_top->addWidget(d_->chk_preview_on_select);
    en_top->addStretch(1);
    en->addLayout(en_top);
    d_->sound_volume = new SliderSpin(
        "Volume", ptd::AudioConfig::kMinVolume * 100.0,
        ptd::AudioConfig::kMaxVolume * 100.0, 1.0, 0, "%");
    en->addWidget(d_->sound_volume);

    // 2. PRESETS & PROFILES (Section 12)
    QVBoxLayout* pr = nullptr;
    lay->addWidget(begin_section(QStringLiteral("PRESETS & PROFILES"), pr));
    auto* prof_row = new QHBoxLayout();
    prof_row->setSpacing(theme::controlPadPx());
    prof_row->addWidget(make_row_label(QStringLiteral("Sound Profile")));
    d_->combo_sound_profile = new QComboBox();
    d_->combo_sound_profile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d_->combo_sound_profile->addItem(QStringLiteral("Default (Silent)"), static_cast<int>(ptd::SoundProfilePreset::Default));
    d_->combo_sound_profile->addItem(QStringLiteral("Minimal"), static_cast<int>(ptd::SoundProfilePreset::Minimal));
    d_->combo_sound_profile->addItem(QStringLiteral("Keyboard Focused"), static_cast<int>(ptd::SoundProfilePreset::KeyboardFocused));
    d_->combo_sound_profile->addItem(QStringLiteral("Mouse Focused"), static_cast<int>(ptd::SoundProfilePreset::MouseFocused));
    d_->combo_sound_profile->addItem(QStringLiteral("Quiet"), static_cast<int>(ptd::SoundProfilePreset::Quiet));
    d_->combo_sound_profile->addItem(QStringLiteral("Custom"), static_cast<int>(ptd::SoundProfilePreset::Custom));
    d_->btn_apply_sound_profile = new QPushButton(QStringLiteral("Apply Profile"));
    prof_row->addWidget(d_->combo_sound_profile, 1);
    prof_row->addWidget(d_->btn_apply_sound_profile);
    pr->addLayout(prof_row);

    auto* io_row = new QHBoxLayout();
    io_row->setSpacing(theme::controlPadPx());
    d_->btn_export_audio_profile = new QPushButton(QStringLiteral("Export Sound Assignments..."));
    d_->btn_import_audio_profile = new QPushButton(QStringLiteral("Import Sound Assignments..."));
    io_row->addWidget(d_->btn_export_audio_profile);
    io_row->addWidget(d_->btn_import_audio_profile);
    io_row->addStretch(1);
    pr->addLayout(io_row);

    // 3. SOUNDS BROWSER & LIVE PREVIEW (Section 10)
    QVBoxLayout* so = nullptr;
    lay->addWidget(begin_section(QStringLiteral("SOUNDS"), so));
    d_->sound_search = new QLineEdit();
    d_->sound_search->setPlaceholderText("Search sounds (380+ WAV catalog)");
    d_->sound_search->setClearButtonEnabled(true);
    so->addWidget(d_->sound_search);
    auto* columns = new QHBoxLayout();
    columns->setSpacing(theme::controlPadPx());
    d_->sound_catalog = new QListWidget();
    d_->sound_catalog->setUniformItemSizes(true);
    d_->sound_catalog->setMinimumHeight(180);
    d_->sound_catalog->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* catalog_col = new QVBoxLayout();
    catalog_col->setSpacing(theme::controlPadPx());
    catalog_col->addWidget(make_row_label(QStringLiteral("All Sounds")));
    catalog_col->addWidget(d_->sound_catalog);
    d_->sound_favorites = new QListWidget();
    d_->sound_favorites->setUniformItemSizes(true);
    d_->sound_favorites->setMinimumHeight(100);
    d_->sound_favorites->setContextMenuPolicy(Qt::CustomContextMenu);
    d_->sound_recent = new QListWidget();
    d_->sound_recent->setUniformItemSizes(true);
    d_->sound_recent->setMaximumHeight(76);
    auto* favorites_col = new QVBoxLayout();
    favorites_col->setSpacing(theme::controlPadPx());
    favorites_col->addWidget(make_row_label(QStringLiteral("Favorites")));
    favorites_col->addWidget(d_->sound_favorites, 2);
    favorites_col->addWidget(make_row_label(QStringLiteral("Recent")));
    favorites_col->addWidget(d_->sound_recent, 1);
    columns->addLayout(catalog_col, 2);
    columns->addLayout(favorites_col, 1);
    so->addLayout(columns);
    auto* preview_row = new QHBoxLayout();
    d_->btn_sound_replay = new QPushButton("Replay");
    d_->btn_sound_stop = new QPushButton("Stop");
    d_->btn_sound_stop_all = new QPushButton("Stop All");
    preview_row->addWidget(d_->btn_sound_replay);
    preview_row->addWidget(d_->btn_sound_stop);
    preview_row->addWidget(d_->btn_sound_stop_all);
    preview_row->addStretch(1);
    so->addLayout(preview_row);

    // 4. KEYBOARD INPUT & PRIVACY SCOPE (Sections 3, 4, 6)
    QVBoxLayout* kb = nullptr;
    lay->addWidget(begin_section(QStringLiteral("KEYBOARD SOUND SYSTEM"), kb));
    auto* kb_top = new QHBoxLayout();
    kb_top->setSpacing(theme::sectionGapPx());
    d_->chk_keyboard_enabled = new QCheckBox(QStringLiteral("Enable Keyboard Sounds"));
    d_->chk_typing_sounds = new QCheckBox(QStringLiteral("Typing Sounds"));
    d_->chk_modifier_tap_sounds = new QCheckBox(QStringLiteral("Modifier Tap Sounds"));
    kb_top->addWidget(d_->chk_keyboard_enabled);
    kb_top->addWidget(d_->chk_typing_sounds);
    kb_top->addWidget(d_->chk_modifier_tap_sounds);
    kb_top->addStretch(1);
    kb->addLayout(kb_top);

    auto* kb_opts = new QHBoxLayout();
    kb_opts->setSpacing(theme::sectionGapPx());
    d_->chk_suppress_repeat_typing = new QCheckBox(QStringLiteral("Suppress Key Auto-Repeat"));
    d_->chk_suppress_sensitive = new QCheckBox(QStringLiteral("Suppress Sensitive Dialogs"));
    d_->chk_suppress_sensitive->setToolTip(
        QStringLiteral("Conservatively suppresses keyboard sounds in known credential/password processes. "
                       "OS-level password field detection is not guaranteed across all apps."));
    kb_opts->addWidget(d_->chk_suppress_repeat_typing);
    // T-120: the AppOnly scope hears ONLY keys typed into ProTrail's own
    // window. Saying so is the difference between a working default and a
    // feature the user believes is broken.
    d_->lbl_keyboard_scope_hint = new QLabel();
    kb_opts->addWidget(d_->lbl_keyboard_scope_hint);
    d_->lbl_keyboard_scope_hint->setWordWrap(true);
    d_->lbl_keyboard_scope_hint->setObjectName(QStringLiteral("hintLabel"));
    d_->lbl_keyboard_scope_hint->setText(
        QStringLiteral("App-Only listens to keys typed in ProTrail's own window while it has focus; "
                       "System-Wide listens everywhere except the excluded processes."));
    kb_opts->addWidget(d_->chk_suppress_sensitive);
    kb_opts->addStretch(1);
    kb->addLayout(kb_opts);

    auto* scope_row = new QHBoxLayout();
    scope_row->setSpacing(theme::controlPadPx());
    scope_row->addWidget(make_row_label(QStringLiteral("Capture Scope")));
    d_->combo_keyboard_scope = new QComboBox();
    d_->combo_keyboard_scope->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d_->combo_keyboard_scope->addItem(QStringLiteral("App-Only (ProTrail windows only - Default)"),
                                      static_cast<int>(ptd::KeyboardScope::AppOnly));
    d_->combo_keyboard_scope->addItem(QStringLiteral("System-Wide (Passive background Raw Input - Opt-In)"),
                                      static_cast<int>(ptd::KeyboardScope::SystemWide));
    scope_row->addWidget(d_->combo_keyboard_scope, 1);
    kb->addLayout(scope_row);

    d_->lbl_keyboard_scope_info = new QLabel(
        QStringLiteral("System-wide mode is OFF by default. When enabled, ProTrail uses passive Windows "
                       "Raw Input (never intercepts, blocks, logs, or reads clipboard/typed text). "
                       "Some OEM/firmware media keys are not observable by Windows."));
    d_->lbl_keyboard_scope_info->setWordWrap(true);
    d_->lbl_keyboard_scope_info->setObjectName(QStringLiteral("hintLabel"));
    kb->addWidget(d_->lbl_keyboard_scope_info);

    // Per-process exclusion list for system-wide keyboard mode
    auto* excl_row = new QHBoxLayout();
    excl_row->setSpacing(theme::controlPadPx());
    d_->edit_excluded_proc = new QLineEdit();
    d_->edit_excluded_proc->setPlaceholderText(QStringLiteral("Exclude process (e.g. keepass.exe)"));
    d_->btn_add_excluded_proc = new QPushButton(QStringLiteral("Add Exclusion"));
    d_->btn_remove_excluded_proc = new QPushButton(QStringLiteral("Remove"));
    excl_row->addWidget(d_->edit_excluded_proc, 1);
    excl_row->addWidget(d_->btn_add_excluded_proc);
    excl_row->addWidget(d_->btn_remove_excluded_proc);
    kb->addLayout(excl_row);

    d_->list_excluded_procs = new QListWidget();
    d_->list_excluded_procs->setMaximumHeight(54);
    kb->addWidget(d_->list_excluded_procs);

    // 5. KEYBOARD, SHORTCUT & APPLICATION EVENT BROWSER (Sections 2, 8, 9, 11)
    QVBoxLayout* evb = nullptr;
    lay->addWidget(begin_section(QStringLiteral("EVENTS & SHORTCUTS"), evb));

    auto* cat_row = new QHBoxLayout();
    cat_row->setSpacing(theme::controlPadPx());
    cat_row->addWidget(make_row_label(QStringLiteral("Category")));
    d_->combo_event_category = new QComboBox();
    d_->combo_event_category->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d_->combo_event_category->addItem(QStringLiteral("All Categories"), -1);
    for (int c = 0; c < static_cast<int>(ptd::SoundEventCategory::Count); ++c) {
        const auto cat = static_cast<ptd::SoundEventCategory>(c);
        d_->combo_event_category->addItem(
            QString::fromLatin1(ptd::sound_event_category_label(cat)), c);
    }
    // Default to Typing category so hundreds of F-keys don't flood an ungrouped flat list
    d_->combo_event_category->setCurrentIndex(1);
    d_->btn_reset_category = new QPushButton(QStringLiteral("Reset Category"));
    cat_row->addWidget(d_->combo_event_category, 1);
    cat_row->addWidget(d_->btn_reset_category);
    evb->addLayout(cat_row);

    d_->edit_event_search = new QLineEdit();
    d_->edit_event_search->setPlaceholderText(QStringLiteral("Search events by name or ID (e.g. Copy, Ctrl+C, Backspace)..."));
    d_->edit_event_search->setClearButtonEnabled(true);
    evb->addWidget(d_->edit_event_search);

    d_->list_sound_events = new QListWidget();
    d_->list_sound_events->setUniformItemSizes(true);
    d_->list_sound_events->setMinimumHeight(140);
    evb->addWidget(d_->list_sound_events);

    // Selected Event Inspector & Per-Event Audio Controls (Section 8)
    d_->lbl_selected_event_title = new QLabel(QStringLiteral("Selected Event: Generic Typing (typing.generic)"));
    d_->lbl_selected_event_title->setWordWrap(true);
    evb->addWidget(d_->lbl_selected_event_title);

    d_->lbl_selected_event_note = new QLabel();
    d_->lbl_selected_event_note->setWordWrap(true);
    d_->lbl_selected_event_note->setObjectName(QStringLiteral("hintLabel"));
    d_->lbl_selected_event_note->setVisible(false);
    evb->addWidget(d_->lbl_selected_event_note);

    auto* ev_top_row = new QHBoxLayout();
    ev_top_row->setSpacing(theme::controlPadPx());
    d_->chk_event_enabled = new QCheckBox(QStringLiteral("Event Enabled"));
    d_->chk_event_use_pool = new QCheckBox(QStringLiteral("Use Random Pool"));
    d_->btn_event_preview = new QPushButton(QStringLiteral("Preview Event"));
    d_->btn_event_reset = new QPushButton(QStringLiteral("Reset Event"));
    ev_top_row->addWidget(d_->chk_event_enabled);
    ev_top_row->addWidget(d_->chk_event_use_pool);
    ev_top_row->addStretch(1);
    ev_top_row->addWidget(d_->btn_event_preview);
    ev_top_row->addWidget(d_->btn_event_reset);
    evb->addLayout(ev_top_row);

    auto* ev_snd_row = new QHBoxLayout();
    ev_snd_row->setSpacing(theme::controlPadPx());
    ev_snd_row->addWidget(make_row_label(QStringLiteral("Assigned WAV")));
    d_->combo_event_sound = new QComboBox();
    d_->combo_event_sound->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d_->btn_event_fav_toggle = new QPushButton(QStringLiteral("Fav"));
    d_->btn_event_fav_toggle->setFixedWidth(48);
    ev_snd_row->addWidget(d_->combo_event_sound, 1);
    ev_snd_row->addWidget(d_->btn_event_fav_toggle);
    evb->addLayout(ev_snd_row);

    auto* ev_pol_row = new QHBoxLayout();
    ev_pol_row->setSpacing(theme::controlPadPx());
    ev_pol_row->addWidget(make_row_label(QStringLiteral("Policy / Scope")));
    d_->combo_event_policy = new QComboBox();
    d_->combo_event_policy->addItem(QStringLiteral("Inherit Category"), static_cast<int>(ptd::PlaybackPolicy::Inherit));
    d_->combo_event_policy->addItem(QStringLiteral("Stack"), static_cast<int>(ptd::PlaybackPolicy::Stack));
    d_->combo_event_policy->addItem(QStringLiteral("Override"), static_cast<int>(ptd::PlaybackPolicy::Override));
    d_->combo_event_policy->addItem(QStringLiteral("Cut"), static_cast<int>(ptd::PlaybackPolicy::Cut));
    d_->combo_event_policy->addItem(QStringLiteral("Queue"), static_cast<int>(ptd::PlaybackPolicy::Queue));
    d_->combo_event_policy->addItem(QStringLiteral("Coalesce"), static_cast<int>(ptd::PlaybackPolicy::Coalesce));

    d_->combo_event_scope = new QComboBox();
    d_->combo_event_scope->addItem(QStringLiteral("Inherit Scope"), static_cast<int>(ptd::InterruptScope::Inherit));
    d_->combo_event_scope->addItem(QStringLiteral("Same Event"), static_cast<int>(ptd::InterruptScope::SameEvent));
    d_->combo_event_scope->addItem(QStringLiteral("Same Group"), static_cast<int>(ptd::InterruptScope::SameGroup));
    d_->combo_event_scope->addItem(QStringLiteral("Global"), static_cast<int>(ptd::InterruptScope::Global));
    ev_pol_row->addWidget(d_->combo_event_policy, 1);
    ev_pol_row->addWidget(d_->combo_event_scope, 1);
    evb->addLayout(ev_pol_row);

    d_->event_gain_db = new SliderSpin(
        "Gain", ptd::EventSoundConfig::kMinGainDb,
        ptd::EventSoundConfig::kMaxGainDb, 0.5, 1, " dB");
    d_->event_cooldown_ms = new SliderSpin(
        "Cooldown", 0.0, 1000.0, 5.0, 0, " ms");
    d_->event_priority = new SliderSpin(
        "Priority", ptd::EventSoundConfig::kMinPriority,
        ptd::EventSoundConfig::kMaxPriority, 1.0, 0, "");
    evb->addWidget(d_->event_gain_db);
    evb->addWidget(d_->event_cooldown_ms);
    evb->addWidget(d_->event_priority);

    // Expandable Advanced Event Controls (Section 8)
    d_->chk_show_advanced_event = new QCheckBox(QStringLiteral("Show Advanced Event Controls (Fade, Polyphony, Pitch, Normalization)"));
    evb->addWidget(d_->chk_show_advanced_event);

    d_->advanced_event_panel = new QWidget();
    auto* adv_ev_lay = new QVBoxLayout(d_->advanced_event_panel);
    adv_ev_lay->setContentsMargins(0, 0, 0, 0);
    adv_ev_lay->setSpacing(theme::controlPadPx());
    d_->event_fade_in_ms = new SliderSpin("Fade In", 0.0, ptd::EventSoundConfig::kMaxFadeMs, 5.0, 0, " ms");
    d_->event_fade_out_ms = new SliderSpin("Fade Out", 0.0, ptd::EventSoundConfig::kMaxFadeMs, 5.0, 0, " ms");
    d_->event_min_retrigger_ms = new SliderSpin("Min Retrigger", 0.0, 1000.0, 5.0, 0, " ms");
    d_->event_polyphony = new SliderSpin("Polyphony", ptd::EventSoundConfig::kMinPolyphony, ptd::EventSoundConfig::kMaxPolyphony, 1.0, 0, "");
    d_->event_pitch_var = new SliderSpin("Pitch Var", 0.0, ptd::EventSoundConfig::kMaxPitchVariation * 100.0, 1.0, 0, "%");
    auto* adv_chk_row = new QHBoxLayout();
    d_->chk_event_no_repeat = new QCheckBox(QStringLiteral("No Immediate Pool Repeat"));
    d_->chk_event_norm_override = new QCheckBox(QStringLiteral("Override Normalization"));
    d_->chk_event_norm_enabled = new QCheckBox(QStringLiteral("Normalize"));
    adv_chk_row->addWidget(d_->chk_event_no_repeat);
    adv_chk_row->addWidget(d_->chk_event_norm_override);
    adv_chk_row->addWidget(d_->chk_event_norm_enabled);
    adv_chk_row->addStretch(1);
    adv_ev_lay->addWidget(d_->event_fade_in_ms);
    adv_ev_lay->addWidget(d_->event_fade_out_ms);
    adv_ev_lay->addWidget(d_->event_min_retrigger_ms);
    adv_ev_lay->addWidget(d_->event_polyphony);
    adv_ev_lay->addWidget(d_->event_pitch_var);
    adv_ev_lay->addLayout(adv_chk_row);

    // T-121 (Gap F): truthful reporting of backend DSP limitations. QSoundEffect
    // lacks real-time DSP pitch shifting, fading, and normalization.
    d_->event_fade_in_ms->setEnabled(false);
    d_->event_fade_in_ms->setToolTip(QStringLiteral("DSP fade-in is not supported by the QSoundEffect audio backend."));
    d_->event_fade_out_ms->setEnabled(false);
    d_->event_fade_out_ms->setToolTip(QStringLiteral("DSP fade-out is not supported by the QSoundEffect audio backend."));
    d_->event_pitch_var->setEnabled(false);
    d_->event_pitch_var->setToolTip(QStringLiteral("DSP pitch variation is not supported by the QSoundEffect audio backend."));
    d_->chk_event_norm_override->setEnabled(false);
    d_->chk_event_norm_override->setToolTip(QStringLiteral("DSP normalization is not supported by the QSoundEffect audio backend."));
    d_->chk_event_norm_enabled->setEnabled(false);
    d_->chk_event_norm_enabled->setToolTip(QStringLiteral("DSP normalization is not supported by the QSoundEffect audio backend."));

    d_->advanced_event_panel->setVisible(false);
    evb->addWidget(d_->advanced_event_panel);

    // Random Sound Pool Editor (Section 9: up to 10 entries, weights, gain, reorder, preview)
    d_->pool_editor_panel = new QWidget();
    auto* pool_lay = new QVBoxLayout(d_->pool_editor_panel);
    pool_lay->setContentsMargins(0, 0, 0, 0);
    pool_lay->setSpacing(theme::controlPadPx());
    pool_lay->addWidget(new QLabel(QStringLiteral("Random Sound Pool (up to 10 entries, auto-normalized weights):")));
    d_->list_pool_entries = new QListWidget();
    d_->list_pool_entries->setMaximumHeight(96);
    pool_lay->addWidget(d_->list_pool_entries);

    auto* pool_add_row = new QHBoxLayout();
    pool_add_row->setSpacing(theme::controlPadPx());
    d_->combo_pool_add_sound = new QComboBox();
    d_->combo_pool_add_sound->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d_->btn_pool_add = new QPushButton(QStringLiteral("Add"));
    d_->btn_pool_remove = new QPushButton(QStringLiteral("Remove"));
    d_->btn_pool_up = new QPushButton(QStringLiteral("Up"));
    d_->btn_pool_down = new QPushButton(QStringLiteral("Down"));
    pool_add_row->addWidget(d_->combo_pool_add_sound, 1);
    pool_add_row->addWidget(d_->btn_pool_add);
    pool_add_row->addWidget(d_->btn_pool_remove);
    pool_add_row->addWidget(d_->btn_pool_up);
    pool_add_row->addWidget(d_->btn_pool_down);
    pool_lay->addLayout(pool_add_row);

    auto* pool_item_row = new QHBoxLayout();
    pool_item_row->setSpacing(theme::controlPadPx());
    d_->chk_pool_entry_enabled = new QCheckBox(QStringLiteral("Entry Enabled"));
    d_->btn_pool_preview_entry = new QPushButton(QStringLiteral("Preview Entry"));
    d_->btn_pool_preview_random = new QPushButton(QStringLiteral("Preview Pool"));
    pool_item_row->addWidget(d_->chk_pool_entry_enabled);
    pool_item_row->addStretch(1);
    pool_item_row->addWidget(d_->btn_pool_preview_entry);
    pool_item_row->addWidget(d_->btn_pool_preview_random);
    pool_lay->addLayout(pool_item_row);

    d_->pool_entry_weight = new SliderSpin("Entry Weight", 0.1, 10.0, 0.1, 1, "x");
    d_->pool_entry_gain_db = new SliderSpin("Entry Gain", ptd::EventSoundConfig::kMinGainDb, ptd::EventSoundConfig::kMaxGainDb, 0.5, 1, " dB");
    pool_lay->addWidget(d_->pool_entry_weight);
    pool_lay->addWidget(d_->pool_entry_gain_db);
    evb->addWidget(d_->pool_editor_panel);

    // 6. CUSTOM SHORTCUT ASSIGNMENT EDITOR (Section 4)
    QVBoxLayout* csc = nullptr;
    lay->addWidget(begin_section(QStringLiteral("CUSTOM SHORTCUTS"), csc));
    auto* csc_row = new QHBoxLayout();
    csc_row->setSpacing(theme::controlPadPx());
    d_->edit_custom_sc_name = new QLineEdit();
    d_->edit_custom_sc_name->setPlaceholderText(QStringLiteral("Name (e.g. Build)"));
    d_->edit_custom_sc_chord = new QLineEdit();
    d_->edit_custom_sc_chord->setPlaceholderText(QStringLiteral("Chord (e.g. Ctrl+Shift+B)"));
    d_->chk_custom_sc_app = new QCheckBox(QStringLiteral("App Shortcut"));
    d_->btn_custom_sc_add = new QPushButton(QStringLiteral("Add"));
    d_->btn_custom_sc_remove = new QPushButton(QStringLiteral("Remove"));
    csc_row->addWidget(d_->edit_custom_sc_name, 1);
    csc_row->addWidget(d_->edit_custom_sc_chord, 1);
    csc_row->addWidget(d_->chk_custom_sc_app);
    csc_row->addWidget(d_->btn_custom_sc_add);
    csc_row->addWidget(d_->btn_custom_sc_remove);
    csc->addLayout(csc_row);
    d_->list_custom_shortcuts = new QListWidget();
    d_->list_custom_shortcuts->setMaximumHeight(64);
    csc->addWidget(d_->list_custom_shortcuts);

    // 7. SCROLL SOUND SYSTEM & LEGACY MOUSE ASSIGNMENTS (Section 5 & T-80)
    QVBoxLayout* sc_sec = nullptr;
    lay->addWidget(begin_section(QStringLiteral("SCROLL SOUND SYSTEM"), sc_sec));
    d_->chk_horizontal_scroll = new QCheckBox(QStringLiteral("Enable Horizontal Scroll Sounds (Scroll Left / Right)"));
    sc_sec->addWidget(d_->chk_horizontal_scroll);
    d_->scroll_sensitivity = new SliderSpin("Sensitivity", 25.0, 400.0, 5.0, 0, "%");
    d_->scroll_cooldown = new SliderSpin("Scroll Cooldown", 0.0, 250.0, 5.0, 0, " ms");
    d_->scroll_max_rate = new SliderSpin("Max Scroll Rate", 1.0, 120.0, 1.0, 0, " Hz");
    d_->scroll_pitch_var = new SliderSpin("Pitch Variation", 0.0, 50.0, 1.0, 0, "%");
    d_->scroll_pitch_var->setEnabled(false);
    d_->scroll_pitch_var->setToolTip(QStringLiteral("DSP pitch variation is not supported by the QSoundEffect audio backend."));
    sc_sec->addWidget(d_->scroll_sensitivity);
    sc_sec->addWidget(d_->scroll_cooldown);
    sc_sec->addWidget(d_->scroll_max_rate);
    sc_sec->addWidget(d_->scroll_pitch_var);

    // EVENT SOUNDS: one assignment per normalized mouse event (preserved for T-80 compatibility).
    QVBoxLayout* as = nullptr;
    lay->addWidget(begin_section(QStringLiteral("EVENT SOUNDS"), as));
    static const char* kEventLabels[ptd::kAudioEventCount] = {
        "Left Down", "Left Up", "Right Down", "Right Up",
        "Middle Down", "Middle Up", "Scroll Up", "Scroll Down"};
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        auto* row = new QHBoxLayout();
        row->setSpacing(theme::controlPadPx());
        auto* lbl = make_row_label(QString::fromUtf8(kEventLabels[i]));
        auto* combo = new QComboBox();
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        combo->addItem(QStringLiteral("None"), QString());
        auto* play = new QPushButton("Play");
        play->setFixedWidth(60);
        row->addWidget(lbl);
        row->addWidget(combo, 1);
        row->addWidget(play);
        as->addLayout(row);
        d_->sound_assign[i] = combo;
        const int index = i;
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, index] { on_sound_assignment_changed(index); });
        connect(play, &QPushButton::clicked, this, [this, index] {
            const QString id = d_->sound_assign[index]->currentData().toString();
            if (!id.isEmpty()) emit sound_preview_requested(id.toStdString());
        });
    }

    refresh_sound_catalog();
    refresh_keyboard_event_list();
    populate_event_editor_for(d_->selected_event_id);
    lay->addStretch();
}

// Rebuilds the catalog list and every assignment combo from the shipped sound
// directory. Existing selections are re-applied afterwards by set_audio_widgets.
void SettingsWindow::refresh_sound_catalog() {
    const QStringList ids = ptd::SoundCatalog::sound_ids();
    const QString filter = d_->sound_search->text().trimmed();

    d_->sound_catalog->setUpdatesEnabled(false);
    d_->sound_catalog->clear();
    for (const QString& id : ids) {
        if (!filter.isEmpty() && !id.contains(filter, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(id, d_->sound_catalog);
        item->setData(Qt::UserRole, id);
    }
    d_->sound_catalog->setUpdatesEnabled(true);

    // Only rebuild combo item lists if the underlying catalog changed
    if (d_->cached_catalog_ids != ids) {
        d_->cached_catalog_ids = ids;
        auto rebuild_wav_combo = [&](QComboBox* combo, bool include_none) {
            if (!combo) return;
            const QString selected = combo->currentData().toString();
            const QSignalBlocker blocker(combo);
            combo->clear();
            if (include_none) combo->addItem(QStringLiteral("None"), QString());
            for (const QString& id : ids) combo->addItem(id, id);
            if (!selected.isEmpty()) {
                const int at = combo->findData(selected);
                if (at >= 0) {
                    combo->setCurrentIndex(at);
                } else {
                    combo->addItem(selected + QStringLiteral(" (missing)"), selected);
                    combo->setCurrentIndex(combo->count() - 1);
                }
            }
        };
        for (int i = 0; i < ptd::kAudioEventCount; ++i) {
            rebuild_wav_combo(d_->sound_assign[i], true);
        }
        rebuild_wav_combo(d_->combo_event_sound, true);
        rebuild_wav_combo(d_->combo_pool_add_sound, false);
    } else {
        // Ensure missing items are still preserved on current selections
        for (int i = 0; i < ptd::kAudioEventCount; ++i) {
            QComboBox* combo = d_->sound_assign[i];
            if (!combo) continue;
            const QString selected = combo->currentData().toString();
            if (!selected.isEmpty() && combo->findData(selected) < 0) {
                const QSignalBlocker blocker(combo);
                combo->addItem(selected + QStringLiteral(" (missing)"), selected);
                combo->setCurrentIndex(combo->count() - 1);
            }
        }
    }

    refresh_sound_favorites();
    refresh_sound_recent();
}

void SettingsWindow::refresh_sound_favorites() {
    if (d_->sound_favorites == nullptr) return;
    const QStringList& ids = d_->cached_catalog_ids;
    const QSignalBlocker blocker(d_->sound_favorites);
    d_->sound_favorites->clear();
    for (const std::string& raw : audio_cfg_.favorites) {
        const QString id = QString::fromStdString(raw);
        const bool present = ids.contains(id);
        auto* item = new QListWidgetItem(
            present ? id : id + QStringLiteral(" (missing)"), d_->sound_favorites);
        item->setData(Qt::UserRole, id);
    }
}

void SettingsWindow::refresh_sound_recent() {
    if (d_->sound_recent == nullptr) return;
    const QStringList& ids = d_->cached_catalog_ids;
    const QSignalBlocker blocker(d_->sound_recent);
    d_->sound_recent->clear();
    for (const std::string& raw : audio_cfg_.recent_sounds) {
        const QString id = QString::fromStdString(raw);
        const bool present = ids.contains(id);
        auto* item = new QListWidgetItem(
            present ? id : id + QStringLiteral(" (missing)"), d_->sound_recent);
        item->setData(Qt::UserRole, id);
    }
}

void SettingsWindow::record_recent_sound(const std::string& sound_id) {
    const std::string clean = ptd::sanitize_sound_id(sound_id);
    if (clean.empty()) return;
    auto& rec = audio_cfg_.recent_sounds;
    rec.erase(std::remove(rec.begin(), rec.end(), clean), rec.end());
    rec.insert(rec.begin(), clean);
    if (static_cast<int>(rec.size()) > ptd::AudioConfig::kMaxRecentSounds) {
        rec.resize(static_cast<std::size_t>(ptd::AudioConfig::kMaxRecentSounds));
    }
    refresh_sound_recent();
}

void SettingsWindow::schedule_catalog_audition(const std::string& sound_id) {
    if (!audio_cfg_.preview_on_selection || sound_id.empty()) return;
    if (audio_cfg_.preview_debounce_ms <= 0 || !d_->preview_debounce_timer) {
        emit sound_preview_requested(sound_id);
        return;
    }
    // Immediate audition for first selection; if user is rapidly scrolling/cycling
    // while debounce timer is already active, cancel stale pending preview and coalesce!
    if (!d_->preview_debounce_timer->isActive()) {
        d_->pending_preview_sound_id.clear();
        emit sound_preview_requested(sound_id);
        d_->preview_debounce_timer->start(audio_cfg_.preview_debounce_ms);
    } else {
        d_->pending_preview_sound_id = sound_id;
        d_->preview_debounce_timer->start(audio_cfg_.preview_debounce_ms);
    }
}

void SettingsWindow::refresh_keyboard_event_list() {
    if (!d_->list_sound_events) return;
    const int cat_filter = d_->combo_event_category ? d_->combo_event_category->currentData().toInt() : -1;
    const QString search = d_->edit_event_search ? d_->edit_event_search->text().trimmed() : QString();

    const QSignalBlocker blocker(d_->list_sound_events);
    d_->list_sound_events->setUpdatesEnabled(false);
    d_->list_sound_events->clear();

    auto format_event_row = [&](const std::string& cid, const QString& display_name,
                                const QString& cat_label, bool observable) {
        const ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(cid);
        QString summary;
        if (!ec.enabled) {
            summary = QStringLiteral("Off");
        } else if (ec.use_pool && !ec.pool.empty()) {
            summary = QStringLiteral("Pool (%1)").arg(ec.pool.size());
        } else if (!ec.sound_id.empty()) {
            const QString qid = QString::fromStdString(ec.sound_id);
            const bool exists = d_->cached_catalog_ids.contains(qid);
            summary = exists ? qid : (qid + QStringLiteral(" (missing)"));
        } else {
            summary = QStringLiteral("None");
        }
        QString label = QStringLiteral("[%1] %2 — %3").arg(cat_label, display_name, summary);
        if (!observable) {
            label += QStringLiteral(" [OS/HW dependent]");
        }
        auto* item = new QListWidgetItem(label, d_->list_sound_events);
        item->setData(Qt::UserRole, QString::fromStdString(cid));
        if (cid == d_->selected_event_id) {
            d_->list_sound_events->setCurrentItem(item);
        }
    };

    for (const auto& desc : ptd::canonical_sound_events()) {
        if (cat_filter >= 0 && static_cast<int>(desc.category) != cat_filter) {
            // When searching by text, search across all categories so user finds any event
            if (search.isEmpty()) continue;
        }
        const QString qid = QString::fromLatin1(desc.id);
        const QString qname = QString::fromLatin1(desc.display_name);
        const QString qcat = QString::fromLatin1(ptd::sound_event_category_label(desc.category));
        if (!search.isEmpty()) {
            if (!qid.contains(search, Qt::CaseInsensitive) &&
                !qname.contains(search, Qt::CaseInsensitive) &&
                !qcat.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
        }
        format_event_row(desc.id, qname, qcat, desc.observable_guarantee);
    }

    // Custom shortcuts in Custom category (or matching search)
    if (cat_filter < 0 || cat_filter == static_cast<int>(ptd::SoundEventCategory::Custom) || !search.isEmpty()) {
        for (const auto& cs : audio_cfg_.custom_shortcuts) {
            const QString qid = QString::fromStdString(cs.id);
            const QString qname = QStringLiteral("%1 (%2)").arg(
                QString::fromStdString(cs.display_name),
                QString::fromStdString(cs.chord));
            if (!search.isEmpty() &&
                !qid.contains(search, Qt::CaseInsensitive) &&
                !qname.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
            format_event_row(cs.id, qname, QStringLiteral("Custom"), true);
        }
    }

    d_->list_sound_events->setUpdatesEnabled(true);
}

void SettingsWindow::populate_event_editor_for(const std::string& canonical_id) {
    const std::string cid = ptd::canonicalize_sound_event_id(canonical_id);
    if (cid.empty()) return;
    d_->selected_event_id = cid;
    const ptd::SoundEventDescriptor* desc = ptd::find_sound_event_descriptor(cid);
    const ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(cid);

    if (d_->lbl_selected_event_title) {
        const QString disp = desc ? QString::fromLatin1(desc->display_name)
                                  : QString::fromStdString(cid);
        d_->lbl_selected_event_title->setText(
            QStringLiteral("Selected Event: %1 (%2)").arg(disp, QString::fromStdString(cid)));
    }
    if (d_->lbl_selected_event_note) {
        if (desc && !desc->observable_guarantee) {
            d_->lbl_selected_event_note->setText(
                QStringLiteral("Note: This key/shortcut may be intercepted by Windows or OEM firmware before reaching applications."));
            d_->lbl_selected_event_note->setVisible(true);
        } else {
            d_->lbl_selected_event_note->setVisible(false);
        }
    }

    QSignalBlocker b_en(d_->chk_event_enabled);
    QSignalBlocker b_pool(d_->chk_event_use_pool);
    QSignalBlocker b_snd(d_->combo_event_sound);
    QSignalBlocker b_pol(d_->combo_event_policy);
    QSignalBlocker b_scp(d_->combo_event_scope);
    QSignalBlocker b_gn(d_->event_gain_db);
    QSignalBlocker b_cd(d_->event_cooldown_ms);
    QSignalBlocker b_pr(d_->event_priority);
    QSignalBlocker b_fi(d_->event_fade_in_ms);
    QSignalBlocker b_fo(d_->event_fade_out_ms);
    QSignalBlocker b_mr(d_->event_min_retrigger_ms);
    QSignalBlocker b_po(d_->event_polyphony);
    QSignalBlocker b_pv(d_->event_pitch_var);
    QSignalBlocker b_nr(d_->chk_event_no_repeat);
    QSignalBlocker b_no(d_->chk_event_norm_override);
    QSignalBlocker b_ne(d_->chk_event_norm_enabled);

    d_->chk_event_enabled->setChecked(ec.enabled);
    d_->chk_event_use_pool->setChecked(ec.use_pool);

    // Populate combo_event_sound selection (showing (missing) if absent from catalog)
    {
        for (int i = d_->combo_event_sound->count() - 1; i >= 1; --i) {
            if (d_->combo_event_sound->itemText(i).endsWith(QStringLiteral(" (missing)"))) {
                d_->combo_event_sound->removeItem(i);
            }
        }
        if (ec.sound_id.empty()) {
            d_->combo_event_sound->setCurrentIndex(0);
        } else {
            const QString qid = QString::fromStdString(ec.sound_id);
            const int at = d_->combo_event_sound->findData(qid);
            if (at >= 0) {
                d_->combo_event_sound->setCurrentIndex(at);
            } else {
                d_->combo_event_sound->addItem(qid + QStringLiteral(" (missing)"), qid);
                d_->combo_event_sound->setCurrentIndex(d_->combo_event_sound->count() - 1);
            }
        }
    }

    const int pol_idx = d_->combo_event_policy->findData(static_cast<int>(ec.policy));
    d_->combo_event_policy->setCurrentIndex(pol_idx >= 0 ? pol_idx : 0);
    const int scp_idx = d_->combo_event_scope->findData(static_cast<int>(ec.interrupt_scope));
    d_->combo_event_scope->setCurrentIndex(scp_idx >= 0 ? scp_idx : 0);

    d_->event_gain_db->set_value(ec.gain_db);
    d_->event_cooldown_ms->set_value(ec.cooldown_ms);
    d_->event_priority->set_value(ec.priority);
    d_->event_fade_in_ms->set_value(ec.fade_in_ms);
    d_->event_fade_out_ms->set_value(ec.fade_out_ms);
    d_->event_min_retrigger_ms->set_value(ec.min_retrigger_ms);
    d_->event_polyphony->set_value(ec.polyphony_allowance);
    d_->event_pitch_var->set_value(ec.pitch_variation * 100.0);
    d_->chk_event_no_repeat->setChecked(ec.no_immediate_repeat);
    d_->chk_event_norm_override->setChecked(ec.normalize_override);
    d_->chk_event_norm_enabled->setChecked(ec.normalize_enabled);

    // Populate Random Pool list
    if (d_->list_pool_entries) {
        const QSignalBlocker bp(d_->list_pool_entries);
        d_->list_pool_entries->clear();
        const std::vector<float> norm_w = ptd::normalized_pool_weights(ec.pool);
        for (std::size_t i = 0; i < ec.pool.size(); ++i) {
            const auto& pe = ec.pool[i];
            const QString qid = QString::fromStdString(pe.sound_id);
            const bool exists = d_->cached_catalog_ids.contains(qid);
            const int pct = static_cast<int>(std::lround(norm_w[i] * 100.0f));
            const QString text = QStringLiteral("%1%2 [w=%3 (%4%), %5 dB]%6")
                .arg(pe.enabled ? QString() : QStringLiteral("[OFF] "))
                .arg(qid)
                .arg(pe.weight, 0, 'f', 1)
                .arg(pct)
                .arg(pe.gain_db, 0, 'f', 1)
                .arg(exists ? QString() : QStringLiteral(" (missing)"));
            auto* item = new QListWidgetItem(text, d_->list_pool_entries);
            item->setData(Qt::UserRole, static_cast<int>(i));
        }
        if (d_->list_pool_entries->count() > 0) {
            d_->list_pool_entries->setCurrentRow(0);
            const auto& first = ec.pool[0];
            QSignalBlocker bpe(d_->chk_pool_entry_enabled);
            QSignalBlocker bpw(d_->pool_entry_weight);
            QSignalBlocker bpg(d_->pool_entry_gain_db);
            d_->chk_pool_entry_enabled->setChecked(first.enabled);
            d_->pool_entry_weight->set_value(first.weight);
            d_->pool_entry_gain_db->set_value(first.gain_db);
        }
    }
}

void SettingsWindow::select_sound_event_for_editing(const QString& canonical_event_id) {
    const std::string cid = ptd::canonicalize_sound_event_id(canonical_event_id.toStdString());
    if (cid.empty()) return;
    populate_event_editor_for(cid);
    refresh_keyboard_event_list();
}

QString SettingsWindow::selected_sound_event_id() const {
    return QString::fromStdString(d_->selected_event_id);
}

void SettingsWindow::reset_sound_category(ptd::SoundEventCategory cat) {
    audio_cfg_.reset_category(cat);
    audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
    set_audio_widgets(audio_cfg_);
    emit_audio_config();
}

void SettingsWindow::reset_selected_sound_event() {
    if (d_->selected_event_id.empty()) return;
    audio_cfg_.set_event_config(d_->selected_event_id, ptd::EventSoundConfig{});
    audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
    set_audio_widgets(audio_cfg_);
    emit_audio_config();
}

void SettingsWindow::apply_sound_profile_preset(ptd::SoundProfilePreset preset) {
    std::vector<std::string> avail;
    for (const QString& qid : d_->cached_catalog_ids) {
        avail.push_back(qid.toStdString());
    }
    audio_cfg_.apply_profile_preset(preset, avail);
    audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
    set_audio_widgets(audio_cfg_);
    emit_audio_config();
}

bool SettingsWindow::import_audio_profile_json(const QByteArray& json, QString* error_msg) {
    auto imported = ptd::ConfigStorage::deserialize_audio_profile_json(json, audio_cfg_, error_msg);
    if (!imported) return false;
    audio_cfg_ = *imported;
    set_audio_widgets(audio_cfg_);
    emit_audio_config();
    return true;
}

QByteArray SettingsWindow::export_audio_profile_json() const {
    return ptd::ConfigStorage::serialize_audio_profile_json(audio_cfg_);
}

bool SettingsWindow::is_browsing_sound_catalog() const {
    if (!isVisible()) return false;
    if (!d_->tabs || d_->tabs->currentWidget() != d_->sound_tab) return false;
    QWidget* fw = focusWidget();
    if (!fw) return false;
    return fw == d_->sound_catalog || fw == d_->sound_favorites ||
           fw == d_->sound_recent || fw == d_->list_sound_events ||
           (d_->sound_catalog && d_->sound_catalog->isAncestorOf(fw));
}

// ---- T-80 / T-120 Sound slots ----

void SettingsWindow::on_sound_enabled_toggled() {
    emit_audio_config();
}

void SettingsWindow::on_sound_mute_toggled() {
    emit_audio_config();
}

void SettingsWindow::on_sound_volume(double) {
    emit_audio_config();
}

void SettingsWindow::on_sound_assignment_changed(int index) {
    if (index < 0 || index >= ptd::kAudioEventCount) return;
    const QString id = d_->sound_assign[index]->currentData().toString();
    const std::string sid = id.toStdString();
    audio_cfg_.sounds[static_cast<std::size_t>(index)] = sid;
    const std::string cid = ptd::canonicalize_sound_event_id(ptd::audio_event_name(index));
    ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(cid);
    ec.sound_id = sid;
    audio_cfg_.set_event_config(cid, ec);
    if (!sid.empty()) {
        record_recent_sound(sid);
        emit sound_preview_requested(sid);
    }
    if (d_->selected_event_id == cid) {
        populate_event_editor_for(cid);
    }
    refresh_keyboard_event_list();
    emit_audio_config();
}

void SettingsWindow::on_sound_catalog_selection_changed() {
    QListWidgetItem* item = d_->sound_catalog->currentItem();
    if (item == nullptr) return;
    const std::string sid = item->data(Qt::UserRole).toString().toStdString();
    record_recent_sound(sid);
    schedule_catalog_audition(sid);
}

void SettingsWindow::on_sound_replay_clicked() {
    QListWidgetItem* item = d_->sound_catalog->currentItem();
    if (item == nullptr) return;
    if (d_->preview_debounce_timer) d_->preview_debounce_timer->stop();
    d_->pending_preview_sound_id.clear();
    emit sound_preview_requested(item->data(Qt::UserRole).toString().toStdString());
}

void SettingsWindow::on_sound_stop_clicked() {
    if (d_->preview_debounce_timer) d_->preview_debounce_timer->stop();
    d_->pending_preview_sound_id.clear();
    emit sound_preview_stop_requested();
}

void SettingsWindow::on_sound_stop_all_clicked() {
    if (d_->preview_debounce_timer) d_->preview_debounce_timer->stop();
    d_->pending_preview_sound_id.clear();
    emit sound_stop_all_requested();
}

void SettingsWindow::on_sound_catalog_context_menu(const QPoint& pos) {
    QListWidgetItem* item = d_->sound_catalog->itemAt(pos);
    if (item == nullptr) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;
    QMenu menu(this);
    const bool is_favorite =
        std::find(audio_cfg_.favorites.begin(), audio_cfg_.favorites.end(),
                  id.toStdString()) != audio_cfg_.favorites.end();
    QAction* toggle = menu.addAction(is_favorite ? QStringLiteral("Remove from Favorites")
                                                 : QStringLiteral("Add to Favorites"));
    if (menu.exec(d_->sound_catalog->viewport()->mapToGlobal(pos)) == toggle) {
        toggle_sound_favorite(id);
    }
}

void SettingsWindow::on_sound_favorites_context_menu(const QPoint& pos) {
    QListWidgetItem* item = d_->sound_favorites->itemAt(pos);
    if (item == nullptr) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;
    QMenu menu(this);
    QAction* remove = menu.addAction(QStringLiteral("Remove from Favorites"));
    if (menu.exec(d_->sound_favorites->viewport()->mapToGlobal(pos)) == remove) {
        toggle_sound_favorite(id);
    }
}

void SettingsWindow::toggle_sound_favorite(const QString& id) {
    const std::string sid = id.toStdString();
    auto it = std::find(audio_cfg_.favorites.begin(), audio_cfg_.favorites.end(), sid);
    if (it == audio_cfg_.favorites.end()) {
        audio_cfg_.favorites.push_back(sid);
    } else {
        audio_cfg_.favorites.erase(it);
    }
    audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
    refresh_sound_favorites();
    emit_audio_config();
}

QStringList SettingsWindow::favorites_for_tests() const {
    QStringList out;
    if (d_->sound_favorites == nullptr) return out;
    for (int i = 0; i < d_->sound_favorites->count(); ++i) {
        out << d_->sound_favorites->item(i)->data(Qt::UserRole).toString();
    }
    return out;
}

// Silent population: widgets carry audio_cfg_ without publishing or previewing.
void SettingsWindow::set_audio_widgets(const ptd::AudioConfig& c) {
    if (d_->preview_debounce_timer) d_->preview_debounce_timer->stop();
    d_->pending_preview_sound_id.clear();

    QSignalBlocker be(d_->chk_sound_enabled);
    QSignalBlocker bm(d_->chk_sound_muted);
    QSignalBlocker bv(d_->sound_volume);
    QSignalBlocker bp(d_->chk_preview_on_select);
    QSignalBlocker bk(d_->chk_keyboard_enabled);
    QSignalBlocker bks(d_->combo_keyboard_scope);
    QSignalBlocker bt(d_->chk_typing_sounds);
    QSignalBlocker bmt(d_->chk_modifier_tap_sounds);
    QSignalBlocker bsr(d_->chk_suppress_repeat_typing);
    QSignalBlocker bss(d_->chk_suppress_sensitive);
    QSignalBlocker bhs(d_->chk_horizontal_scroll);
    QSignalBlocker bssens(d_->scroll_sensitivity);
    QSignalBlocker bscd(d_->scroll_cooldown);
    QSignalBlocker bsmr(d_->scroll_max_rate);
    QSignalBlocker bspv(d_->scroll_pitch_var);
    QSignalBlocker bprof(d_->combo_sound_profile);

    d_->chk_sound_enabled->setChecked(c.enabled);
    d_->chk_sound_muted->setChecked(c.muted);
    d_->sound_volume->set_value(c.volume * 100.0);
    if (d_->chk_preview_on_select) d_->chk_preview_on_select->setChecked(c.preview_on_selection);

    if (d_->chk_keyboard_enabled) d_->chk_keyboard_enabled->setChecked(c.keyboard_enabled);
    if (d_->combo_keyboard_scope) {
        const int idx = d_->combo_keyboard_scope->findData(static_cast<int>(c.keyboard_scope));
        d_->combo_keyboard_scope->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (d_->chk_typing_sounds) d_->chk_typing_sounds->setChecked(c.typing_sounds_enabled);
    if (d_->chk_modifier_tap_sounds) d_->chk_modifier_tap_sounds->setChecked(c.modifier_tap_sounds_enabled);
    if (d_->chk_suppress_repeat_typing) d_->chk_suppress_repeat_typing->setChecked(c.suppress_repeat_typing);
    if (d_->chk_suppress_sensitive) d_->chk_suppress_sensitive->setChecked(c.suppress_sensitive_contexts);

    if (d_->list_excluded_procs) {
        const QSignalBlocker bex(d_->list_excluded_procs);
        d_->list_excluded_procs->clear();
        for (const std::string& proc : c.excluded_processes) {
            d_->list_excluded_procs->addItem(QString::fromStdString(proc));
        }
    }

    if (d_->list_custom_shortcuts) {
        const QSignalBlocker bcs(d_->list_custom_shortcuts);
        d_->list_custom_shortcuts->clear();
        for (const auto& cs : c.custom_shortcuts) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1 [%2]%3")
                    .arg(QString::fromStdString(cs.display_name),
                         QString::fromStdString(cs.chord),
                         cs.is_app_shortcut ? QStringLiteral(" (App)") : QStringLiteral(" (User)")),
                d_->list_custom_shortcuts);
            item->setData(Qt::UserRole, QString::fromStdString(cs.id));
        }
    }

    if (d_->chk_horizontal_scroll) d_->chk_horizontal_scroll->setChecked(c.horizontal_scroll_enabled);
    if (d_->scroll_sensitivity) d_->scroll_sensitivity->set_value(c.scroll_sensitivity * 100.0);
    if (d_->scroll_cooldown) d_->scroll_cooldown->set_value(c.scroll_cooldown_ms);
    if (d_->scroll_max_rate) d_->scroll_max_rate->set_value(c.scroll_max_rate_hz);
    if (d_->scroll_pitch_var) d_->scroll_pitch_var->set_value(c.scroll_pitch_variation * 100.0);

    if (d_->combo_sound_profile) {
        const int idx = d_->combo_sound_profile->findData(static_cast<int>(c.active_profile));
        d_->combo_sound_profile->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        QComboBox* combo = d_->sound_assign[i];
        const QSignalBlocker blocker(combo);
        for (int j = combo->count() - 1; j >= 1; --j) {
            if (combo->itemText(j).endsWith(QStringLiteral(" (missing)"))) {
                combo->removeItem(j);
            }
        }
        const std::string id = ptd::sanitize_sound_id(c.sounds[static_cast<std::size_t>(i)]);
        if (id.empty()) {
            combo->setCurrentIndex(0);
            continue;
        }
        const QString qid = QString::fromStdString(id);
        const int at = combo->findData(qid);
        if (at >= 0) {
            combo->setCurrentIndex(at);
        } else {
            combo->addItem(qid + QStringLiteral(" (missing)"), qid);
            combo->setCurrentIndex(combo->count() - 1);
        }
    }

    audio_cfg_ = c;
    refresh_sound_favorites();
    refresh_sound_recent();
    refresh_keyboard_event_list();
    populate_event_editor_for(d_->selected_event_id);
}

void SettingsWindow::emit_audio_config() {
    audio_cfg_.enabled = d_->chk_sound_enabled->isChecked();
    audio_cfg_.muted = d_->chk_sound_muted->isChecked();
    audio_cfg_.volume =
        static_cast<float>(d_->sound_volume->value() / 100.0);
    if (d_->chk_preview_on_select)
        audio_cfg_.preview_on_selection = d_->chk_preview_on_select->isChecked();
    if (d_->chk_keyboard_enabled)
        audio_cfg_.keyboard_enabled = d_->chk_keyboard_enabled->isChecked();
    if (d_->combo_keyboard_scope)
        audio_cfg_.keyboard_scope = static_cast<ptd::KeyboardScope>(d_->combo_keyboard_scope->currentData().toInt());
    if (d_->chk_typing_sounds)
        audio_cfg_.typing_sounds_enabled = d_->chk_typing_sounds->isChecked();
    if (d_->chk_modifier_tap_sounds)
        audio_cfg_.modifier_tap_sounds_enabled = d_->chk_modifier_tap_sounds->isChecked();
    if (d_->chk_suppress_repeat_typing)
        audio_cfg_.suppress_repeat_typing = d_->chk_suppress_repeat_typing->isChecked();
    if (d_->chk_suppress_sensitive)
        audio_cfg_.suppress_sensitive_contexts = d_->chk_suppress_sensitive->isChecked();
    if (d_->chk_horizontal_scroll)
        audio_cfg_.horizontal_scroll_enabled = d_->chk_horizontal_scroll->isChecked();
    if (d_->scroll_sensitivity)
        audio_cfg_.scroll_sensitivity = static_cast<float>(d_->scroll_sensitivity->value() / 100.0);
    if (d_->scroll_cooldown)
        audio_cfg_.scroll_cooldown_ms = static_cast<int>(d_->scroll_cooldown->value());
    if (d_->scroll_max_rate)
        audio_cfg_.scroll_max_rate_hz = static_cast<int>(d_->scroll_max_rate->value());
    if (d_->scroll_pitch_var)
        audio_cfg_.scroll_pitch_variation = static_cast<float>(d_->scroll_pitch_var->value() / 100.0);
    if (d_->combo_sound_profile)
        audio_cfg_.active_profile = static_cast<ptd::SoundProfilePreset>(d_->combo_sound_profile->currentData().toInt());

    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        audio_cfg_.sounds[static_cast<std::size_t>(i)] =
            d_->sound_assign[i]->currentData().toString().toStdString();
    }
    audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
    emit audio_config_changed(audio_cfg_);
}

void SettingsWindow::build_click_tab() {
    d_->click_tab = new QWidget();
    d_->click_tab->setObjectName(QStringLiteral("tab_click"));
    auto* root = new QVBoxLayout(d_->click_tab);
    root->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                             theme::groupPadPx(), theme::groupPadPx());
    root->setSpacing(theme::sectionGapPx());

    auto scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget();
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(theme::sectionGapPx());
    scroll->setWidget(content);

    // STYLE (T-020 Phase 10): 7 styles as a compact checkable grid.
    QVBoxLayout* ks = nullptr;
    lay->addWidget(begin_section(QStringLiteral("STYLE"), ks));
    auto* cstyle_grid_widget = new QWidget();
    auto* cgrid = new QGridLayout(cstyle_grid_widget);
    cgrid->setContentsMargins(0, 0, 0, 0);
    cgrid->setHorizontalSpacing(theme::controlPadPx());
    cgrid->setVerticalSpacing(theme::controlPadPx());
    const QStringList cstyle_texts = {
        "Ring", "Double Ring", "Ripple", "Burst",
        "Spark Burst", "Soft Flash", "Dot + Ring",
        // T-022 Elemental Click VFX.
        "Air", "Fire", "Water", "Earth"};
    const QStringList cstyle_names = {
        "click_style_ring", "click_style_double_ring", "click_style_ripple",
        "click_style_burst", "click_style_spark_burst", "click_style_soft_flash",
        "click_style_dot_ring",
        "click_style_air", "click_style_fire", "click_style_water",
        "click_style_earth"};
    d_->click_style_group = new QButtonGroup(this);
    d_->click_style_group->setExclusive(true);
    for (int i = 0; i < cstyle_texts.size(); ++i) {
        auto* btn = make_check(cstyle_texts.at(i), cstyle_names.at(i));
        d_->click_style_group->addButton(btn, i);
        cgrid->addWidget(btn, i / 4, i % 4);
        cgrid->setColumnStretch(i % 4, 1);
    }
    ks->addWidget(cstyle_grid_widget);

    // T-020R1: particle amount is a style-specific control (Burst / Spark
    // Burst), so it lives in a bounded options region -- the same principle
    // as the Trail STYLE region. Without it, switching click style changed
    // the height of STYLE and moved TRIGGERS / COLOR / ANIMATION.
    //
    // T-022: the region now reserves TWO rows, because the elemental styles
    // show Particles AND Element tint together. The height is fixed at the
    // maximum either way, so switching to any style -- elemental or not --
    // still moves no anchor; only the row visibility changes.
    d_->click_style_options = new QWidget();
    auto* click_options_lay = new QVBoxLayout(d_->click_style_options);
    click_options_lay->setContentsMargins(0, 0, 0, 0);
    click_options_lay->setSpacing(0);
    d_->click_particles = new SliderSpin(
        "Particles", ptd::ClickConfig::kMinParticleAmount,
        ptd::ClickConfig::kMaxParticleAmount, 1.0, 0, "");
    click_options_lay->addWidget(d_->click_particles);
    d_->click_element_tint = new SliderSpin(
        "Element tint", ptd::ClickConfig::kMinElementTint,
        ptd::ClickConfig::kMaxElementTint, 0.01, 2, "");
    click_options_lay->addWidget(d_->click_element_tint);
    d_->click_style_options->setFixedHeight(
        d_->click_particles->sizeHint().height()
        + d_->click_element_tint->sizeHint().height());
    ks->addWidget(d_->click_style_options);

    // TRIGGERS (T-020 Phase 9): one compact horizontal row.
    QVBoxLayout* tg = nullptr;
    lay->addWidget(begin_section(QStringLiteral("TRIGGERS"), tg));
    auto* trig_row = new QHBoxLayout();
    trig_row->setSpacing(theme::sectionGapPx());
    d_->chk_trig_left   = new QCheckBox("Left");
    d_->chk_trig_right  = new QCheckBox("Right");
    d_->chk_trig_middle = new QCheckBox("Middle");
    trig_row->addWidget(d_->chk_trig_left);
    trig_row->addWidget(d_->chk_trig_right);
    trig_row->addWidget(d_->chk_trig_middle);
    trig_row->addStretch(1);
    tg->addLayout(trig_row);

    // HOLD (T-024 + T-027). Two gesture toggles and four artistic
    // multipliers. There is deliberately STILL no second HOLD style selector
    // (Click style is the only style authority) and no activation-threshold
    // control: the short-click / HOLD boundary stays a stable internal
    // constant. Every row is always present -- toggling Hold FX or Motion
    // Wake changes ENABLED state only, so this section's height is a
    // constant and nothing below it can shift.
    QVBoxLayout* hd = nullptr;
    lay->addWidget(begin_section(QStringLiteral("HOLD"), hd));
    auto* hold_row = new QHBoxLayout();
    hold_row->setSpacing(theme::sectionGapPx());
    d_->chk_hold_fx = new QCheckBox("Hold FX");
    d_->chk_hold_wake = new QCheckBox("Motion Wake");
    hold_row->addWidget(d_->chk_hold_fx);
    hold_row->addWidget(d_->chk_hold_wake);
    hold_row->addStretch(1);
    hd->addLayout(hold_row);

    // Intensity / Wake Density / Wake Life / Release Strength. Ranges come
    // from the ClickConfig bound constants, never from duplicated magic
    // numbers (same contract as every other slider in this window).
    d_->hold_intensity = new SliderSpin(
        "Intensity", ptd::ClickConfig::kMinHoldIntensity * 100.0,
        ptd::ClickConfig::kMaxHoldIntensity * 100.0, 1.0, 0, "%");
    d_->hold_wake_density = new SliderSpin(
        "Wake Density", ptd::ClickConfig::kMinHoldWakeDensity * 100.0,
        ptd::ClickConfig::kMaxHoldWakeDensity * 100.0, 1.0, 0, "%");
    // Wake Life is shown in SECONDS (0.15 .. 2.50) because that is how a
    // user thinks about a fade; the config field stays in milliseconds.
    d_->hold_wake_lifetime = new SliderSpin(
        "Wake Life", ptd::ClickConfig::kMinHoldWakeLifetimeMs / 1000.0,
        ptd::ClickConfig::kMaxHoldWakeLifetimeMs / 1000.0, 0.05, 2, " s");
    d_->hold_release_strength = new SliderSpin(
        "Release Strength", ptd::ClickConfig::kMinHoldReleaseStrength * 100.0,
        ptd::ClickConfig::kMaxHoldReleaseStrength * 100.0, 1.0, 0, "%");
    hd->addWidget(d_->hold_intensity);
    hd->addWidget(d_->hold_wake_density);
    hd->addWidget(d_->hold_wake_lifetime);
    hd->addWidget(d_->hold_release_strength);

    // T-36 ADVANCED MOTION WAKE. The user explicitly asked for more Motion
    // Wake controls; these stay inside the existing HOLD section (no new
    // style selector, no threshold control, no exposed safety cap). Every
    // range comes from the ClickConfig bound constants. Rows are always
    // present; toggling Motion Wake changes ENABLED state only.
    QVBoxLayout* adv = nullptr;
    lay->addWidget(begin_section(QStringLiteral("ADVANCED MOTION WAKE"), adv));
    d_->wake_strength = new SliderSpin(
        "Wake Strength", ptd::ClickConfig::kMinWakeStrength * 100.0,
        ptd::ClickConfig::kMaxWakeStrength * 100.0, 1.0, 0, "%");
    d_->wake_size = new SliderSpin(
        "Wake Size", ptd::ClickConfig::kMinWakeSize * 100.0,
        ptd::ClickConfig::kMaxWakeSize * 100.0, 1.0, 0, "%");
    d_->wake_spread = new SliderSpin(
        "Wake Spread", ptd::ClickConfig::kMinWakeSpread * 100.0,
        ptd::ClickConfig::kMaxWakeSpread * 100.0, 1.0, 0, "%");
    d_->speed_response = new SliderSpin(
        "Speed Response", ptd::ClickConfig::kMinSpeedResponse * 100.0,
        ptd::ClickConfig::kMaxSpeedResponse * 100.0, 1.0, 0, "%");
    // Minimum Motion Speed is a px/s gate; 0 disables it.
    d_->min_motion_speed = new SliderSpin(
        "Min Motion Speed", ptd::ClickConfig::kMinMotionSpeedPxPerSec,
        ptd::ClickConfig::kMaxMotionSpeedPxPerSec, 10.0, 0, " px/s");
    adv->addWidget(d_->wake_strength);
    adv->addWidget(d_->wake_size);
    adv->addWidget(d_->wake_spread);
    adv->addWidget(d_->speed_response);
    adv->addWidget(d_->min_motion_speed);
    auto* accent_row = new QHBoxLayout();
    accent_row->setSpacing(theme::sectionGapPx());
    d_->chk_turn_accent = new QCheckBox("Turn Accent");
    d_->chk_stop_accent = new QCheckBox("Stop Accent");
    accent_row->addWidget(d_->chk_turn_accent);
    accent_row->addWidget(d_->chk_stop_accent);
    accent_row->addStretch(1);
    adv->addLayout(accent_row);

    // COLOR
    QVBoxLayout* cl = nullptr;
    lay->addWidget(begin_section(QStringLiteral("COLOR"), cl));
    d_->click_color = new ColorEditor("RGB", click_cfg_.color_r,
                                      click_cfg_.color_g, click_cfg_.color_b);
    cl->addWidget(d_->click_color);

    // ANIMATION (T-020 Phase 13): geometry / opacity + easing buttons.
    QVBoxLayout* an = nullptr;
    lay->addWidget(begin_section(QStringLiteral("ANIMATION"), an));
    d_->click_start_size = new SliderSpin(
        "Start size", ptd::ClickConfig::kMinRadiusPx,
        ptd::ClickConfig::kMaxStartRadiusPx, 1.0, 0, " px");
    d_->click_end_size = new SliderSpin(
        "End size", 1.0, ptd::ClickConfig::kMaxEndRadiusPx, 1.0, 0, " px");
    d_->click_duration = new SliderSpin(
        "Duration", ptd::ClickConfig::kMinDurationMs,
        ptd::ClickConfig::kMaxDurationMs, 10.0, 0, " ms");
    d_->click_opacity = new SliderSpin(
        "Opacity", ptd::ClickConfig::kMinOpacity * 100.0,
        ptd::ClickConfig::kMaxOpacity * 100.0, 1.0, 0, "%");
    d_->click_outline = new SliderSpin(
        "Outline", ptd::ClickConfig::kMinOutlinePx,
        ptd::ClickConfig::kMaxOutlinePx, 0.1, 1, " px");
    d_->click_fill = new SliderSpin(
        "Fill opacity", ptd::ClickConfig::kMinFillOpacity * 100.0,
        ptd::ClickConfig::kMaxFillOpacity * 100.0, 1.0, 0, "%");
    an->addWidget(d_->click_start_size);
    an->addWidget(d_->click_end_size);
    an->addWidget(d_->click_duration);
    an->addWidget(d_->click_opacity);
    // T-022: Outline and Fill opacity are style-specific (Soft Flash and
    // Spark Burst draw no outline; only Ring and Dot + Ring carry a fill),
    // so they get the SAME bounded-region treatment the STYLE options
    // already have: a fixed two-row host whose height never changes, only
    // the visibility of the rows inside it. Without this, hiding a row
    // re-flowed the ANIMATION section and moved its anchor -- the exact
    // defect class the T-020R1 selector contract exists to prevent.
    d_->click_param_options = new QWidget();
    auto* click_param_lay = new QVBoxLayout(d_->click_param_options);
    click_param_lay->setContentsMargins(0, 0, 0, 0);
    click_param_lay->setSpacing(0);
    click_param_lay->addWidget(d_->click_outline);
    click_param_lay->addWidget(d_->click_fill);
    d_->click_param_options->setFixedHeight(
        d_->click_outline->sizeHint().height()
        + d_->click_fill->sizeHint().height());
    an->addWidget(d_->click_param_options);

    // Easing (T-020 Phase 12): three mutually exclusive buttons.
    auto* ease_row = new QHBoxLayout();
    ease_row->setSpacing(theme::controlPadPx());
    ease_row->addWidget(make_row_label(QStringLiteral("Easing")));
    auto* ease_widget = make_selector_row(
        {"Linear", "Smooth", "Ease Out"},
        {"click_easing_linear", "click_easing_smooth", "click_easing_ease_out"},
        &d_->click_easing_group, this);
    ease_row->addWidget(ease_widget, 1);
    an->addLayout(ease_row);

    // Restore
    d_->btn_restore_click = new QPushButton("Restore Click Defaults");
    lay->addWidget(d_->btn_restore_click);
    lay->addStretch();

    root->addWidget(scroll);
}

void SettingsWindow::build_dev_tab() {
    d_->dev_tab = new QWidget();
    auto* root = new QVBoxLayout(d_->dev_tab);
    root->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget();
    auto* lay = new QVBoxLayout(inner);
    lay->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                            theme::groupPadPx(), theme::groupPadPx());
    lay->setSpacing(theme::sectionGapPx());

    // 1. CAPTURE
    QVBoxLayout* cap_lay = nullptr;
    lay->addWidget(begin_section(QStringLiteral("CAPTURE SETTINGS"), cap_lay));
    d_->btn_dev_capture = new QPushButton(QStringLiteral("Capture Current Settings"));
    d_->lbl_dev_captured = new QLabel(QStringLiteral("No captured snapshot."));
    d_->lbl_dev_captured->setWordWrap(true);
    cap_lay->addWidget(d_->btn_dev_capture);
    cap_lay->addWidget(d_->lbl_dev_captured);

    // 2. DEV PRESETS
    QVBoxLayout* pre_lay = nullptr;
    lay->addWidget(begin_section(QStringLiteral("DEVELOPER PRESETS"), pre_lay));

    auto* save_row = new QHBoxLayout();
    d_->edit_dev_preset_name = new QLineEdit();
    d_->edit_dev_preset_name->setPlaceholderText(QStringLiteral("Preset name (leave empty for timestamp)"));
    d_->btn_dev_save_preset = new QPushButton(QStringLiteral("Save as Dev Preset"));
    save_row->addWidget(d_->edit_dev_preset_name, 1);
    save_row->addWidget(d_->btn_dev_save_preset);
    pre_lay->addLayout(save_row);

    d_->list_dev_presets = new QListWidget();
    d_->list_dev_presets->setFixedHeight(72);
    d_->btn_dev_apply_preset = new QPushButton(QStringLiteral("Apply Dev Preset"));
    pre_lay->addWidget(new QLabel(QStringLiteral("Available Presets (dev/presets):")));
    pre_lay->addWidget(d_->list_dev_presets);
    pre_lay->addWidget(d_->btn_dev_apply_preset);

    d_->btn_dev_apply_defaults = new QPushButton(QStringLiteral("Apply Release Defaults to Current"));
    pre_lay->addWidget(d_->btn_dev_apply_defaults);

    // 3. DIFF & PROMOTION
    QVBoxLayout* prom_lay = nullptr;
    lay->addWidget(begin_section(QStringLiteral("RELEASE DEFAULTS DIFF & PROMOTION"), prom_lay));

    prom_lay->addWidget(new QLabel(QStringLiteral("Diff Source:")));
    QWidget* src_row = make_selector_row(
        {QStringLiteral("Live Settings"), QStringLiteral("Captured Snapshot"), QStringLiteral("Selected Preset")},
        {QStringLiteral("btn_src_live"), QStringLiteral("btn_src_captured"), QStringLiteral("btn_src_preset")},
        &d_->dev_source_group, this);
    if (auto* btn = d_->dev_source_group->button(0)) {
        btn->setChecked(true);
    }
    prom_lay->addWidget(src_row);

    d_->btn_dev_show_diff = new QPushButton(QStringLiteral("Show Difference From Release Defaults"));
    prom_lay->addWidget(d_->btn_dev_show_diff);

    d_->btn_dev_promote = new QPushButton(QStringLiteral("Set Current as Release Defaults"));
    d_->btn_dev_promote->setStyleSheet(QStringLiteral("font-weight: bold;"));
    prom_lay->addWidget(d_->btn_dev_promote);

    d_->lbl_dev_status = new QLabel();
    d_->lbl_dev_status->setWordWrap(true);
    prom_lay->addWidget(d_->lbl_dev_status);

    d_->txt_dev_diff = new QPlainTextEdit();
    d_->txt_dev_diff->setReadOnly(true);
    d_->txt_dev_diff->setMinimumHeight(150);
    QFont mono_font(QStringLiteral("Consolas"));
    mono_font.setStyleStrategy(QFont::NoAntialias);
    d_->txt_dev_diff->setFont(mono_font);
    prom_lay->addWidget(d_->txt_dev_diff);

    lay->addStretch();
    scroll->setWidget(inner);
    root->addWidget(scroll);
}

void SettingsWindow::refresh_dev_presets() {
    if (d_->list_dev_presets == nullptr) return;
    QString current;
    if (d_->list_dev_presets->currentItem()) {
        current = d_->list_dev_presets->currentItem()->text();
    }
    d_->list_dev_presets->clear();
    auto list = ptd::list_dev_presets();
    for (const auto& item : list) {
        d_->list_dev_presets->addItem(QString::fromStdString(item));
    }
    if (!current.isEmpty()) {
        auto items = d_->list_dev_presets->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty()) {
            d_->list_dev_presets->setCurrentItem(items.first());
        }
    } else if (d_->list_dev_presets->count() > 0) {
        d_->list_dev_presets->setCurrentRow(0);
    }
}

std::optional<ptd::AppConfig> SettingsWindow::resolve_dev_selected_config() const {
    const int id = d_->dev_source_group ? d_->dev_source_group->checkedId() : 0;
    if (id == 1) {
        if (captured_config_.has_value()) {
            return captured_config_.value();
        }
        return capture_current_settings();
    } else if (id == 2) {
        if (d_->list_dev_presets && d_->list_dev_presets->currentItem()) {
            std::string name = d_->list_dev_presets->currentItem()->text().toStdString();
            return ptd::load_dev_preset(name);
        }
    }
    return capture_current_settings();
}

void SettingsWindow::on_dev_capture() {
    captured_config_ = capture_current_settings();
    if (d_->lbl_dev_captured) {
        d_->lbl_dev_captured->setText(QStringLiteral("Captured snapshot: master=%1, trail=%2, click=%3, start=%4")
            .arg(captured_config_->master_enabled ? "ON" : "OFF")
            .arg(captured_config_->trail.enabled ? "ON" : "OFF")
            .arg(captured_config_->click.enabled ? "ON" : "OFF")
            .arg(captured_config_->start_with_windows ? "ON" : "OFF"));
    }
    if (d_->lbl_dev_status) {
        d_->lbl_dev_status->setText(QStringLiteral("Current settings captured successfully."));
    }
    on_dev_show_diff();
}

void SettingsWindow::on_dev_save_preset() {
    if (!d_->edit_dev_preset_name) return;
    std::string name = d_->edit_dev_preset_name->text().trimmed().toStdString();
    ptd::AppConfig cfg = captured_config_.value_or(capture_current_settings());
    std::string err;
    if (!ptd::save_dev_preset(name, cfg, &err)) {
        if (d_->lbl_dev_status) {
            d_->lbl_dev_status->setText(QString::fromStdString("Failed to save preset: " + err));
        }
        return;
    }
    if (d_->lbl_dev_status) {
        d_->lbl_dev_status->setText(QStringLiteral("Preset saved successfully."));
    }
    refresh_dev_presets();
}

void SettingsWindow::on_dev_apply_preset() {
    if (!d_->list_dev_presets || !d_->list_dev_presets->currentItem()) {
        if (d_->lbl_dev_status) {
            d_->lbl_dev_status->setText(QStringLiteral("No preset selected."));
        }
        return;
    }
    std::string name = d_->list_dev_presets->currentItem()->text().toStdString();
    std::string err;
    auto loaded = ptd::load_dev_preset(name, &err);
    if (!loaded) {
        if (d_->lbl_dev_status) {
            d_->lbl_dev_status->setText(QString::fromStdString("Failed to load preset: " + err));
        }
        return;
    }
    apply_config(*loaded);
    if (d_->lbl_dev_status) {
        d_->lbl_dev_status->setText(QStringLiteral("Applied preset: %1").arg(QString::fromStdString(name)));
    }
    on_dev_show_diff();
}

void SettingsWindow::on_dev_apply_defaults() {
    on_restore_all();
    if (d_->lbl_dev_status) {
        d_->lbl_dev_status->setText(QStringLiteral("Applied canonical Release Defaults to current settings."));
    }
    on_dev_show_diff();
}

void SettingsWindow::on_dev_show_diff() {
    if (!d_->txt_dev_diff) return;
    auto target = resolve_dev_selected_config();
    if (!target) {
        d_->txt_dev_diff->setPlainText(QStringLiteral("Error: target configuration unavailable."));
        return;
    }
    std::string diff = ptd::diff_configs(*target, ptd::release_defaults());
    d_->txt_dev_diff->setPlainText(QString::fromStdString(diff));
}void SettingsWindow::on_dev_promote() {
    // In the application the controller owns the canonical full snapshot.
    // The direct fallback exists only for isolated SettingsWindow tests.
    if (developer_defaults_controller_) {
        emit set_current_as_release_defaults_requested();
        return;
    }
    const ptd::AppConfig cfg = capture_current_settings();
    const ptd::PromoteResult result = ptd::promote_to_release_defaults(cfg);
    show_dev_status(QString::fromStdString(result.message));
}

void SettingsWindow::show_dev_status(const QString& text) {
    if (d_->lbl_dev_status) {
        d_->lbl_dev_status->setText(text);
    }
}

void SettingsWindow::set_developer_defaults_controller(bool enabled) {
    developer_defaults_controller_ = enabled;
}

void SettingsWindow::setup_connections() {
    // General
    connect(d_->chk_master, &QCheckBox::toggled, this, &SettingsWindow::on_master_toggled);
    connect(d_->chk_trail,  &QCheckBox::toggled, this, &SettingsWindow::on_trail_toggled);
    connect(d_->chk_click,  &QCheckBox::toggled, this, &SettingsWindow::on_click_toggled);
    connect(d_->chk_start_with_windows, &QCheckBox::toggled,
            this, &SettingsWindow::on_start_with_windows_toggled);
    connect(d_->btn_restore_all, &QPushButton::clicked, this, &SettingsWindow::on_restore_all);

    // Sound (T-80)
    connect(d_->chk_sound_enabled, &QCheckBox::toggled,
            this, &SettingsWindow::on_sound_enabled_toggled);
    connect(d_->chk_sound_muted, &QCheckBox::toggled,
            this, &SettingsWindow::on_sound_mute_toggled);
    connect(d_->sound_volume, &SliderSpin::value_changed,
            this, &SettingsWindow::on_sound_volume);
    connect(d_->sound_search, &QLineEdit::textEdited, this, [this] {
        refresh_sound_catalog();
    });
    connect(d_->sound_catalog, &QListWidget::currentItemChanged, this, [this] {
        on_sound_catalog_selection_changed();
    });
    connect(d_->btn_sound_replay, &QPushButton::clicked,
            this, &SettingsWindow::on_sound_replay_clicked);
    connect(d_->btn_sound_stop, &QPushButton::clicked,
            this, &SettingsWindow::on_sound_stop_clicked);
    // T-107: Stop All + the Favorites column and its right-click menus.
    connect(d_->btn_sound_stop_all, &QPushButton::clicked,
            this, &SettingsWindow::on_sound_stop_all_clicked);
    connect(d_->sound_catalog, &QListWidget::customContextMenuRequested,
            this, &SettingsWindow::on_sound_catalog_context_menu);
    connect(d_->sound_favorites, &QListWidget::customContextMenuRequested,
            this, &SettingsWindow::on_sound_favorites_context_menu);
    connect(d_->sound_favorites, &QListWidget::currentItemChanged, this, [this] {
        QListWidgetItem* item = d_->sound_favorites->currentItem();
        if (item == nullptr) return;
        emit sound_preview_requested(item->data(Qt::UserRole).toString().toStdString());
    });

    // T-120 / T-121: keyboard subsystem + scroll settings. Every control here
    // only feeds emit_audio_config(), which is the single publication path.
    if (d_->chk_keyboard_enabled) {
        connect(d_->chk_keyboard_enabled, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_typing_sounds) {
        connect(d_->chk_typing_sounds, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_modifier_tap_sounds) {
        connect(d_->chk_modifier_tap_sounds, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_suppress_repeat_typing) {
        connect(d_->chk_suppress_repeat_typing, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_suppress_sensitive) {
        connect(d_->chk_suppress_sensitive, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_preview_on_select) {
        connect(d_->chk_preview_on_select, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->combo_keyboard_scope) {
        connect(d_->combo_keyboard_scope, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->chk_horizontal_scroll) {
        connect(d_->chk_horizontal_scroll, &QCheckBox::toggled,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->scroll_sensitivity) {
        connect(d_->scroll_sensitivity, &SliderSpin::value_changed,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->scroll_cooldown) {
        connect(d_->scroll_cooldown, &SliderSpin::value_changed,
                this, &SettingsWindow::emit_audio_config);
    }
    if (d_->scroll_max_rate) {
        connect(d_->scroll_max_rate, &SliderSpin::value_changed,
                this, &SettingsWindow::emit_audio_config);
    }

    if (d_->combo_event_category) {
        connect(d_->combo_event_category, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this] { refresh_keyboard_event_list(); });
    }
    if (d_->edit_event_search) {
        connect(d_->edit_event_search, &QLineEdit::textEdited,
                this, [this] { refresh_keyboard_event_list(); });
    }
    if (d_->list_sound_events) {
        connect(d_->list_sound_events, &QListWidget::currentItemChanged, this, [this] {
            QListWidgetItem* item = d_->list_sound_events->currentItem();
            if (item == nullptr) return;
            const QString cid = item->data(Qt::UserRole).toString();
            if (cid.isEmpty() || cid.toStdString() == d_->selected_event_id) return;
            populate_event_editor_for(cid.toStdString());
        });
    }

    // Reads every editor widget into a fresh EventSoundConfig, writes it through
    // set_event_config, and republishes. One edit = one validated publication,
    // so the editor and the runtime can never disagree about the stored value.
    auto commit_event_editor = [this] {
        if (d_->selected_event_id.empty()) return;
        ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(d_->selected_event_id);
        if (d_->chk_event_enabled) ec.enabled = d_->chk_event_enabled->isChecked();
        if (d_->chk_event_use_pool) ec.use_pool = d_->chk_event_use_pool->isChecked();
        if (d_->combo_event_sound) {
            ec.sound_id = d_->combo_event_sound->currentData().toString().toStdString();
        }
        if (d_->combo_event_policy) {
            ec.policy = static_cast<ptd::PlaybackPolicy>(
                d_->combo_event_policy->currentData().toInt());
        }
        if (d_->combo_event_scope) {
            ec.interrupt_scope = static_cast<ptd::InterruptScope>(
                d_->combo_event_scope->currentData().toInt());
        }
        if (d_->event_gain_db) ec.gain_db = static_cast<float>(d_->event_gain_db->value());
        if (d_->event_cooldown_ms) {
            ec.cooldown_ms = static_cast<int>(d_->event_cooldown_ms->value());
        }
        if (d_->event_priority) ec.priority = static_cast<int>(d_->event_priority->value());
        if (d_->event_fade_in_ms) {
            ec.fade_in_ms = static_cast<int>(d_->event_fade_in_ms->value());
        }
        if (d_->event_fade_out_ms) {
            ec.fade_out_ms = static_cast<int>(d_->event_fade_out_ms->value());
        }
        if (d_->event_min_retrigger_ms) {
            ec.min_retrigger_ms = static_cast<int>(d_->event_min_retrigger_ms->value());
        }
        if (d_->event_polyphony) {
            ec.polyphony_allowance = static_cast<int>(d_->event_polyphony->value());
        }
        if (d_->event_pitch_var) {
            ec.pitch_variation = static_cast<float>(d_->event_pitch_var->value() / 100.0);
        }
        if (d_->chk_event_no_repeat) {
            ec.no_immediate_repeat = d_->chk_event_no_repeat->isChecked();
        }
        if (d_->chk_event_norm_override) {
            ec.normalize_override = d_->chk_event_norm_override->isChecked();
        }
        if (d_->chk_event_norm_enabled) {
            ec.normalize_enabled = d_->chk_event_norm_enabled->isChecked();
        }
        audio_cfg_.set_event_config(d_->selected_event_id, ec);
        audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
        // The eight mouse events have two views of the same slot: this editor
        // and the legacy EVENT SOUNDS combos. Validated() back-fills the event
        // config from the legacy combo, so leaving that combo stale would
        // resurrect the sound the user just cleared.
        const int legacy = ptd::legacy_mouse_event_index(d_->selected_event_id);
        if (legacy >= 0 && d_->sound_assign[legacy]) {
            QComboBox* combo = d_->sound_assign[legacy];
            const QString qid = QString::fromStdString(ec.sound_id);
            int at = qid.isEmpty() ? 0 : combo->findData(qid);
            if (at < 0) {
                combo->addItem(qid + QStringLiteral(" (missing)"), qid);
                at = combo->count() - 1;
            }
            const QSignalBlocker block(combo);
            combo->setCurrentIndex(at);
        }
        if (!ec.sound_id.empty()) record_recent_sound(ec.sound_id);
        refresh_keyboard_event_list();
        populate_event_editor_for(d_->selected_event_id);
        emit_audio_config();
    };

    if (d_->chk_event_enabled) {
        connect(d_->chk_event_enabled, &QCheckBox::toggled, this, commit_event_editor);
    }
    if (d_->chk_event_use_pool) {
        connect(d_->chk_event_use_pool, &QCheckBox::toggled, this, commit_event_editor);
    }
    if (d_->combo_event_sound) {
        connect(d_->combo_event_sound, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, commit_event_editor);
    }
    if (d_->combo_event_policy) {
        connect(d_->combo_event_policy, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, commit_event_editor);
    }
    if (d_->combo_event_scope) {
        connect(d_->combo_event_scope, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, commit_event_editor);
    }
    if (d_->event_gain_db) {
        connect(d_->event_gain_db, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_cooldown_ms) {
        connect(d_->event_cooldown_ms, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_priority) {
        connect(d_->event_priority, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_fade_in_ms) {
        connect(d_->event_fade_in_ms, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_fade_out_ms) {
        connect(d_->event_fade_out_ms, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_min_retrigger_ms) {
        connect(d_->event_min_retrigger_ms, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_polyphony) {
        connect(d_->event_polyphony, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->event_pitch_var) {
        connect(d_->event_pitch_var, &SliderSpin::value_changed, this, commit_event_editor);
    }
    if (d_->chk_event_no_repeat) {
        connect(d_->chk_event_no_repeat, &QCheckBox::toggled, this, commit_event_editor);
    }
    if (d_->chk_event_norm_override) {
        connect(d_->chk_event_norm_override, &QCheckBox::toggled, this, commit_event_editor);
    }
    if (d_->chk_event_norm_enabled) {
        connect(d_->chk_event_norm_enabled, &QCheckBox::toggled, this, commit_event_editor);
    }

    if (d_->chk_show_advanced_event) {
        connect(d_->chk_show_advanced_event, &QCheckBox::toggled, this, [this](bool on) {
            if (d_->advanced_event_panel) d_->advanced_event_panel->setVisible(on);
        });
    }
    if (d_->btn_event_preview) {
        connect(d_->btn_event_preview, &QPushButton::clicked, this, [this] {
            if (d_->selected_event_id.empty()) return;
            emit sound_event_preview_requested(
                audio_cfg_.effective_event_config(d_->selected_event_id));
        });
    }
    if (d_->btn_event_reset) {
        connect(d_->btn_event_reset, &QPushButton::clicked,
                this, &SettingsWindow::reset_selected_sound_event);
    }
    if (d_->btn_event_fav_toggle) {
        connect(d_->btn_event_fav_toggle, &QPushButton::clicked, this, [this] {
            const QString id = d_->combo_event_sound
                ? d_->combo_event_sound->currentData().toString() : QString();
            if (!id.isEmpty()) toggle_sound_favorite(id);
        });
    }
    if (d_->btn_reset_category) {
        connect(d_->btn_reset_category, &QPushButton::clicked, this, [this] {
            const int cat = d_->combo_event_category
                ? d_->combo_event_category->currentData().toInt() : -1;
            if (cat < 0) {
                show_dev_status(QStringLiteral("Select a specific category to reset."));
                return;
            }
            reset_sound_category(static_cast<ptd::SoundEventCategory>(cat));
        });
    }

    // Random Sound Pool editor: add/remove/reorder plus per-entry edits, each
    // committing through set_event_config so the pool is never read-only state.
    auto pool_entry_index = [this]() -> int {
        if (!d_->list_pool_entries) return -1;
        QListWidgetItem* item = d_->list_pool_entries->currentItem();
        return item ? item->data(Qt::UserRole).toInt() : -1;
    };
    if (d_->list_pool_entries) {
        connect(d_->list_pool_entries, &QListWidget::currentItemChanged, this, [this] {
            if (d_->selected_event_id.empty()) return;
            const int idx = [this]() -> int {
                if (!d_->list_pool_entries) return -1;
                QListWidgetItem* item = d_->list_pool_entries->currentItem();
                return item ? item->data(Qt::UserRole).toInt() : -1;
            }();
            if (idx < 0) return;
            const ptd::EventSoundConfig ec =
                audio_cfg_.effective_event_config(d_->selected_event_id);
            if (static_cast<std::size_t>(idx) >= ec.pool.size()) return;
            QSignalBlocker be(d_->chk_pool_entry_enabled);
            QSignalBlocker bw(d_->pool_entry_weight);
            QSignalBlocker bg(d_->pool_entry_gain_db);
            const auto& pe = ec.pool[static_cast<std::size_t>(idx)];
            d_->chk_pool_entry_enabled->setChecked(pe.enabled);
            d_->pool_entry_weight->set_value(pe.weight);
            d_->pool_entry_gain_db->set_value(pe.gain_db);
        });
    }
    auto commit_pool_entry = [this, pool_entry_index] {
        if (d_->selected_event_id.empty()) return;
        const int idx = pool_entry_index();
        if (idx < 0) return;
        ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(d_->selected_event_id);
        if (static_cast<std::size_t>(idx) >= ec.pool.size()) return;
        auto& pe = ec.pool[static_cast<std::size_t>(idx)];
        if (d_->chk_pool_entry_enabled) pe.enabled = d_->chk_pool_entry_enabled->isChecked();
        if (d_->pool_entry_weight) pe.weight = static_cast<float>(d_->pool_entry_weight->value());
        if (d_->pool_entry_gain_db) {
            pe.gain_db = static_cast<float>(d_->pool_entry_gain_db->value());
        }
        audio_cfg_.set_event_config(d_->selected_event_id, ec);
        audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
        refresh_keyboard_event_list();
        emit_audio_config();
    };
    if (d_->chk_pool_entry_enabled) {
        connect(d_->chk_pool_entry_enabled, &QCheckBox::toggled, this, commit_pool_entry);
    }
    if (d_->pool_entry_weight) {
        connect(d_->pool_entry_weight, &SliderSpin::value_changed, this, commit_pool_entry);
    }
    if (d_->pool_entry_gain_db) {
        connect(d_->pool_entry_gain_db, &SliderSpin::value_changed, this, commit_pool_entry);
    }
    if (d_->btn_pool_add) {
        connect(d_->btn_pool_add, &QPushButton::clicked, this, [this] {
            if (d_->selected_event_id.empty()) return;
            const QString sid = d_->combo_pool_add_sound
                ? d_->combo_pool_add_sound->currentData().toString() : QString();
            if (sid.isEmpty()) return;
            ptd::EventSoundConfig ec =
                audio_cfg_.effective_event_config(d_->selected_event_id);
            if (static_cast<int>(ec.pool.size()) >= ptd::EventSoundConfig::kMaxPoolEntries) return;
            for (const auto& pe : ec.pool) {
                if (pe.sound_id == sid.toStdString()) return;
            }
            ptd::SoundPoolEntry pe;
            pe.enabled = true;
            pe.sound_id = sid.toStdString();
            pe.weight = 1.0f;
            pe.gain_db = 0.0f;
            ec.pool.push_back(pe);
            // Enabling the pool on first entry is what makes the entries audible.
            ec.use_pool = true;
            audio_cfg_.set_event_config(d_->selected_event_id, ec);
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            refresh_keyboard_event_list();
            populate_event_editor_for(d_->selected_event_id);
            emit_audio_config();
        });
    }
    if (d_->btn_pool_remove) {
        connect(d_->btn_pool_remove, &QPushButton::clicked, this, [this] {
            if (d_->selected_event_id.empty()) return;
            if (!d_->list_pool_entries || !d_->list_pool_entries->currentItem()) return;
            const int idx = d_->list_pool_entries->currentItem()->data(Qt::UserRole).toInt();
            ptd::EventSoundConfig ec =
                audio_cfg_.effective_event_config(d_->selected_event_id);
            if (idx < 0 || static_cast<std::size_t>(idx) >= ec.pool.size()) return;
            ec.pool.erase(ec.pool.begin() + idx);
            audio_cfg_.set_event_config(d_->selected_event_id, ec);
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            refresh_keyboard_event_list();
            populate_event_editor_for(d_->selected_event_id);
            emit_audio_config();
        });
    }
    auto move_pool_entry = [this](int delta) {
        if (d_->selected_event_id.empty()) return;
        if (!d_->list_pool_entries || !d_->list_pool_entries->currentItem()) return;
        const int idx = d_->list_pool_entries->currentItem()->data(Qt::UserRole).toInt();
        ptd::EventSoundConfig ec = audio_cfg_.effective_event_config(d_->selected_event_id);
        const int target = idx + delta;
        if (idx < 0 || target < 0 || static_cast<std::size_t>(target) >= ec.pool.size()) return;
        std::swap(ec.pool[static_cast<std::size_t>(idx)], ec.pool[static_cast<std::size_t>(target)]);
        audio_cfg_.set_event_config(d_->selected_event_id, ec);
        audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
        refresh_keyboard_event_list();
        populate_event_editor_for(d_->selected_event_id);
        emit_audio_config();
    };
    if (d_->btn_pool_up) {
        connect(d_->btn_pool_up, &QPushButton::clicked, this, [move_pool_entry] { move_pool_entry(-1); });
    }
    if (d_->btn_pool_down) {
        connect(d_->btn_pool_down, &QPushButton::clicked, this, [move_pool_entry] { move_pool_entry(1); });
    }
    if (d_->btn_pool_preview_entry) {
        connect(d_->btn_pool_preview_entry, &QPushButton::clicked, this, [this] {
            if (d_->selected_event_id.empty()) return;
            if (!d_->list_pool_entries || !d_->list_pool_entries->currentItem()) return;
            const int idx = d_->list_pool_entries->currentItem()->data(Qt::UserRole).toInt();
            const ptd::EventSoundConfig ec =
                audio_cfg_.effective_event_config(d_->selected_event_id);
            if (idx < 0 || static_cast<std::size_t>(idx) >= ec.pool.size()) return;
            emit sound_preview_requested(ec.pool[static_cast<std::size_t>(idx)].sound_id);
        });
    }
    if (d_->btn_pool_preview_random) {
        connect(d_->btn_pool_preview_random, &QPushButton::clicked, this, [this] {
            if (d_->selected_event_id.empty()) return;
            emit sound_event_preview_requested(
                audio_cfg_.effective_event_config(d_->selected_event_id));
        });
    }

    // Custom shortcuts: validate before storing, drop by id on remove.
    if (d_->btn_custom_sc_add) {
        connect(d_->btn_custom_sc_add, &QPushButton::clicked, this, [this] {
            const std::string name = d_->edit_custom_sc_name
                ? d_->edit_custom_sc_name->text().trimmed().toStdString() : std::string();
            const std::string chord = d_->edit_custom_sc_chord
                ? d_->edit_custom_sc_chord->text().trimmed().toStdString() : std::string();
            if (name.empty() || chord.empty()) {
                show_dev_status(QStringLiteral("Custom shortcut needs both a name and a chord."));
                return;
            }
            if (static_cast<int>(audio_cfg_.custom_shortcuts.size()) >=
                ptd::AudioConfig::kMaxCustomShortcuts) {
                show_dev_status(QStringLiteral("Custom shortcut limit reached."));
                return;
            }
            ptd::CustomShortcutBinding cs;
            // First unused slot id: AudioConfig::validated rejects an id that
            // does not look like a custom/key event, so it must be well-formed.
            int slot = 1;
            while (slot <= ptd::AudioConfig::kMaxCustomShortcuts) {
                const std::string candidate =
                    "custom.shortcut_" + std::to_string(slot);
                const bool taken = std::any_of(
                    audio_cfg_.custom_shortcuts.begin(), audio_cfg_.custom_shortcuts.end(),
                    [&](const ptd::CustomShortcutBinding& existing) {
                        return existing.id == candidate;
                    });
                if (!taken) break;
                ++slot;
            }
            cs.id = "custom.shortcut_" + std::to_string(slot);
            cs.display_name = name;
            cs.chord = chord;
            cs.is_app_shortcut = d_->chk_custom_sc_app
                ? d_->chk_custom_sc_app->isChecked() : false;
            const ptd::CustomShortcutBinding clean = ptd::CustomShortcutBinding::validated(cs);
            if (clean.id.empty() || clean.display_name.empty() || clean.chord.empty()) {
                show_dev_status(QStringLiteral("Custom shortcut is not valid; check the chord text."));
                return;
            }
            audio_cfg_.custom_shortcuts.push_back(clean);
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            if (d_->edit_custom_sc_name) d_->edit_custom_sc_name->clear();
            if (d_->edit_custom_sc_chord) d_->edit_custom_sc_chord->clear();
            set_audio_widgets(audio_cfg_);
            emit_audio_config();
        });
    }
    if (d_->btn_custom_sc_remove) {
        connect(d_->btn_custom_sc_remove, &QPushButton::clicked, this, [this] {
            if (!d_->list_custom_shortcuts || !d_->list_custom_shortcuts->currentItem()) return;
            const std::string id =
                d_->list_custom_shortcuts->currentItem()->data(Qt::UserRole).toString().toStdString();
            if (id.empty()) return;
            audio_cfg_.custom_shortcuts.erase(
                std::remove_if(audio_cfg_.custom_shortcuts.begin(),
                               audio_cfg_.custom_shortcuts.end(),
                               [&](const ptd::CustomShortcutBinding& cs) { return cs.id == id; }),
                audio_cfg_.custom_shortcuts.end());
            audio_cfg_.event_configs.erase(id);
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            set_audio_widgets(audio_cfg_);
            emit_audio_config();
        });
    }

    // Per-process exclusions in System-Wide scope.
    if (d_->btn_add_excluded_proc) {
        connect(d_->btn_add_excluded_proc, &QPushButton::clicked, this, [this] {
            const std::string proc = d_->edit_excluded_proc
                ? ptd::sanitize_process_name(d_->edit_excluded_proc->text().trimmed().toStdString())
                : std::string();
            if (proc.empty()) {
                show_dev_status(QStringLiteral("Enter an executable name such as keepass.exe."));
                return;
            }
            if (static_cast<int>(audio_cfg_.excluded_processes.size()) >=
                ptd::AudioConfig::kMaxProcessExclusions) {
                show_dev_status(QStringLiteral("Process exclusion limit reached."));
                return;
            }
            if (std::find(audio_cfg_.excluded_processes.begin(),
                          audio_cfg_.excluded_processes.end(), proc) !=
                audio_cfg_.excluded_processes.end()) {
                if (d_->edit_excluded_proc) d_->edit_excluded_proc->clear();
                return;
            }
            audio_cfg_.excluded_processes.push_back(proc);
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            if (d_->edit_excluded_proc) d_->edit_excluded_proc->clear();
            set_audio_widgets(audio_cfg_);
            emit_audio_config();
        });
    }
    if (d_->btn_remove_excluded_proc) {
        connect(d_->btn_remove_excluded_proc, &QPushButton::clicked, this, [this] {
            if (!d_->list_excluded_procs || !d_->list_excluded_procs->currentItem()) return;
            const std::string proc =
                d_->list_excluded_procs->currentItem()->text().toStdString();
            audio_cfg_.excluded_processes.erase(
                std::remove_if(audio_cfg_.excluded_processes.begin(),
                               audio_cfg_.excluded_processes.end(),
                               [&](const std::string& existing) { return existing == proc; }),
                audio_cfg_.excluded_processes.end());
            audio_cfg_ = ptd::AudioConfig::validated(audio_cfg_);
            set_audio_widgets(audio_cfg_);
            emit_audio_config();
        });
    }

    // Profiles: apply a template, or move the whole assignment block through JSON.
    if (d_->btn_apply_sound_profile) {
        connect(d_->btn_apply_sound_profile, &QPushButton::clicked, this, [this] {
            const int preset = d_->combo_sound_profile
                ? d_->combo_sound_profile->currentData().toInt() : 0;
            apply_sound_profile_preset(static_cast<ptd::SoundProfilePreset>(preset));
        });
    }
    if (d_->btn_export_audio_profile) {
        connect(d_->btn_export_audio_profile, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("Export Sound Assignments"), QStringLiteral("protrail-audio.json"),
                QStringLiteral("Audio profile (*.json)"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                show_dev_status(QStringLiteral("Export failed: cannot write the selected file."));
                return;
            }
            const QByteArray data = export_audio_profile_json();
            if (f.write(data) != static_cast<qint64>(data.size())) {
                show_dev_status(QStringLiteral("Export failed: short write."));
                return;
            }
            show_dev_status(QStringLiteral("Sound assignments exported."));
        });
    }
    if (d_->btn_import_audio_profile) {
        connect(d_->btn_import_audio_profile, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Import Sound Assignments"), QString(),
                QStringLiteral("Audio profile (*.json)"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                show_dev_status(QStringLiteral("Import failed: cannot read the selected file."));
                return;
            }
            const QByteArray data = f.readAll();
            QString err;
            if (!import_audio_profile_json(data, &err)) {
                show_dev_status(QStringLiteral("Import failed: %1").arg(err));
                return;
            }
            show_dev_status(QStringLiteral("Sound assignments imported."));
        });
    }

    // Trail
    connect(d_->trail_head_thickness, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_tail_thickness, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_taper, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_lifetime, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_opacity, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_smoothing, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_fade_start, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_glow, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_spacing, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    // T-021 sparkle parameter rows publish like every other Trail row.
    connect(d_->trail_sparkle_amount, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_sparkle_size, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    connect(d_->trail_sparkle_spread, &SliderSpin::value_changed,
            this, &SettingsWindow::on_trail_slider);
    // T-020 selectors: idClicked fires on user activation only.
    connect(d_->trail_color_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_trail_mode_clicked);
    connect(d_->trail_style_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_trail_style_clicked);
    connect(d_->trail_fade_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_trail_fade_clicked);
    connect(d_->trail_sparkle_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_trail_sparkle_clicked);
    connect(d_->trail_start_color, &ColorEditor::color_changed,
            this, &SettingsWindow::on_trail_color);
    connect(d_->trail_fade_color, &ColorEditor::color_changed,
            this, &SettingsWindow::on_trail_color);
    connect(d_->btn_restore_trail, &QPushButton::clicked,
            this, &SettingsWindow::on_restore_trail);

    // Click
    connect(d_->chk_trig_left,   &QCheckBox::toggled,
            this, &SettingsWindow::emit_click_config);
    connect(d_->chk_trig_right,  &QCheckBox::toggled,
            this, &SettingsWindow::emit_click_config);
    connect(d_->chk_trig_middle, &QCheckBox::toggled,
            this, &SettingsWindow::emit_click_config);
    // T-024: toggling Hold FX publishes exactly ONE coherent ClickConfig
    // through the same path as every other Click control, so the controller
    // cancels live candidates/holds and persists schema 8 in one step.
    // T-027: the wake toggle and the four Hold sliders ride the same path.
    connect(d_->chk_hold_fx, &QCheckBox::toggled,
            this, &SettingsWindow::on_hold_toggled);
    connect(d_->chk_hold_wake, &QCheckBox::toggled,
            this, &SettingsWindow::on_hold_toggled);
    connect(d_->hold_intensity, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->hold_wake_density, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->hold_wake_lifetime, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->hold_release_strength, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    // T-36 Advanced Motion Wake controls ride the same coherent ClickConfig
    // publication path (one signal, one persistence operation).
    connect(d_->wake_strength, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->wake_size, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->wake_spread, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->speed_response, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->min_motion_speed, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->chk_turn_accent, &QCheckBox::toggled,
            this, &SettingsWindow::on_motion_accent_toggled);
    connect(d_->chk_stop_accent, &QCheckBox::toggled,
            this, &SettingsWindow::on_motion_accent_toggled);
    connect(d_->click_start_size, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_end_size, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_duration, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_opacity, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_outline, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_fill, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_easing_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_click_easing_clicked);
    connect(d_->click_style_group, &QButtonGroup::idClicked,
            this, &SettingsWindow::on_click_style_clicked);
    connect(d_->click_element_tint, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_particles, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_color, &ColorEditor::color_changed,
            this, &SettingsWindow::on_click_color);
    connect(d_->btn_restore_click, &QPushButton::clicked,
            this, &SettingsWindow::on_restore_click);

    // T-34 Developer tab
    if (d_->dev_tab != nullptr) {
        connect(d_->btn_dev_capture, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_capture);
        connect(d_->btn_dev_save_preset, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_save_preset);
        connect(d_->btn_dev_apply_preset, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_apply_preset);
        connect(d_->btn_dev_apply_defaults, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_apply_defaults);
        connect(d_->btn_dev_show_diff, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_show_diff);
        connect(d_->btn_dev_promote, &QPushButton::clicked,
                this, &SettingsWindow::on_dev_promote);
        refresh_dev_presets();
    }
}

void SettingsWindow::on_master_toggled(bool on) {
    master_enabled_ = on;
    d_->chk_trail->setEnabled(on);
    d_->chk_click->setEnabled(on);
    emit master_enabled_changed(on);
}

void SettingsWindow::on_trail_toggled(bool on) {
    trail_cfg_.enabled = on;
    emit trail_enabled_changed(on);
}

void SettingsWindow::on_click_toggled(bool on) {
    click_cfg_.enabled = on;
    emit click_enabled_changed(on);
}

void SettingsWindow::on_hold_toggled(bool) {
    // Re-evaluate the contextual surface (silently), then publish ONE
    // coherent ClickConfig -- exactly the same rule as every other selector
    // in this window: one user action, one publication.
    update_click_visibility();
    emit_click_config();
}

// T-36: the Turn/Stop Accent toggles only change fields inside ClickConfig;
// they publish one coherent ClickConfig like every other Click control.
void SettingsWindow::on_motion_accent_toggled(bool) {
    emit_click_config();
}

void SettingsWindow::on_trail_slider(double) {
    // Taper drives Tail-width relevance live (Phase 5): re-evaluate the
    // contextual surface, then publish once. Visibility itself never emits.
    update_trail_visibility();
    emit_trail_config();
}

void SettingsWindow::on_click_slider(double) {
    // T-010R Repair 3: the GUI owns the end_radius_px >= start_radius_px
    // invariant explicitly. ClickConfig::validated() repairs the runtime
    // value, but the WIDGETS must agree with the emitted config, so the
    // reconcile happens on the widgets (silently) BEFORE publication:
    //   - Start > End  -> raise End to Start immediately (silently).
    //   - End < Start   -> clamp End back up to Start (user cannot create
    //     End < Start through the End control).
    // SliderSpin::set_value is signal-silent, so this is exactly one
    // coherent publication per user action.
    auto* src = qobject_cast<SliderSpin*>(sender());
    if (src == d_->click_start_size) {
        if (d_->click_start_size->value() > d_->click_end_size->value()) {
            d_->click_end_size->set_value(d_->click_start_size->value());
        }
    } else if (src == d_->click_end_size) {
        if (d_->click_end_size->value() < d_->click_start_size->value()) {
            d_->click_end_size->set_value(d_->click_start_size->value());
        }
    }
    emit_click_config();
}

void SettingsWindow::on_trail_color(int, int, int) {
    // Trail ColorEditor changed: publish TrailConfig only.
    emit_trail_config();
}

void SettingsWindow::on_click_color(int, int, int) {
    // Click ColorEditor changed: publish ClickConfig only.
    emit_click_config();
}

// ---- T-020 selector activation: one click = one coherent publication ----

void SettingsWindow::on_trail_mode_clicked(int) {
    update_color_section();
    emit_trail_config();
}

void SettingsWindow::on_trail_style_clicked(int) {
    update_trail_visibility();
    emit_trail_config();
}

void SettingsWindow::on_trail_fade_clicked(int) {
    emit_trail_config();
}

void SettingsWindow::on_trail_sparkle_clicked(int) {
    // T-021: the mode drives the contextual parameter rows; the region's
    // reserved height keeps every other section anchored.
    update_trail_visibility();
    emit_trail_config();
}

void SettingsWindow::on_click_style_clicked(int) {
    update_click_visibility();
    emit_click_config();
}

void SettingsWindow::on_click_easing_clicked(int) {
    emit_click_config();
}

// ---- T-020 contextual surfaces (silent; never publish, never reset) ----

void SettingsWindow::update_trail_visibility() {
    const auto style = static_cast<ptd::TrailStyle>(
        d_->trail_style_group->checkedId());
    const bool glow_style = style == ptd::TrailStyle::SoftGlow
                         || style == ptd::TrailStyle::Neon;
    const bool dot_style = style == ptd::TrailStyle::Dotted
                        || style == ptd::TrailStyle::Spark;
    // T-020R1: this region is the ONLY place a Trail style may add or remove
    // a control. It keeps its reserved height for every style, so the COLOR /
    // SHAPE / TRAIL section anchors never move when the style changes.
    // Ribbon is listed as having no style-specific control: taper and tail
    // width are permanent SHAPE rows, and a style that ignores them simply
    // leaves their live values in place (nothing is re-enabled or disabled).
    d_->trail_glow->setVisible(glow_style);
    d_->trail_spacing->setVisible(dot_style);
    d_->trail_style_options->setVisible(true);
    // T-021: the sparkle parameter rows show only when a mode is active.
    // They appear/disappear inside the reserved SPARKLES region, so no
    // section anchor outside it ever moves.
    const auto sparkle_mode = static_cast<ptd::TrailSparkleMode>(
        d_->trail_sparkle_group->checkedId());
    const bool sparkle_on = sparkle_mode != ptd::TrailSparkleMode::Off;
    d_->trail_sparkle_amount->setVisible(sparkle_on);
    d_->trail_sparkle_size->setVisible(sparkle_on);
    d_->trail_sparkle_spread->setVisible(sparkle_on);
    d_->trail_sparkle_options->setVisible(true);
}

void SettingsWindow::update_color_section() {
    const auto mode = static_cast<ptd::TrailColorMode>(
        d_->trail_color_group->checkedId());
    bool start_first = true;
    bool start_visible = true;
    bool fade_visible = false;
    QString start_label = QStringLiteral("Color");
    QString fade_label = QStringLiteral("Fade / Tail");
    switch (mode) {
    case ptd::TrailColorMode::StartAccent:  // Head
        start_first = false;
        start_visible = true;
        fade_visible = true;
        start_label = QStringLiteral("Head accent");
        fade_label = QStringLiteral("Base / Tail");
        break;
    case ptd::TrailColorMode::FadeAccent:   // Tail
        start_first = true;
        start_visible = true;
        fade_visible = true;
        start_label = QStringLiteral("Base / Head");
        fade_label = QStringLiteral("Tail accent");
        break;
    case ptd::TrailColorMode::Gradient:
        start_first = false;
        start_visible = true;
        fade_visible = true;
        start_label = QStringLiteral("Head");
        fade_label = QStringLiteral("Tail");
        break;
    case ptd::TrailColorMode::Full:         // Solid
    default:
        start_first = true;
        start_visible = true;
        fade_visible = false;
        start_label = QStringLiteral("Color");
        fade_label = QStringLiteral("Fade / Tail");
        break;
    }
    d_->trail_start_color->set_label(start_label);
    d_->trail_fade_color->set_label(fade_label);
    // Reorder so the base color reads first; hidden editors keep their
    // stored RGB values (Phase 2: switching modes never destroys either
    // stored color).
    d_->trail_editors_lay->removeWidget(d_->trail_start_color);
    d_->trail_editors_lay->removeWidget(d_->trail_fade_color);
    ColorEditor* first = start_first ? d_->trail_start_color
                                     : d_->trail_fade_color;
    ColorEditor* second = start_first ? d_->trail_fade_color
                                      : d_->trail_start_color;
    d_->trail_editors_lay->insertWidget(0, first);
    d_->trail_editors_lay->insertWidget(1, second);
    d_->trail_start_color->setVisible(start_visible);
    d_->trail_fade_color->setVisible(fade_visible);
    // The editors just changed places, and Qt's focus chain is built in
    // widget-creation order -- so Tab would keep walking to the editor that
    // VISUALLY moved. Declare the section's tab order explicitly instead.
    sync_tab_order(d_->trail_color_group, first, second);
}

void SettingsWindow::update_click_visibility() {
    const auto style = static_cast<ptd::ClickStyle>(
        d_->click_style_group->checkedId());
    // T-022: every elemental style spawns particles, so Particles applies
    // to them too, and Element tint applies to them ONLY.
    const bool elemental = ptd::is_elemental_click_style(style);
    const bool burst = style == ptd::ClickStyle::Burst
                    || style == ptd::ClickStyle::SparkBurst;
    const bool filled = style == ptd::ClickStyle::Ring
                     || style == ptd::ClickStyle::DotRing;
    // Fire draws no outlined ring at all (only a filled base flash), so its
    // outline thickness would be a dead control.
    const bool outline = !(style == ptd::ClickStyle::SoftFlash
                        || style == ptd::ClickStyle::SparkBurst
                        || style == ptd::ClickStyle::Fire);
    d_->click_particles->setVisible(burst || elemental);
    d_->click_element_tint->setVisible(elemental);
    d_->click_fill->setVisible(filled);
    d_->click_outline->setVisible(outline);

    // T-027 Hold Controls gating. Visibility NEVER changes here: the Hold
    // block is always laid out, only its interactive state follows the two
    // toggles. Hold FX off disables the whole block; with Hold FX on but
    // Motion Wake off only the wake-specific rows (Density, Life) are
    // disabled, because Intensity and Release Strength still apply to the
    // attached aura and the release payoff.
    const bool hold_on = d_->chk_hold_fx->isChecked();
    const bool wake_on = hold_on && d_->chk_hold_wake->isChecked();
    d_->chk_hold_wake->setEnabled(hold_on);
    d_->hold_intensity->setEnabled(hold_on);
    d_->hold_wake_density->setEnabled(wake_on);
    d_->hold_wake_lifetime->setEnabled(wake_on);
    d_->hold_release_strength->setEnabled(hold_on);
    // T-36 Advanced Motion Wake gating: these controls shape the DETACHED
    // wake, so they follow Motion Wake (which itself requires Hold FX). Turn
    // and Stop accents fire only through motion episodes, so they gate the
    // same way. Visibility never changes (constant layout).
    d_->wake_strength->setEnabled(wake_on);
    d_->wake_size->setEnabled(wake_on);
    d_->wake_spread->setEnabled(wake_on);
    d_->speed_response->setEnabled(wake_on);
    d_->min_motion_speed->setEnabled(wake_on);
    d_->chk_turn_accent->setEnabled(wake_on);
    d_->chk_stop_accent->setEnabled(wake_on);
}

void SettingsWindow::emit_trail_config() {
    // SliderSpin::value() is double; config fields are float. Widget ranges
    // are derived from the config constants above, so every value is inside
    // the validated range before this intentional narrowing (T-010R Repair
    // 1: explicit conversion keeps /W4 /WX clean, no warning suppression).
    trail_cfg_.enabled = d_->chk_trail->isChecked();
    trail_cfg_.head_thickness_px =
        static_cast<float>(d_->trail_head_thickness->value());
    trail_cfg_.tail_thickness_px =
        static_cast<float>(d_->trail_tail_thickness->value());
    trail_cfg_.taper_strength =
        static_cast<float>(d_->trail_taper->value() / 100.0);
    trail_cfg_.lifetime_ms =
        static_cast<float>(d_->trail_lifetime->value());
    trail_cfg_.base_opacity =
        static_cast<float>(d_->trail_opacity->value() / 100.0);
    trail_cfg_.smoothing =
        static_cast<float>(d_->trail_smoothing->value() / 100.0);
    trail_cfg_.fade_start =
        static_cast<float>(d_->trail_fade_start->value() / 100.0);
    trail_cfg_.glow_strength =
        static_cast<float>(d_->trail_glow->value() / 100.0);
    trail_cfg_.segment_spacing_px =
        static_cast<float>(d_->trail_spacing->value());
    // T-021 sparkle decoration fields (Amount is a 0..100 UI percent).
    trail_cfg_.sparkle_mode = static_cast<ptd::TrailSparkleMode>(
        d_->trail_sparkle_group->checkedId());
    trail_cfg_.sparkle_amount =
        static_cast<float>(d_->trail_sparkle_amount->value() / 100.0);
    trail_cfg_.sparkle_size_px =
        static_cast<float>(d_->trail_sparkle_size->value());
    trail_cfg_.sparkle_spread_px =
        static_cast<float>(d_->trail_sparkle_spread->value());
    // Hidden controls keep their stored widget values, so a contextual
    // publication always carries the full coherent config (Phase 16).
    trail_cfg_.style = static_cast<ptd::TrailStyle>(
        d_->trail_style_group->checkedId());
    trail_cfg_.fade_curve = static_cast<ptd::FadeCurve>(
        d_->trail_fade_group->checkedId());
    trail_cfg_.color_mode = static_cast<ptd::TrailColorMode>(
        d_->trail_color_group->checkedId());

    int r, g, b;
    d_->trail_start_color->get(r, g, b);
    trail_cfg_.start_color_r = static_cast<uint8_t>(r);
    trail_cfg_.start_color_g = static_cast<uint8_t>(g);
    trail_cfg_.start_color_b = static_cast<uint8_t>(b);
    d_->trail_fade_color->get(r, g, b);
    trail_cfg_.fade_color_r = static_cast<uint8_t>(r);
    trail_cfg_.fade_color_g = static_cast<uint8_t>(g);
    trail_cfg_.fade_color_b = static_cast<uint8_t>(b);

    trail_cfg_ = ptd::TrailConfig::validated(trail_cfg_);
    emit trail_config_changed(trail_cfg_);
}

void SettingsWindow::emit_click_config() {
    click_cfg_.enabled = d_->chk_click->isChecked();
    click_cfg_.trigger_left   = d_->chk_trig_left->isChecked();
    click_cfg_.trigger_right  = d_->chk_trig_right->isChecked();
    click_cfg_.trigger_middle = d_->chk_trig_middle->isChecked();
    int r, g, b;
    d_->click_color->get(r, g, b);
    click_cfg_.color_r = static_cast<uint8_t>(r);
    click_cfg_.color_g = static_cast<uint8_t>(g);
    click_cfg_.color_b = static_cast<uint8_t>(b);
    click_cfg_.start_radius_px =
        static_cast<float>(d_->click_start_size->value());
    click_cfg_.end_radius_px =
        static_cast<float>(d_->click_end_size->value());
    click_cfg_.duration_ms =
        static_cast<float>(d_->click_duration->value());
    click_cfg_.base_opacity =
        static_cast<float>(d_->click_opacity->value() / 100.0);
    click_cfg_.outline_thickness_px =
        static_cast<float>(d_->click_outline->value());
    click_cfg_.fill_opacity =
        static_cast<float>(d_->click_fill->value() / 100.0);
    click_cfg_.easing = static_cast<ptd::ClickEasing>(
        d_->click_easing_group->checkedId());
    click_cfg_.style = static_cast<ptd::ClickStyle>(
        d_->click_style_group->checkedId());
    click_cfg_.particle_amount = static_cast<uint8_t>(
        static_cast<int>(d_->click_particles->value()));
    click_cfg_.element_tint = static_cast<float>(d_->click_element_tint->value());
    click_cfg_.hold_enabled = d_->chk_hold_fx->isChecked();  // T-024
    // T-027 Hold Controls / Motion Wake. Wake Life is a seconds widget and a
    // milliseconds field; the conversion is explicit and the value is inside
    // the published range by construction, so validated() has nothing to
    // repair.
    click_cfg_.hold_wake_enabled = d_->chk_hold_wake->isChecked();
    click_cfg_.hold_intensity =
        static_cast<float>(d_->hold_intensity->value() / 100.0);
    click_cfg_.hold_wake_density =
        static_cast<float>(d_->hold_wake_density->value() / 100.0);
    click_cfg_.hold_wake_lifetime_ms =
        static_cast<float>(d_->hold_wake_lifetime->value() * 1000.0);
    click_cfg_.hold_release_strength =
        static_cast<float>(d_->hold_release_strength->value() / 100.0);
    // T-36 Advanced Motion Wake.
    click_cfg_.wake_strength =
        static_cast<float>(d_->wake_strength->value() / 100.0);
    click_cfg_.wake_size = static_cast<float>(d_->wake_size->value() / 100.0);
    click_cfg_.wake_spread = static_cast<float>(d_->wake_spread->value() / 100.0);
    click_cfg_.speed_response =
        static_cast<float>(d_->speed_response->value() / 100.0);
    click_cfg_.min_motion_speed_px_s =
        static_cast<float>(d_->min_motion_speed->value());
    click_cfg_.turn_accent = d_->chk_turn_accent->isChecked();
    click_cfg_.stop_accent = d_->chk_stop_accent->isChecked();

    click_cfg_ = ptd::ClickConfig::validated(click_cfg_);
    emit click_config_changed(click_cfg_);
}

// ---- T-020 programmatic selection helpers (silent by contract) ----

namespace {
void selector_set(QButtonGroup* group, int id) {
    // Programmatic setChecked emits no idClicked, so config loading and
    // restore paths never publish through this path.
    if (group != nullptr) {
        if (auto* btn = group->button(id)) {
            btn->setChecked(true);
        }
    }
}
} // namespace

// Silent programmatic assignment: copies a validated config into the
// Trail widgets with all intermediate signals blocked (Defects B/E).
// Enable checkboxes (General tab) are intentionally untouched -- restore
// must not reset master/effect state.
void SettingsWindow::set_trail_widgets(const ptd::TrailConfig& c) {
    selector_set(d_->trail_color_group, static_cast<int>(c.color_mode));
    d_->trail_start_color->set(c.start_color_r, c.start_color_g, c.start_color_b);
    d_->trail_fade_color->set(c.fade_color_r, c.fade_color_g, c.fade_color_b);
    d_->trail_glow->set_value(c.glow_strength * 100.0);
    d_->trail_spacing->set_value(c.segment_spacing_px);
    selector_set(d_->trail_style_group, static_cast<int>(c.style));
    d_->trail_head_thickness->set_value(c.head_thickness_px);
    d_->trail_tail_thickness->set_value(c.tail_thickness_px);
    d_->trail_taper->set_value(c.taper_strength * 100.0);
    d_->trail_lifetime->set_value(c.lifetime_ms);
    d_->trail_opacity->set_value(c.base_opacity * 100.0);
    d_->trail_smoothing->set_value(c.smoothing * 100.0);
    d_->trail_fade_start->set_value(c.fade_start * 100.0);
    // T-021: silent sparkle widget assignment (Off stays fully silent).
    selector_set(d_->trail_sparkle_group, static_cast<int>(c.sparkle_mode));
    d_->trail_sparkle_amount->set_value(c.sparkle_amount * 100.0);
    d_->trail_sparkle_size->set_value(c.sparkle_size_px);
    d_->trail_sparkle_spread->set_value(c.sparkle_spread_px);
    selector_set(d_->trail_fade_group, static_cast<int>(c.fade_curve));
    update_trail_visibility();
    update_color_section();
}

// Silent programmatic assignment for the Click widgets (Defects B/E).
void SettingsWindow::set_click_widgets(const ptd::ClickConfig& c) {
    QSignalBlocker tl(d_->chk_trig_left);
    QSignalBlocker tr(d_->chk_trig_right);
    QSignalBlocker tm(d_->chk_trig_middle);
    QSignalBlocker hf(d_->chk_hold_fx);
    QSignalBlocker hw(d_->chk_hold_wake);
    QSignalBlocker tac(d_->chk_turn_accent);
    QSignalBlocker sac(d_->chk_stop_accent);
    d_->chk_trig_left->setChecked(c.trigger_left);
    d_->chk_trig_right->setChecked(c.trigger_right);
    d_->chk_trig_middle->setChecked(c.trigger_middle);
    d_->chk_hold_fx->setChecked(c.hold_enabled);  // T-024, silent like the rest
    d_->chk_hold_wake->setChecked(c.hold_wake_enabled);  // T-026/T-027
    d_->hold_intensity->set_value(c.hold_intensity * 100.0);
    d_->hold_wake_density->set_value(c.hold_wake_density * 100.0);
    d_->hold_wake_lifetime->set_value(c.hold_wake_lifetime_ms / 1000.0);
    d_->hold_release_strength->set_value(c.hold_release_strength * 100.0);
    // T-36 Advanced Motion Wake (silent population, like every field here).
    d_->wake_strength->set_value(c.wake_strength * 100.0);
    d_->wake_size->set_value(c.wake_size * 100.0);
    d_->wake_spread->set_value(c.wake_spread * 100.0);
    d_->speed_response->set_value(c.speed_response * 100.0);
    d_->min_motion_speed->set_value(c.min_motion_speed_px_s);
    d_->chk_turn_accent->setChecked(c.turn_accent);
    d_->chk_stop_accent->setChecked(c.stop_accent);
    d_->click_color->set(c.color_r, c.color_g, c.color_b);
    d_->click_start_size->set_value(c.start_radius_px);
    d_->click_end_size->set_value(c.end_radius_px);
    d_->click_duration->set_value(c.duration_ms);
    d_->click_opacity->set_value(c.base_opacity * 100.0);
    d_->click_outline->set_value(c.outline_thickness_px);
    d_->click_fill->set_value(c.fill_opacity * 100.0);
    selector_set(d_->click_easing_group, static_cast<int>(c.easing));
    d_->click_particles->set_value(static_cast<double>(c.particle_amount));
    d_->click_element_tint->set_value(static_cast<double>(c.element_tint));
    selector_set(d_->click_style_group, static_cast<int>(c.style));
    update_click_visibility();
}

// Single explicit initialization path from validated configs to widgets
// (Defect B). All signals blocked: construction publishes nothing.
// CORE-004: the authoritative baseline is already carried in the fields set by
// the constructor/apply_config; nothing additional is preserved here because
// SettingsWindow owns only UI-editable fields and keeps a baseline AppConfig.
void SettingsWindow::populate_from_config() {
    set_trail_widgets(trail_cfg_);
    set_click_widgets(click_cfg_);
    set_audio_widgets(audio_cfg_);
    QSignalBlocker bm(d_->chk_master);
    QSignalBlocker bt(d_->chk_trail);
    QSignalBlocker bc(d_->chk_click);
    QSignalBlocker bs(d_->chk_start_with_windows);
    d_->chk_master->setChecked(master_enabled_);
    d_->chk_trail->setChecked(trail_cfg_.enabled);
    d_->chk_click->setChecked(click_cfg_.enabled);
    d_->chk_start_with_windows->setChecked(start_with_windows_);
}

// T-032: the user's own choice. Publishes exactly one signal; Application
// owns the registry side effect and the persistence that follows it.
void SettingsWindow::on_start_with_windows_toggled(bool on) {
    if (start_with_windows_ == on) return;
    start_with_windows_ = on;
    emit start_with_windows_changed(on);
}

// T-032: silent programmatic population. Signals blocked, so a config load
// never publishes back.
void SettingsWindow::set_start_with_windows(bool on) {
    if (start_with_windows_ == on
        && d_->chk_start_with_windows->isChecked() == on) {
        return;
    }
    start_with_windows_ = on;
    QSignalBlocker blocker(d_->chk_start_with_windows);
    d_->chk_start_with_windows->setChecked(on);
}

bool SettingsWindow::start_with_windows() const {
    return start_with_windows_;
}

// Enable/disable the sub-checkboxes per master state, WITHOUT emitting
// anything (the emitting variant is on_master_toggled).
void SettingsWindow::apply_enable_states() {
    const bool on = d_->chk_master->isChecked();
    d_->chk_trail->setEnabled(on);
    d_->chk_click->setEnabled(on);
}

bool SettingsWindow::has_dev_tab() const {
    return d_->dev_tab != nullptr;
}

QWidget* SettingsWindow::dev_tab() const {
    return d_->dev_tab;
}

std::optional<ptd::AppConfig> SettingsWindow::captured_config() const {
    return captured_config_;
}

ptd::AppConfig SettingsWindow::capture_current_settings() const {
    ptd::AppConfig cfg{};
    cfg.schema_version = ptd::AppConfig::kCurrentSchemaVersion;
    cfg.master_enabled = master_enabled_;
    cfg.start_with_windows = start_with_windows_;
    cfg.trail = trail_cfg_;
    cfg.click = click_cfg_;
    cfg.audio = audio_cfg_;
    // CORE-004: complete-state authority -- non-UI fields come from the
    // retained baseline (seeded via apply_config / populate_from_config).
    cfg.render = render_baseline_;
    return ptd::AppConfig::validated(cfg);
}

void SettingsWindow::set_full_snapshot_for_tests(const ptd::AppConfig& cfg) {
    const ptd::AppConfig validated = ptd::AppConfig::validated(cfg);
    master_enabled_ = validated.master_enabled;
    start_with_windows_ = validated.start_with_windows;
    trail_cfg_ = validated.trail;
    click_cfg_ = validated.click;
    audio_cfg_ = validated.audio;
    render_baseline_ = validated.render;
    populate_from_config();
}

void SettingsWindow::apply_config(const ptd::AppConfig& cfg) {
    const ptd::AppConfig validated = ptd::AppConfig::validated(cfg);

    const bool master_changed = master_enabled_ != validated.master_enabled;
    master_enabled_ = validated.master_enabled;

    const bool startup_changed = start_with_windows_ != validated.start_with_windows;
    start_with_windows_ = validated.start_with_windows;

    trail_cfg_ = validated.trail;
    click_cfg_ = validated.click;
    // CORE-004: retain non-UI baseline for future whole-config captures.
    render_baseline_ = validated.render;
    audio_cfg_ = validated.audio;

    // Programmatically update all widgets with signals blocked
    {
        QSignalBlocker bm(d_->chk_master);
        QSignalBlocker bt(d_->chk_trail);
        QSignalBlocker bc(d_->chk_click);
        QSignalBlocker bs(d_->chk_start_with_windows);
        d_->chk_master->setChecked(master_enabled_);
        d_->chk_trail->setChecked(trail_cfg_.enabled);
        d_->chk_click->setChecked(click_cfg_.enabled);
        d_->chk_start_with_windows->setChecked(start_with_windows_);
    }
    apply_enable_states();

    set_trail_widgets(trail_cfg_);
    set_click_widgets(click_cfg_);
    set_audio_widgets(audio_cfg_);

    // CORE-003 + W2-002: a whole-AppConfig operation is ONE logical
    // publication. Emitting the narrow per-field signals here made
    // Application observe a sequence of unrelated partial updates and
    // commit intermediate hybrids; instead the validated complete config is
    // published once and Application applies it as one transaction.
    // (master_changed/startup_changed were only used by the removed narrow
    // fan-out; the transaction owns those edge decisions now.)
    (void)master_changed;
    (void)startup_changed;
    emit app_config_applied(validated);
}

void SettingsWindow::on_restore_all() {
    // T-33: the explicit Restore path applies the SAME canonical Release
    // Defaults a fresh configuration gets. There is no second set of
    // defaults hiding in C++ struct initializers.
    apply_config(ptd::release_defaults());
}

void SettingsWindow::on_restore_trail() {
    // Approved defaults (T-33: the canonical Release Defaults), silent widget
    // reset (selects Classic/Solid/Smooth and rebuilds contextual
    // visibility), exactly one final publication.
    trail_cfg_ = ptd::release_defaults().trail;
    set_trail_widgets(trail_cfg_);
    emit_trail_config();
}

void SettingsWindow::on_restore_click() {
    // Approved defaults (T-33: the canonical Release Defaults), silent widget
    // reset (selects Ring/Ease Out and rebuilds default visibility, triggers
    // included, no intermediate publications), exactly one final publication.
    click_cfg_ = ptd::release_defaults().click;
    set_click_widgets(click_cfg_);
    emit_click_config();
}

void SettingsWindow::on_preset_clicked(int index) {
    apply_preset(index);
}

// T-015 Phase 11: one preset click = one coherent (Trail, Click) pair.
// ONLY color_mode + Trail Start/Fade RGB + Click RGB change; every other
// field of both configs keeps its current value. Widgets are updated
// silently first (set_trail_widgets / set_click_widgets are signal-free),
// then exactly one preset_applied publication -- no intermediate
// trail_config_changed / click_config_changed half-states.
void SettingsWindow::apply_preset(int index) {
    if (index < 0 || index >= static_cast<int>(ptd::kColorPresets.size())) {
        return;
    }
    const ptd::ColorPreset& p = ptd::kColorPresets[static_cast<std::size_t>(index)];

    trail_cfg_.color_mode = p.trail_mode;
    trail_cfg_.start_color_r = p.trail_start_r;
    trail_cfg_.start_color_g = p.trail_start_g;
    trail_cfg_.start_color_b = p.trail_start_b;
    trail_cfg_.fade_color_r = p.trail_fade_r;
    trail_cfg_.fade_color_g = p.trail_fade_g;
    trail_cfg_.fade_color_b = p.trail_fade_b;

    click_cfg_.color_r = p.click_r;
    click_cfg_.color_g = p.click_g;
    click_cfg_.color_b = p.click_b;

    set_trail_widgets(trail_cfg_);
    set_click_widgets(click_cfg_);

    emit preset_applied(trail_cfg_, click_cfg_);
}

void SettingsWindow::set_master_enabled(bool on) {
    if (master_enabled_ != on) {
        master_enabled_ = on;
        d_->chk_master->blockSignals(true);
        d_->chk_master->setChecked(on);
        d_->chk_master->blockSignals(false);
        d_->chk_trail->setEnabled(on);
        d_->chk_click->setEnabled(on);
    }
}

void SettingsWindow::set_object_names() {
    d_->chk_master->setObjectName("chk_master");
    d_->chk_trail->setObjectName("chk_trail");
    d_->chk_click->setObjectName("chk_click");
    d_->chk_start_with_windows->setObjectName("chk_start_with_windows");
    d_->btn_restore_all->setObjectName("btn_restore_all");
    // T-020: stable names for the selector button groups (the buttons
    // themselves are named at creation: trail_color_solid,
    // trail_style_classic, trail_fade_smooth, click_style_ring, ...).
    d_->trail_color_group->setObjectName("trail_color_mode");
    d_->trail_style_group->setObjectName("trail_style");
    d_->trail_fade_group->setObjectName("trail_fade_curve");
    d_->trail_sparkle_group->setObjectName("trail_sparkle_mode");
    d_->click_style_group->setObjectName("click_style");
    d_->click_easing_group->setObjectName("click_easing");
    d_->trail_start_color->setObjectName("trail_start_color");
    d_->trail_fade_color->setObjectName("trail_fade_color");
    d_->trail_glow->setObjectName("trail_glow");
    d_->trail_spacing->setObjectName("trail_spacing");
    d_->trail_style_options->setObjectName("trail_style_options");
    d_->click_style_options->setObjectName("click_style_options");
    for (int i = 0; i < d_->preset_buttons.size(); ++i) {
        d_->preset_buttons.at(i)->setObjectName(
            QStringLiteral("preset_btn_%1").arg(
                QString::fromUtf8(ptd::kColorPresets[static_cast<std::size_t>(i)].name)));
    }
    d_->trail_head_thickness->setObjectName("trail_head_thickness");
    d_->trail_tail_thickness->setObjectName("trail_tail_thickness");
    d_->trail_taper->setObjectName("trail_taper");
    d_->trail_lifetime->setObjectName("trail_lifetime");
    d_->trail_opacity->setObjectName("trail_opacity");
    d_->trail_smoothing->setObjectName("trail_smoothing");
    d_->trail_fade_start->setObjectName("trail_fade_start");
    d_->trail_sparkle_amount->setObjectName("trail_sparkle_amount");
    d_->trail_sparkle_size->setObjectName("trail_sparkle_size");
    d_->trail_sparkle_spread->setObjectName("trail_sparkle_spread");
    d_->trail_sparkle_options->setObjectName("trail_sparkle_options");
    d_->btn_restore_trail->setObjectName("btn_restore_trail");
    d_->chk_trig_left->setObjectName("chk_trig_left");
    d_->chk_trig_right->setObjectName("chk_trig_right");
    d_->chk_trig_middle->setObjectName("chk_trig_middle");
    d_->chk_hold_fx->setObjectName("chk_hold_fx");
    d_->chk_hold_wake->setObjectName("chk_hold_wake");
    d_->hold_intensity->setObjectName("hold_intensity");
    d_->hold_wake_density->setObjectName("hold_wake_density");
    d_->hold_wake_lifetime->setObjectName("hold_wake_lifetime");
    d_->hold_release_strength->setObjectName("hold_release_strength");
    // T-36 Advanced Motion Wake object names (stable test surface).
    d_->wake_strength->setObjectName("wake_strength");
    d_->wake_size->setObjectName("wake_size");
    d_->wake_spread->setObjectName("wake_spread");
    d_->speed_response->setObjectName("speed_response");
    d_->min_motion_speed->setObjectName("min_motion_speed");
    d_->chk_turn_accent->setObjectName("chk_turn_accent");
    d_->chk_stop_accent->setObjectName("chk_stop_accent");
    d_->click_color->setObjectName("click_color");
    d_->click_start_size->setObjectName("click_start_size");
    d_->click_end_size->setObjectName("click_end_size");
    d_->click_duration->setObjectName("click_duration");
    d_->click_opacity->setObjectName("click_opacity");
    d_->click_outline->setObjectName("click_outline");
    d_->click_fill->setObjectName("click_fill");
    d_->click_param_options->setObjectName("click_param_options");
    d_->click_particles->setObjectName("click_particles");
    d_->click_element_tint->setObjectName("click_element_tint");
    d_->btn_restore_click->setObjectName("btn_restore_click");

    // T-80 / T-120: Sound tab object names (stable test surface).
    d_->sound_tab->setObjectName("tab_sound");
    d_->chk_sound_enabled->setObjectName("chk_sound_enabled");
    d_->chk_sound_muted->setObjectName("chk_sound_muted");
    d_->sound_volume->setObjectName("sound_volume");
    if (d_->chk_preview_on_select) d_->chk_preview_on_select->setObjectName("chk_preview_on_select");
    d_->sound_search->setObjectName("sound_search");
    d_->sound_catalog->setObjectName("sound_catalog");
    d_->sound_favorites->setObjectName("sound_favorites");
    if (d_->sound_recent) d_->sound_recent->setObjectName("sound_recent");
    d_->btn_sound_replay->setObjectName("btn_sound_replay");
    d_->btn_sound_stop->setObjectName("btn_sound_stop");
    d_->btn_sound_stop_all->setObjectName("btn_sound_stop_all");
    for (int i = 0; i < ptd::kAudioEventCount; ++i) {
        d_->sound_assign[i]->setObjectName(
            QStringLiteral("sound_assign_%1")
                .arg(QString::fromLatin1(ptd::audio_event_name(i))));
    }
    if (d_->chk_keyboard_enabled) d_->chk_keyboard_enabled->setObjectName("chk_keyboard_enabled");
    if (d_->combo_keyboard_scope) d_->combo_keyboard_scope->setObjectName("combo_keyboard_scope");
    if (d_->lbl_keyboard_scope_info) d_->lbl_keyboard_scope_info->setObjectName("lbl_keyboard_scope_info");
    if (d_->chk_typing_sounds) d_->chk_typing_sounds->setObjectName("chk_typing_sounds");
    if (d_->chk_modifier_tap_sounds) d_->chk_modifier_tap_sounds->setObjectName("chk_modifier_tap_sounds");
    if (d_->chk_suppress_repeat_typing) d_->chk_suppress_repeat_typing->setObjectName("chk_suppress_repeat_typing");
    if (d_->chk_suppress_sensitive) d_->chk_suppress_sensitive->setObjectName("chk_suppress_sensitive");
    if (d_->edit_excluded_proc) d_->edit_excluded_proc->setObjectName("edit_excluded_proc");
    if (d_->btn_add_excluded_proc) d_->btn_add_excluded_proc->setObjectName("btn_add_excluded_proc");
    if (d_->btn_remove_excluded_proc) d_->btn_remove_excluded_proc->setObjectName("btn_remove_excluded_proc");
    if (d_->list_excluded_procs) d_->list_excluded_procs->setObjectName("list_excluded_procs");
    if (d_->combo_event_category) d_->combo_event_category->setObjectName("combo_event_category");
    if (d_->edit_event_search) d_->edit_event_search->setObjectName("edit_event_search");
    if (d_->btn_reset_category) d_->btn_reset_category->setObjectName("btn_reset_category");
    if (d_->list_sound_events) d_->list_sound_events->setObjectName("list_sound_events");
    if (d_->lbl_selected_event_title) d_->lbl_selected_event_title->setObjectName("lbl_selected_event_title");
    if (d_->lbl_selected_event_note) d_->lbl_selected_event_note->setObjectName("lbl_selected_event_note");
    if (d_->chk_event_enabled) d_->chk_event_enabled->setObjectName("chk_event_enabled");
    if (d_->combo_event_sound) d_->combo_event_sound->setObjectName("combo_event_sound");
    if (d_->btn_event_fav_toggle) d_->btn_event_fav_toggle->setObjectName("btn_event_fav_toggle");
    if (d_->chk_event_use_pool) d_->chk_event_use_pool->setObjectName("chk_event_use_pool");
    if (d_->combo_event_policy) d_->combo_event_policy->setObjectName("combo_event_policy");
    if (d_->combo_event_scope) d_->combo_event_scope->setObjectName("combo_event_scope");
    if (d_->event_gain_db) d_->event_gain_db->setObjectName("event_gain_db");
    if (d_->event_cooldown_ms) d_->event_cooldown_ms->setObjectName("event_cooldown_ms");
    if (d_->event_priority) d_->event_priority->setObjectName("event_priority");
    if (d_->btn_event_preview) d_->btn_event_preview->setObjectName("btn_event_preview");
    if (d_->btn_event_reset) d_->btn_event_reset->setObjectName("btn_event_reset");
    if (d_->chk_show_advanced_event) d_->chk_show_advanced_event->setObjectName("chk_show_advanced_event");
    if (d_->advanced_event_panel) d_->advanced_event_panel->setObjectName("advanced_event_panel");
    if (d_->event_fade_in_ms) d_->event_fade_in_ms->setObjectName("event_fade_in_ms");
    if (d_->event_fade_out_ms) d_->event_fade_out_ms->setObjectName("event_fade_out_ms");
    if (d_->event_min_retrigger_ms) d_->event_min_retrigger_ms->setObjectName("event_min_retrigger_ms");
    if (d_->event_polyphony) d_->event_polyphony->setObjectName("event_polyphony");
    if (d_->event_pitch_var) d_->event_pitch_var->setObjectName("event_pitch_var");
    if (d_->chk_event_no_repeat) d_->chk_event_no_repeat->setObjectName("chk_event_no_repeat");
    if (d_->chk_event_norm_override) d_->chk_event_norm_override->setObjectName("chk_event_norm_override");
    if (d_->chk_event_norm_enabled) d_->chk_event_norm_enabled->setObjectName("chk_event_norm_enabled");
    if (d_->pool_editor_panel) d_->pool_editor_panel->setObjectName("pool_editor_panel");
    if (d_->list_pool_entries) d_->list_pool_entries->setObjectName("list_pool_entries");
    if (d_->combo_pool_add_sound) d_->combo_pool_add_sound->setObjectName("combo_pool_add_sound");
    if (d_->btn_pool_add) d_->btn_pool_add->setObjectName("btn_pool_add");
    if (d_->btn_pool_remove) d_->btn_pool_remove->setObjectName("btn_pool_remove");
    if (d_->btn_pool_up) d_->btn_pool_up->setObjectName("btn_pool_up");
    if (d_->btn_pool_down) d_->btn_pool_down->setObjectName("btn_pool_down");
    if (d_->chk_pool_entry_enabled) d_->chk_pool_entry_enabled->setObjectName("chk_pool_entry_enabled");
    if (d_->pool_entry_weight) d_->pool_entry_weight->setObjectName("pool_entry_weight");
    if (d_->pool_entry_gain_db) d_->pool_entry_gain_db->setObjectName("pool_entry_gain_db");
    if (d_->btn_pool_preview_entry) d_->btn_pool_preview_entry->setObjectName("btn_pool_preview_entry");
    if (d_->btn_pool_preview_random) d_->btn_pool_preview_random->setObjectName("btn_pool_preview_random");
    if (d_->edit_custom_sc_name) d_->edit_custom_sc_name->setObjectName("edit_custom_sc_name");
    if (d_->edit_custom_sc_chord) d_->edit_custom_sc_chord->setObjectName("edit_custom_sc_chord");
    if (d_->chk_custom_sc_app) d_->chk_custom_sc_app->setObjectName("chk_custom_sc_app");
    if (d_->btn_custom_sc_add) d_->btn_custom_sc_add->setObjectName("btn_custom_sc_add");
    if (d_->btn_custom_sc_remove) d_->btn_custom_sc_remove->setObjectName("btn_custom_sc_remove");
    if (d_->list_custom_shortcuts) d_->list_custom_shortcuts->setObjectName("list_custom_shortcuts");
    if (d_->chk_horizontal_scroll) d_->chk_horizontal_scroll->setObjectName("chk_horizontal_scroll");
    if (d_->scroll_sensitivity) d_->scroll_sensitivity->setObjectName("scroll_sensitivity");
    if (d_->scroll_cooldown) d_->scroll_cooldown->setObjectName("scroll_cooldown");
    if (d_->scroll_max_rate) d_->scroll_max_rate->setObjectName("scroll_max_rate");
    if (d_->scroll_pitch_var) d_->scroll_pitch_var->setObjectName("scroll_pitch_var");
    if (d_->combo_sound_profile) d_->combo_sound_profile->setObjectName("combo_sound_profile");
    if (d_->btn_apply_sound_profile) d_->btn_apply_sound_profile->setObjectName("btn_apply_sound_profile");
    if (d_->btn_export_audio_profile) d_->btn_export_audio_profile->setObjectName("btn_export_audio_profile");
    if (d_->btn_import_audio_profile) d_->btn_import_audio_profile->setObjectName("btn_import_audio_profile");

    if (d_->dev_tab != nullptr) {
        d_->dev_tab->setObjectName("tab_dev");
        d_->btn_dev_capture->setObjectName("btn_dev_capture");
        d_->lbl_dev_captured->setObjectName("lbl_dev_captured_info");
        d_->edit_dev_preset_name->setObjectName("edit_dev_preset_name");
        d_->btn_dev_save_preset->setObjectName("btn_dev_save_preset");
        d_->list_dev_presets->setObjectName("list_dev_presets");
        d_->btn_dev_apply_preset->setObjectName("btn_dev_apply_preset");
        d_->btn_dev_apply_defaults->setObjectName("btn_dev_apply_defaults");
        d_->dev_source_group->setObjectName("dev_source_group");
        d_->btn_dev_show_diff->setObjectName("btn_dev_show_diff");
        d_->btn_dev_promote->setObjectName("btn_dev_promote");
        d_->txt_dev_diff->setObjectName("txt_dev_diff");
        d_->lbl_dev_status->setObjectName("lbl_dev_status");
    }
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}

} // namespace ui
} // namespace ptd
