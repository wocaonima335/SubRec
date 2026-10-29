#pragma once
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace capture::test {
// Only standalone test targets opt in at compile time. Application modules must
// keep the runtime AutoTest gate even when the top-level tests option is ON.
inline bool hooksEnabled() {
#if defined(SUBREC_ENABLE_TEST_HOOKS) && SUBREC_ENABLE_TEST_HOOKS
    return true;
#else
    const char *scenario = std::getenv("SUBREC_AUTOTEST_SCENARIO");
    if (!scenario) return false;
    // main.cpp treats a trimmed, nonempty scenario as AutoTest mode.
    for (; *scenario; ++scenario)
        if (!std::isspace(static_cast<unsigned char>(*scenario))) return true;
    return false;
#endif
}
inline const char *hookEnvironment(const char *name) {
    return hooksEnabled() ? std::getenv(name) : nullptr;
}
inline bool videoOpenTimeoutRequested() {
    const char *value = hookEnvironment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT");
    return value && std::strcmp(value, "1") == 0;
}
}
