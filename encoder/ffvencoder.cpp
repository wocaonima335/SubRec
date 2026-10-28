#include "logging/frame_log.h"
#include "ffvencoder.h"
#include "queue/ffvpacketqueue.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QStringList>
#include <QtGlobal>
#include <cstring>
#include <iostream>
#include <thread>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}


namespace {
QByteArray framePlaneMd5(const AVFrame *frame)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P || frame->width <= 0 || frame->height <= 0) {
        return {};
    }

    QCryptographicHash md5(QCryptographicHash::Md5);
    for (int row = 0; row < frame->height; ++row) {
        md5.addData(reinterpret_cast<const char *>(frame->data[0] + row * frame->linesize[0]), frame->width);
    }
    for (int row = 0; row < frame->height / 2; ++row) {
        md5.addData(reinterpret_cast<const char *>(frame->data[1] + row * frame->linesize[1]), frame->width / 2);
    }
    for (int row = 0; row < frame->height / 2; ++row) {
        md5.addData(reinterpret_cast<const char *>(frame->data[2] + row * frame->linesize[2]), frame->width / 2);
    }
    return md5.result().toHex();
}

// True if the encoder takes AV_PIX_FMT_D3D11 frames through hw_frames_ctx (its own hw_configs).
bool acceptsD3D11Frames(const AVCodec *codec)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) {
            return false;
        }
        if (config->pix_fmt == AV_PIX_FMT_D3D11
            && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX)) {
            return true;
        }
    }
}

struct FrameGuard
{
    AVFrame *frame = nullptr;
    ~FrameGuard() { av_frame_free(&frame); }
};
}
FFVEncoder::FFVEncoder() {}

FFVEncoder::~FFVEncoder()
{
    close();
}

void FFVEncoder::init(FFVPacketQueue *pktQueue_)
{
    pktQueue = pktQueue_;
}

void FFVEncoder::close()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (codecCtx)
    {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }

    if (vPars)
    {
        delete vPars;
        vPars = nullptr;
    }

    lastPts = -1;
    m_selection = {};
    m_downloadHwFrames = false;
    sws_freeContext(m_downloadSws);
    m_downloadSws = nullptr;
}

void FFVEncoder::wakeAllThread()
{
    requestAbort();
}

void FFVEncoder::requestDrainStop()
{
    qDebug() << "[FFVEncoder] requestDrainStop";
}

void FFVEncoder::requestAbort()
{
    qDebug() << "[FFVEncoder] requestAbort";
    if (pktQueue)
    {
        pktQueue->close(MediaQueueCloseReason::RequestedStop);
    }
}

