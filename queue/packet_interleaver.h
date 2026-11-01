#pragma once

#include "ffpacket.h"

#include <cstddef>
#include <cstdint>
#include <deque>

extern "C" {
#include <libavutil/rational.h>
}

// Two-stream packet interleaver for the mux worker. Pure logic: no threads,
// no clocks (callers pass the current time) and no Qt.
//
// - Ordering key: dts (pts if dts is missing) converted to microseconds; ties
//   go to video. A head is released only when every other unfinished stream
//   has a head at or after it, so the muxer sees a globally non-decreasing dts.
// - Duration: a head is "ready" once the next packet of its stream is known
//   (or the stream finished). Its duration becomes next.dts - head.dts, so a
//   skipped video tick shows as a longer previous frame.
// - Startup: before the other stream has delivered anything, a stream may hold
//   at most maxPendingWhileOpposingAbsent packets (wants() then returns false so
//   the caller leaves packets in the bounded producer queue). Startup never
//   triggers a stall release, which preserves AAC priming in front of video.
// - Steady-state stall: if another stream delivered before but is now empty for
//   stallReleaseUs, it is treated as absent until it delivers again.
// - Video packets before the first keyframe are dropped.
// Ownership: push() takes the FFPacket; pop()/popForFlush() hand it back.
// Remaining packets are released (av_packet_unref + av_freep) on destruction.

enum class StreamKind : uint8_t { Audio = 0, Video = 1 };

struct InterleaverConfig {
    bool audioEnabled = true;
    AVRational audioTimeBase{0, 1};
    AVRational videoTimeBase{0, 1};
    size_t maxPendingWhileOpposingAbsent = 24;
    size_t maxPending = 64;
    int64_t stallReleaseUs = 1000000;
};

struct InterleaverStats {
    uint64_t pushed[2] = {};
    uint64_t popped[2] = {};
    uint64_t durationAdjusted[2] = {};
    uint64_t stallReleases = 0;
    uint64_t droppedBeforeKeyframe = 0;
    size_t maxPending[2] = {};
    // Largest number of packets held before the opposing stream first delivered.
    size_t maxStartupPending[2] = {};
};

class PacketInterleaver
{
public:
    explicit PacketInterleaver(const InterleaverConfig &config);
    ~PacketInterleaver();
    PacketInterleaver(const PacketInterleaver &) = delete;
    PacketInterleaver &operator=(const PacketInterleaver &) = delete;

    // False: do not pull more packets of this kind for now (bounded backpressure),
    // or the stream is disabled/finished.
    bool wants(StreamKind kind) const;
    void push(StreamKind kind, FFPacket *packet);
    void finish(StreamKind kind);

    // Next packet that may be written now, or nullptr to wait.
    FFPacket *pop(int64_t nowUs, StreamKind *kind);
    // Ignores waiting rules (both streams finished, or stopping): smallest key first.
    FFPacket *popForFlush(StreamKind *kind);

    bool empty() const;
    bool finished(StreamKind kind) const;
    bool everPushed(StreamKind kind) const;
    size_t pending(StreamKind kind) const;
    const InterleaverStats &stats() const { return stats_; }

private:
    struct Pending {
        FFPacket *packet = nullptr;
        int64_t keyUs = 0;
    };

    bool enabled(int index) const;
    bool active(int index) const;   // enabled and not finished
    bool ready(int index) const;
    FFPacket *take(int index, StreamKind *kind);
    int64_t keyUs(int index, const AVPacket &packet) const;

    InterleaverConfig config_;
    InterleaverStats stats_;
    std::deque<Pending> queue_[2];
    bool finished_[2] = {};
    bool everPushed_[2] = {};
    bool stalled_[2] = {};
    bool videoKeyframeSeen_ = false;
    int64_t waitSinceUs_ = -1;
};
