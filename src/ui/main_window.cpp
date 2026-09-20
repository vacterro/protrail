#include "main_window.h"
#include "theme.h"
#include "../config/dev_defaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCloseEvent>

#include <cstddef>

namespace ptd {
namespace ui {

namespace {

constexpr int kComboMinPx = 118;

// T-36 config bounds drive the Essentials sliders, exactly like Settings --
// no duplicated magic numbers.
constexpr double kWakeDensityScale = 100.0;
constexpr double kHoldIntensityScale = 100.0;

} // namespace

struct MainWindow::Impl {
    QCheckBox* chk_master = nullptr;
    QCheckBox* chk_trail = nullptr;
    QCheckBox* chk_click = nullptr;
    QCheckBox* chk_hold = nullptr;
    QCheckBox* chk_motion_wake = nullptr;
    QCheckBox* chk_start_with_windows = nullptr;
    QComboBox* cmb_trail_style = nullptr;
    QComboBox* cmb_sparkle_mode = nullptr;
    QComboBox* cmb_click_style = nullptr;
    QSlider* sld_wake_density = nullptr;
    QSlider* sld_hold_intensity = nullptr;
    QLabel* lbl_wake_density = nullptr;
    QLabel* lbl_hold_intensity = nullptr;
    QPushButton* btn_advanced_settings = nullptr;
    QPushButton* btn_restore_defaults = nullptr;
    // Developer builds only; nullptr in a production Release build.
    QPushButton* btn_set_defaults = nullptr;

