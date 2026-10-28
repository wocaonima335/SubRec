#pragma once
#include <chrono>
#include <cstdlib>

namespace capture_stop {
// Applies to each drain wait and its cancellation grace period. Snapshot at
// shutdown; recording/pausing itself never expires merely because input is idle.
inline std::chrono::milliseconds timeout() {
    const char *text = std::getenv("SUBREC_STOP_TIMEOUT_MS");
    if (text && *text) {
        char *end = nullptr;
        const long value = std::strtol(text, &end, 10);
        if (end != text && *end == '\0' && value >= 1 && value <= 60000)
            return std::chrono::milliseconds(value);
    }
    return std::chrono::milliseconds(5000);
}
// One budget for a failed startup, shared by every worker (not N * timeout).
class Deadline {
public:
    explicit Deadline(std::chrono::milliseconds limit = timeout())
        : end_(std::chrono::steady_clock::now() + limit) {}
    std::chrono::milliseconds remaining() const {
        const auto now = std::chrono::steady_clock::now();
        return now >= end_ ? std::chrono::milliseconds(0)
            : std::chrono::duration_cast<std::chrono::milliseconds>(end_ - now);
    }
private:
    std::chrono::steady_clock::time_point end_;
};

// False means the worker still owns its borrowed objects. The caller MUST keep
// those objects alive and must not close/reinitialize them or detach the worker.
template<class Worker, class Cancel, class Log>
bool drain(Worker &worker, Cancel cancel, Log log,
           std::chrono::milliseconds limit = timeout()) {
    if (worker.waitFor(limit)) return true;
    log("drain timeout; cancelling queue waits");
    cancel();
    if (worker.waitFor(limit)) return true;
    log("cancel timeout; keeping worker and session resources alive");
    return false;
}
}
