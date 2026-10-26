#ifndef FFADECODER_H
#define FFADECODER_H

#include <atomic>
#include <iostream>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
}

struct FFAudioPars
{
    int sampleRate;
    int nbChannels;
    AVRational timeBase;
    enum AVSampleFormat aFormatEnum;
    int sampleSize;
};

class FFAFrameQueue;
class FFAResampler;
class FFADecoder
{
public:
    explicit FFADecoder();
    FFADecoder(const FFADecoder &) = delete;
    FFADecoder &operator=(FFADecoder &) = delete;
    ~FFADecoder();

    void decode(AVPacket *packet);
    void setCaptureTimestampOutput(bool enabled) { captureTimestampOutput = enabled; }
    void init(AVStream *stream_, FFAFrameQueue *frmQueue_);
    void flushDecoder();
    FFAudioPars *getAudioPars();
    int getTotalSec();

    void wakeAllThreads();
    void requestDrainStop();
    void requestAbort();
    void stop();
    void enqueueNull();
    void flushQueue();
    void close();

    AVCodecContext *getCodecCtx();
    AVStream *getStream();

private:
    void printError(int ret);
    void initAudioPars(AVFrame *frame);
    void initResampler();
    void printFmt();
    void processDecodedFrame(AVFrame *frame);

private:
    bool captureTimestampOutput = false;
    AVCodecContext *codecCtx = nullptr;
    FFAFrameQueue *frmQueue = nullptr;
    AVStream *stream = nullptr;
    FFAudioPars *aPars = nullptr;
    FFAudioPars *swraPars = nullptr;
    FFAResampler *resampler = nullptr;
    std::atomic<bool> m_stop;

    std::mutex mutex;
};

#endif // FFADECODER_H
