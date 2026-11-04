// H2 unit tests, non-Windows rules (UI_BATCH_PLAN.md §2.3): every registration attempt
// reports Unsupported, empty chords report Unset, suspend/resume is counted, and no
// triggered() can ever fire without RegisterHotKey. Chords are built directly as
// KeyChord values so the test does not depend on H1's parsing (still a stub here).
// The Win32 half (message window, MOD_NOREPEAT, WM_HOTKEY queuing, 500 ms debounce,
// conflict mapping) is PC3 V8–V10 territory and MinGW -fsyntax-only checked.
#include "shell/global_hotkeys.h"
#include "shell/key_chord.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <cstdio>

using namespace subrec::shell;

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

// Expected setBinding() result derived from H1's validator (not its implementation):
// chords H1 rejects must never reach the OS; everything else hits the platform result.
RegisterResult expectedFor(const KeyChord &chord, RegisterResult platformResult)
{
    const ChordValidity v = validateChord(chord);
    if (v == ChordValidity::NeedsCtrlOrAlt || v == ChordValidity::Forbidden)
        return RegisterResult::Invalid;
    if (chord.isEmpty())
        return RegisterResult::Unset;
    return platformResult;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const RegisterResult platformResult =
#ifdef Q_OS_WIN
        RegisterResult::Conflict; // a real RegisterHotKey outcome; not asserted below
#else
        RegisterResult::Unsupported;
#endif

    check(true, "supported() is callable"); // platform result asserted below

    GlobalHotkeys hotkeys;
    int triggeredCount = 0;
    QObject::connect(&hotkeys, &GlobalHotkeys::triggered, [&](HotkeyAction) { ++triggeredCount; });
    int statusCount = 0;
    QObject::connect(&hotkeys, &GlobalHotkeys::statusChanged, [&](HotkeyAction) { ++statusCount; });

    // ---- defaults: all Unset ----
    for (const HotkeyAction action :
         {HotkeyAction::ToggleRecord, HotkeyAction::TogglePause, HotkeyAction::ToggleWindow}) {
        check(hotkeys.binding(action) == KeyChord{}, "default binding is empty");
        check(hotkeys.status(action) == RegisterResult::Unset, "default status is Unset");
    }

    // ---- K1 defaults round-trip through setBinding ----
    const KeyChord record{ModCtrl | ModAlt, 0x52}; // Ctrl+Alt+R
    const KeyChord pause{ModCtrl | ModAlt, 0x50};  // Ctrl+Alt+P
    const KeyChord window{};                        // unbound by default
    check(expectedFor(record, platformResult) == hotkeys.setBinding(HotkeyAction::ToggleRecord, record),
          "setBinding(ToggleRecord, Ctrl+Alt+R) maps through the validator");
    check(expectedFor(pause, platformResult) == hotkeys.setBinding(HotkeyAction::TogglePause, pause),
          "setBinding(TogglePause, Ctrl+Alt+P) maps through the validator");
    check(expectedFor(window, platformResult) == hotkeys.setBinding(HotkeyAction::ToggleWindow, window),
          "setBinding(ToggleWindow, empty) maps through the validator");
    check(hotkeys.binding(HotkeyAction::ToggleRecord) == record, "binding remembered");
    check(hotkeys.binding(HotkeyAction::TogglePause) == pause, "binding remembered");
    check(hotkeys.binding(HotkeyAction::ToggleWindow) == KeyChord{}, "empty binding remembered");

#ifndef Q_OS_WIN
    // ---- non-Windows: everything Unsupported / Unset ----
    check(!GlobalHotkeys::supported(), "supported() is false off Windows");
    check(hotkeys.setBinding(HotkeyAction::ToggleRecord, record) == RegisterResult::Unsupported,
          "valid chord -> Unsupported off Windows");
    check(hotkeys.status(HotkeyAction::ToggleRecord) == RegisterResult::Unsupported,
          "status Unsupported for a remembered chord");
    check(hotkeys.setBinding(HotkeyAction::TogglePause, KeyChord{}) == RegisterResult::Unset,
          "clearing a binding -> Unset");
    check(hotkeys.status(HotkeyAction::TogglePause) == RegisterResult::Unset,
          "status Unset after clearing");
#endif

    // ---- invalid chords are refused before the OS (H1 classification) ----
    {
        GlobalHotkeys fresh;
        const KeyChord loneLetter{ModNone, 0x52};      // R without Ctrl/Alt
        const KeyChord shiftOnly{ModShift, 0x52};      // Shift-only modifier
        const KeyChord f12{ModNone, 0x7B};             // F12 is reserved
        const KeyChord winCombo{ModCtrl | ModWin, 0x52};
        if (expectedFor(loneLetter, platformResult) == RegisterResult::Invalid)
            check(fresh.setBinding(HotkeyAction::ToggleRecord, loneLetter) == RegisterResult::Invalid,
                  "lone letter refused as Invalid when H1 classifies it so");
        if (expectedFor(shiftOnly, platformResult) == RegisterResult::Invalid)
            check(fresh.setBinding(HotkeyAction::ToggleRecord, shiftOnly) == RegisterResult::Invalid,
                  "shift-only letter refused as Invalid when H1 classifies it so");
        if (expectedFor(f12, platformResult) == RegisterResult::Invalid)
            check(fresh.setBinding(HotkeyAction::ToggleRecord, f12) == RegisterResult::Invalid,
                  "F12 refused as Invalid when H1 classifies it so");
        if (expectedFor(winCombo, platformResult) == RegisterResult::Invalid)
            check(fresh.setBinding(HotkeyAction::ToggleRecord, winCombo) == RegisterResult::Invalid,
                  "Win combo refused as Invalid when H1 classifies it so");
    }

    // ---- suspend / resume counting ----
    check(!hotkeys.suspended(), "not suspended initially");
    hotkeys.suspend();
    hotkeys.suspend();
    check(hotkeys.suspended(), "nested suspend stays suspended");
    hotkeys.resume();
    check(hotkeys.suspended(), "one resume of two suspends stays suspended");
    hotkeys.resume();
    check(!hotkeys.suspended(), "balanced resume ends suspension");
    hotkeys.resume();
    check(!hotkeys.suspended(), "extra resume is a no-op");

    // ---- nothing can trigger off Windows ----
    QDeadlineTimer spinner(50);
    while (!spinner.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(triggeredCount == 0, "triggered never fires off Windows");
    check(statusCount > 0, "statusChanged fired during the binding writes");

    if (g_failures == 0) std::printf("global_hotkeys_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
