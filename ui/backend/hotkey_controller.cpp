#include "ui/backend/hotkey_controller.h"

#include "shell/global_hotkeys.h"
#include "ui/backend/singleton_provider.h"

#include <QQuickWindow>

namespace subrec::ui {

namespace {
HotkeyController *s_instance = nullptr;
}

HotkeyController::HotkeyController(QObject *parent) : QObject(parent) {}
HotkeyController::~HotkeyController()
{
    if (s_instance == this) s_instance = nullptr;
}

void HotkeyController::setInstance(HotkeyController *instance) { s_instance = instance; }
HotkeyController *HotkeyController::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(H3): load/persist hotkey/*, register with GlobalHotkeys, key capture filter.
void HotkeyController::attach(shell::GlobalHotkeys *hotkeys, QQuickWindow *window)
{
    hotkeys_ = hotkeys;
    window_ = window;
}
QStringList HotkeyController::startupConflicts() const { return {}; }

QString HotkeyController::toggleRecordText() const { return QStringLiteral("Ctrl + Alt + R"); }
QString HotkeyController::togglePauseText() const { return QStringLiteral("Ctrl + Alt + P"); }
QString HotkeyController::toggleWindowText() const { return {}; }
QString HotkeyController::toggleRecordStatus() const { return QStringLiteral("unsupported"); }
QString HotkeyController::togglePauseStatus() const { return QStringLiteral("unsupported"); }
QString HotkeyController::toggleWindowStatus() const { return QStringLiteral("unset"); }
int HotkeyController::capturingAction() const { return -1; }
QString HotkeyController::captureText() const { return {}; }

void HotkeyController::beginCapture(int) {}
void HotkeyController::cancelCapture() {}
void HotkeyController::clear(int) {}
void HotkeyController::restoreDefaults() {}
QString HotkeyController::shortcutLabel(int) const { return {}; }

} // namespace subrec::ui