int FFVEncoder::encode(AVFrame *frame, int streamIndex, int64_t pts, AVRational timeBase)
{
    Q_UNUSED(timeBase);

    if (codecCtx == nullptr)
    {
        std::cout << "nullptr" << std::endl;
        return 0;
    }

    if (frame == nullptr)
    {
        int ret = avcodec_send_frame(codecCtx, nullptr);
        if (ret < 0 && ret != AVERROR_EOF)
        {
            printError(ret);
            return -1;
        }

        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            return -1;
        }

        while (true)
        {
            ret = avcodec_receive_packet(codecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            if (ret < 0)
            {
                printError(ret);
                av_packet_free(&pkt);
                return -1;
            }

            pkt->stream_index = streamIndex;
            pktQueue->enqueue(pkt);
            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        return 0;
    }

    // // 鎬ц兘鐩戞帶寮€濮?
    // auto encodeStart = std::chrono::steady_clock::now();

    // ddagrab GPU frames with a system-memory encoder: download + convert here.
    FrameGuard converted;
    if (m_downloadHwFrames && frame->format == AV_PIX_FMT_D3D11)
    {
        converted.frame = downloadToYuv420p(frame);
        if (!converted.frame)
        {
            return -1;
        }
        frame = converted.frame;
    }

    static qint64 s_lastInputMd5LogMs = 0;
    frame->pts = pts; // 灏嗚绠楀ソ鐨?PTS 鍐欏埌 AVFrame 涓婏紝浜ょ粰缂栫爜鍣?
    qCDebug(subrec::logging::frameLog) << "[FFVEncoder]:encode pts :" << pts;
    const qint64 nowMs = av_gettime_relative() / 1000;
    if (nowMs - s_lastInputMd5LogMs >= 500) {
        s_lastInputMd5LogMs = nowMs;
        qCDebug(subrec::logging::frameLog).noquote() << "[EncoderInputMd5]" << "pts=" << pts << "md5=" << framePlaneMd5(frame);
    }
    qCDebug(subrec::logging::frameLog) << "[VEnc] before send_frame";

    int ret = avcodec_send_frame(codecCtx, frame);
    qCDebug(subrec::logging::frameLog) << "[VEnc] after send_frame ret=" << ret;
    if (ret < 0)
    {
        printError(ret);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
    {
        return -1;
    }

    while (true)
    {
        ret = avcodec_receive_packet(codecCtx, pkt);
        qCDebug(subrec::logging::frameLog) << "[VEnc] receive_packet ret=" << ret;
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        if (ret < 0)
        {
            printError(ret);
            av_packet_free(&pkt);
            return -1;
        }

        pkt->stream_index = streamIndex;
        pktQueue->enqueue(pkt);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    return 0;
}

void FFVEncoder::finish(int streamIndex)
{
    encode(nullptr, streamIndex, AV_NOPTS_VALUE, codecCtx ? codecCtx->time_base : AVRational{0, 1});
    if (pktQueue) {
        pktQueue->sendFinish();
    }
}

AVCodecContext *FFVEncoder::getCodecCtx()
{
    return codecCtx;
}

FFVEncoderPars *FFVEncoder::getEncoderPars()
{
    return vPars;
}

void FFVEncoder::printError(int ret)
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

bool FFVEncoder::initVideo(AVFrame *frame, AVRational fps)
{
    std::lock_guard<std::mutex> lock(mutex);
    resetPtsClock();
    m_selection = {};
    m_downloadHwFrames = false;
    sws_freeContext(m_downloadSws);
    m_downloadSws = nullptr;

    if (!frame || frame->width <= 0 || frame->height <= 0 || fps.num <= 0 || fps.den <= 0)
    {
        m_selection.detail = QStringLiteral("invalid video frame or frame rate");
        qWarning() << "[VEncSelection] result=failed reason=" << m_selection.detail;
        return false;
    }

    if (codecCtx)
        avcodec_free_context(&codecCtx);
    if (vPars)
    {
        delete vPars;
        vPars = nullptr;
    }

    FFVEncoderPars requestedPars{};
    requestedPars.width = frame->width;
    requestedPars.height = frame->height;
    requestedPars.videoFmt = AV_PIX_FMT_YUV420P;
    requestedPars.frameRate = fps;

    const int pixelCount = requestedPars.width * requestedPars.height;
    if (pixelCount > 1920 * 1080)
        requestedPars.biteRate = 4 * 1024 * 1024;
    else if (pixelCount > 1280 * 720)
        requestedPars.biteRate = 2 * 1024 * 1024;
    else
        requestedPars.biteRate = 1 * 1024 * 1024;

    struct EncoderCandidate
    {
        const char *name;
        const char *description;
        bool isHardware;
    };

    const EncoderCandidate candidates[] = {
        {"h264_nvenc", "NVIDIA NVENC", true},
        {"h264_amf", "AMD AMF", true},
        {"h264_qsv", "Intel QSV", true},
        {"libx264", "Software H264", false},
    };

    const bool hwFrames = frame->format == AV_PIX_FMT_D3D11 && frame->hw_frames_ctx;
    const AVPixelFormat hwSwFormat = hwFrames
        ? reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data)->sw_format
        : AV_PIX_FMT_NONE;

    QStringList failures;
    // Pass 0 (D3D11 input only): GPU passthrough to encoders that take D3D11 frames.
    // Pass 1: the original yuv420p configuration (D3D11 frames are converted in encode()).
    for (int pass = hwFrames ? 0 : 1; pass < 2; ++pass)
    {
    const bool passthrough = pass == 0;
    for (const auto &candidate : candidates)
    {
        if (passthrough && !candidate.isHardware)
            continue;
        const QString label = passthrough ? QStringLiteral("%1[d3d11]").arg(QLatin1String(candidate.name))
                                          : QString::fromLatin1(candidate.name);
        const AVCodec *codec = avcodec_find_encoder_by_name(candidate.name);
        if (codec && passthrough && !acceptsD3D11Frames(codec))
        {
            const QString reason = QStringLiteral("no D3D11 frame input");
            failures.append(QStringLiteral("%1: %2").arg(label, reason));
            qInfo().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=lookup result=skipped reason=%2")
                                     .arg(label, reason);
            continue;
        }
        if (!codec)
        {
            const QString reason = QStringLiteral("not found");
            failures.append(QStringLiteral("%1: %2").arg(label, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=lookup result=failed reason=%2")
                                             .arg(label, reason);
            continue;
        }

        AVCodecContext *candidateCtx = avcodec_alloc_context3(codec);
        if (!candidateCtx)
        {
            const QString reason = QStringLiteral("context allocation failed");
            failures.append(QStringLiteral("%1: %2").arg(label, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=allocate result=failed reason=%2")
                                             .arg(label, reason);
            continue;
        }

        candidateCtx->width = requestedPars.width;
        candidateCtx->height = requestedPars.height;
        candidateCtx->bit_rate = requestedPars.biteRate;
        candidateCtx->rc_max_rate = requestedPars.biteRate * 12 / 10;
        candidateCtx->rc_buffer_size = requestedPars.biteRate;
        candidateCtx->framerate = requestedPars.frameRate;
        candidateCtx->time_base = av_inv_q(requestedPars.frameRate);
        if (passthrough)
        {
            // The encoder reads the ddagrab textures (sw_format bgra) and converts on the GPU.
            candidateCtx->pix_fmt = AV_PIX_FMT_D3D11;
            candidateCtx->sw_pix_fmt = hwSwFormat;
            candidateCtx->hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
        }
        else
        {
            candidateCtx->pix_fmt = requestedPars.videoFmt;
        }
        candidateCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        candidateCtx->max_b_frames = 0;

        if (candidate.isHardware)
        {
            candidateCtx->gop_size = 60;
            candidateCtx->thread_count = 1;
            candidateCtx->thread_type = 0;
            candidateCtx->qmin = 10;
            candidateCtx->qmax = 40;
            candidateCtx->max_qdiff = 4;
        }
        else
        {
            candidateCtx->gop_size = 30;
            candidateCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            const int threadCount = static_cast<int>(std::thread::hardware_concurrency());
            candidateCtx->thread_count = std::max(1, std::min(threadCount, 16));
            candidateCtx->thread_type = FF_THREAD_FRAME;
            candidateCtx->slices = candidateCtx->thread_count;
            candidateCtx->qmin = 18;
            candidateCtx->qmax = 45;
        }
        candidateCtx->keyint_min = candidateCtx->gop_size;

        AVDictionary *options = nullptr;
        if (std::strstr(codec->name, "nvenc"))
        {
            av_dict_set(&options, "preset", "fast", 0);
            av_dict_set(&options, "rc", "cbr", 0);
        }
        else if (std::strstr(codec->name, "amf"))
        {
            av_dict_set(&options, "quality", "speed", 0);
            av_dict_set(&options, "rc", "cbr", 0);
        }
        else if (std::strstr(codec->name, "qsv"))
        {
            av_dict_set(&options, "preset", "fast", 0);
        }
        else
        {
            av_dict_set(&options, "preset", "ultrafast", 0);
            av_dict_set(&options, "tune", "zerolatency", 0);
            av_dict_set(&options, "x264-params", "no-cabac:ref=1:deblock=0:me=dia:subme=0:rc-lookahead=0:weightp=0:mixed-refs=0:8x8dct=0:trellis=0", 0);
        }

        const int ret = avcodec_open2(candidateCtx, codec, &options);
        av_dict_free(&options);
        if (ret < 0)
        {
            char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(ret, errorBuffer, sizeof(errorBuffer));
            const QString reason = QString::fromLocal8Bit(errorBuffer);
            failures.append(QStringLiteral("%1: %2").arg(label, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=open result=failed fferr=%2 error=%3")
                                             .arg(label)
                                             .arg(ret)
                                             .arg(reason);
            avcodec_free_context(&candidateCtx);
            continue;
        }

        codecCtx = candidateCtx;
        vPars = new FFVEncoderPars(requestedPars);
        m_downloadHwFrames = hwFrames && !passthrough;
        m_selection.codecName = QString::fromLatin1(candidate.name);
        m_selection.displayName = QString::fromLatin1(candidate.description);
        m_selection.isHardware = candidate.isHardware;
        m_selection.success = true;
        m_selection.detail = failures.isEmpty()
            ? QStringLiteral("selected with production configuration")
            : QStringLiteral("fallback after %1").arg(failures.join(QStringLiteral("; ")));
        if (passthrough)
            m_selection.detail += QStringLiteral("; input D3D11 frames (GPU passthrough)");
        else if (m_downloadHwFrames)
            m_selection.detail += QStringLiteral("; input D3D11 frames downloaded to yuv420p");

        qInfo().noquote() << QStringLiteral("[VEncSelection] winner=%1 hardware=%2 width=%3 height=%4 fps=%5/%6 bitrate=%7 detail=%8")
                                  .arg(m_selection.codecName)
                                  .arg(m_selection.isHardware ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(vPars->width)
                                  .arg(vPars->height)
                                  .arg(vPars->frameRate.num)
                                  .arg(vPars->frameRate.den)
                                  .arg(vPars->biteRate)
                                  .arg(m_selection.detail);
        return true;
    }
    } // pass

    m_selection.detail = failures.join(QStringLiteral("; "));
    qWarning().noquote() << QStringLiteral("[VEncSelection] result=failed detail=%1")
                                     .arg(m_selection.detail);
    return false;
}

VideoEncoderSelection FFVEncoder::videoEncoderSelection() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return m_selection;
}

void FFVEncoder::resetPtsClock()
{
    lastPts = -1;
}

AVFrame *FFVEncoder::downloadToYuv420p(const AVFrame *hwFrame)
{
    // Same conversion as the system-memory paths: BT.601 limited range (swscale default).
    AVFrame *download = av_frame_alloc();
    AVFrame *yuv = av_frame_alloc();
    int ret = (download && yuv) ? av_hwframe_transfer_data(download, hwFrame, 0) : AVERROR(ENOMEM);
    if (ret >= 0)
    {
        yuv->format = AV_PIX_FMT_YUV420P;
        yuv->width = codecCtx->width;
        yuv->height = codecCtx->height;
        ret = av_frame_get_buffer(yuv, 0);
    }
    if (ret >= 0)
    {
        m_downloadSws = sws_getCachedContext(m_downloadSws,
                                             download->width, download->height,
                                             static_cast<AVPixelFormat>(download->format),
                                             yuv->width, yuv->height, AV_PIX_FMT_YUV420P,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr);
        ret = m_downloadSws ? sws_scale(m_downloadSws, download->data, download->linesize, 0,
                                        download->height, yuv->data, yuv->linesize)
                            : AVERROR(EINVAL);
    }
    if (ret >= 0)
    {
        ret = av_frame_copy_props(yuv, hwFrame);
    }
    av_frame_free(&download);
    if (ret < 0)
    {
        qWarning() << "[FFVEncoder] D3D11 frame download/convert failed";
        printError(ret);
        av_frame_free(&yuv);
        return nullptr;
    }
    return yuv;
}
