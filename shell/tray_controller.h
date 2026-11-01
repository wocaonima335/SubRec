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
    void setEnabled(bool enabled);
    void update(const ShellSnapshot &snapshot);
    // WindowController must check notificationAllowed before calling.
    void notify(NotifyId id, const QString &title, const QString &body,
                QSystemTrayIcon::MessageIcon icon);
    QMenu *menu() const; // Borrowed, owned by this object; for addMenu().

signals:
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
