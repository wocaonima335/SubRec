#include "capture/capture_error_state.h"

#include <string>

int main()
{
    CaptureErrorState<std::string> state;
    if (!state.isEmpty()) return 1;
    if (!state.setFirst("muxer", "finalize")) return 2;
    if (state.setFirst("audio mix", "device")) return 3;
    if (state.message() != "muxer" || state.remediation() != "finalize") return 4;
    state.clear();
    if (!state.isEmpty()) return 5;
    if (!state.setFirst("next session", "retry")) return 6;
    if (state.message() != "next session") return 7;
    return 0;
}
