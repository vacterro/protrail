#include "settings_window.h"
#include "theme.h"
#include "../effects/effect_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QTabWidget>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QCloseEvent>

#include <algorithm>
#include <cstddef>

namespace ptd {
namespace ui {

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
    d_->lbl->setFixedWidth(140);            // fixed label column width
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
    d_->spin->setFixedWidth(80);            // fixed spin width (no reflow)

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
    d_->lbl->setFixedWidth(140);
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
    // T-015: six one-click color presets (kColorPresets order).
    QList<QPushButton*> preset_buttons;
    QPushButton* btn_restore_all;

    // Trail controls (T-015: dual-color model replaces the single editor)
    QComboBox* trail_color_mode;
    ColorEditor* trail_start_color;
    ColorEditor* trail_fade_color;
    // T-016: trail style surface
    QComboBox* trail_style;
    SliderSpin* trail_glow;
    SliderSpin* trail_spacing;
    SliderSpin* trail_head_thickness;
    SliderSpin* trail_tail_thickness;
    SliderSpin* trail_taper;
    SliderSpin* trail_lifetime;
    SliderSpin* trail_opacity;
    SliderSpin* trail_smoothing;
    SliderSpin* trail_fade_start;
    QComboBox* trail_fade_curve;
    QPushButton* btn_restore_trail;

