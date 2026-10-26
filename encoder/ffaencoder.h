#ifndef FFAENCODER_H
#define FFAENCODER_H

#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class FFAPacketQueue;

struct FFAEncoderPars
{
    int sampleRate;
    int nbChannel;
    int biteRate;
    enum AVSampleFormat audioFmt;
};

struct PendingFrame
{
    std::vector<uint8_t> data[8];
    int samples = 0;
    int64_t next_pts = AV_NOPTS_VALUE;
};

class FFAEncoder
{
public:
    FFAEncoder();
    ~FFAEncoder();

    void init(FFAPacketQueue *pktQueue_);
    void requestDrainStop();
    void requestAbort();
    void close();
    void wakeAllThread();

    int encode(AVFrame *frame, int streamIndex, int64_t pts, AVRational timeBase);
    void finish(int streamIndex);

    FFAEncoderPars *getEncoderPars();
    AVCodecContext *getCodecCtx();
    void flush();
    void resetPending(int64_t base_pts);

    void initAudio(AVFrame *frame);

private:
    void printError(int ret);

private:
    FFAPacketQueue *pktQueue = nullptr;
    AVCodecContext *codecCtx = nullptr;
    FFAEncoderPars *aPars = nullptr;

    std::mutex mutex;
    PendingFrame pendingFrame;
};

#endif
