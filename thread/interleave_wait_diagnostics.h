#pragma once
#include <cstdint>

// Observation only: one event per continuous >=1 s wait for a stream while
// the other stream has a packet ready. No timeout/release/order policy here.
// Single worker owns each episode; callers accumulate events atomically.
class InterleaveWaitDiagnostics {
public:
    bool observe(int64_t nowUs, bool opposingPending) {
        if (!opposingPending) { reset(); return false; }
        if (startedUs_ < 0) startedUs_ = nowUs;
        if (!reported_ && nowUs - startedUs_ >= 1000000) {
            reported_ = true;
            return true;
        }
        return false;
    }
    void reset() { startedUs_ = -1; reported_ = false; }
private:
    int64_t startedUs_ = -1;
    bool reported_ = false;
};
