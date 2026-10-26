#include "ffvdecoder.h"
#include "queue/ffvframequeue.h"
#include "resampler/ffvresampler.h"

#include <iostream>
#include <QCryptographicHash>

namespace {
std::atomic<int> g_vdecFullMd5LogCount{0};

quint64 hashPlaneSample(const uint8_t *data, int width, int height, int linesize, int maxSamples = 128)
{
    static constexpr quint64 kOffset = 1469598103934665603ull;
    static constexpr quint64 kPrime = 1099511628211ull;

    if (!data || width <= 0 || height <= 0 || linesize <= 0) {
        return 0;
    }

    quint64 hash = kOffset;
    const int rowStep = std::max(1, height / 16);
    const int colStep = std::max(1, width / 16);
    int samples = 0;

    for (int row = 0; row < height && samples < maxSamples; row += rowStep) {
        const uint8_t *line = data + row * linesize;
        for (int col = 0; col < width && samples < maxSamples; col += colStep) {
            hash ^= line[col];
            hash *= kPrime;
            ++samples;
        }
    }

    hash ^= data[(height - 1) * linesize + std::min(width - 1, linesize - 1)];
    hash *= kPrime;
    return hash;
}

quint64 frameFingerprint(const AVFrame *frame)
{
    if (!frame) {
        return 0;
    }

    quint64 hash = 1469598103934665603ull;
    auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(hashPlaneSample(frame->data[0], frame->width, frame->height, frame->linesize[0]));
    if (frame->data[1]) {
        mix(hashPlaneSample(frame->data[1], std::max(1, frame->width / 2), std::max(1, frame->height / 2), frame->linesize[1], 64));
    }
    if (frame->data[2]) {
        mix(hashPlaneSample(frame->data[2], std::max(1, frame->width / 2), std::max(1, frame->height / 2), frame->linesize[2], 64));
    }
    return hash;
}

void addPlaneMd5(QCryptographicHash &md5,
                 const uint8_t *data,
                 int widthBytes,
                 int height,
                 int linesize)
{
    if (!data || widthBytes <= 0 || height <= 0 || linesize == 0) {
        return;
    }

    for (int row = 0; row < height; ++row) {
        md5.addData(reinterpret_cast<const char *>(data + row * linesize), widthBytes);
    }
}

QByteArray fullFrameMd5(const AVFrame *frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return {};
    }

    QCryptographicHash md5(QCryptographicHash::Md5);
    const auto fmt = static_cast<AVPixelFormat>(frame->format);

    switch (fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        addPlaneMd5(md5, frame->data[0], frame->width, frame->height, frame->linesize[0]);
        addPlaneMd5(md5, frame->data[1], frame->width / 2, frame->height / 2, frame->linesize[1]);
        addPlaneMd5(md5, frame->data[2], frame->width / 2, frame->height / 2, frame->linesize[2]);
        break;
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV422P:
        addPlaneMd5(md5, frame->data[0], frame->width, frame->height, frame->linesize[0]);
        addPlaneMd5(md5, frame->data[1], frame->width / 2, frame->height, frame->linesize[1]);
        addPlaneMd5(md5, frame->data[2], frame->width / 2, frame->height, frame->linesize[2]);
        break;
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGBA:
        addPlaneMd5(md5, frame->data[0], frame->width * 4, frame->height, frame->linesize[0]);
        break;
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
        addPlaneMd5(md5, frame->data[0], frame->width * 3, frame->height, frame->linesize[0]);
        break;
    default:
        addPlaneMd5(md5, frame->data[0], std::min(frame->linesize[0], frame->width * 4), frame->height, frame->linesize[0]);
        break;
    }

    return md5.result().toHex();
}
}

FFVDecoder::~FFVDecoder()
{
    close();
}

