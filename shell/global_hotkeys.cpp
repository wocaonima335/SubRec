#include "shell/global_hotkeys.h"

namespace subrec::shell {

// TODO(H2): message-only window, RegisterHotKey/UnregisterHotKey, WM_HOTKEY -> queued
// emit, 500 ms per-action debounce. Keep <windows.h> confined to this file.
class GlobalHotkeys::Private {
public:
    KeyChord bindings[3];
    int suspendDepth = 0;
};

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}
GlobalHotkeys::~GlobalHotkeys() = default;

bool GlobalHotkeys::supported() { return false; }

RegisterResult GlobalHotkeys::setBinding(HotkeyAction action, const KeyChord &chord)
{
    d->bindings[static_cast<int>(action)] = chord;
    return chord.isEmpty() ? RegisterResult::Unset : RegisterResult::Unsupported;
}

KeyChord GlobalHotkeys::binding(HotkeyAction action) const { return d->bindings[static_cast<int>(action)]; }

RegisterResult GlobalHotkeys::status(HotkeyAction action) const
{
    return binding(action).isEmpty() ? RegisterResult::Unset : RegisterResult::Unsupported;
}

void GlobalHotkeys::suspend() { ++d->suspendDepth; }
void GlobalHotkeys::resume()
{
    if (d->suspendDepth > 0) --d->suspendDepth;
}
bool GlobalHotkeys::suspended() const { return d->suspendDepth > 0; }

} // namespace subrec::shell
