#ifndef SUBREC_SHELL_TRAY_CONTROLLER_H
#define SUBREC_SHELL_TRAY_CONTROLLER_H

#include "shell/shell_types.h"

#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

QT_BEGIN_NAMESPACE
class QMenu;
QT_END_NAMESPACE

namespace subrec::shell {

// GUI thread only; requires QApplication. Emits intents, never calls recorder
// actions. WindowController connects startStopRequested to requestStartStop.
class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;
    static bool systemTrayAvailable();
    static bool notificationsSupported();
    void setEnabled(bool enabled);
    void update(const ShellSnapshot &snapshot);
    // WindowController must check notificationAllowed before calling.
    void notify(NotifyId id, const QString &title, const QString &body,
                QSystemTrayIcon::MessageIcon icon);
    QMenu *menu() const; // Borrowed, owned by this object; for addMenu().

    // Batch 2 (module H4). Stubs until H4 lands.
    // Menu item 6 "显示悬浮条" (§3.3): visible only in capturing states and when the floating
    // bar is not configured "never" by an unsupported system; checked = bar currently shown.
    void setFloatingBarItem(bool visible, bool checked);
    // Right-aligned shortcut texts on items 1/3/4 (§6.2), compact form "Ctrl+Alt+R"; "" hides.
    void setShortcutTexts(const QString &toggleWindow, const QString &startStop,
                          const QString &pauseResume);

signals:
    void floatingBarToggled(bool checked); // batch 2: item 6, this recording only
    void toggleWindowRequested();
    void startStopRequested();
    void pauseResumeRequested();
    void openOutputFolderRequested();
    void openSettingsRequested();
    void exitRequested();
    void notificationClicked(subrec::shell::NotifyId id);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

#endif // SUBREC_SHELL_TRAY_CONTROLLER_H