    // Last canonical snapshot this surface was populated from. The Main
    // surface is a VIEW: it never owns config beyond this.
    ptd::AppConfig cfg{};
};

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>()) {
    setWindowTitle(QStringLiteral("ProTrail"));
    setMinimumSize(420, 260);
    resize(460, 300);
    setStyleSheet(theme::golden_stylesheet());
    QFont f(theme::fontFamily(), theme::fontBodyPx());
    f.setStyleStrategy(QFont::NoAntialias);
    setFont(f);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(theme::outerMarginPx(), theme::outerMarginPx(),
                             theme::outerMarginPx(), theme::outerMarginPx());
    root->setSpacing(theme::sectionGapPx());

    // Compact two-column grid: a label/control pair per Essentials row.
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(theme::sectionGapPx());
    grid->setVerticalSpacing(theme::controlPadPx() + 4);
    root->addLayout(grid);

    int row = 0;
    auto add_toggle = [&](const QString& text, const QString& name) -> QCheckBox* {
        auto* box = new QCheckBox(text);
        box->setObjectName(name);
        grid->addWidget(box, row++, 1);
        return box;
    };
    auto add_label = [&](const QString& text, int at_row) {
        auto* lbl = new QLabel(text);
        grid->addWidget(lbl, at_row, 0);
    };

    add_label(QStringLiteral("Master FX"), row);
    d_->chk_master = add_toggle(QString(), QStringLiteral("main_chk_master"));

    add_label(QStringLiteral("Trail FX"), row);
    d_->chk_trail = add_toggle(QString(), QStringLiteral("main_chk_trail"));

    add_label(QStringLiteral("Trail Style"), row);
    d_->cmb_trail_style = new QComboBox();
    d_->cmb_trail_style->setObjectName(QStringLiteral("main_cmb_trail_style"));
    d_->cmb_trail_style->setMinimumWidth(kComboMinPx);
    grid->addWidget(d_->cmb_trail_style, row++, 1);

    add_label(QStringLiteral("Sparkle Mode"), row);
    d_->cmb_sparkle_mode = new QComboBox();
    d_->cmb_sparkle_mode->setObjectName(QStringLiteral("main_cmb_sparkle_mode"));
    d_->cmb_sparkle_mode->setMinimumWidth(kComboMinPx);
    grid->addWidget(d_->cmb_sparkle_mode, row++, 1);

    add_label(QStringLiteral("Click FX"), row);
    d_->chk_click = add_toggle(QString(), QStringLiteral("main_chk_click"));

    add_label(QStringLiteral("Click Style"), row);
    d_->cmb_click_style = new QComboBox();
    d_->cmb_click_style->setObjectName(QStringLiteral("main_cmb_click_style"));
    d_->cmb_click_style->setMinimumWidth(kComboMinPx);
    grid->addWidget(d_->cmb_click_style, row++, 1);

    add_label(QStringLiteral("Hold FX"), row);
    d_->chk_hold = add_toggle(QString(), QStringLiteral("main_chk_hold"));

    add_label(QStringLiteral("Motion Wake"), row);
    d_->chk_motion_wake = add_toggle(QString(), QStringLiteral("main_chk_motion_wake"));

    add_label(QStringLiteral("Wake Density"), row);
    {
        auto* host = new QHBoxLayout();
        d_->sld_wake_density = new QSlider(Qt::Horizontal);
        d_->sld_wake_density->setObjectName(QStringLiteral("main_sld_wake_density"));
        d_->sld_wake_density->setRange(
            static_cast<int>(ptd::ClickConfig::kMinHoldWakeDensity * kWakeDensityScale),
            static_cast<int>(ptd::ClickConfig::kMaxHoldWakeDensity * kWakeDensityScale));
        d_->lbl_wake_density = new QLabel();
        host->addWidget(d_->sld_wake_density, 1);
        host->addWidget(d_->lbl_wake_density, 0);
        grid->addLayout(host, row++, 1);
    }

    add_label(QStringLiteral("Hold Intensity"), row);
    {
        auto* host = new QHBoxLayout();
        d_->sld_hold_intensity = new QSlider(Qt::Horizontal);
        d_->sld_hold_intensity->setObjectName(QStringLiteral("main_sld_hold_intensity"));
        d_->sld_hold_intensity->setRange(
            static_cast<int>(ptd::ClickConfig::kMinHoldIntensity * kHoldIntensityScale),
            static_cast<int>(ptd::ClickConfig::kMaxHoldIntensity * kHoldIntensityScale));
        d_->lbl_hold_intensity = new QLabel();
        host->addWidget(d_->sld_hold_intensity, 1);
        host->addWidget(d_->lbl_hold_intensity, 0);
        grid->addLayout(host, row++, 1);
    }

    add_label(QStringLiteral("Start with Windows"), row);
    d_->chk_start_with_windows =
        add_toggle(QString(), QStringLiteral("main_chk_start_with_windows"));

    d_->btn_advanced_settings = new QPushButton(QStringLiteral("Advanced Settings..."));
    d_->btn_advanced_settings->setObjectName(QStringLiteral("main_btn_advanced_settings"));
    d_->btn_restore_defaults = new QPushButton(QStringLiteral("Restore Defaults"));
    d_->btn_restore_defaults->setObjectName(QStringLiteral("main_btn_restore_defaults"));
    auto* actions = new QHBoxLayout();
    actions->addWidget(d_->btn_advanced_settings);
    actions->addWidget(d_->btn_restore_defaults);
    // Developer convenience only: the SAME controller operation the Settings
    // developer panel publishes. is_dev_build() is the one authority for
    // "is this a developer build", so the action cannot appear in an ordinary
    // production Release build. The visible label is short; the tooltip makes
    // the meaning unambiguous.
    if (ptd::is_dev_build()) {
        d_->btn_set_defaults = new QPushButton(QStringLiteral("Set Defaults"));
        d_->btn_set_defaults->setObjectName(QStringLiteral("main_btn_set_defaults"));
        d_->btn_set_defaults->setToolTip(QStringLiteral(
            "Developer: set the current configuration as the canonical "
            "Release Defaults"));
        actions->addWidget(d_->btn_set_defaults);
    }
    root->addLayout(actions);

    // Combo population is programmatic and emits nothing.
    {
        static const QStringList kTrailStyles = {
            "Classic", "Soft Glow", "Comet", "Neon",
            "Dotted", "Pulse", "Ribbon", "Spark"};
        static const QStringList kSparkleModes = {
            "Off", "Stardust", "Twinkle", "Glitter", "Firefly", "Shards"};
        static const QStringList kClickStyles = {
            "Ring", "Double Ring", "Ripple", "Burst",
            "Spark Burst", "Soft Flash", "Dot + Ring",
            "Air", "Fire", "Water", "Earth"};
        QSignalBlocker b1(d_->cmb_trail_style);
        QSignalBlocker b2(d_->cmb_sparkle_mode);
        QSignalBlocker b3(d_->cmb_click_style);
        d_->cmb_trail_style->addItems(kTrailStyles);
        d_->cmb_sparkle_mode->addItems(kSparkleModes);
        d_->cmb_click_style->addItems(kClickStyles);
    }

    // One user action -> exactly one publication. toggled() fires only on a
    // real change; combo currentIndexChanged fires only on a real change; the
    // sliders emit live and Application debounces persistence (PERF-002).
    connect(d_->chk_master, &QCheckBox::toggled,
            this, &MainWindow::on_master_toggled);
    connect(d_->chk_trail, &QCheckBox::toggled,
            this, &MainWindow::on_child_toggled);
    connect(d_->chk_click, &QCheckBox::toggled,
            this, &MainWindow::on_child_toggled);
    connect(d_->chk_hold, &QCheckBox::toggled,
            this, &MainWindow::on_child_toggled);
    connect(d_->chk_motion_wake, &QCheckBox::toggled,
            this, &MainWindow::on_motion_wake_toggled);
    connect(d_->chk_start_with_windows, &QCheckBox::toggled,
            this, &MainWindow::on_start_with_windows_toggled);
    connect(d_->cmb_trail_style, &QComboBox::currentIndexChanged,
            this, &MainWindow::on_trail_style_changed);
    connect(d_->cmb_sparkle_mode, &QComboBox::currentIndexChanged,
            this, &MainWindow::on_sparkle_mode_changed);
    connect(d_->cmb_click_style, &QComboBox::currentIndexChanged,
            this, &MainWindow::on_click_style_changed);
    connect(d_->sld_wake_density, &QSlider::valueChanged,
            this, &MainWindow::on_wake_density_changed);
    connect(d_->sld_hold_intensity, &QSlider::valueChanged,
            this, &MainWindow::on_hold_intensity_changed);
    connect(d_->btn_advanced_settings, &QPushButton::clicked,
            this, &MainWindow::on_advanced_settings);
    connect(d_->btn_restore_defaults, &QPushButton::clicked,
            this, &MainWindow::on_restore_defaults);
    if (d_->btn_set_defaults) {
        connect(d_->btn_set_defaults, &QPushButton::clicked,
                this, &MainWindow::on_set_current_as_release_defaults);
    }

    set_from_app_config(d_->cfg);
}

MainWindow::~MainWindow() = default;

void MainWindow::set_from_app_config(const ptd::AppConfig& cfg) {
    const ptd::AppConfig v = ptd::AppConfig::validated(cfg);
    d_->cfg = v;

    QSignalBlocker bm(d_->chk_master);
    QSignalBlocker bt(d_->chk_trail);
    QSignalBlocker bc(d_->chk_click);
    QSignalBlocker bh(d_->chk_hold);
    QSignalBlocker bw(d_->chk_motion_wake);
    QSignalBlocker bs(d_->chk_start_with_windows);
    QSignalBlocker b1(d_->cmb_trail_style);
    QSignalBlocker b2(d_->cmb_sparkle_mode);
    QSignalBlocker b3(d_->cmb_click_style);
    QSignalBlocker b4(d_->sld_wake_density);
    QSignalBlocker b5(d_->sld_hold_intensity);

    d_->chk_master->setChecked(v.master_enabled);
    d_->chk_trail->setChecked(v.trail.enabled);
    d_->chk_click->setChecked(v.click.enabled);
    d_->chk_hold->setChecked(v.click.hold_enabled);
    d_->chk_motion_wake->setChecked(v.click.hold_wake_enabled);
    d_->chk_start_with_windows->setChecked(v.start_with_windows);

    d_->cmb_trail_style->setCurrentIndex(static_cast<int>(v.trail.style));
    d_->cmb_sparkle_mode->setCurrentIndex(static_cast<int>(v.trail.sparkle_mode));
    d_->cmb_click_style->setCurrentIndex(static_cast<int>(v.click.style));

    d_->sld_wake_density->setValue(
        static_cast<int>(v.click.hold_wake_density * kWakeDensityScale));
    d_->sld_hold_intensity->setValue(
        static_cast<int>(v.click.hold_intensity * kHoldIntensityScale));
    d_->lbl_wake_density->setText(
        QStringLiteral("%1%").arg(d_->sld_wake_density->value()));
    d_->lbl_hold_intensity->setText(
        QStringLiteral("%1%").arg(d_->sld_hold_intensity->value()));

    // Contextual enablement follows the snapshot (silent: never publishes).
    const bool master = v.master_enabled;
    const bool hold = master && v.click.enabled && v.click.hold_enabled;
    const bool wake = hold && v.click.hold_wake_enabled;
    d_->chk_trail->setEnabled(master);
    d_->chk_click->setEnabled(master);
    d_->chk_hold->setEnabled(master && v.click.enabled);
    d_->chk_motion_wake->setEnabled(hold);
    d_->cmb_trail_style->setEnabled(master && v.trail.enabled);
    d_->cmb_sparkle_mode->setEnabled(master && v.trail.enabled);
    d_->cmb_click_style->setEnabled(master && v.click.enabled);
    d_->sld_wake_density->setEnabled(wake);
    d_->sld_hold_intensity->setEnabled(hold);
}

void MainWindow::update_enable_states() {
    const bool master = d_->cfg.master_enabled;
    const bool hold = master && d_->cfg.click.enabled && d_->cfg.click.hold_enabled;
    const bool wake = hold && d_->cfg.click.hold_wake_enabled;
    d_->chk_trail->setEnabled(master);
    d_->chk_click->setEnabled(master);
    d_->chk_hold->setEnabled(master && d_->cfg.click.enabled);
    d_->chk_motion_wake->setEnabled(hold);
    d_->cmb_trail_style->setEnabled(master && d_->cfg.trail.enabled);
    d_->cmb_sparkle_mode->setEnabled(master && d_->cfg.trail.enabled);
    d_->cmb_click_style->setEnabled(master && d_->cfg.click.enabled);
    d_->sld_wake_density->setEnabled(wake);
    d_->sld_hold_intensity->setEnabled(hold);
}

void MainWindow::on_master_toggled(bool on) {
    d_->cfg.master_enabled = on;
    update_enable_states();
    emit master_enabled_changed(on);
}

void MainWindow::on_child_toggled(bool) {
    QObject* source = sender();
    if (source == d_->chk_trail) {
        d_->cfg.trail.enabled = d_->chk_trail->isChecked();
        update_enable_states();
        emit trail_enabled_changed(d_->cfg.trail.enabled);
        emit_trail();
    } else if (source == d_->chk_click) {
        d_->cfg.click.enabled = d_->chk_click->isChecked();
        update_enable_states();
        emit click_enabled_changed(d_->cfg.click.enabled);
        emit_click();
    } else if (source == d_->chk_hold) {
        d_->cfg.click.hold_enabled = d_->chk_hold->isChecked();
        update_enable_states();
        emit_click();
    }
}

void MainWindow::on_trail_style_changed(int index) {
    d_->cfg.trail.style = static_cast<ptd::TrailStyle>(index);
    emit_trail();
}

void MainWindow::on_sparkle_mode_changed(int index) {
    d_->cfg.trail.sparkle_mode = static_cast<ptd::TrailSparkleMode>(index);
    emit_trail();
}

void MainWindow::on_click_style_changed(int index) {
    d_->cfg.click.style = static_cast<ptd::ClickStyle>(index);
    emit_click();
}

void MainWindow::on_wake_density_changed(double) {
    d_->cfg.click.hold_wake_density =
        static_cast<float>(d_->sld_wake_density->value() / kWakeDensityScale);
    d_->lbl_wake_density->setText(
        QStringLiteral("%1%").arg(d_->sld_wake_density->value()));
    emit_click();
}

void MainWindow::on_hold_intensity_changed(double) {
    d_->cfg.click.hold_intensity =
        static_cast<float>(d_->sld_hold_intensity->value() / kHoldIntensityScale);
    d_->lbl_hold_intensity->setText(
        QStringLiteral("%1%").arg(d_->sld_hold_intensity->value()));
    emit_click();
}

void MainWindow::on_motion_wake_toggled(bool on) {
    d_->cfg.click.hold_wake_enabled = on;
    update_enable_states();
    emit_click();
}

void MainWindow::on_start_with_windows_toggled(bool on) {
    d_->cfg.start_with_windows = on;
    emit start_with_windows_changed(on);
}

void MainWindow::emit_trail() {
    d_->cfg.trail = ptd::TrailConfig::validated(d_->cfg.trail);
    emit trail_config_changed(d_->cfg.trail);
}

void MainWindow::emit_click() {
    d_->cfg.click = ptd::ClickConfig::validated(d_->cfg.click);
    emit click_config_changed(d_->cfg.click);
}

void MainWindow::on_advanced_settings() {
    emit advanced_settings_requested();
}

void MainWindow::on_restore_defaults() {
    emit restore_defaults_requested();
}

void MainWindow::on_set_current_as_release_defaults() {
    emit set_current_as_release_defaults_requested();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}

} // namespace ui
} // namespace ptd
