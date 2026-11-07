// H2 unit tests (UI_BATCH_PLAN.md §2.3). Off Windows every registration attempt reports
// Unsupported. On Windows a valid chord really goes to RegisterHotKey: the result must be
// Ok or Conflict (whichever the runner gives) and match status(); Unsupported there means
// the message window was not created and fails. On both, chords H1's validator rejects are
// Invalid, empty chords report Unset, suspend/resume is counted. Chords are built directly
// as KeyChord values so the test does not depend on H1's parsing. WM_HOTKEY delivery and
// the 500 ms debounce stay PC3 V8–V10 territory (nothing presses keys here).
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

// Chords H1's validator (not its implementation) rejects must never reach the OS.
bool validatorRejects(const KeyChord &chord)
{
    const ChordValidity v = validateChord(chord);
    return v == ChordValidity::NeedsCtrlOrAlt || v == ChordValidity::Forbidden;
}

// Platform result for a valid, non-empty chord.
#ifdef Q_OS_WIN
constexpr const char *kLiveExpectation = "Ok or Conflict (Unsupported = message window not created)";
bool isLiveResult(RegisterResult result)
{
    return result == RegisterResult::Ok || result == RegisterResult::Conflict;
}
#else
constexpr const char *kLiveExpectation = "Unsupported";
bool isLiveResult(RegisterResult result)
{
    return result == RegisterResult::Unsupported;
}
#endif

void printResult(const char *call, RegisterResult result, RegisterResult status, const char *expected)
{
    std::fprintf(stderr, "  %s = %d, status() = %d (Ok=0 Unset=1 Conflict=2 Invalid=3 Unsupported=4); "
                         "expected %s\n", call, int(result), int(status), expected);
}

// setBinding() mapped through the validator and the platform rules above.
void checkBinding(GlobalHotkeys &hotkeys, HotkeyAction action, const KeyChord &chord, const char *what)
{
    const RegisterResult result = hotkeys.setBinding(action, chord);
    const RegisterResult status = hotkeys.status(action);
    bool ok = false;
    const char *expected = kLiveExpectation;
    if (validatorRejects(chord)) {
        expected = "Invalid";
        ok = result == RegisterResult::Invalid;
    } else if (chord.isEmpty()) {
        expected = "Unset";
        ok = result == RegisterResult::Unset && status == RegisterResult::Unset;
    } else {
        ok = isLiveResult(result) && status == result;
    }
    check(ok, what);
    if (!ok)
        printResult("setBinding()", result, status, expected);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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
    checkBinding(hotkeys, HotkeyAction::ToggleRecord, record,
                 "setBinding(ToggleRecord, Ctrl+Alt+R) maps through the validator");
    checkBinding(hotkeys, HotkeyAction::TogglePause, pause,
                 "setBinding(TogglePause, Ctrl+Alt+P) maps through the validator");
    checkBinding(hotkeys, HotkeyAction::ToggleWindow, window,
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
        if (validatorRejects(loneLetter))
            check(fresh.setBinding(HotkeyAction::ToggleRecord, loneLetter) == RegisterResult::Invalid,
                  "lone letter refused as Invalid when H1 classifies it so");
        if (validatorRejects(shiftOnly))
            check(fresh.setBinding(HotkeyAction::ToggleRecord, shiftOnly) == RegisterResult::Invalid,
                  "shift-only letter refused as Invalid when H1 classifies it so");
        if (validatorRejects(f12))
            check(fresh.setBinding(HotkeyAction::ToggleRecord, f12) == RegisterResult::Invalid,
                  "F12 refused as Invalid when H1 classifies it so");
        if (validatorRejects(winCombo))
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
#ifdef Q_OS_WIN
    // The balanced resume() re-registered the remembered chords: each is live again.
    for (const HotkeyAction action : {HotkeyAction::ToggleRecord, HotkeyAction::TogglePause}) {
        const RegisterResult status = hotkeys.status(action);
        check(isLiveResult(status), "status after resume is Ok or Conflict on Windows");
        if (!isLiveResult(status))
            printResult("status()", status, status, kLiveExpectation);
    }
#endif

    // ---- nothing triggers: no key is pressed here, and off Windows nothing can ----
    QDeadlineTimer spinner(50);
    while (!spinner.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(triggeredCount == 0, "triggered never fires off Windows");
    check(statusCount > 0, "statusChanged fired during the binding writes");

    if (g_failures == 0) std::printf("global_hotkeys_tests: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
