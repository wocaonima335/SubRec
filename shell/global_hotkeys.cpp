#include "shell/global_hotkeys.h"

#include <QDateTime>
#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000 // Windows 7+; MinGW headers may predate it
#endif
#ifndef ERROR_HOTKEY_ALREADY_REGISTERED
#define ERROR_HOTKEY_ALREADY_REGISTERED 1409
#endif
#endif

namespace subrec::shell {

namespace {
constexpr int kActionCount = 3;
// §3.9: a repeated trigger of the same action within 500 ms is dropped.
constexpr qint64 kDebounceMs = 500;

#ifdef Q_OS_WIN
quint32 winModifiers(quint32 mods)
{
    quint32 native = MOD_NOREPEAT;
    if (mods & ModCtrl) native |= MOD_CONTROL;
    if (mods & ModAlt) native |= MOD_ALT;
    if (mods & ModShift) native |= MOD_SHIFT;
    if (mods & ModWin) native |= MOD_WIN;
    return native;
}
#endif
} // namespace

// Win32 implementation: message-only window + RegisterHotKey (MINIMIZE_DESIGN §4.3).
// <windows.h> stays confined to this file; every Win32 touch is inside Q_OS_WIN.
// The Windows half is compile-checked with MinGW -fsyntax-only and exercised on PC3
// (V8–V10); the Linux build keeps the Unsupported/Unset rules below.
class GlobalHotkeys::Private {
public:
    KeyChord bindings[kActionCount];
    RegisterResult status[kActionCount] = {RegisterResult::Unset, RegisterResult::Unset,
                                           RegisterResult::Unset};
    int suspendDepth = 0;
    qint64 lastAcceptedMs[kActionCount] = {0, 0, 0};

#ifdef Q_OS_WIN
    HWND messageWindow = nullptr;
   ATOM windowClass = 0;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_HOTKEY) {
            // Never act inside the window procedure (§4.3 item 3): re-enter through the
            // Qt event loop so recording control cannot recurse into a foreign modal loop.
            GlobalHotkeys *self = reinterpret_cast<GlobalHotkeys *>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            const int action = static_cast<int>(wParam);
            if (self && action >= 0 && action < kActionCount) {
                QMetaObject::invokeMethod(self, [self, action] { self->dispatch(action); },
                                          Qt::QueuedConnection);
            }
            return 0;
        }
        if (msg == WM_DESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(nullptr));
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void createMessageWindow()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Private::windowProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"SubRecGlobalHotkeys";
        windowClass = RegisterClassExW(&wc);
        if (!windowClass)
            return;
        // Message-only window: parent HWND_MESSAGE, not visible, not in any window list,
        // and its messages are delivered by every message loop (unlike thread hotkeys).
        messageWindow = CreateWindowExW(0, wc.lpszClassName, L"SubRec Hotkeys", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, instance, nullptr);
        if (messageWindow)
            SetWindowLongPtrW(messageWindow, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(owner));
    }

    bool registerOne(HotkeyAction action)
    {
        const KeyChord &chord = bindings[static_cast<int>(action)];
        if (chord.isEmpty() || !messageWindow)
            return false;
        return RegisterHotKey(messageWindow, static_cast<int>(action),
                              winModifiers(chord.modifiers), chord.virtualKey) != FALSE;
    }

    void unregisterOne(HotkeyAction action)
    {
        if (messageWindow)
            UnregisterHotKey(messageWindow, static_cast<int>(action));
    }

    GlobalHotkeys *owner = nullptr;
#endif
};

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
#ifdef Q_OS_WIN
    d->owner = this;
    d->createMessageWindow();
#endif
}

GlobalHotkeys::~GlobalHotkeys()
{
#ifdef Q_OS_WIN
    if (d->messageWindow) {
        for (int i = 0; i < kActionCount; ++i)
            d->unregisterOne(static_cast<HotkeyAction>(i));
        DestroyWindow(d->messageWindow);
        d->messageWindow = nullptr;
    }
    if (d->windowClass) {
        UnregisterClassW(MAKEINTATOM(d->windowClass), GetModuleHandleW(nullptr));
        d->windowClass = 0;
    }
#endif
}

