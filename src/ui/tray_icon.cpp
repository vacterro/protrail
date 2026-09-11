#include "tray_icon.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPixmap>
#include <QPainter>
#include <QPolygonF>
#include <QPointF>

namespace ptd {
namespace ui {

QIcon TrayIcon::create_icon(bool enabled) {
    QIcon icon;
    for (int size : {16, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        // Gold palette for enabled, neutral gray for disabled
        const QColor fill = enabled ? QColor(212, 160, 23) : QColor(120, 120, 120);
        const QColor border = enabled ? QColor(255, 223, 70) : QColor(160, 160, 160);

        const qreal s = size / 16.0;
        p.setPen(QPen(border, 1.0 * s));
        p.setBrush(fill);

        // Arrow cursor shape pointing top-left
        QPolygonF poly;
        poly << QPointF(3.0 * s, 2.0 * s)
             << QPointF(13.0 * s, 8.0 * s)
             << QPointF(8.5 * s, 9.5 * s)
             << QPointF(6.0 * s, 14.0 * s);
        p.drawPolygon(poly);

        icon.addPixmap(pm);
    }
    return icon;
}

TrayIcon::TrayIcon(bool master_enabled, QObject* parent)
    : QObject(parent),
      master_enabled_(master_enabled),
      tray_icon_(std::make_unique<QSystemTrayIcon>(this)),
      menu_(std::make_unique<QMenu>()) {

    // Action 1: Settings
    action_settings_ = menu_->addAction(QStringLiteral("Settings"));
    connect(action_settings_, &QAction::triggered, this, &TrayIcon::settings_requested);

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
                    emit settings_requested();
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

QAction* TrayIcon::action_settings() const {
    return action_settings_;
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