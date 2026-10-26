#include "ffvfilter.h"

#include "decoder/ffvdecoder.h"
#include "queue/ffvframequeue.h"

#include <iostream>

extern "C" {
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#define FF_VIDEO_FRMAE_RATE {30, 1}
#define FF_VIDEO_TIME_BASE {1, 10000000}

FFVFilter::FFVFilter()
    : overlayX(0)
    , overlayY(0)
    , overlayW(0)
    , overlayH(0)
{}

FFVFilter::~FFVFilter()
{
    std::lock_guard<std::shared_mutex> lock(filterMutex);
    if (filterGraph) {
        avfilter_graph_free(&filterGraph);
    }
}

void FFVFilter::init(FFVFrameQueue *encodecFrmQueue_, FFVDecoder *vDecoder_)
{
    std::lock_guard<std::shared_mutex> lock(filterMutex);
    encoderFrameQueue = encodecFrmQueue_;

    vDecoder = vDecoder_;
}

void FFVFilter::updateDecoder(FFVDecoder *vDecoder_)
{
    std::lock_guard<std::shared_mutex> lock(filterMutex);
    vDecoder = vDecoder_;
    codecCtx = nullptr;
    stream = nullptr;
}

int FFVFilter::sendFilter()
{
    std::lock_guard<std::shared_mutex> lock(filterMutex);

    if (codecCtx == nullptr) {
        initFilter(vDecoder->getCodecCtx(), vDecoder->getStream());
    }

    AVFrame *filterFrame = av_frame_alloc();
    int ret = av_buffersink_get_frame(bufferSinkCtx, filterFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_frame_unref(filterFrame);
        av_frame_free(&filterFrame);
        return 0;
    } else if (ret < 0) {
        qDebug() << "Get BufferSinkCtx Frame Fail !";
        printError(ret);
        av_frame_unref(filterFrame);
        av_frame_free(&filterFrame);
        return -1;
    }

    encoderFrameQueue->enqueue(filterFrame);

    return 0;
}

AVRational FFVFilter::getFrameRate()
{
    return FF_VIDEO_FRMAE_RATE;
}

AVRational FFVFilter::getTimeBase()
{
    return FF_VIDEO_TIME_BASE;
}

AVMediaType FFVFilter::getMediaType()
{
    return AVMEDIA_TYPE_VIDEO;
}

void FFVFilter::setOverLayPos(int x, int y, int w, int h)
{
    overlayX = x;
    overlayY = y;
    overlayW = w;
    overlayH = h;
}

void FFVFilter::sendEncodeFrame(AVFrame *frame)
{
    // if (codecCtx == nullptr) {
    //     codecCtx = vDecoder->getCodecCtx();
    //     stream = vDecoder->getStream();
    // }
    encoderFrameQueue->enqueue(frame);
}

void FFVFilter::sendFinish()
{
    if (encoderFrameQueue) {
        encoderFrameQueue->sendFinish();
    }
}

void FFVFilter::initFilter(AVCodecContext *codecCtx_, AVStream *stream_)
{
    codecCtx = codecCtx_;
    stream = stream_;

    return;

    filterGraph = avfilter_graph_alloc();
    if (!filterGraph) {
        qDebug() << "Allocc Filter Graph Fail";
    }

    createBufferFilter(&bufferCtx, codecCtx, stream, "in");
    createBufferSinkFilter();

    linkFilters(bufferCtx, 0, bufferSinkCtx, 0);

    avfilter_graph_set_auto_convert(filterGraph, AVFILTER_AUTO_CONVERT_ALL);

    int ret = avfilter_graph_config(filterGraph, nullptr);

    if (ret < 0) {
        printError(ret);
        qDebug() << "Config FilterGraph Failed!";
        return;
    }
}

void FFVFilter::printError(int ret)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int res = av_strerror(ret, errorBuffer, sizeof errorBuffer);
    if (res < 0) {
        qDebug() << "Unknow Error!";
    } else {
        qDebug() << "Error:" << errorBuffer;
    }
}

void FFVFilter::createBufferFilter(AVFilterContext **ctx,
                                   AVCodecContext *codecCtx,
                                   AVStream *stream,
                                   const char *name)
{
    const AVFilter *bufferFilter = avfilter_get_by_name("buffer");

    if (!bufferFilter) {
        qDebug() << "Buffer filter not found!";
        return;
    }

    AVRational timeBase = stream->time_base;

    char args[512];
    snprintf(args,
             sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             codecCtx->width,
             codecCtx->height,
             AV_PIX_FMT_YUV420P,
             timeBase.num,
             timeBase.den,
             codecCtx->sample_aspect_ratio.num,
             codecCtx->sample_aspect_ratio.den);

    int ret = avfilter_graph_create_filter(ctx, bufferFilter, name, args, nullptr, filterGraph);
    if (ret < 0) {
        printError(ret);
        qDebug() << "Create Buffer Filter Failed: " << name;
        return;
    }
}

void FFVFilter::linkFilters(AVFilterContext *src, int srcPad, AVFilterContext *dst, int dstPad)
{
    int ret = avfilter_link(src, srcPad, dst, dstPad);
    if (ret < 0) {
        printError(ret);
        qDebug() << "Failed to link filters: " << avfilter_pad_get_name(src->output_pads, srcPad)
                 << " -> " << avfilter_pad_get_name(dst->input_pads, dstPad);
        return;
    }
}

void FFVFilter::createBufferSinkFilter()
{
    const AVFilter *bufferSinkFilter = avfilter_get_by_name("buffersink");
    if (!bufferSinkFilter) {
        qDebug() << "Buffersink filter not found!";
        return;
    }

    // 先分配上下文，再设置参数
    int ret = avfilter_graph_create_filter(&bufferSinkCtx,
                                           bufferSinkFilter,
                                           "out",
                                           nullptr,
                                           nullptr,
                                           filterGraph);
    if (ret < 0) {
        printError(ret);
        qDebug() << "Create BufferSink Failed!";
        return;
    }
}

void FFVFilter::getOverLayPos(int *x, int *y, int *w, int *h)
{
    std::lock_guard<std::shared_mutex> lock(filterMutex);
    *x = overlayX;
    *y = overlayY;
    *w = overlayW;
    *h = overlayH;
}
