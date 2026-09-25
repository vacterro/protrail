#pragma once

#include <QObject>
#include <QIcon>
#include <memory>

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace ptd {
namespace ui {

// MVP 09: Native system tray icon and lifecycle controller.
// Maintains canonical master-enabled state sync with the ProTrail window,
// provides hide-on-close restore, and explicit application exit.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(bool master_enabled = true, QObject* parent = nullptr);
    ~TrayIcon() override;

    bool is_master_enabled() const;
    void set_master_enabled(bool enabled);

    void show();
    void hide();
    bool is_visible() const;

    QString tooltip() const;
    QString toggle_action_text() const;

    QAction* action_home() const;
    QAction* action_toggle() const;
    QAction* action_exit() const;
    QMenu* menu() const;
    QSystemTrayIcon* system_tray_icon() const;

    static QIcon create_icon(bool enabled);

signals:
    void home_requested();
    void master_enabled_toggled(bool enabled);
    void exit_requested();

private slots:
    void on_toggle_triggered();

private:
    void update_visual_state();

    bool master_enabled_ = true;
    std::unique_ptr<QSystemTrayIcon> tray_icon_;
    std::unique_ptr<QMenu> menu_;
    QAction* action_home_ = nullptr;
    QAction* action_toggle_ = nullptr;
    QAction* action_exit_ = nullptr;
};

} // namespace ui
} // namespace ptd