void FFVDecoder::decode(AVPacket *packet)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (codecCtx == nullptr)
    {
        return;
    }

    qDebug() << "[VDec] decode enter, packet=" << packet
             << " stop=" << m_stop.load(std::memory_order_acquire);

    int ret = avcodec_send_packet(codecCtx, packet);
    qDebug() << "[VDec] send_packet ret=" << ret;

    if (ret < 0 && ret != AVERROR(EAGAIN))
    {
        printError(ret);
        avcodec_free_context(&codecCtx);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    while (ret >= 0)
    {
        if (m_stop.load(std::memory_order_acquire))
        {
            qDebug() << "[VDec] m_stop observed, break receive-loop";
            break;
        }

        qDebug() << "[VDec] before receive_frame";
        ret = avcodec_receive_frame(codecCtx, frame);
        qDebug() << "[VDec] receive_frame ret=" << ret;

        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                continue;
            }
            else
            {
                printError(ret);
                av_frame_free(&frame);
                avcodec_free_context(&codecCtx);
                return;
            }
        }
        else
        {
            if (vPars == nullptr)
            {
                vPars = new FFVideoPars();
                swsvPars = new FFVideoPars();

                initVideoPars(frame);
                if (vPars->pixFmtEnum != swsvPars->pixFmtEnum)
                {
                    resampler = new FFVResampler();
                    initResampler();
                }
                // 初始化视频参数后打印一次
                qDebug() << "[VDec] initVideoPars: w=" << frame->width << " h=" << frame->height
                         << " in_pixfmt=" << codecCtx->pix_fmt
                         << " out_pixfmt=" << swsvPars->pixFmtEnum
                         << " fr=" << codecCtx->framerate.num << "/" << codecCtx->framerate.den;
            }

            if (resampler)
            {
                qDebug() << "[VDec] before resample";
                AVFrame *swsFrame = nullptr;
                resampler->resample(frame, &swsFrame);
                qDebug() << "[VDec] after resample";
                qDebug() << "[VDecHash] raw=0x" << QString::number(frameFingerprint(frame), 16)
                         << " yuv=0x" << QString::number(frameFingerprint(swsFrame), 16)
                         << " pts=" << frame->pts;
                const int md5LogIndex = g_vdecFullMd5LogCount.fetch_add(1, std::memory_order_relaxed) + 1;
                if (md5LogIndex <= 60) {
                    qDebug().noquote()
                        << "[VDecFullMd5]"
                        << "index=" << md5LogIndex
                        << "rawPts=" << frame->pts
                        << "rawMd5=" << fullFrameMd5(frame)
                        << "yuvMd5=" << fullFrameMd5(swsFrame)
                        << "fmt=" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format))
                        << "outFmt=" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(swsFrame->format));
                }
                av_frame_unref(frame);
                if (m_stop.load(std::memory_order_acquire))
                {
                    av_frame_unref(swsFrame);
                    av_frame_free(&swsFrame);
                    m_stop.store(false, std::memory_order_release);
                    break;
                }
                else
                {
                    // 解码队列
                    AVFrame *decoderFrame = av_frame_clone(swsFrame);
                    qDebug() << "video frame pts: " << decoderFrame->pts;
                    if (frmQueue != nullptr)
                    {
                        qDebug() << "[VDec] before frmQueue enqueue";
                        frmQueue->enqueue(decoderFrame);
                        qDebug() << "[VDec] after frmQueue enqueue";
                    }
                    qDebug() << "[VDec] before release decoderFrame/swsFrame";
                    AVFrameTraits::release(decoderFrame);
                    AVFrameTraits::release(swsFrame);
                    qDebug() << "[VDec] after release decoderFrame/swsFrame";
                }
            }
            else
            {
                if (m_stop.load(std::memory_order_acquire))
                {
                    av_frame_unref(frame);
                    break;
                }
                else
                {
                    qDebug() << "[VDec][nosws] frame pts=" << frame->pts;

                    if (frmQueue)
                    {
                        AVFrame *decoderFrame = av_frame_clone(frame);
                        qDebug() << "[VDec][nosws] before frmQueue enqueue";
                        frmQueue->enqueue(decoderFrame);
                        qDebug() << "[VDec][nosws] after frmQueue enqueue";
                        av_frame_free(&decoderFrame);
                        qDebug() << "VIDEO decoder run !";
                    }

                    av_frame_unref(frame);
                    qDebug() << "decoder eunqueue!";
                }
            }
        }
    }
    av_frame_free(&frame);
    qDebug() << "[VDec] decode leave";
}