bool GlobalHotkeys::supported()
{
#ifdef Q_OS_WIN
    // The message-only window is created per instance in the constructor; if that ever
    // failed, setBinding()/resume() report Unsupported for every action.
    return true;
#else
    return false;
#endif
}

RegisterResult GlobalHotkeys::setBinding(HotkeyAction action, const KeyChord &chord)
{
    const int index = static_cast<int>(action);
    if (index < 0 || index >= kActionCount)
        return RegisterResult::Invalid;

    // §4.3 item 5: never hand an invalid chord to Windows.
    const ChordValidity validity = validateChord(chord);
    if (validity == ChordValidity::NeedsCtrlOrAlt || validity == ChordValidity::Forbidden)
        return RegisterResult::Invalid;

    d->bindings[index] = chord;

    if (chord.isEmpty()) {
#ifdef Q_OS_WIN
        d->unregisterOne(action);
#endif
        d->status[index] = RegisterResult::Unset;
        emit statusChanged(action);
        return RegisterResult::Unset;
    }

#ifdef Q_OS_WIN
    if (!d->messageWindow || d->suspendDepth > 0) {
        // Remembered only; (re-)registered on resume(). Without a message window nothing
        // can ever be live in this process.
        if (d->status[index] != RegisterResult::Unsupported) {
            d->status[index] = RegisterResult::Unsupported;
            emit statusChanged(action);
        }
        return RegisterResult::Unsupported;
    }

    d->unregisterOne(action);
    const bool ok = d->registerOne(action);
    // ERROR_HOTKEY_ALREADY_REGISTERED (1409) and any other failure both surface as
    // Conflict: the chord is kept but not live (§4.3 item 4).
    d->status[index] = ok ? RegisterResult::Ok : RegisterResult::Conflict;
    emit statusChanged(action);
    return d->status[index];
#else
    // Non-Windows: nothing is ever registered; the binding is remembered for the UI.
    d->status[index] = RegisterResult::Unsupported;
    emit statusChanged(action);
    return RegisterResult::Unsupported;
#endif
}

KeyChord GlobalHotkeys::binding(HotkeyAction action) const
{
    const int index = static_cast<int>(action);
    return index >= 0 && index < kActionCount ? d->bindings[index] : KeyChord{};
}

RegisterResult GlobalHotkeys::status(HotkeyAction action) const
{
    const int index = static_cast<int>(action);
    if (index < 0 || index >= kActionCount)
        return RegisterResult::Invalid;
    if (d->bindings[index].isEmpty())
        return RegisterResult::Unset;
    if (!GlobalHotkeys::supported())
        return RegisterResult::Unsupported;
    return d->status[index];
}

void GlobalHotkeys::suspend()
{
    ++d->suspendDepth;
    if (d->suspendDepth == 1) {
#ifdef Q_OS_WIN
        // §6.5 K-录入中: unregister everything, keep the bindings.
        for (int i = 0; i < kActionCount; ++i)
            d->unregisterOne(static_cast<HotkeyAction>(i));
#endif
    }
}

void GlobalHotkeys::resume()
{
    if (d->suspendDepth == 0)
        return;
    --d->suspendDepth;
    if (d->suspendDepth == 0) {
#ifdef Q_OS_WIN
        for (int i = 0; i < kActionCount; ++i) {
            const auto action = static_cast<HotkeyAction>(i);
            d->unregisterOne(action);
            const bool ok = d->registerOne(action);
            const RegisterResult next = d->bindings[i].isEmpty() ? RegisterResult::Unset
                                        : ok ? RegisterResult::Ok : RegisterResult::Conflict;
            if (next != d->status[i]) {
                d->status[i] = next;
                emit statusChanged(action);
            }
        }
#endif
    }
}

bool GlobalHotkeys::suspended() const { return d->suspendDepth > 0; }

void GlobalHotkeys::dispatch(int action)
{
    // GUI-thread slot invoked from the queued WM_HOTKEY lambda.
    if (action < 0 || action >= kActionCount)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - d->lastAcceptedMs[action] < kDebounceMs)
        return; // §3.9: repeated triggers within 500 ms are dropped
    d->lastAcceptedMs[action] = now;
    emit triggered(static_cast<HotkeyAction>(action));
}

} // namespace subrec::shell
