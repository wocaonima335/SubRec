#include "ffvencoder.h"
#include "queue/ffvpacketqueue.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QStringList>
#include <QtGlobal>
#include <cstring>
#include <iostream>
#include <thread>


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

    static qint64 s_lastInputMd5LogMs = 0;
    frame->pts = pts; // 灏嗚绠楀ソ鐨?PTS 鍐欏埌 AVFrame 涓婏紝浜ょ粰缂栫爜鍣?
    qDebug() << "[FFVEncoder]:encode pts :" << pts;
    const qint64 nowMs = av_gettime_relative() / 1000;
    if (nowMs - s_lastInputMd5LogMs >= 500) {
        s_lastInputMd5LogMs = nowMs;
        qDebug().noquote() << "[EncoderInputMd5]" << "pts=" << pts << "md5=" << framePlaneMd5(frame);
    }
    qDebug() << "[VEnc] before send_frame";

    int ret = avcodec_send_frame(codecCtx, frame);
    qDebug() << "[VEnc] after send_frame ret=" << ret;
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
        qDebug() << "[VEnc] receive_packet ret=" << ret;
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

    QStringList failures;
    for (const auto &candidate : candidates)
    {
        const AVCodec *codec = avcodec_find_encoder_by_name(candidate.name);
        if (!codec)
        {
            const QString reason = QStringLiteral("not found");
            failures.append(QStringLiteral("%1: %2").arg(candidate.name, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=lookup result=failed reason=%2")
                                             .arg(candidate.name, reason);
            continue;
        }

        AVCodecContext *candidateCtx = avcodec_alloc_context3(codec);
        if (!candidateCtx)
        {
            const QString reason = QStringLiteral("context allocation failed");
            failures.append(QStringLiteral("%1: %2").arg(candidate.name, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=allocate result=failed reason=%2")
                                             .arg(candidate.name, reason);
            continue;
        }

        candidateCtx->width = requestedPars.width;
        candidateCtx->height = requestedPars.height;
        candidateCtx->bit_rate = requestedPars.biteRate;
        candidateCtx->rc_max_rate = requestedPars.biteRate * 12 / 10;
        candidateCtx->rc_buffer_size = requestedPars.biteRate;
        candidateCtx->framerate = requestedPars.frameRate;
        candidateCtx->time_base = av_inv_q(requestedPars.frameRate);
        candidateCtx->pix_fmt = requestedPars.videoFmt;
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
            failures.append(QStringLiteral("%1: %2").arg(candidate.name, reason));
            qWarning().noquote() << QStringLiteral("[VEncSelection] candidate=%1 stage=open result=failed fferr=%2 error=%3")
                                             .arg(candidate.name)
                                             .arg(ret)
                                             .arg(reason);
            avcodec_free_context(&candidateCtx);
            continue;
        }

        codecCtx = candidateCtx;
        vPars = new FFVEncoderPars(requestedPars);
        m_selection.codecName = QString::fromLatin1(candidate.name);
        m_selection.displayName = QString::fromLatin1(candidate.description);
        m_selection.isHardware = candidate.isHardware;
        m_selection.success = true;
        m_selection.detail = failures.isEmpty()
            ? QStringLiteral("selected with production configuration")
            : QStringLiteral("fallback after %1").arg(failures.join(QStringLiteral("; ")));

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
