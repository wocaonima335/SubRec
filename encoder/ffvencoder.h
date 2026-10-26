#ifndef FFVENCODER_H
#define FFVENCODER_H

#include <chrono>
#include <mutex>

#include <QString>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

class FFVPacketQueue;

struct FFVEncoderPars
{
    int width;
    int height;
    int biteRate;
    enum AVPixelFormat videoFmt;
    AVRational frameRate;
};

struct VideoEncoderSelection
{
    QString codecName;
    QString displayName;
    bool isHardware = false;
    bool success = false;
    QString detail;
};

class FFVEncoder
{
public:
    FFVEncoder();
    ~FFVEncoder();

    void init(FFVPacketQueue *pktQueue_);
    void requestDrainStop();
    void requestAbort();
    void close();
    void wakeAllThread();

    int encode(AVFrame *frame, int streamIndex, int64_t pts, AVRational timeBase);
    void finish(int streamIndex);
    AVCodecContext *getCodecCtx();
    FFVEncoderPars *getEncoderPars();
    bool initVideo(AVFrame *frame, AVRational fps);
    VideoEncoderSelection videoEncoderSelection() const;
    void resetPtsClock();

private:
    void printError(int ret);

private:
    FFVPacketQueue *pktQueue = nullptr;
    AVCodecContext *codecCtx = nullptr;
    FFVEncoderPars *vPars = nullptr;
    VideoEncoderSelection m_selection;
    int64_t lastPts = -1;

    mutable std::mutex mutex;

    // 性能监控相关
    std::chrono::steady_clock::time_point lastEncodeTime;
    double avgEncodeTime = 0.0;
    int encodeCount = 0;
    static constexpr double TARGET_ENCODE_TIME_MS = 33.0; // 30fps目标时间
};

#endif // FFVENCODER_H
