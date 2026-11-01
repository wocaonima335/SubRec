#include "packet_guard.h"

#include <algorithm>

uint64_t PacketGuardStats::droppedTotal() const
{
    uint64_t total = 0;
    for (const uint64_t count : dropped)
        total += count;
    return total;
}

void PacketGuard::configure(const PacketGuardConfig &config)
{
    config_ = config;
    reset();
}

void PacketGuard::reset()
{
    stats_ = PacketGuardStats{};
    lastDts_ = AV_NOPTS_VALUE;
    keyframeSeen_ = false;
}

GuardVerdict PacketGuard::drop(GuardReason reason, GuardReason *out)
{
    if (out)
        *out = reason;
    ++stats_.dropped[static_cast<int>(reason)];
    ++stats_.consecutiveDrops;
    stats_.maxConsecutiveDrops = std::max(stats_.maxConsecutiveDrops, stats_.consecutiveDrops);
    if (config_.fatalConsecutiveDrops > 0 && stats_.consecutiveDrops >= config_.fatalConsecutiveDrops)
        return GuardVerdict::Fatal;
    return GuardVerdict::Drop;
}

GuardVerdict PacketGuard::check(AVPacket &packet, GuardReason *reason)
{
    if (reason)
        *reason = GuardReason::None;

    if (packet.pts == AV_NOPTS_VALUE && packet.dts != AV_NOPTS_VALUE) {
        packet.pts = packet.dts;
        ++stats_.ptsFilled;
    } else if (packet.dts == AV_NOPTS_VALUE && packet.pts != AV_NOPTS_VALUE) {
        packet.dts = packet.pts;
        ++stats_.dtsFilled;
    }
    if (packet.pts == AV_NOPTS_VALUE || packet.dts == AV_NOPTS_VALUE)
        return drop(GuardReason::MissingTimestamp, reason);

    // Preserve encoder priming and reorder timestamps; only an impossible
    // dts > pts is lowered (the muxer has always done this).
    if (packet.dts > packet.pts) {
        packet.dts = packet.pts;
        ++stats_.dtsClampedToPts;
    }

    if (config_.requireKeyframeFirst && !keyframeSeen_ && !(packet.flags & AV_PKT_FLAG_KEY))
        return drop(GuardReason::BeforeFirstKeyframe, reason);

    if (lastDts_ != AV_NOPTS_VALUE && packet.dts <= lastDts_)
        return drop(GuardReason::NonIncreasingDts, reason);

    if (packet.duration <= 0) {
        if (config_.nominalDuration > 0) {
            packet.duration = config_.nominalDuration;
            ++stats_.durationFilled;
        } else if (packet.duration < 0) {
            packet.duration = 0;
        }
    }

    if (packet.flags & AV_PKT_FLAG_KEY)
        keyframeSeen_ = true;
    lastDts_ = packet.dts;
    ++stats_.accepted;
    stats_.consecutiveDrops = 0;
    return GuardVerdict::Accept;
}

const char *guardReasonName(GuardReason reason)
{
    switch (reason) {
    case GuardReason::None: return "none";
    case GuardReason::MissingTimestamp: return "missing_timestamp";
    case GuardReason::NonIncreasingDts: return "non_increasing_dts";
    case GuardReason::BeforeFirstKeyframe: return "before_first_keyframe";
    case GuardReason::Count: break;
    }
    return "unknown";
}
