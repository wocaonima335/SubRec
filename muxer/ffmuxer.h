#ifndef FFMUXER_H
#define FFMUXER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "muxer/mux_sink.h"
#include "queue/packet_guard.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

class FFMuxer : public IMuxSink
{
public:
    FFMuxer();
    ~FFMuxer();
    // 0 on success, negative AVERROR on failure; old ignored returns are valid.
    // Reinit requires close and joining previous workers first.
    int init(const std::string &url_, const std::string &format_ = "mp4", bool audioEnabled = true);
    // Borrows only for this call. AVStream owns a deep copy of codecpar.
    int addStream(const AVCodecContext *codecCtx);
    // Legacy entry point: 0 for written or skipped packets, negative AVERROR when
    // the output is unusable. write() reports the same outcome as MuxWriteResult.
    int mux(AVPacket *packet);
    int writeHeader(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    int writeTrailer();
    // No join; wakes stream waits and stops accepting packets. Before any packet
    // was written it also interrupts FFmpeg I/O at once. Afterwards it only arms
    // the finalize budget, so the trailer can still be written.
    void requestAbort();
    int getAStreamIndex();
    int getVStreamIndex();
    int getStreamTiming(AVMediaType type, AVRational *timeBase,
                        AVRational *frameRate = nullptr);
    // Serialized with mux; idempotent. Finalizes with the default budget when
    // finalize() was not called. Callers own worker lifetime.
    int close();

    MuxWriteResult write(AVPacket *packet) override;
    FinalizeState finalize(std::chrono::milliseconds budget) override;
    FinalizeState finalizeState() const override;
    uint64_t writtenPackets() const override;
    // Budget used by close() and by requestAbort() after data was written.
    void setFinalizeBudget(std::chrono::milliseconds budget);
    // av_interleaved_write_frame() EINVAL/INVALIDDATA results that were skipped.
    uint64_t rejectedWrites() const;
    // Streams whose container time base cannot represent every encoder tick.
    uint64_t timescaleMismatches() const;
    // Result of the trailer write + AVIO close performed by finalize()/close().
    int lastCloseResult() const;
    uint64_t skippedPackets() const;
    // Missing timestamps rejected by the pre-write guard. Duplicate DTS remains
    // exclusively skippedPackets(), so these counters are disjoint.
    uint64_t guardDropped() const;
    uint64_t guardDropped(AVMediaType type) const;

private:
    int initMuxer();
    int muxLocked(AVPacket *packet, MuxWriteResult *result);
    void armFinalizeDeadline(std::chrono::milliseconds budget);
    static int64_t steadyNowUs();
    int failLocked(int error);
    int writeTrailerLocked();
    int closeLocked();
    static int interruptIO(void *opaque);
    void printError(int ret);

    std::string url;
    std::string format;
    bool expectAudio = true;
    bool headerFlag = false;
    bool trailerFlag = false;
    int terminalError = 0;
    int trailerResult = 0;
    int closeResult = 0;
    PacketGuard guards[2]; // audio, video: third timestamp line, in stream time base
    uint64_t guardDroppedCount[2]{}; // audio, video; retained on close, reset by init
    uint64_t skippedPacketCount = 0;
    uint64_t skippedWarningCount = 0;
    uint64_t rejectedWriteCount = 0, consecutiveRejectedWrites = 0;
    uint64_t rejectedWindowCount = 0;
    int64_t rejectedWindowStartUs = 0;
    uint64_t timescaleMismatchCount = 0;
    FinalizeState finalizeResult = FinalizeState::NotFinalized;
    std::atomic<uint64_t> writtenPacketCount{0};
    uint64_t generation = 0;
    std::atomic<bool> abortRequested{false};
    std::atomic<bool> hardInterrupt{false};      // interrupt FFmpeg I/O now
    std::atomic<int64_t> finalizeDeadlineUs{0};  // steady clock; 0 = none
    std::atomic<int64_t> finalizeBudgetMs{5000};
    AVFormatContext *fmtCtx = nullptr;
    AVStream *aStream = nullptr;
    AVStream *vStream = nullptr;
    AVRational aTimeBase{0, 1};
    AVRational vTimeBase{0, 1};
    AVRational vFrameRate{0, 1};
    int aStreamIndex = -1;
    int vStreamIndex = -1;
    mutable std::mutex mutex;
    std::condition_variable streamsReady;
};

#endif // FFMUXER_H
