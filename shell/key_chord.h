#ifndef SUBREC_SHELL_KEY_CHORD_H
#define SUBREC_SHELL_KEY_CHORD_H

#include <QString>
#include <QtGlobal>

// Batch 2, module H1. Pure value type + functions for global hotkey chords
// (MINIMIZE_DESIGN §4.3 items 5–6, §6.5). No Win32 and no Qt GUI: Windows virtual-key codes
// are plain numbers here so tests/shell can run everything on Linux.
namespace subrec::shell {

enum KeyModifier : quint32 {
    ModNone = 0,
    ModCtrl = 0x1,
    ModAlt = 0x2,
    ModShift = 0x4,
    ModWin = 0x8,
};

// virtualKey: Windows VK_* code (0 = none). Stored form "Ctrl+Alt+R" (QSettings hotkey/*).
struct KeyChord {
    quint32 modifiers = ModNone;
    quint32 virtualKey = 0;

    bool isEmpty() const { return virtualKey == 0; }
    friend bool operator==(const KeyChord &a, const KeyChord &b)
    {
        return a.modifiers == b.modifiers && a.virtualKey == b.virtualKey;
    }
    friend bool operator!=(const KeyChord &a, const KeyChord &b) { return !(a == b); }
};

enum class ChordValidity {
    Ok,                 // letters/digits with Ctrl or Alt, or F-keys with modifiers
    OkFunctionKeyAlone, // F1–F11, F13–F24 without modifiers: save, show "其他程序将收不到 F9"
    NeedsCtrlOrAlt,     // letter/digit/other key with no or Shift-only modifier
    Forbidden,          // any Win combo, F12, Esc, Tab, Enter, Space, Backspace (alone or not)
    Empty,
};

// "Ctrl+Alt+R" -> {ModCtrl|ModAlt, 0x52}. Case-insensitive, tolerant of spaces around '+'.
// Unknown key names or garbage -> empty chord. "" -> empty chord.
KeyChord parseChord(const QString &storageText);
// Canonical storage text, modifier order Ctrl, Alt, Shift, Win: "Ctrl+Alt+R"; empty -> "".
QString storageText(const KeyChord &chord);
// UI text with spaced pluses: "Ctrl + Alt + R"; numpad keys "小键盘 5"; empty -> "".
QString displayText(const KeyChord &chord);
// Tooltip text without spaces: "Ctrl+Alt+R" (UI_REDESIGN §3.2.2 ToolTip "(F9 / Ctrl+Alt+R)").
QString compactText(const KeyChord &chord);
// Rules of §4.3 item 5.
ChordValidity validateChord(const KeyChord &chord);
// Name of a VK code as used in storage/display ("R", "F9", "Num5", ...); "" if unsupported.
QString keyName(quint32 virtualKey);

// K1 defaults (MINIMIZE_DESIGN §5.2).
inline QString defaultToggleRecordText() { return QStringLiteral("Ctrl+Alt+R"); }
inline QString defaultTogglePauseText() { return QStringLiteral("Ctrl+Alt+P"); }
inline QString defaultToggleWindowText() { return QString(); }

} // namespace subrec::shell

#endif // SUBREC_SHELL_KEY_CHORD_H
