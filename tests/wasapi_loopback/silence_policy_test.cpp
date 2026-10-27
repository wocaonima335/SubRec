#include "capture/wasapi_silence_policy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using capture::wasapi::SilencePolicy;
using capture::wasapi::kOutputSampleRate;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
constexpr int64_t epoch = 123456000000;
constexpr int64_t margin = SilencePolicy::kSafetyUs * kOutputSampleRate / 1000000;
int64_t atMs(int ms) { return int64_t(ms)*kOutputSampleRate/1000; }
int64_t empty(SilencePolicy &p, int ms, int64_t samples = 0, int64_t pending = 0) {
    return p.target(epoch + ms*1000, samples, pending, false, true);
}
void latePackets() {
    for (int delay : {20, 40, 60}) {
        SilencePolicy p(epoch);
        // Wall time is already far beyond the cursor: the empty streak, rather
        // than insufficient elapsed session time, must prevent false positives.
        p.packet();
        for (int ms = 1000; ms <= 1000+delay; ms += 5) CHECK(empty(p, ms) == 0);
        p.packet();
        CHECK(p.target(epoch+(1000+delay)*1000, 0, 0, true, true) == 0);
        for (int ms = 1005+delay; ms < 1105+delay; ms += 5) CHECK(empty(p, ms) == 0);
    }
}
void sustainedEmpty() {
    SilencePolicy p(epoch);
    int64_t output = 0;
    for (int ms = 0; ms <= 1000; ms += 5) {
        const auto target = empty(p, ms, output);
        CHECK(target >= output);
        if (ms < 100) CHECK(target == 0);
        else CHECK(target == atMs(ms) - margin);
        output = target;
    }
    CHECK(output == kOutputSampleRate - margin);
    CHECK(empty(p, 1000, output) == output); // repeated observation cannot duplicate samples
    std::cout << "never-received packet: 1000 ms -> " << output << " silent samples, margin=" << margin << '\n';
    // Sound occupies the first second; idle continues past the last packet end.
    SilencePolicy stopped(epoch); stopped.packet(); output = kOutputSampleRate;
    for (int ms = 1000; ms <= 2000; ms += 5) {
        output = empty(stopped, ms, output);
        CHECK(output == (ms < 1100 ? kOutputSampleRate : atMs(ms)-margin));
    }
    CHECK(output == 2*kOutputSampleRate-margin);
    SilencePolicy pending(epoch);
    for (int ms = 0; ms <= 1000; ms += 5) output = empty(pending, ms, 0, 17);
    CHECK(output == kOutputSampleRate-margin-17);
    CHECK(empty(pending, 1005, 0, -17) == atMs(1005)-margin); // ignore invalid negative delay
}
void evidenceRequired() {
    SilencePolicy p(epoch);
    CHECK(empty(p, 1000) == 0);
    CHECK(empty(p, 1200) == 0); // elapsed time without >=3 polls is insufficient
    CHECK(empty(p, 1205) == atMs(1205)-margin);
    CHECK(p.target(epoch+1210000, 0, 0, false, false) == 0); // unknown/not empty
    for (int ms = 1215; ms < 1315; ms += 5) CHECK(empty(p, ms) == 0);
    CHECK(empty(p, 1315) == atMs(1315)-margin);
    p.reset(); // device loss/reopen must requalify, retaining the original epoch
    for (int ms = 2000; ms < 2100; ms += 5) CHECK(empty(p, ms) == 0);
    CHECK(empty(p, 2100) == atMs(2100)-margin);
}
void packetsAndClockReset() {
    SilencePolicy p(epoch);
    for (int ms = 1000; ms < 1200; ms += 5) {
        p.packet(); // SILENT / timestamp-error packets are data too
        CHECK(p.target(epoch+ms*1000, 0, 0, true, true) == 0);
    }
    CHECK(empty(p, 1200) == 0); CHECK(empty(p, 1250) == 0);
    CHECK(empty(p, 1010) == 0); // clock reversal restarts evidence
    for (int ms = 1015; ms < 1110; ms += 5) CHECK(empty(p, ms) == 0);
    CHECK(empty(p, 1110) == atMs(1110)-margin);
}
}
int main() {
    try {
        latePackets(); sustainedEmpty(); evidenceRequired(); packetsAndClockReset();
        std::cout << "PASS: 20-60 ms late packets, >=100 ms / >=3 empty polls, session timeline, pending tail and resets\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
