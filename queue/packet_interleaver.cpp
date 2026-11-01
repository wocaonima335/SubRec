#include "packet_interleaver.h"

#include <algorithm>
#include <limits>

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

namespace {
constexpr int kAudio = static_cast<int>(StreamKind::Audio);
constexpr int kVideo = static_cast<int>(StreamKind::Video);

// Same release as FFPacketTraits::release (queue/ffboundedqueue.h), kept local so
// this unit does not depend on Qt.
void releasePacket(FFPacket *packet)
{
    if (!packet)
        return;
    av_packet_unref(&packet->packet);
    av_freep(&packet);
}
}

PacketInterleaver::PacketInterleaver(const InterleaverConfig &config) : config_(config)
{
    config_.maxPendingWhileOpposingAbsent = std::max<size_t>(2, config_.maxPendingWhileOpposingAbsent);
    config_.maxPending = std::max<size_t>(2, config_.maxPending);
}

PacketInterleaver::~PacketInterleaver()
{
    for (auto &queue : queue_) {
        for (auto &pending : queue)
            releasePacket(pending.packet);
        queue.clear();
    }
}

bool PacketInterleaver::enabled(int index) const
{
    return index == kVideo || config_.audioEnabled;
}

bool PacketInterleaver::active(int index) const
{
    return enabled(index) && !finished_[index];
}

bool PacketInterleaver::ready(int index) const
{
    const auto &queue = queue_[index];
    return !queue.empty() && (queue.size() >= 2 || finished_[index] || stalled_[index]);
}

int64_t PacketInterleaver::keyUs(int index, const AVPacket &packet) const
{
    const int64_t ts = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
    if (ts == AV_NOPTS_VALUE)
        return std::numeric_limits<int64_t>::min();   // let the muxer guard reject it first
    const AVRational tb = index == kAudio ? config_.audioTimeBase : config_.videoTimeBase;
    if (tb.num <= 0 || tb.den <= 0)
        return ts;
    return av_rescale_q(ts, tb, AVRational{1, 1000000});
}

bool PacketInterleaver::wants(StreamKind kind) const
{
    const int index = static_cast<int>(kind);
    if (!active(index))
        return false;
    const int other = 1 - index;
    const size_t limit = active(other) && !everPushed_[other] ? config_.maxPendingWhileOpposingAbsent
                                                             : config_.maxPending;
    return queue_[index].size() < limit;
}

void PacketInterleaver::push(StreamKind kind, FFPacket *packet)
{
    if (!packet)
        return;
    const int index = static_cast<int>(kind);
    if (!enabled(index) || finished_[index]) {
        releasePacket(packet);
        return;
    }
    if (index == kVideo && !videoKeyframeSeen_) {
        if (!(packet->packet.flags & AV_PKT_FLAG_KEY)) {
            ++stats_.droppedBeforeKeyframe;
            releasePacket(packet);
            return;
        }
        videoKeyframeSeen_ = true;
    }
    queue_[index].push_back(Pending{packet, keyUs(index, packet->packet)});
    everPushed_[index] = true;
    stalled_[index] = false;
    ++stats_.pushed[index];
    stats_.maxPending[index] = std::max(stats_.maxPending[index], queue_[index].size());
    const int other = 1 - index;
    if (active(other) && !everPushed_[other])
        stats_.maxStartupPending[index] = std::max(stats_.maxStartupPending[index], queue_[index].size());
}

void PacketInterleaver::finish(StreamKind kind)
{
    finished_[static_cast<int>(kind)] = true;
}

FFPacket *PacketInterleaver::take(int index, StreamKind *kind)
{
    Pending head = queue_[index].front();
    queue_[index].pop_front();
    AVPacket &packet = head.packet->packet;
    if (!queue_[index].empty()) {
        const AVPacket &next = queue_[index].front().packet->packet;
        if (packet.dts != AV_NOPTS_VALUE && next.dts != AV_NOPTS_VALUE) {
            const int64_t duration = next.dts - packet.dts;
            if (duration > 0) {
                if (duration != packet.duration)
                    ++stats_.durationAdjusted[index];
                packet.duration = duration;
            }
        }
    }
    ++stats_.popped[index];
    if (kind)
        *kind = static_cast<StreamKind>(index);
    return head.packet;
}

FFPacket *PacketInterleaver::pop(int64_t nowUs, StreamKind *kind)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        int candidate = -1;
        for (const int index : {kVideo, kAudio}) {   // strict '<' keeps ties on video
            if (ready(index) && (candidate < 0 || queue_[index].front().keyUs < queue_[candidate].front().keyUs))
                candidate = index;
        }
        if (candidate < 0) {
            waitSinceUs_ = -1;
            return nullptr;
        }
        const int other = 1 - candidate;
        const int64_t candidateKey = queue_[candidate].front().keyUs;
        const bool blocked = active(other) && !stalled_[other] &&
                             (queue_[other].empty() || queue_[other].front().keyUs < candidateKey);
        if (!blocked) {
            waitSinceUs_ = -1;
            return take(candidate, kind);
        }
        // Startup: wait for the first packet of the other stream indefinitely;
        // the bounded producer queue applies backpressure.
        if (!everPushed_[other]) {
            waitSinceUs_ = -1;
            return nullptr;
        }
        if (waitSinceUs_ < 0)
            waitSinceUs_ = nowUs;
        if (nowUs - waitSinceUs_ < config_.stallReleaseUs)
            return nullptr;
        // The other stream stopped delivering: treat it as absent (or its lone
        // head as ready) until it pushes again, then re-evaluate once.
        stalled_[other] = true;
        ++stats_.stallReleases;
        waitSinceUs_ = -1;
    }
    return nullptr;
}

FFPacket *PacketInterleaver::popForFlush(StreamKind *kind)
{
    int candidate = -1;
    for (const int index : {kVideo, kAudio}) {
        if (!queue_[index].empty() &&
            (candidate < 0 || queue_[index].front().keyUs < queue_[candidate].front().keyUs))
            candidate = index;
    }
    return candidate < 0 ? nullptr : take(candidate, kind);
}

bool PacketInterleaver::empty() const
{
    return queue_[kAudio].empty() && queue_[kVideo].empty();
}

bool PacketInterleaver::finished(StreamKind kind) const
{
    return finished_[static_cast<int>(kind)];
}

bool PacketInterleaver::everPushed(StreamKind kind) const
{
    return everPushed_[static_cast<int>(kind)];
}

size_t PacketInterleaver::pending(StreamKind kind) const
{
    return queue_[static_cast<int>(kind)].size();
}
