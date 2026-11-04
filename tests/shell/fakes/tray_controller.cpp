// TEST ONLY: lets the controller exercise W3 on offscreen CI without a desktop tray.
#include "shell/tray_controller.h"
#include <QMenu>
namespace subrec::shell {
class TrayController::Private { public: QMenu menu; };
TrayController::TrayController(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}
TrayController::~TrayController() = default;
bool TrayController::systemTrayAvailable() { return true; }
void TrayController::setEnabled(bool) {}
void TrayController::update(const ShellSnapshot &) {}
void TrayController::notify(NotifyId, const QString &, const QString &, QSystemTrayIcon::MessageIcon) {}
QMenu *TrayController::menu() const { return &d->menu; }
void TrayController::setFloatingBarItem(bool, bool) {}
void TrayController::setShortcutTexts(const QString &, const QString &, const QString &) {}
}
