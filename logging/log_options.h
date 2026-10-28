#pragma once
#include <cstdlib>
#include <string_view>

namespace subrec::logging {
// A positive opt-in wins. Read once before starting capture workers; no getenv,
// mutable switches, or extra locks on the per-frame path. FFmpeg's independent
// SUBREC_FFMPEG_LOG setting is deliberately unchanged.
inline bool debugRequested(int argc, const char *const *argv,
                           const char *debugEnv, const char *levelEnv) {
    const auto equals = [](const char *value, std::string_view expected) {
        return value && std::string_view(value) == expected;
    };
    if (equals(debugEnv, "1") || equals(levelEnv, "debug")) return true;
    for (int i = 1; i < argc && argv; ++i) {
        if (equals(argv[i], "--")) break;
        if (equals(argv[i], "--debug-log")) return true;
    }
    return false;
}
inline bool debugRequested(int argc, const char *const *argv) {
    return debugRequested(argc, argv, std::getenv("SUBREC_DEBUG_LOG"),
                          std::getenv("SUBREC_LOG_LEVEL"));
}
inline const char *levelMessage(bool debug) {
    return debug ? "[Logging] level=debug frame_debug=on"
                 : "[Logging] level=info frame_debug=off";
}
}
