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

ShellView::ShellView(QObject *parent) : QObject(parent)
{
}
ShellView::~ShellView()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void ShellView::setInstance(ShellView *instance)
{
    s_instance = instance;
}
ShellView *ShellView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B2): connect WindowController/ShellSettings signals and forward every call.
void ShellView::attach(shell::WindowController *controller, shell::ShellSettings *settings, bool trayAvailable)
{
    if (controller_)
        disconnect(controller_, nullptr, this, nullptr);
    if (settings_)
        disconnect(settings_, nullptr, this, nullptr);
    controller_ = controller;
    settings_ = settings;
    trayAvailable_ = trayAvailable;
    if (controller_) {
        connect(controller_, &shell::WindowController::windowShownChanged, this, &ShellView::windowShownChanged);
        connect(controller_, &shell::WindowController::closeChoiceRequested, this, &ShellView::closeChoiceRequested);
        connect(controller_, &shell::WindowController::exitConfirmRequested, this, &ShellView::exitConfirmRequested);
        connect(controller_, &shell::WindowController::settingsPageRequested, this, &ShellView::settingsPageRequested);
        connect(controller_, &shell::WindowController::startRejected, this, &ShellView::startRejected);
        connect(controller_, &shell::WindowController::pageRequested, this, &ShellView::pageRequested);
    }
    if (settings_)
        connect(settings_, &shell::ShellSettings::changed, this, &ShellView::settingsChanged);
    emit windowShownChanged();
    emit settingsChanged();
}
bool ShellView::windowShown() const
{
    return controller_ ? controller_->windowShown() : true;
}
bool ShellView::trayAvailable() const
{
    return trayAvailable_;
}
bool ShellView::trayEnabled() const
{
    return settings_ ? settings_->trayEnabled() : true;
}
void ShellView::setTrayEnabled(bool value)
{
    if (settings_)
        settings_->setTrayEnabled(value);
}
bool ShellView::minimizeToTray() const
{
    return settings_ ? settings_->minimizeToTray() : false;
}
void ShellView::setMinimizeToTray(bool value)
{
    if (settings_)
        settings_->setMinimizeToTray(value);
}
QString ShellView::closeAction() const
{
    return settings_ ? settings_->closeActionText() : QStringLiteral("ask");
}
void ShellView::setCloseAction(const QString &value)
{
    if (settings_)
        settings_->setCloseActionText(value);
}
bool ShellView::startMinimized() const
{
    return settings_ ? settings_->startMinimized() : false;
}
void ShellView::setStartMinimized(bool value)
{
    if (settings_)
        settings_->setStartMinimized(value);
}
QString ShellView::floatingBar() const
{
    return settings_ ? settings_->floatingBarText() : QStringLiteral("whenHidden");
}
void ShellView::setFloatingBar(const QString &value)
{
    if (settings_)
        settings_->setFloatingBarText(value);
}
QString ShellView::excludeFromCapture() const
{
    return settings_ ? settings_->excludeText() : QStringLiteral("recording");
}
void ShellView::setExcludeFromCapture(const QString &value)
{
    if (settings_)
        settings_->setExcludeText(value);
}
bool ShellView::hideOnRecordStart() const
{
    return settings_ ? settings_->hideOnRecordStart() : false;
}
void ShellView::setHideOnRecordStart(bool value)
{
    if (settings_)
        settings_->setHideOnRecordStart(value);
}
bool ShellView::confirmExitWhileRecording() const
{
    return settings_ ? settings_->confirmExit() : true;
}
void ShellView::setConfirmExitWhileRecording(bool value)
{
    if (settings_)
        settings_->setConfirmExit(value);
}
bool ShellView::excludeSupported() const
{
    return settings_ ? settings_->excludeSupported() : false;
}
void ShellView::requestClose()
{
    if (controller_)
        controller_->requestClose();
}
void ShellView::requestMinimize()
{
    if (controller_)
        controller_->requestMinimize();
}
void ShellView::confirmExit()
{
    if (controller_)
        controller_->confirmExit();
}
void ShellView::cancelExit()
{
    if (controller_)
        controller_->cancelExit();
}
void ShellView::requestStart()
{
    if (controller_)
        controller_->requestStart();
}
void ShellView::requestStop()
{
    if (controller_)
        controller_->requestStop();
}
void ShellView::requestPause()
{
    if (controller_)
        controller_->requestPause();
}
void ShellView::requestResume()
{
    if (controller_)
        controller_->requestResume();
}
void ShellView::requestStartStop()
{
    if (controller_)
        controller_->requestStartStop();
}
void ShellView::requestPauseResume()
{
    if (controller_)
        controller_->requestPauseResume();
}
void ShellView::showMainWindow()
{
    if (controller_)
        controller_->showMainWindow();
}
void ShellView::resolveCloseChoice(bool toTray, bool remember)
{
    if (controller_)
        controller_->resolveCloseChoice(toTray, remember);
}

} // namespace subrec::ui
