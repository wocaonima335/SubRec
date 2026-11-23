#include "muxer/ffmuxer.h"
// Link-time fake using the production declaration, like encoder_failure_tests.
// The harness drains actual AAC packets to this sink; no MP4 is created/decoded.
FFMuxer::FFMuxer() = default;
FFMuxer::~FFMuxer() = default;
int FFMuxer::addStream(const AVCodecContext *) { return 0; }
int FFMuxer::getAStreamIndex() { return 0; }
MuxWriteResult FFMuxer::write(AVPacket *packet)
{
    if (!packet || packet->size <= 0) return MuxWriteResult::Fatal;
    writtenPacketCount.fetch_add(1, std::memory_order_relaxed);
    return MuxWriteResult::Ok;
}
FinalizeState FFMuxer::finalize(std::chrono::milliseconds) { return FinalizeState::NotFinalized; }
FinalizeState FFMuxer::finalizeState() const { return FinalizeState::NotFinalized; }
uint64_t FFMuxer::writtenPackets() const { return writtenPacketCount.load(std::memory_order_relaxed); }
