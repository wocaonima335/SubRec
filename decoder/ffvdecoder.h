#ifndef FFVDECODER_H
#define FFVDECODER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/cpu.h>
#include <libavutil/hwcontext.h>
}
#include <atomic>
#include <memory>
#include <mutex>

struct FFVideoPars
{
    int width;
    int height;
    AVRational frameRate;
    AVPixelFormat pixFmtEnum;
    AVRational timeBase;
};

class FFVFrameQueue;
class FFVResampler;

class FFVDecoder
{
public:
    explicit FFVDecoder();
    ~FFVDecoder();
    void decode(AVPacket *packet);
    void init(AVStream *stream_, FFVFrameQueue *frmQueue_);
    void flushDecoder();
    int getTotalSec();
    FFVideoPars *getVideoPars();

    AVCodecContext *getCodecCtx();
    AVStream *getStream();

    void enqueueNull();
    void wakeAllThread();
    void requestDrainStop();
    void requestAbort();
    void stop();
    void flushQueue();
    void close();

private:
    // 私有构造函数

    void initResampler();
    void initVideoPars(AVFrame *frame);
    void printError(int ret);

private:
    AVCodecContext *codecCtx = nullptr;
    AVStream *stream = nullptr;
    FFVFrameQueue *frmQueue = nullptr;
    FFVideoPars *vPars = nullptr;
    FFVideoPars *swsvPars = nullptr;
    FFVResampler *resampler = nullptr;

    bool hardDecodeFlag = false;

    std::atomic<bool> m_stop;

    std::mutex mutex;
};

#endif // FFVDECODER_H
