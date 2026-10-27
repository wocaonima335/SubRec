#pragma once

namespace capture::wasapi {
struct DrainResult {
    bool receivedPacket = false;
    bool empty = false; // false on interruption/removal: not evidence of silence
};

// readPacket must release each acquired packet before returning (also on errors).
// During normal polling cancelled may include stop/switch; during final draining
// it includes abort only, so a drain-stop does not discard the queued tail.
template<class Cancelled, class NextSize, class ReadPacket>
DrainResult drainPackets(Cancelled cancelled, NextSize nextSize, ReadPacket readPacket) {
    DrainResult result;
    while (!cancelled()) {
        if (!nextSize()) { result.empty = true; break; }
        if (cancelled()) break;
        if (!readPacket()) { result.empty = true; break; } // AUDCLNT_S_BUFFER_EMPTY
        result.receivedPacket = true;
    }
    return result;
}

// Freeze production before draining a finite old buffer. Even if Stop reports
// removal, attempt to read what remains; loss during reading is unavoidable but
// must not turn a normal drain-stop into a failed recording. Other errors retain
// the existing fatal-error behavior.
template<class NativeError, class Stop, class Drain, class Cancelled, class IsLost, class ReportLost>
DrainResult stopAndDrain(Stop stop, Drain drain, Cancelled cancelled, IsLost isLost, ReportLost reportLost) {
    bool lost = false;
    try { stop(); }
    catch (const NativeError &error) {
        if (!isLost(error)) throw;
        lost = true; reportLost(error);
    }
    if (cancelled()) return {};
    try {
        auto result = drain();
        if (lost) result.empty = false;
        return result;
    } catch (const NativeError &error) {
        if (!isLost(error)) throw;
        reportLost(error);
        return {};
    }
}
}
