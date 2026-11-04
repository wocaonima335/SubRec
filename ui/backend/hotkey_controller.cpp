#include "ui/backend/hotkey_controller.h"

#include "shell/global_hotkeys.h"
#include "ui/backend/settings_store.h"
#include "ui/backend/singleton_provider.h"

#include <QKeyEvent>
#include <QQuickWindow>

namespace subrec::ui {

namespace {
HotkeyController *s_instance = nullptr;

QString actionKey(int action)
{
    return action == 0 ? QStringLiteral("toggleRecord")
                       : action == 1 ? QStringLiteral("togglePause") : QStringLiteral("toggleWindow");
}

QString defaultText(int action)
{
    return action == 0 ? shell::defaultToggleRecordText()
                       : action == 1 ? shell::defaultTogglePauseText()
                                     : shell::defaultToggleWindowText();
}

QString statusText(shell::RegisterResult status, bool empty)
{
    if (empty)
        return QStringLiteral("unset");
    switch (status) {
    case shell::RegisterResult::Ok: return QStringLiteral("ok");
    case shell::RegisterResult::Conflict: return QStringLiteral("conflict");
    case shell::RegisterResult::Unsupported: return QStringLiteral("unsupported");
    default: return QStringLiteral("unsupported");
    }
}
} // namespace

HotkeyController::HotkeyController(QObject *parent)
    : QObject(parent)
{
    statuses_.fill(shell::RegisterResult::Unsupported);
    for (int action = 0; action < 3; ++action)
        bindings_[action] = shell::parseChord(defaultText(action));
}

HotkeyController::~HotkeyController()
{
    cancelCapture();
    if (window_)
        window_->removeEventFilter(this);
    if (s_instance == this)
        s_instance = nullptr;
}

void HotkeyController::setInstance(HotkeyController *instance)
{
    s_instance = instance;
}

HotkeyController *HotkeyController::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(H3): load/persist hotkey/*, register with GlobalHotkeys, key capture filter.
void HotkeyController::attach(shell::GlobalHotkeys *hotkeys, QQuickWindow *window)
{
    cancelCapture();
    if (hotkeys_)
        disconnect(hotkeys_, nullptr, this, nullptr);
    if (window_)
        window_->removeEventFilter(this);
    hotkeys_ = hotkeys;
    statuses_.fill(shell::RegisterResult::Unsupported);
    window_ = window;
    if (window_)
        window_->installEventFilter(this);

    if (hotkeys_) {
        connect(hotkeys_, &shell::GlobalHotkeys::statusChanged, this, [this](shell::HotkeyAction action) {
            statuses_[static_cast<int>(action)] = hotkeys_->status(action);
            emit bindingsChanged();
        });
    }
    const auto settings = openUiSettings();
    for (int action = 0; action < 3; ++action) {
        bindings_[action] = shell::parseChord(settings->value(
            QStringLiteral("hotkey/") + actionKey(action), defaultText(action)).toString());
        if (hotkeys_)
            statuses_[action] = hotkeys_->setBinding(static_cast<shell::HotkeyAction>(action),
                                                     bindings_[action]);
    }
    emit bindingsChanged();
}

QStringList HotkeyController::startupConflicts() const
{
    QStringList conflicts;
    for (int action = 0; action < 3; ++action) {
        if (statuses_[action] == shell::RegisterResult::Conflict)
            conflicts << shell::displayText(bindings_[action]);
    }
    return conflicts;
}

QString HotkeyController::toggleRecordText() const
{
    return shell::displayText(bindings_[0]);
}
QString HotkeyController::togglePauseText() const
{
    return shell::displayText(bindings_[1]);
}
QString HotkeyController::toggleWindowText() const
{
    return shell::displayText(bindings_[2]);
}
QString HotkeyController::toggleRecordStatus() const
{
    return statusText(statuses_[0], bindings_[0].isEmpty());
}
QString HotkeyController::togglePauseStatus() const
{
    return statusText(statuses_[1], bindings_[1].isEmpty());
}
QString HotkeyController::toggleWindowStatus() const
{
    return statusText(statuses_[2], bindings_[2].isEmpty());
}
int HotkeyController::capturingAction() const
{
    return capturing_;
}
QString HotkeyController::captureText() const
{
    return captureText_;
}

void HotkeyController::beginCapture(int action)
{
    if (action < 0 || action > 2)
        return;
    cancelCapture();
    capturing_ = action;
    captureText_.clear();
    if (hotkeys_)
        hotkeys_->suspend();
    emit captureChanged();
}

void HotkeyController::finishCapture(int result, const QString &message)
{
    if (capturing_ < 0)
        return;
    const int action = capturing_;
    capturing_ = -1;
    captureText_.clear();
    if (hotkeys_)
        hotkeys_->resume();
    emit captureChanged();
    emit captureFinished(action, result, message);
}

void HotkeyController::cancelCapture()
{
    finishCapture(Cancelled);
}

void HotkeyController::saveBinding(int action, const shell::KeyChord &chord)
{
    bindings_[action] = chord;
    openUiSettings()->setValue(QStringLiteral("hotkey/") + actionKey(action), shell::storageText(chord));
    statuses_[action] = hotkeys_ ? hotkeys_->setBinding(static_cast<shell::HotkeyAction>(action), chord)
                                : chord.isEmpty() ? shell::RegisterResult::Unset : shell::RegisterResult::Unsupported;
    emit bindingsChanged();
}

void HotkeyController::clear(int action)
{
    if (action < 0 || action > 2)
        return;
    saveBinding(action, {});
    if (capturing_ == action)
        finishCapture(Cleared);
    else
        emit captureFinished(action, Cleared, {});
}

void HotkeyController::restoreDefaults()
{
    cancelCapture();
    for (int action = 0; action < 3; ++action)
        saveBinding(action, shell::parseChord(defaultText(action)));
}

QString HotkeyController::shortcutLabel(int action) const
{
    return action >= 0 && action < 3 ? shell::compactText(bindings_[action]) : QString();
}

bool HotkeyController::eventFilter(QObject *, QEvent *event)
{
    if (capturing_ < 0)
        return false;
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        cancelCapture();
        return false;
    }
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->isAutoRepeat())
        return true;
    if (key->key() == Qt::Key_Escape) {
        cancelCapture();
        return true;
    }
    if (event->type() == QEvent::KeyPress
        && (key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete)) {
        clear(capturing_);
        return true;
    }
    quint32 modifiers = shell::ModNone;
    if (key->modifiers() & Qt::ControlModifier)
        modifiers |= shell::ModCtrl;
    if (key->modifiers() & Qt::AltModifier)
        modifiers |= shell::ModAlt;
    if (key->modifiers() & Qt::ShiftModifier)
        modifiers |= shell::ModShift;
    if (key->modifiers() & Qt::MetaModifier)
        modifiers |= shell::ModWin;
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Alt
        || key->key() == Qt::Key_Shift || key->key() == Qt::Key_Meta) {
        const quint32 bit = key->key() == Qt::Key_Control ? shell::ModCtrl
            : key->key() == Qt::Key_Alt ? shell::ModAlt
            : key->key() == Qt::Key_Shift ? shell::ModShift : shell::ModWin;
        if (event->type() == QEvent::KeyPress)
            modifiers |= bit;
        else
            modifiers &= ~bit;
        captureText_ = shell::displayText({modifiers, 'R'});
        captureText_.chop(1);
        captureText_ = modifiers ? captureText_ + QStringLiteral("…") : QString();
        emit captureChanged();
        return true;
    }
    if (event->type() == QEvent::KeyRelease)
        return true;
    quint32 virtualKey = key->nativeVirtualKey();
#ifndef Q_OS_WIN
    // Qt key fallback makes the editor usable on unsupported platforms and in offscreen tests.
    virtualKey = 0;
    if (key->key() >= Qt::Key_A && key->key() <= Qt::Key_Z)
        virtualKey = key->key();
    if (key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9)
        virtualKey = (key->modifiers() & Qt::KeypadModifier) ? 0x60 + key->key() - Qt::Key_0 : key->key();
    if (key->key() >= Qt::Key_F1 && key->key() <= Qt::Key_F24)
        virtualKey = 0x70 + key->key() - Qt::Key_F1;
#endif
    const shell::KeyChord chord{modifiers, virtualKey};
    const auto validity = shell::validateChord(chord);
    if (validity != shell::ChordValidity::Ok && validity != shell::ChordValidity::OkFunctionKeyAlone) {
        finishCapture(Invalid, QStringLiteral("需要带 Ctrl 或 Alt；不能使用 Win 键和 F12"));
        return true;
    }
    const QStringList actionNames{QStringLiteral("开始/停止录制"), QStringLiteral("暂停/继续录制"), QStringLiteral("显示/隐藏窗口")};
    for (int other = 0; other < 3; ++other) {
        if (other != capturing_ && bindings_[other] == chord) {
            finishCapture(Duplicate, QStringLiteral("已用于'%1'").arg(actionNames.at(other)));
            return true;
        }
    }
    const int action = capturing_;
    saveBinding(action, chord);
    // Registration while suspended is deferred; resume before reading its result.
    if (hotkeys_)
        hotkeys_->resume();
    if (hotkeys_)
        statuses_[action] = hotkeys_->status(static_cast<shell::HotkeyAction>(action));
    // Balance finishCapture's resume without taking another platform registration pass.
    capturing_ = -1;
    captureText_.clear();
    emit captureChanged();
    const bool conflict = statuses_[action] == shell::RegisterResult::Conflict;
    emit captureFinished(action, conflict ? Conflict
        : validity == shell::ChordValidity::OkFunctionKeyAlone ? SavedWithNotice : Saved,
        conflict ? QStringLiteral("已被其他程序占用，请换一个组合")
        : validity == shell::ChordValidity::OkFunctionKeyAlone
            ? QStringLiteral("其他程序将收不到 %1").arg(shell::keyName(virtualKey)) : QString());
    return true;
}

} // namespace subrec::ui
