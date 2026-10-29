#include "capture/test_video_start_delay.h"
#include <cstdio>
#include <stdexcept>

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
using namespace std::chrono_literals;
namespace {
void environment(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
void checkHooks(bool expected) {
    CHECK(capture::test::hooksEnabled() == expected);
    CHECK(capture::test::videoOpenTimeoutRequested() == expected);
    capture::test::VideoStartDelay delay;
    int waits = 0, logs = 0;
    auto wait = [&](auto duration) { CHECK(duration == 2s); ++waits; return false; };
    auto log = [&](auto ms) { CHECK(ms == 2000); ++logs; };
    CHECK(delay.beforeFirstPacket(wait, log));
    CHECK(delay.beforeFirstPacket(wait, log));
    CHECK(waits == (expected ? 1 : 0) && logs == waits);
}
}
int main() {
    try {
#if defined(SUBREC_ENABLE_TEST_HOOKS) && SUBREC_ENABLE_TEST_HOOKS
        constexpr bool testBuild = true;
#else
        constexpr bool testBuild = false;
#endif
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", "2000");
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "1");
        const char *scenarios[] = {nullptr, "", " \t\r\n"};
        for (const char *scenario : scenarios) {
            environment("SUBREC_AUTOTEST_SCENARIO", scenario);
            checkHooks(testBuild); // Hook variables alone must not affect GUI recording.
        }
        environment("SUBREC_AUTOTEST_SCENARIO", " record_seconds \t");
        checkHooks(true);
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "0");
        CHECK(!capture::test::videoOpenTimeoutRequested());
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", "invalid");
        capture::test::VideoStartDelay invalid;
        CHECK(invalid.beforeFirstPacket([](auto) { throw std::runtime_error("unexpected wait"); return false; },
                                       [](auto) { throw std::runtime_error("unexpected log"); }));
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", nullptr);
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", nullptr);
        CHECK(!capture::test::videoOpenTimeoutRequested());
        capture::test::VideoStartDelay unset;
        CHECK(unset.beforeFirstPacket([](auto) { throw std::runtime_error("unexpected wait"); return false; },
                                     [](auto) { throw std::runtime_error("unexpected log"); }));
        std::printf("PASS hook gate: test_build=%d, normal/empty/whitespace/AutoTest/unset/invalid/one-shot\n", testBuild);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
