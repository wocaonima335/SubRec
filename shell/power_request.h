#pragma once

// S0b contract (ZCode). Implementation: shell/power_request.cpp (S6).
// Design: MINIMIZE_DESIGN.md §3.9 (last item), §4.8, §8.4.6.

namespace subrec::shell {

// Prevents system sleep and display-off while a recording is in progress
// (Preparing / Running / Paused / Stopping), because DXGI duplication gets no new
// frames once the display turns off.
//
// Uses SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
// to activate and SetThreadExecutionState(ES_CONTINUOUS) to release. The state is
// per-thread, so every call — including construction and destruction — MUST happen on
// the GUI thread (the same thread for the object's whole life).
//
// Not a QObject; owned by main.cpp, referenced by WindowController.
// Lifetime: the destructor releases if still active.
// Failure semantics: if SetThreadExecutionState returns 0 the failure is logged with
//   "[Shell]" and active() stays false; recording is never blocked. It cannot stop a
//   user-initiated sleep, lid close or lock screen. No-op on non-Windows.
class PowerRequest
{
public:
    PowerRequest() = default;
    ~PowerRequest();

    PowerRequest(const PowerRequest &) = delete;
    PowerRequest &operator=(const PowerRequest &) = delete;

    // Idempotent: repeated calls with the same value do not call the API again.
    void setActive(bool active);
    bool active() const { return m_active; }

private:
    bool m_active = false;
};

} // namespace subrec::shell
