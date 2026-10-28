#include "logging/log_options.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
int main(int argc, char **argv) {
    try {
        using subrec::logging::debugRequested;
        bool expected = false;
        for (int i = 1; i < argc; ++i)
            if (std::string_view(argv[i]) == "--expect-debug") expected = true;
        CHECK(debugRequested(argc, argv) == expected);
        const char *plain[] = {"SubRec"};
        const char *flag[] = {"SubRec", "--debug-log"};
        const char *ended[] = {"SubRec", "--", "--debug-log"};
        const char *lookalike[] = {"SubRec", "--debug-log=false"};
        CHECK(!debugRequested(0, nullptr, nullptr, nullptr));
        CHECK(!debugRequested(1, plain, "0", "info"));
        CHECK(!debugRequested(1, plain, "invalid", "warning"));
        CHECK(!debugRequested(3, ended, nullptr, nullptr));
        CHECK(!debugRequested(2, lookalike, nullptr, nullptr));
        CHECK(debugRequested(2, flag, "0", "info"));
        CHECK(debugRequested(1, plain, "1", "info"));
        CHECK(debugRequested(1, plain, "0", "debug"));
        CHECK(std::string_view(subrec::logging::levelMessage(expected)) ==
              (expected ? "[Logging] level=debug frame_debug=on" : "[Logging] level=info frame_debug=off"));
        std::printf("PASS log options: frame_debug=%s\n", expected ? "on" : "off");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
