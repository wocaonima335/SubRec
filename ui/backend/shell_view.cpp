#include "ui/backend/shell_view.h"

#include "shell/shell_settings.h"
#include "shell/window_controller.h"
#include "ui/backend/singleton_provider.h"

// Both shell headers are Qt-only. The stub calls nothing in them; the B2 implementation
// does and then needs shell_module at link time (ui/CMakeLists.txt links it when present).

namespace subrec::ui {

namespace {
ShellView *s_instance = nullptr;
}

ShellView::ShellView(QObject *parent) : QObject(parent) {}
ShellView::~ShellView()
{
    if (s_instance == this) s_instance = nullptr;
}

void ShellView::setInstance(ShellView *instance) { s_instance = instance; }
ShellView *ShellView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B2): connect WindowController/ShellSettings signals and forward every call.
void ShellView::attach(shell::WindowController *controller, shell::ShellSettings *settings,
                       bool trayAvailable)
{
    controller_ = controller;
    settings_ = settings;
    trayAvailable_ = trayAvailable;
}

bool ShellView::windowShown() const { return true; }
bool ShellView::trayAvailable() const { return trayAvailable_; }
bool ShellView::trayEnabled() const { return true; }
void ShellView::setTrayEnabled(bool) {}
bool ShellView::minimizeToTray() const { return false; }
void ShellView::setMinimizeToTray(bool) {}
QString ShellView::closeAction() const { return QStringLiteral("ask"); }
void ShellView::setCloseAction(const QString &) {}
bool ShellView::startMinimized() const { return false; }
void ShellView::setStartMinimized(bool) {}
QString ShellView::floatingBar() const { return QStringLiteral("whenHidden"); }
void ShellView::setFloatingBar(const QString &) {}
QString ShellView::excludeFromCapture() const { return QStringLiteral("recording"); }
void ShellView::setExcludeFromCapture(const QString &) {}
bool ShellView::hideOnRecordStart() const { return false; }
void ShellView::setHideOnRecordStart(bool) {}
bool ShellView::confirmExitWhileRecording() const { return true; }
void ShellView::setConfirmExitWhileRecording(bool) {}
bool ShellView::excludeSupported() const { return false; }

void ShellView::requestClose() {}
void ShellView::requestMinimize() {}
void ShellView::resolveCloseChoice(bool, bool) {}
void ShellView::confirmExit() {}
void ShellView::cancelExit() {}
void ShellView::requestStart() {}
void ShellView::requestStop() {}
void ShellView::requestPause() {}
void ShellView::requestResume() {}
void ShellView::requestStartStop() {}
void ShellView::requestPauseResume() {}
void ShellView::showMainWindow() {}

} // namespace subrec::ui
