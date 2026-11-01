#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
}

// Per-stream timestamp guard shared by the encoder output (second line) and the
// muxer input (third line). It never reorders or retimes packets: it only fills
// a missing pts/dts from the other value, lowers dts to pts, fills a non-positive
// duration and drops packets that would break a strictly increasing dts.
// Single-threaded: each stream owner keeps its own instance.

enum class GuardVerdict : uint8_t { Accept, Drop, Fatal };

enum class GuardReason : uint8_t {
    None = 0,
    MissingTimestamp,     // neither pts nor dts
    NonIncreasingDts,     // dts <= previous accepted dts
    BeforeFirstKeyframe,  // video packet before the first keyframe (when required)
    Count
};

struct PacketGuardConfig {
    // > 0: fill a missing or non-positive duration with this value (time base of
    // the checked packets). 0: keep the muxer's legacy rule (negative -> 0).
    int64_t nominalDuration = 0;
    bool requireKeyframeFirst = false;
    // Consecutive drops that turn Drop into Fatal; 0 disables escalation.
    uint32_t fatalConsecutiveDrops = 30;
};

struct PacketGuardStats {
    uint64_t accepted = 0;
    uint64_t dropped[static_cast<int>(GuardReason::Count)] = {};
    uint64_t ptsFilled = 0, dtsFilled = 0, dtsClampedToPts = 0, durationFilled = 0;
    uint32_t consecutiveDrops = 0, maxConsecutiveDrops = 0;

    uint64_t droppedTotal() const;
    uint64_t droppedFor(GuardReason reason) const { return dropped[static_cast<int>(reason)]; }
};

class PacketGuard
{
public:
    PacketGuard() = default;
    explicit PacketGuard(const PacketGuardConfig &config) : config_(config) {}

    void configure(const PacketGuardConfig &config);
    // New session: forget the previous dts, keyframe state and counters.
    void reset();

    // May normalize packet fields in place. Fatal is returned instead of Drop
    // once fatalConsecutiveDrops packets in a row were rejected.
    GuardVerdict check(AVPacket &packet, GuardReason *reason = nullptr);

    int64_t lastDts() const { return lastDts_; }
    const PacketGuardConfig &config() const { return config_; }
    const PacketGuardStats &stats() const { return stats_; }

private:
    GuardVerdict drop(GuardReason reason, GuardReason *out);

    PacketGuardConfig config_;
    PacketGuardStats stats_;
    int64_t lastDts_ = AV_NOPTS_VALUE;
    bool keyframeSeen_ = false;
};

const char *guardReasonName(GuardReason reason);