void FFVDecoder::init(AVStream *stream_, FFVFrameQueue *frmQueue_)
{
    std::lock_guard<std::mutex> lock(mutex);
    stream = stream_;
    frmQueue = frmQueue_;
    m_stop.store(false, std::memory_order_release);
    g_vdecFullMd5LogCount.store(0, std::memory_order_release);

    const AVCodec *codec = nullptr;
    if (stream->codecpar == nullptr)
    {
        return;
    }

    if (stream->codecpar->codec_id == AV_CODEC_ID_H264)
    {
        codec = avcodec_find_decoder_by_name("h264");
        hardDecodeFlag = false;
    }
    else
    {
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
    }

    if (codec == nullptr)
    {
        qDebug() << "找不到视频解码器";
        return;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr)
    {
        qDebug() << "分配解码器上下文失败";
        avcodec_free_context(&codecCtx);
        return;
    }

    int ret = avcodec_parameters_to_context(codecCtx, stream->codecpar);
    if (ret < 0)
    {
        printError(ret);
        avcodec_free_context(&codecCtx);
        return;
    }
    AVDictionary *codec_options = nullptr;
    const bool isRawVideo = stream->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO;
    qDebug() << "[VDec] init codec=" << avcodec_get_name(stream->codecpar->codec_id)
             << " pix_fmt=" << av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecCtx->pix_fmt))
             << " rawvideo=" << isRawVideo;
    if (isRawVideo)
    {
        codecCtx->thread_count = 1;
        codecCtx->thread_type = 0;
        qDebug() << "[VDec] rawvideo decoder forced single-thread mode";
    }
    else if (!hardDecodeFlag)
    {
        codecCtx->thread_count = av_cpu_count();
        av_dict_set(&codec_options, "fast", "1", 0);
    }
    else
    {
        av_dict_set(&codec_options, "low_latency", "1", 0);
    }

    ret = avcodec_open2(codecCtx, codec, &codec_options);

    if (ret < 0)
    {
        printError(ret);
        avcodec_free_context(&codecCtx);
        return;
    }
    av_dict_free(&codec_options);
}

void FFVDecoder::flushDecoder()
{
    avcodec_flush_buffers(codecCtx);
}

int FFVDecoder::getTotalSec()
{
    return static_cast<int>(stream->duration * av_q2d(stream->time_base));
}

FFVideoPars *FFVDecoder::getVideoPars()
{
    return swsvPars;
}

AVCodecContext *FFVDecoder::getCodecCtx()
{
    return codecCtx;
}

AVStream *FFVDecoder::getStream()
{
    return stream;
}

void FFVDecoder::enqueueNull()
{
    frmQueue->sendFinish();
}

void FFVDecoder::wakeAllThread()
{
    frmQueue->wakeAllThread();
}

void FFVDecoder::requestDrainStop()
{
    qDebug() << "[VDec] requestDrainStop";
}

void FFVDecoder::requestAbort()
{
    m_stop.store(true, std::memory_order_release);
}

void FFVDecoder::stop()
{
    requestAbort();
}

void FFVDecoder::flushQueue()
{
    frmQueue->flushQueue();
}

void FFVDecoder::close()
{
    decode(nullptr);
    stop();

    std::lock_guard<std::mutex> lock(mutex);
    if (codecCtx)
    {
        avcodec_free_context(&codecCtx);
    }
    if (vPars)
    {
        delete vPars;
        vPars = nullptr;
    }
    if (resampler)
    {
        delete resampler;
        resampler = nullptr;
    }
    if (swsvPars)
    {
        delete swsvPars;
        swsvPars = nullptr;
    }

    qDebug() << "video Deocder close!";
}

FFVDecoder::FFVDecoder()
    : m_stop(false)
{
}

void FFVDecoder::initResampler()
{
    resampler->init(vPars, swsvPars);
}

void FFVDecoder::initVideoPars(AVFrame *frame)
{
    vPars->timeBase = stream->time_base;
    vPars->pixFmtEnum = codecCtx->pix_fmt;
    vPars->frameRate = codecCtx->framerate;

    if (codecCtx->framerate.den == 0 || codecCtx->framerate.num == 0)
    {
        vPars->frameRate = stream->avg_frame_rate;
        codecCtx->framerate = stream->avg_frame_rate; // 调整frameRate
    }

    vPars->width = frame->width;
    vPars->height = frame->height;

    memcpy(swsvPars, vPars, sizeof(FFVideoPars));
    swsvPars->pixFmtEnum = AV_PIX_FMT_YUV420P;
}

void FFVDecoder::printError(int ret)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int res = av_strerror(ret, errorBuffer, sizeof errorBuffer);
    if (res < 0)
    {
        qDebug() << "Unknow Error!";
    }
    else
    {
        qDebug() << "Error:" << errorBuffer;
    }
}
