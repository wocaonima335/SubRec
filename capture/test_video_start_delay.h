#pragma once
#include "test_hook_gate.h"
#include <chrono>
#include <cstdlib>

namespace capture::test {
// Test-only, one shot per source run; no sleep or log when unset/invalid/zero.
class VideoStartDelay {
public:
    static std::chrono::milliseconds parse(const char *value) {
        if (!value || !*value) return {};
        char *end = nullptr;
        const long ms = std::strtol(value, &end, 10);
        return end != value && *end == '\0' && ms > 0 && ms <= 60000
            ? std::chrono::milliseconds(ms) : std::chrono::milliseconds(0);
    }
    explicit VideoStartDelay(std::chrono::milliseconds delay =
        parse(hookEnvironment("SUBREC_TEST_VIDEO_START_DELAY_MS"))) : delay_(delay) {}
    // wait returns true on cancellation. Use the source's existing CV/stop flag.
    template<class Wait, class Log> bool beforeFirstPacket(Wait wait, Log log) {
        if (used_ || delay_.count() == 0) return true;
        used_ = true;
        log(delay_.count());
        return !wait(delay_);
    }
private:
    std::chrono::milliseconds delay_;
    bool used_ = false;
};
}
