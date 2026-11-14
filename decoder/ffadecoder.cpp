#include "logging/frame_log.h"
#include "ffadecoder.h"

#include "queue/ffaframequeue.h"
#include "resampler/ffaresampler.h"

#include <cstring>

FFADecoder::FFADecoder()
    : m_stop(false)
{
}

FFADecoder::~FFADecoder()
{
    close();
}

void FFADecoder::decode(AVPacket *packet)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (codecCtx == nullptr)
    {
        return;
    }

    int ret = avcodec_send_packet(codecCtx, packet);

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
            qDebug() << "[ADec] m_stop observed, break receive-loop";
            break;
        }

        ret = avcodec_receive_frame(codecCtx, frame);

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
            if (captureTimestampOutput) {
                frame->time_base = stream->time_base;
                frmQueue->enqueue(frame);
                continue;
            }
            if (aPars == nullptr)
            {
                aPars = new FFAudioPars();
                swraPars = new FFAudioPars();
                initAudioPars(frame);
                if (aPars->aFormatEnum != swraPars->aFormatEnum)
                {
                    resampler = new FFAResampler();
                    initResampler();
                    printFmt();
                }
            }

            if (resampler)
            {
                AVFrame *swrFrame = nullptr;
                // qDebug() << "[ADec] Before resample: frame pts=" << frame->pts
                //          << "nb_samples=" << frame->nb_samples
                //          << "format=" << frame->format;
                resampler->resample(frame, &swrFrame);
                av_frame_unref(frame);
                if (m_stop.load(std::memory_order_acquire))
                {
                    qDebug() << "[ADec] m_stop detected after resample, cleaning up";
                    av_frame_unref(frame);
                    av_frame_free(&swrFrame);
                    m_stop.store(false, std::memory_order_release);
                    break;
                }
                else
                {
                    // qDebug() << "[ADec] After resample: swrFrame pts=" << swrFrame->pts
                    //          << "nb_samples=" << swrFrame->nb_samples
                    //          << "format=" << swrFrame->format;

                    if (frmQueue != nullptr)
                    {
                        qCDebug(subrec::logging::frameLog) << "[ADec] enqueue resampled frame";
                        frmQueue->enqueue(swrFrame);
                    }
                    else
                    {
                        qDebug() << "[ADec] frmQueue is nullptr, skipping enqueue";
                    }
                    // AVFrameTraits::release(swrFrame);
                }
            }
            else
            {
                if (m_stop.load(std::memory_order_acquire))
                {
                    qDebug() << "[ADec] m_stop detected (no resampler), cleaning up";
                    av_frame_unref(frame);
                    m_stop.store(false, std::memory_order_release);
                    break;
                }
                else
                {
                    qCDebug(subrec::logging::frameLog) << "[ADec] No resampler: frame pts=" << frame->pts
                             << "nb_samples=" << frame->nb_samples
                             << "format=" << frame->format;
                    frmQueue->enqueue(frame);
                }
            }
        }
    }
    av_frame_free(&frame);
}

void FFADecoder::init(AVStream *stream_, FFAFrameQueue *frmQueue_)
{
    std::lock_guard<std::mutex> lock(mutex);
    m_stop.store(false, std::memory_order_release);
    stream = stream_;
    frmQueue = frmQueue_;

    if (stream->codecpar == nullptr)
    {
        return;
    }

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);

    if (codec == nullptr)
    {
        qDebug() << "找不到音频解码器";
        return;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr)
    {
        qDebug() << "分配解码器上下文失败";
        return;
    }

    int ret = avcodec_parameters_to_context(codecCtx, stream->codecpar);
    if (ret < 0)
    {
        printError(ret);
        avcodec_free_context(&codecCtx);
        return;
    }

    codecCtx->pkt_timebase = stream->time_base;
    AVDictionary *codec_options = nullptr;

    ret = avcodec_open2(codecCtx, codec, &codec_options);
    if (ret < 0)
    {
        printError(ret);
        avcodec_free_context(&codecCtx);
        return;
    }
}

void FFADecoder::flushDecoder()
{
    avcodec_flush_buffers(codecCtx);
}

FFAudioPars *FFADecoder::getAudioPars()
{
    return swraPars;
}

int FFADecoder::getTotalSec()
{
    return static_cast<int>(0.5 + stream->duration * av_q2d(stream->time_base));
}

void FFADecoder::wakeAllThreads()
{
    frmQueue->wakeAllThread();
}

void FFADecoder::requestDrainStop()
{
    qDebug() << "[ADec] requestDrainStop";
}

void FFADecoder::requestAbort()
{
    m_stop.store(true, std::memory_order_release);
}

void FFADecoder::stop()
{
    requestAbort();
}

void FFADecoder::enqueueNull()
{
    frmQueue->sendFinish();
}

void FFADecoder::flushQueue()
{
    frmQueue->flushQueue();
}

void FFADecoder::close()
{
    stop();

    std::lock_guard<std::mutex> lock(mutex);

    // 刷新解码器
    if (codecCtx)
    {
        avcodec_send_packet(codecCtx, nullptr);
        avcodec_free_context(&codecCtx);
    }

    if (aPars)
    {
        delete aPars;
        aPars = nullptr;
    }
    if (swraPars)
    {
        delete swraPars;
        swraPars = nullptr;
    }

    if (resampler)
    {
        delete resampler;
        resampler = nullptr;
    }

    qDebug() << "[AEnc] close";
}

AVCodecContext *FFADecoder::getCodecCtx()
{
    return codecCtx;
}

AVStream *FFADecoder::getStream()
{
    return stream;
}

void FFADecoder::printError(int ret)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int res = av_strerror(ret, errorBuffer, sizeof(errorBuffer));
    if (res < 0)
    {
        qDebug() << "UnKonw Error!";
    }
    else
    {
        qDebug() << "Error:" << errorBuffer;
    }
}

void FFADecoder::initAudioPars(AVFrame *frame)
{
    aPars->timeBase = stream->time_base;
    aPars->nbChannels = frame->ch_layout.nb_channels;
    aPars->aFormatEnum = codecCtx->sample_fmt;
    aPars->sampleSize = av_get_bytes_per_sample(codecCtx->sample_fmt);
    aPars->sampleRate = frame->sample_rate;

    memcpy(swraPars, aPars, sizeof(FFAudioPars));
    swraPars->aFormatEnum = AV_SAMPLE_FMT_FLTP;
    swraPars->sampleSize = av_get_bytes_per_sample(AV_SAMPLE_FMT_FLTP);
    swraPars->sampleRate = 48000;
}

void FFADecoder::initResampler()
{
    resampler->init(aPars, swraPars);
}

void FFADecoder::printFmt()
{
    std::cout << "audio format : " << av_get_sample_fmt_name(aPars->aFormatEnum);
    std::cout << "sample_rate : " << aPars->sampleRate;
    std::cout << "channels : " << aPars->nbChannels;
    std::cout << "time_base : " << aPars->timeBase.num << " / " << aPars->timeBase.den;

    std::cout << "=================";
    std::cout << "audio format : " << av_get_sample_fmt_name(swraPars->aFormatEnum);
    std::cout << "sample_rate : " << swraPars->sampleRate;
    std::cout << "channels : " << swraPars->nbChannels;
    std::cout << "time_base : " << swraPars->timeBase.num << " / " << swraPars->timeBase.den;
}
