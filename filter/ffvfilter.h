#ifndef FFVFILTER_H
#define FFVFILTER_H

#include <shared_mutex>

// 前向声明
struct AVFrame;
struct AVCodecContext;
struct AVStream;
struct AVFilterGraph;
struct AVFilterContext;
struct AVRational;

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
}

class FFVFrameQueue;
class FFVDecoder;

class FFVFilter
{
public:
    FFVFilter();
    ~FFVFilter();

    void init(FFVFrameQueue *encoderFrmQueue_, FFVDecoder *vDecoder_);
    void updateDecoder(FFVDecoder *vDecoder_);
    int sendFilter();

    AVRational getFrameRate();
    AVRational getTimeBase();
    enum AVMediaType getMediaType();
    void setOverLayPos(int x, int y, int w, int h);
    void sendEncodeFrame(AVFrame *frame);
    void sendFinish();

private:
    void initFilter(AVCodecContext *codecCtx_, AVStream *stream_);
    void printError(int ret);

    void createBufferFilter(AVFilterContext **ctx,
                            AVCodecContext *codecCtx,
                            AVStream *stream,
                            const char *name);
    void linkFilters(AVFilterContext *src, int srcPad, AVFilterContext *dst, int dstPad);
    void createBufferSinkFilter();

    void getOverLayPos(int *x, int *y, int *w, int *h);

private:
    FFVFrameQueue *encoderFrameQueue = nullptr;
    FFVDecoder *vDecoder = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVStream *stream = nullptr;

    AVFilterGraph *filterGraph = nullptr;
    AVFilterContext *bufferCtx = nullptr;
    AVFilterContext *bufferSinkCtx = nullptr;
    AVFilterContext *scaleCtx = nullptr;
    AVFilterContext *overlayCtx = nullptr;

    int overlayX;
    int overlayY;
    int overlayW;
    int overlayH;

    std::shared_mutex filterMutex;
};

#endif // FFVFILTER_H
