#include "queue/packet_guard.h"

#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)

namespace {
AVPacket packet(int64_t pts, int64_t dts, int64_t duration = 1, bool key = false)
{
    AVPacket p{};
    p.pts = pts;
    p.dts = dts;
    p.duration = duration;
    p.flags = key ? AV_PKT_FLAG_KEY : 0;
    return p;
}

void acceptsAndNormalizes()
{
    PacketGuard guard(PacketGuardConfig{1, false, 30});
    AVPacket a = packet(AV_NOPTS_VALUE, 5);
    CHECK(guard.check(a) == GuardVerdict::Accept && a.pts == 5);
    AVPacket b = packet(7, AV_NOPTS_VALUE);
    CHECK(guard.check(b) == GuardVerdict::Accept && b.dts == 7);
    AVPacket c = packet(8, 9, 0);   // dts > pts is lowered, duration filled
    CHECK(guard.check(c) == GuardVerdict::Accept && c.dts == 8 && c.duration == 1);
    const auto &s = guard.stats();
    CHECK(s.accepted == 3 && s.ptsFilled == 1 && s.dtsFilled == 1);
    CHECK(s.dtsClampedToPts == 1 && s.durationFilled == 1 && s.droppedTotal() == 0);
    CHECK(guard.lastDts() == 8);
}

void legacyDurationRule()
{
    PacketGuard guard;   // nominalDuration 0: negative -> 0, zero stays zero
    AVPacket a = packet(0, 0, -3);
    CHECK(guard.check(a) == GuardVerdict::Accept && a.duration == 0);
    AVPacket b = packet(1, 1, 0);
    CHECK(guard.check(b) == GuardVerdict::Accept && b.duration == 0);
    CHECK(guard.stats().durationFilled == 0);
}

void dropsWithReasons()
{
    PacketGuard guard(PacketGuardConfig{1, true, 30});
    GuardReason reason = GuardReason::None;
    AVPacket missing = packet(AV_NOPTS_VALUE, AV_NOPTS_VALUE);
    CHECK(guard.check(missing, &reason) == GuardVerdict::Drop && reason == GuardReason::MissingTimestamp);
    AVPacket delta = packet(0, 0);
    CHECK(guard.check(delta, &reason) == GuardVerdict::Drop && reason == GuardReason::BeforeFirstKeyframe);
    AVPacket key = packet(1, 1, 1, true);
    CHECK(guard.check(key, &reason) == GuardVerdict::Accept && reason == GuardReason::None);
    AVPacket afterKey = packet(2, 2);   // non-key packets are fine after the first key
    CHECK(guard.check(afterKey) == GuardVerdict::Accept);
    AVPacket duplicate = packet(2, 2);
    CHECK(guard.check(duplicate, &reason) == GuardVerdict::Drop && reason == GuardReason::NonIncreasingDts);
    AVPacket backwards = packet(1, 1);
    CHECK(guard.check(backwards, &reason) == GuardVerdict::Drop && reason == GuardReason::NonIncreasingDts);
    const auto &s = guard.stats();
    CHECK(s.droppedFor(GuardReason::MissingTimestamp) == 1);
    CHECK(s.droppedFor(GuardReason::BeforeFirstKeyframe) == 1);
    CHECK(s.droppedFor(GuardReason::NonIncreasingDts) == 2);
    CHECK(s.droppedTotal() == 4 && s.accepted == 2 && s.maxConsecutiveDrops == 2);
    CHECK(guard.lastDts() == 2);
}

void negativePrimingIsAccepted()
{
    // Native AAC starts at -1024: negative dts is a valid, increasing sequence.
    PacketGuard guard(PacketGuardConfig{1024, false, 30});
    for (int64_t ts = -1024; ts <= 2048; ts += 1024) {
        AVPacket p = packet(ts, ts, 1024, true);
        CHECK(guard.check(p) == GuardVerdict::Accept);
    }
    CHECK(guard.stats().accepted == 4);
}

void escalatesAfterConsecutiveDrops()
{
    PacketGuard guard(PacketGuardConfig{1, false, 3});
    AVPacket first = packet(10, 10);
    CHECK(guard.check(first) == GuardVerdict::Accept);
    AVPacket stale = packet(10, 10);
    CHECK(guard.check(stale) == GuardVerdict::Drop);
    CHECK(guard.check(stale) == GuardVerdict::Drop);
    CHECK(guard.check(stale) == GuardVerdict::Fatal);
    // An accepted packet resets the run; the maximum is kept for diagnostics.
    AVPacket next = packet(11, 11);
    CHECK(guard.check(next) == GuardVerdict::Accept);
    CHECK(guard.check(stale) == GuardVerdict::Drop);
    CHECK(guard.stats().consecutiveDrops == 1 && guard.stats().maxConsecutiveDrops == 3);

    PacketGuard unlimited(PacketGuardConfig{1, false, 0});
    for (int i = 0; i < 100; ++i) {
        AVPacket p = packet(AV_NOPTS_VALUE, AV_NOPTS_VALUE);
        CHECK(unlimited.check(p) == GuardVerdict::Drop);
    }
}

void resetStartsNewSession()
{
    PacketGuard guard(PacketGuardConfig{1, true, 30});
    AVPacket key = packet(100, 100, 1, true);
    CHECK(guard.check(key) == GuardVerdict::Accept);
    guard.reset();
    CHECK(guard.lastDts() == AV_NOPTS_VALUE && guard.stats().accepted == 0);
    AVPacket earlier = packet(0, 0);   // keyframe requirement applies again
    CHECK(guard.check(earlier) == GuardVerdict::Drop);
    AVPacket earlierKey = packet(0, 0, 1, true);
    CHECK(guard.check(earlierKey) == GuardVerdict::Accept);
    CHECK(std::string(guardReasonName(GuardReason::NonIncreasingDts)) == "non_increasing_dts");
}
}

int main()
{
    try {
        acceptsAndNormalizes();
        legacyDurationRule();
        dropsWithReasons();
        negativePrimingIsAccepted();
        escalatesAfterConsecutiveDrops();
        resetStartsNewSession();
        std::cout << "PASS: packet guard\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
