#include "capture/test_video_start_delay.h"
#include <cstdio>
#include <stdexcept>

#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
#error "This target must compile with SUBREC_STRIP_TEST_HOOKS"
#endif
static_assert(!capture::test::hooksEnabled(), "production hook gate was not stripped");
static_assert(!capture::test::videoOpenTimeoutRequested(), "timeout hook was not stripped");

static_assert(capture::test::muxFailAtPacket() == 0, "mux failure hook was not stripped");

int main()
{
    try {
        // The compile-time assertions and the default constructor prove that no
        // environment read or hook path is present in this production variant.
        capture::test::VideoStartDelay delay;
        if (!delay.beforeFirstPacket([](auto) { throw std::runtime_error("hook ran"); return false; },
                                     [](auto) { throw std::runtime_error("hook logged"); }))
            throw std::runtime_error("stripped hook delayed a packet");
        std::puts("PASS stripped test hooks: constexpr gate disabled");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