    // Click controls
    QCheckBox* chk_trig_left;
    QCheckBox* chk_trig_right;
    QCheckBox* chk_trig_middle;
    ColorEditor* click_color;
    SliderSpin* click_start_size;
    SliderSpin* click_end_size;
    SliderSpin* click_duration;
    SliderSpin* click_opacity;
    SliderSpin* click_outline;
    SliderSpin* click_fill;
    QComboBox* click_easing;
    // T-017: click style surface
    QComboBox* click_style;
    SliderSpin* click_particles;
    QPushButton* btn_restore_click;
};

SettingsWindow::SettingsWindow(const ptd::TrailConfig& trail,
                               const ptd::ClickConfig& click,
                               bool master_enabled,
                               QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>()),
      master_enabled_(master_enabled),
      trail_cfg_(trail), click_cfg_(click) {
    setWindowTitle(QStringLiteral("ProTrail Settings"));
    setMinimumSize(640, 540);
    resize(640, 540);

    // Apply Golden Default theme
    setStyleSheet(theme::golden_stylesheet());
    QFont f(theme::fontFamily(), theme::fontBodyPx());
    f.setStyleStrategy(QFont::NoAntialias);
    setFont(f);

    build_general_tab();
    build_trail_tab();
    build_click_tab();

    d_->tabs = new QTabWidget();
    d_->tabs->addTab(d_->general_tab, "General");
    d_->tabs->addTab(d_->trail_tab, "Trail");
    d_->tabs->addTab(d_->click_tab, "Click");

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(theme::outerMarginPx(), theme::outerMarginPx(),
                             theme::outerMarginPx(), theme::outerMarginPx());
    root->setSpacing(theme::sectionGapPx());
    root->addWidget(d_->tabs);

    setup_connections();
    set_object_names();

    // Single explicit initialization: copy validated config -> widgets
    // with all signals blocked; no publications during construction.
    populate_from_config();
    apply_enable_states();
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::build_general_tab() {
    d_->general_tab = new QWidget();
    auto* lay = new QVBoxLayout(d_->general_tab);
    lay->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                            theme::groupPadPx(), theme::groupPadPx());
    lay->setSpacing(theme::sectionGapPx());

    auto* gb = new QGroupBox("Enable");
    auto* gl = new QGridLayout(gb);
    gl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    gl->setHorizontalSpacing(theme::controlPadPx());
    gl->setVerticalSpacing(theme::controlPadPx());

    d_->chk_master = new QCheckBox("Enable ProTrail");
    d_->chk_trail = new QCheckBox("Enable Trail");
    d_->chk_click = new QCheckBox("Enable Click Effect");

    gl->addWidget(d_->chk_master, 0, 0);
    gl->addWidget(d_->chk_trail, 1, 0);
    gl->addWidget(d_->chk_click, 2, 0);

    lay->addWidget(gb);

    // T-015: quick color presets. kColorPresets is the ONLY authority;
    // a preset changes Trail color_mode/Start/Fade and Click RGB, nothing
    // else. Visible scope note (never tooltip-only).
    auto* gb_preset = new QGroupBox("Quick Color Presets");
    auto* pl = new QVBoxLayout(gb_preset);
    pl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    pl->setSpacing(theme::controlPadPx());

    auto* row = new QHBoxLayout();
    row->setSpacing(theme::controlPadPx());
    for (int i = 0; i < static_cast<int>(ptd::kColorPresets.size()); ++i) {
        auto* btn = new QPushButton(
            QString::fromUtf8(ptd::kColorPresets[static_cast<std::size_t>(i)].name));
        const int idx = i;
        connect(btn, &QPushButton::clicked,
                this, [this, idx] { on_preset_clicked(idx); });
        row->addWidget(btn);
        d_->preset_buttons.append(btn);
    }
    pl->addLayout(row);

    auto* note = new QLabel("Applies Trail + Click colors only.");
    note->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    pl->addWidget(note);

    lay->addWidget(gb_preset);

    // T-018: restore all defaults
    d_->btn_restore_all = new QPushButton("Restore All Defaults");
    lay->addWidget(d_->btn_restore_all);
    lay->addStretch();
}

void SettingsWindow::build_trail_tab() {
    d_->trail_tab = new QWidget();
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

    // Trail Color (T-015: dual color model). BOTH editors stay visible in
    // every mode; the mode combo selects how Start/Fade are applied.
    auto* gb_color = new QGroupBox("Trail Color");
    auto* cl = new QVBoxLayout(gb_color);
    cl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    cl->setSpacing(theme::controlPadPx());

    auto* mode_lay = new QHBoxLayout();
    mode_lay->setSpacing(theme::controlPadPx());
    auto* mode_lbl = new QLabel("Color mode");
    mode_lbl->setFixedWidth(140);
    mode_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d_->trail_color_mode = new QComboBox();
    d_->trail_color_mode->addItem("Full",
        static_cast<int>(ptd::TrailColorMode::Full));
    d_->trail_color_mode->addItem("Start only",
        static_cast<int>(ptd::TrailColorMode::StartAccent));
    d_->trail_color_mode->addItem("Fade only",
        static_cast<int>(ptd::TrailColorMode::FadeAccent));
    d_->trail_color_mode->addItem("Gradient",
        static_cast<int>(ptd::TrailColorMode::Gradient));
    mode_lay->addWidget(mode_lbl);
    mode_lay->addWidget(d_->trail_color_mode, 1);
    cl->addLayout(mode_lay);

    d_->trail_start_color = new ColorEditor("Start / Head",
        trail_cfg_.start_color_r, trail_cfg_.start_color_g,
        trail_cfg_.start_color_b);
    d_->trail_fade_color = new ColorEditor("Fade / Tail",
        trail_cfg_.fade_color_r, trail_cfg_.fade_color_g,
        trail_cfg_.fade_color_b);
    cl->addWidget(d_->trail_start_color);
    cl->addWidget(d_->trail_fade_color);

    // Compact semantic explanation, always visible (never tooltip-only).
    auto* mode_hint = new QLabel(
        "Full = Start everywhere  |  Start only = head accent  |  "
        "Fade only = tail accent  |  Gradient = Fade -> Start");
    mode_hint->setWordWrap(true);
    mode_hint->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    cl->addWidget(mode_hint);

    lay->addWidget(gb_color);

    // Trail Effects (T-016): 8 styles + style parameters.
    auto* gb_style = new QGroupBox("Trail Effects");
    auto* sl = new QVBoxLayout(gb_style);
    sl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    sl->setSpacing(theme::controlPadPx());

    auto* style_lay = new QHBoxLayout();
    style_lay->setSpacing(theme::controlPadPx());
    auto* style_lbl = new QLabel("Style");
    style_lbl->setFixedWidth(140);
    style_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d_->trail_style = new QComboBox();
    d_->trail_style->addItem("Classic",   static_cast<int>(ptd::TrailStyle::Classic));
    d_->trail_style->addItem("Soft Glow", static_cast<int>(ptd::TrailStyle::SoftGlow));
    d_->trail_style->addItem("Comet",     static_cast<int>(ptd::TrailStyle::Comet));
    d_->trail_style->addItem("Neon",      static_cast<int>(ptd::TrailStyle::Neon));
    d_->trail_style->addItem("Dotted",    static_cast<int>(ptd::TrailStyle::Dotted));
    d_->trail_style->addItem("Pulse",     static_cast<int>(ptd::TrailStyle::Pulse));
    d_->trail_style->addItem("Ribbon",    static_cast<int>(ptd::TrailStyle::Ribbon));
    d_->trail_style->addItem("Spark",     static_cast<int>(ptd::TrailStyle::Spark));
    style_lay->addWidget(style_lbl);
    style_lay->addWidget(d_->trail_style, 1);
    sl->addLayout(style_lay);

    d_->trail_glow = new SliderSpin(
        "Glow strength", ptd::TrailConfig::kMinGlowStrength * 100.0,
        ptd::TrailConfig::kMaxGlowStrength * 100.0, 1.0, 0, "%");
    d_->trail_spacing = new SliderSpin(
        "Dot spacing", ptd::TrailConfig::kMinSegmentSpacingPx,
        ptd::TrailConfig::kMaxSegmentSpacingPx, 1.0, 0, " px");
    sl->addWidget(d_->trail_glow);
    sl->addWidget(d_->trail_spacing);

    auto* style_hint = new QLabel(
        "Glow: Soft Glow / Neon   |   Dot spacing: Dotted / Spark");
    style_hint->setWordWrap(true);
    style_hint->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    sl->addWidget(style_hint);

    lay->addWidget(gb_style);

    // Width / Taper (Phase F)
    auto* gb_width = new QGroupBox("Width & Taper");
    auto* wl = new QVBoxLayout(gb_width);
    wl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    wl->setSpacing(theme::controlPadPx());

    d_->trail_head_thickness = new SliderSpin(
        "Head thickness", ptd::TrailConfig::kMinThicknessPx,
        ptd::TrailConfig::kMaxThicknessPx, 0.1, 1, " px");
    d_->trail_tail_thickness = new SliderSpin(
        "Tail thickness", ptd::TrailConfig::kMinThicknessPx,
        ptd::TrailConfig::kMaxThicknessPx, 0.1, 1, " px");
    // Percentage controls use UI percent units 0..100 (runtime keeps
    // normalized 0..1; conversion happens in emit_trail_config).
    d_->trail_taper = new SliderSpin(
        "Taper strength", 0.0, 100.0, 1.0, 0, "%");

    wl->addWidget(d_->trail_head_thickness);
    wl->addWidget(d_->trail_tail_thickness);
    wl->addWidget(d_->trail_taper);
    lay->addWidget(gb_width);

    // Lifetime / Opacity / Smoothing
    auto* gb_basic = new QGroupBox("Lifetime / Opacity / Smoothing");
    auto* bl = new QVBoxLayout(gb_basic);
    bl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    bl->setSpacing(theme::controlPadPx());

    d_->trail_lifetime = new SliderSpin(
        "Lifetime", ptd::TrailConfig::kMinLifetimeMs,
        ptd::TrailConfig::kMaxLifetimeMs, 10.0, 0, " ms");
    d_->trail_opacity = new SliderSpin(
        "Opacity", ptd::TrailConfig::kMinOpacity * 100.0,
        ptd::TrailConfig::kMaxOpacity * 100.0, 1.0, 0, "%");
    d_->trail_smoothing = new SliderSpin(
        "Smoothing", ptd::TrailConfig::kMinSmoothing * 100.0,
        ptd::TrailConfig::kMaxSmoothing * 100.0, 1.0, 0, "%");

    bl->addWidget(d_->trail_lifetime);
    bl->addWidget(d_->trail_opacity);
    bl->addWidget(d_->trail_smoothing);
    lay->addWidget(gb_basic);

    // Fade controls (Phase G)
    auto* gb_fade = new QGroupBox("Fade");
    auto* fl = new QVBoxLayout(gb_fade);
    fl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    fl->setSpacing(theme::controlPadPx());

    d_->trail_fade_start = new SliderSpin(
        "Fade start", ptd::TrailConfig::kMinFadeStart * 100.0,
        ptd::TrailConfig::kMaxFadeStart * 100.0, 1.0, 0, "%");

    d_->trail_fade_curve = new QComboBox();
    d_->trail_fade_curve->addItem("Linear",  static_cast<int>(ptd::FadeCurve::Linear));
    d_->trail_fade_curve->addItem("Smooth",  static_cast<int>(ptd::FadeCurve::Smooth));
    d_->trail_fade_curve->addItem("Ease Out", static_cast<int>(ptd::FadeCurve::EaseOut));

    auto* fc_lay = new QHBoxLayout();
    fc_lay->addWidget(new QLabel("Curve"));
    fc_lay->addWidget(d_->trail_fade_curve, 1);
    fc_lay->setContentsMargins(0, 0, 0, 0);

    fl->addWidget(d_->trail_fade_start);
    fl->addLayout(fc_lay);
    lay->addWidget(gb_fade);

    // Restore
    d_->btn_restore_trail = new QPushButton("Restore Trail Defaults");
    lay->addWidget(d_->btn_restore_trail);
    lay->addStretch();

    root->addWidget(scroll);
}

void SettingsWindow::build_click_tab() {
    d_->click_tab = new QWidget();
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

    // Triggers (Phase J)
    auto* gb_trig = new QGroupBox("Trigger");
    auto* tl = new QVBoxLayout(gb_trig);
    tl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    tl->setSpacing(theme::controlPadPx());

    d_->chk_trig_left   = new QCheckBox("Left");
    d_->chk_trig_right  = new QCheckBox("Right");
    d_->chk_trig_middle = new QCheckBox("Middle");
    tl->addWidget(d_->chk_trig_left);
    tl->addWidget(d_->chk_trig_right);
    tl->addWidget(d_->chk_trig_middle);
    lay->addWidget(gb_trig);

    // Click Style (T-017): 7 styles + particle amount.
    auto* gb_cstyle = new QGroupBox("Click Style");
    auto* ks = new QVBoxLayout(gb_cstyle);
    ks->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    ks->setSpacing(theme::controlPadPx());

    auto* cstyle_lay = new QHBoxLayout();
    cstyle_lay->setSpacing(theme::controlPadPx());
    auto* cstyle_lbl = new QLabel("Style");
    cstyle_lbl->setFixedWidth(140);
    cstyle_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d_->click_style = new QComboBox();
    d_->click_style->addItem("Ring",        static_cast<int>(ptd::ClickStyle::Ring));
    d_->click_style->addItem("Double Ring", static_cast<int>(ptd::ClickStyle::DoubleRing));
    d_->click_style->addItem("Ripple",      static_cast<int>(ptd::ClickStyle::Ripple));
    d_->click_style->addItem("Burst",       static_cast<int>(ptd::ClickStyle::Burst));
    d_->click_style->addItem("Spark Burst", static_cast<int>(ptd::ClickStyle::SparkBurst));
    d_->click_style->addItem("Soft Flash",  static_cast<int>(ptd::ClickStyle::SoftFlash));
    d_->click_style->addItem("Dot + Ring",  static_cast<int>(ptd::ClickStyle::DotRing));
    cstyle_lay->addWidget(cstyle_lbl);
    cstyle_lay->addWidget(d_->click_style, 1);
    ks->addLayout(cstyle_lay);

    d_->click_particles = new SliderSpin(
        "Particles", ptd::ClickConfig::kMinParticleAmount,
        ptd::ClickConfig::kMaxParticleAmount, 1.0, 0, "");
    ks->addWidget(d_->click_particles);

    lay->addWidget(gb_cstyle);

    // Color
    auto* gb_color = new QGroupBox("Color");
    auto* cl = new QHBoxLayout(gb_color);
    cl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    d_->click_color = new ColorEditor("RGB", click_cfg_.color_r,
                                      click_cfg_.color_g, click_cfg_.color_b);
    cl->addWidget(d_->click_color, 1);
    lay->addWidget(gb_color);

    // Sizes / Duration / Opacity / Outline / Fill
    auto* gb_geom = new QGroupBox("Geometry & Opacity");
    auto* gl = new QVBoxLayout(gb_geom);
    gl->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    gl->setSpacing(theme::controlPadPx());

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

    gl->addWidget(d_->click_start_size);
    gl->addWidget(d_->click_end_size);
    gl->addWidget(d_->click_duration);
    gl->addWidget(d_->click_opacity);
    gl->addWidget(d_->click_outline);
    gl->addWidget(d_->click_fill);
    lay->addWidget(gb_geom);

    // Easing (Phase J)
    auto* gb_ease = new QGroupBox("Easing");
    auto* el = new QHBoxLayout(gb_ease);
    el->setContentsMargins(theme::groupPadPx(), theme::groupPadPx(),
                           theme::groupPadPx(), theme::groupPadPx());
    d_->click_easing = new QComboBox();
    d_->click_easing->addItem("Linear",  static_cast<int>(ptd::ClickEasing::Linear));
    d_->click_easing->addItem("Smooth",  static_cast<int>(ptd::ClickEasing::Smooth));
    d_->click_easing->addItem("Ease Out", static_cast<int>(ptd::ClickEasing::EaseOut));
    el->addWidget(new QLabel("Curve"));
    el->addWidget(d_->click_easing, 1);
    lay->addWidget(gb_ease);

    // Restore
    d_->btn_restore_click = new QPushButton("Restore Click Defaults");
    lay->addWidget(d_->btn_restore_click);
    lay->addStretch();

    root->addWidget(scroll);
}

void SettingsWindow::setup_connections() {
    // General
    connect(d_->chk_master, &QCheckBox::toggled, this, &SettingsWindow::on_master_toggled);
    connect(d_->chk_trail,  &QCheckBox::toggled, this, &SettingsWindow::on_trail_toggled);
    connect(d_->chk_click,  &QCheckBox::toggled, this, &SettingsWindow::on_click_toggled);
    connect(d_->btn_restore_all, &QPushButton::clicked, this, &SettingsWindow::on_restore_all);

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
    connect(d_->trail_style, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::emit_trail_config);
    connect(d_->trail_fade_curve, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::emit_trail_config);
    connect(d_->trail_color_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::emit_trail_config);
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
    connect(d_->click_easing, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::emit_click_config);
    connect(d_->click_style, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::emit_click_config);
    connect(d_->click_particles, &SliderSpin::value_changed,
            this, &SettingsWindow::on_click_slider);
    connect(d_->click_color, &ColorEditor::color_changed,
            this, &SettingsWindow::on_click_color);
    connect(d_->btn_restore_click, &QPushButton::clicked,
            this, &SettingsWindow::on_restore_click);
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

void SettingsWindow::on_trail_slider(double) {
    // Build trail_cfg_ from all controls and emit once.
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
    // SliderSpin::set_value is signal-silent (QSignalBlocker-equivalent
    // updating_ guard), so no recursive emission: exactly one coherent
    // ClickConfig is published per user action.
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
    trail_cfg_.style = static_cast<ptd::TrailStyle>(
        d_->trail_style->currentData().toInt());
    trail_cfg_.fade_curve = static_cast<ptd::FadeCurve>(
        d_->trail_fade_curve->currentData().toInt());
    trail_cfg_.color_mode = static_cast<ptd::TrailColorMode>(
        d_->trail_color_mode->currentData().toInt());

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
        d_->click_easing->currentData().toInt());
    click_cfg_.style = static_cast<ptd::ClickStyle>(
        d_->click_style->currentData().toInt());
    click_cfg_.particle_amount = static_cast<uint8_t>(
        static_cast<int>(d_->click_particles->value()));

    click_cfg_ = ptd::ClickConfig::validated(click_cfg_);
    emit click_config_changed(click_cfg_);
}

// Silent programmatic assignment: copies a validated config into the
// Trail widgets with all intermediate signals blocked (Defects B/E).
// Enable checkboxes (General tab) are intentionally untouched -- restore
// must not reset master/effect state.
void SettingsWindow::set_trail_widgets(const ptd::TrailConfig& c) {
    QSignalBlocker mode(d_->trail_color_mode);
    d_->trail_color_mode->setCurrentIndex(static_cast<int>(c.color_mode));
    d_->trail_start_color->set(c.start_color_r, c.start_color_g, c.start_color_b);
    d_->trail_fade_color->set(c.fade_color_r, c.fade_color_g, c.fade_color_b);
    d_->trail_glow->set_value(c.glow_strength * 100.0);
    d_->trail_spacing->set_value(c.segment_spacing_px);
    QSignalBlocker style(d_->trail_style);
    d_->trail_style->setCurrentIndex(static_cast<int>(c.style));
    d_->trail_head_thickness->set_value(c.head_thickness_px);
    d_->trail_tail_thickness->set_value(c.tail_thickness_px);
    d_->trail_taper->set_value(c.taper_strength * 100.0);
    d_->trail_lifetime->set_value(c.lifetime_ms);
    d_->trail_opacity->set_value(c.base_opacity * 100.0);
    d_->trail_smoothing->set_value(c.smoothing * 100.0);
    d_->trail_fade_start->set_value(c.fade_start * 100.0);
    QSignalBlocker curve(d_->trail_fade_curve);
    d_->trail_fade_curve->setCurrentIndex(static_cast<int>(c.fade_curve));
}

// Silent programmatic assignment for the Click widgets (Defects B/E).
void SettingsWindow::set_click_widgets(const ptd::ClickConfig& c) {
    QSignalBlocker tl(d_->chk_trig_left);
    QSignalBlocker tr(d_->chk_trig_right);
    QSignalBlocker tm(d_->chk_trig_middle);
    d_->chk_trig_left->setChecked(c.trigger_left);
    d_->chk_trig_right->setChecked(c.trigger_right);
    d_->chk_trig_middle->setChecked(c.trigger_middle);
    d_->click_color->set(c.color_r, c.color_g, c.color_b);
    d_->click_start_size->set_value(c.start_radius_px);
    d_->click_end_size->set_value(c.end_radius_px);
    d_->click_duration->set_value(c.duration_ms);
    d_->click_opacity->set_value(c.base_opacity * 100.0);
    d_->click_outline->set_value(c.outline_thickness_px);
    d_->click_fill->set_value(c.fill_opacity * 100.0);
    QSignalBlocker ease(d_->click_easing);
    d_->click_easing->setCurrentIndex(static_cast<int>(c.easing));
    d_->click_particles->set_value(static_cast<double>(c.particle_amount));
    QSignalBlocker cstyle(d_->click_style);
    d_->click_style->setCurrentIndex(static_cast<int>(c.style));
}

// Single explicit initialization path from validated configs to widgets
// (Defect B). All signals blocked: construction publishes nothing.
void SettingsWindow::populate_from_config() {
    set_trail_widgets(trail_cfg_);
    set_click_widgets(click_cfg_);
    QSignalBlocker bm(d_->chk_master);
    QSignalBlocker bt(d_->chk_trail);
    QSignalBlocker bc(d_->chk_click);
    d_->chk_master->setChecked(master_enabled_);
    d_->chk_trail->setChecked(trail_cfg_.enabled);
    d_->chk_click->setChecked(click_cfg_.enabled);
}

// Enable/disable the sub-checkboxes per master state, WITHOUT emitting
// anything (the emitting variant is on_master_toggled).
void SettingsWindow::apply_enable_states() {
    const bool on = d_->chk_master->isChecked();
    d_->chk_trail->setEnabled(on);
    d_->chk_click->setEnabled(on);
}

void SettingsWindow::on_restore_all() {
    d_->chk_master->setChecked(true);
    trail_cfg_ = ptd::TrailConfig{};
    click_cfg_ = ptd::ClickConfig{};
    set_trail_widgets(trail_cfg_);
    set_click_widgets(click_cfg_);
    emit_trail_config();
    emit_click_config();
}

void SettingsWindow::on_restore_trail() {
    // Approved defaults, silent widget reset, exactly one final
    // publication (Defect E). Click side untouched.
    trail_cfg_ = ptd::TrailConfig{};
    set_trail_widgets(trail_cfg_);
    emit_trail_config();
}

void SettingsWindow::on_restore_click() {
    // Approved defaults, silent widget reset (triggers + easing included,
    // no intermediate publications), exactly one final publication.
    click_cfg_ = ptd::ClickConfig{};
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
    d_->btn_restore_all->setObjectName("btn_restore_all");
    d_->trail_color_mode->setObjectName("trail_color_mode");
    d_->trail_start_color->setObjectName("trail_start_color");
    d_->trail_fade_color->setObjectName("trail_fade_color");
    d_->trail_style->setObjectName("trail_style");
    d_->trail_glow->setObjectName("trail_glow");
    d_->trail_spacing->setObjectName("trail_spacing");
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
    d_->trail_fade_curve->setObjectName("trail_fade_curve");
    d_->btn_restore_trail->setObjectName("btn_restore_trail");
    d_->chk_trig_left->setObjectName("chk_trig_left");
    d_->chk_trig_right->setObjectName("chk_trig_right");
    d_->chk_trig_middle->setObjectName("chk_trig_middle");
    d_->click_color->setObjectName("click_color");
    d_->click_start_size->setObjectName("click_start_size");
    d_->click_end_size->setObjectName("click_end_size");
    d_->click_duration->setObjectName("click_duration");
    d_->click_opacity->setObjectName("click_opacity");
    d_->click_outline->setObjectName("click_outline");
    d_->click_fill->setObjectName("click_fill");
    d_->click_easing->setObjectName("click_easing");
    d_->click_style->setObjectName("click_style");
    d_->click_particles->setObjectName("click_particles");
    d_->btn_restore_click->setObjectName("btn_restore_click");
}

void SettingsWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}

} // namespace ui
} // namespace ptd