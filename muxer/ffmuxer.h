#ifndef FFMUXER_H
#define FFMUXER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

class FFMuxer
{
public:
    FFMuxer();
    ~FFMuxer();
    // 0 on success, negative AVERROR on failure; old ignored returns are valid.
    // Reinit requires close and joining previous workers first.
    int init(const std::string &url_, const std::string &format_ = "mp4");
    // Borrows only for this call. AVStream owns a deep copy of codecpar.
    int addStream(const AVCodecContext *codecCtx);
    int mux(AVPacket *packet);
    int writeHeader(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    int writeTrailer();
    void requestAbort(); // No join; wakes stream wait and cooperative FFmpeg I/O.
    int getAStreamIndex();
    int getVStreamIndex();
    int getStreamTiming(AVMediaType type, AVRational *timeBase,
                        AVRational *frameRate = nullptr);
    int close(); // Serialized with mux; idempotent. Callers own worker lifetime.

private:
    int initMuxer();
    int failLocked(int error);
    int writeTrailerLocked();
    int closeLocked();
    static int interruptIO(void *opaque);
    void printError(int ret);

    std::string url;
    std::string format;
    bool headerFlag = false;
    bool trailerFlag = false;
    int terminalError = 0;
    int trailerResult = 0;
    int closeResult = 0;
    uint64_t generation = 0;
    std::atomic<bool> abortRequested{false};
    AVFormatContext *fmtCtx = nullptr;
    AVStream *aStream = nullptr;
    AVStream *vStream = nullptr;
    AVRational aTimeBase{0, 1};
    AVRational vTimeBase{0, 1};
    AVRational vFrameRate{0, 1};
    int aStreamIndex = -1;
    int vStreamIndex = -1;
    std::mutex mutex;
    std::condition_variable streamsReady;
};

#endif // FFMUXER_H
