#include "capture/wasapi_silence_policy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using capture::wasapi::SilencePolicy;
using capture::wasapi::kOutputSampleRate;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
constexpr int64_t epoch = 123456000000;
constexpr int64_t output = kOutputSampleRate;
constexpr int64_t gap = SilencePolicy::kMinimumGapSamples;
int64_t empty(SilencePolicy &p, int ms, int64_t samples = output, int64_t pending = 0) {
    return p.target(epoch + ms*1000, samples, pending, false, true);
}
void latePackets() {
    for (int delay : {20, 40, 60}) {
        SilencePolicy p;
        // Give the policy a large known gap: refusal must depend on the idle
        // observation interval, not merely the amount of output outstanding.
        p.packet(output + 3*gap);
        for (int ms = 0; ms <= delay; ms += 5) CHECK(empty(p, ms) == output);
        p.packet(output + 4*gap); // delayed data arrived; restart qualification
        CHECK(p.target(epoch+delay*1000, output, 0, true, true) == output);
        for (int ms = delay+5; ms < delay+105; ms += 5) CHECK(empty(p, ms) == output);
    }
}
void sustainedEmpty() {
    SilencePolicy p; p.packet(output + gap);
    for (int ms = 0; ms < 100; ms += 5) CHECK(empty(p, ms) == output);
    CHECK(empty(p, 100) == output + gap); // inclusive 100 ms threshold
    // A wall clock arbitrarily far ahead cannot move the packet-derived bound.
    CHECK(empty(p, 10000) == output + gap);
    CHECK(empty(p, 10005, output + gap) == output + gap); // no duplicate padding
    SilencePolicy shortGap; shortGap.packet(output + gap - 1);
    for (int ms = 0; ms <= 200; ms += 5) CHECK(empty(shortGap, ms) == output);
    SilencePolicy pending; pending.packet(output + gap + 17);
    for (int ms = 0; ms < 100; ms += 5) CHECK(empty(pending, ms, output, 17) == output);
    CHECK(empty(pending, 100, output, 17) == output + gap); // reserve converter tail
}
void evidenceRequired() {
    SilencePolicy p;
    for (int ms = 0; ms <= 200; ms += 5) CHECK(empty(p, ms, 0) == 0); // no first packet
    p.packet(output); // no interval beyond the last real packet can be invented
    for (int ms = 0; ms <= 200; ms += 5) CHECK(empty(p, ms) == output);
    p.packet(output + gap);
    CHECK(empty(p, 0) == output);
    CHECK(empty(p, 200) == output); // a single long wait is not multiple polls
    CHECK(empty(p, 205) == output + gap);
    CHECK(p.target(epoch + 210000, output, 0, false, false) == output); // unknown/not empty
    for (int ms = 215; ms < 315; ms += 5) CHECK(empty(p, ms) == output);
    CHECK(empty(p, 315) == output + gap);
    p.packet(-1); // timestamp error must not keep the old valid endpoint
    for (int ms = 320; ms <= 520; ms += 5) CHECK(empty(p, ms) == output);
    p.packet(output+gap); p.reset(); // device lost/reopened
    for (int ms = 525; ms <= 725; ms += 5) CHECK(empty(p, ms) == output);
}
void packetsAndClockReset() {
    SilencePolicy p;
    // A SILENT packet is already real zero-filled audio. Even if draining it
    // leaves the buffer empty, regular packets must never qualify as no data.
    for (int ms = 0; ms < 200; ms += 5) {
        p.packet(output + gap);
        CHECK(p.target(epoch + ms*1000, output, 0, true, true) == output);
    }
    CHECK(empty(p, 200) == output); CHECK(empty(p, 250) == output);
    CHECK(empty(p, 10) == output); // clock reversal restarts the idle interval
    for (int ms = 15; ms < 110; ms += 5) CHECK(empty(p, ms) == output);
    CHECK(empty(p, 110) == output + gap);
}
}
int main() {
    try {
        latePackets(); sustainedEmpty(); evidenceRequired(); packetsAndClockReset();
        std::cout << "PASS: 20-60 ms late packets, >=100 ms sustained empty, packet-end bound, resets and resampler tail\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
