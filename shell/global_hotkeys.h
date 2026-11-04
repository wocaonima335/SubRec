#ifndef SUBREC_SHELL_GLOBAL_HOTKEYS_H
#define SUBREC_SHELL_GLOBAL_HOTKEYS_H

#include "shell/key_chord.h"

#include <QMetaType>
#include <QObject>
#include <memory>

namespace subrec::shell {

enum class HotkeyAction { ToggleRecord, TogglePause, ToggleWindow };
enum class RegisterResult {
    Ok,          // registered and live
    Unset,       // empty chord: nothing registered
    Conflict,    // RegisterHotKey failed, ERROR_HOTKEY_ALREADY_REGISTERED (or any failure)
    Invalid,     // validateChord() rejected it; never passed to Windows
    Unsupported, // non-Windows build or the message window could not be created
};

// Batch 2, module H2. GUI thread only. Win32 RegisterHotKey on a private message-only
// window (parent HWND_MESSAGE), never on a QML window HWND and never as a thread hotkey
// (MINIMIZE_DESIGN §4.3 items 1–4, K2). MOD_NOREPEAT is always added.
//
// WM_HOTKEY is NOT handled inside the window procedure: it is posted to the Qt event loop
// (QMetaObject::invokeMethod, Qt::QueuedConnection) and then `triggered` is emitted, so no
// recording control re-enters from a foreign modal loop. Per action, a trigger within
// 500 ms of the previous accepted one is dropped (§3.9).
//
// Persistence and validation UI live in ui::HotkeyController; this class only registers.
// AutoTest: main.cpp does not create it (§3.8).
class GlobalHotkeys : public QObject {
    Q_OBJECT

public:
    explicit GlobalHotkeys(QObject *parent = nullptr);
    ~GlobalHotkeys() override; // unregisters everything, destroys the message window

    static bool supported(); // true on Windows when the message window exists

    // Replaces the binding of one action: unregisters the old chord, registers the new one
    // (unless suspended: then it is only remembered and registered on resume()).
    RegisterResult setBinding(HotkeyAction action, const KeyChord &chord);
    KeyChord binding(HotkeyAction action) const;
    RegisterResult status(HotkeyAction action) const;

    // KeySequenceField capture (§6.5 K-录入中): unregister all, keep bindings; resume()
    // re-registers all and refreshes status(). Nested calls are counted.
    void suspend();
    void resume();
    bool suspended() const;

signals:
    void triggered(subrec::shell::HotkeyAction action);
    void statusChanged(subrec::shell::HotkeyAction action);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace subrec::shell

Q_DECLARE_METATYPE(subrec::shell::HotkeyAction)

#endif // SUBREC_SHELL_GLOBAL_HOTKEYS_H
