#pragma once
#include <exception>

namespace capture::wasapi {
constexpr int kReconnectAttempts = 10;

// NativeError is the WASAPI HRESULT wrapper (not derived from std::exception).
// wait() retains the caller's interruptible backoff; failed() releases any
// partially opened stream. Keep initial open outside this recovery policy.
template<class NativeError, class Cancelled, class Wait, class Open, class Failed>
bool reconnect(Cancelled cancelled, Wait wait, Open open, Failed failed) {
    for (int attempt = 1; attempt <= kReconnectAttempts; ++attempt) {
        if (cancelled()) return false;
        wait();
        if (cancelled()) return false;
        try {
            open();
            return true;
        } catch (const NativeError &error) {
            failed(attempt, error);
        } catch (const std::exception &error) {
            // Includes unsupported/malformed formats from openStream(), and
            // converter initialization failures during a default-device switch.
            failed(attempt, error);
        }
    }
    return false;
}
}
