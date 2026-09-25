#include "tray_icon.h"

#include "branding.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

namespace ptd {
namespace ui {

// One icon authority: the product icon resource when present, otherwise the
// generated development fallback (see branding.h).
QIcon TrayIcon::create_icon(bool enabled) {
    return branding::tray_icon(enabled);
}

TrayIcon::TrayIcon(bool master_enabled, QObject* parent)
    : QObject(parent),
      master_enabled_(master_enabled),
      tray_icon_(std::make_unique<QSystemTrayIcon>(this)),
      menu_(std::make_unique<QMenu>()) {

    // One product surface. Tray navigation never forks into a second editor.
    action_home_ = menu_->addAction(QStringLiteral("Open ProTrail"));
    connect(action_home_, &QAction::triggered, this, &TrayIcon::home_requested);

    // Action 2: Enable / Disable
    action_toggle_ = menu_->addAction(master_enabled_ ? QStringLiteral("Disable") : QStringLiteral("Enable"));
    connect(action_toggle_, &QAction::triggered, this, &TrayIcon::on_toggle_triggered);

    menu_->addSeparator();

    // Action 3: Exit
    action_exit_ = menu_->addAction(QStringLiteral("Exit"));
    connect(action_exit_, &QAction::triggered, this, &TrayIcon::exit_requested);

    tray_icon_->setContextMenu(menu_.get());

    connect(tray_icon_.get(), &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    emit home_requested();
                }
            });

    update_visual_state();
}

TrayIcon::~TrayIcon() {
    hide();
}

bool TrayIcon::is_master_enabled() const {
    return master_enabled_;
}

void TrayIcon::set_master_enabled(bool enabled) {
    if (master_enabled_ == enabled) return;
    master_enabled_ = enabled;
    update_visual_state();
}

void TrayIcon::update_visual_state() {
    // Phase 4:
    // Enabled: "ProTrail — Enabled"
    // Disabled: "ProTrail — Disabled"
    const QString tip = master_enabled_
        ? QString::fromUtf8("ProTrail \u2014 Enabled")
        : QString::fromUtf8("ProTrail \u2014 Disabled");

    tray_icon_->setToolTip(tip);
    tray_icon_->setIcon(create_icon(master_enabled_));

    if (action_toggle_) {
        action_toggle_->setText(master_enabled_ ? QStringLiteral("Disable") : QStringLiteral("Enable"));
    }
}

void TrayIcon::show() {
    tray_icon_->show();
}

void TrayIcon::hide() {
    tray_icon_->hide();
}

bool TrayIcon::is_visible() const {
    return tray_icon_->isVisible();
}

QString TrayIcon::tooltip() const {
    return tray_icon_->toolTip();
}

QString TrayIcon::toggle_action_text() const {
    return action_toggle_ ? action_toggle_->text() : QString();
}

QAction* TrayIcon::action_home() const {
    return action_home_;
}

QAction* TrayIcon::action_toggle() const {
    return action_toggle_;
}

QAction* TrayIcon::action_exit() const {
    return action_exit_;
}

QMenu* TrayIcon::menu() const {
    return menu_.get();
}

QSystemTrayIcon* TrayIcon::system_tray_icon() const {
    return tray_icon_.get();
}

void TrayIcon::on_toggle_triggered() {
    set_master_enabled(!master_enabled_);
    emit master_enabled_toggled(master_enabled_);
}

} // namespace ui
} // namespace ptd