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
struct SwsContext;

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
    // frame decides the input path: yuv420p system memory (dshow, ddagrab cpu mode) or
    // AV_PIX_FMT_D3D11 with hw_frames_ctx (ddagrab gpu mode). D3D11 frames go to an encoder
    // that takes them directly (h264_nvenc, h264_amf); if none opens, the usual candidates
    // are opened for yuv420p and every D3D11 frame is downloaded and converted in encode().
    bool initVideo(AVFrame *frame, AVRational fps);
    VideoEncoderSelection videoEncoderSelection() const;
    void resetPtsClock();

private:
    void printError(int ret);
    AVFrame *downloadToYuv420p(const AVFrame *hwFrame);

private:
    FFVPacketQueue *pktQueue = nullptr;
    bool m_downloadHwFrames = false;
    SwsContext *m_downloadSws = nullptr;
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
