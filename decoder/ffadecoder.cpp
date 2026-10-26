#include "ffadecoder.h"

#include "queue/ffaframequeue.h"
#include "recorder/ffrecorder.h"
#include "resampler/ffaresampler.h"

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
                        qDebug() << "[ADec] Calling processDecodedFrame and enqueue";
                        processDecodedFrame(swrFrame);
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
                    qDebug() << "[ADec] No resampler: frame pts=" << frame->pts
                             << "nb_samples=" << frame->nb_samples
                             << "format=" << frame->format;
                    processDecodedFrame(frame);
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

void FFADecoder::processDecodedFrame(AVFrame *frame)
{
    if (!frame || !frame->nb_samples)
    {
        return;
    }

    // 获取全局FFRecorder实例
    FFRecorder &recorder = FFRecorder::getInstance();
    FFAudioSampler *sampler = recorder.getSampler();

    if (!sampler || !sampler->isActive())
    {
        qDebug() << "Audio sampler is not active, skipping sample collection.";
        return; // 采样器未启动，跳过
    }

    switch (frame->format)
    {
    case AV_SAMPLE_FMT_FLT:
    {
        // 单通道或交错float格式
        float *samples = (float *)frame->data[0];
        int sampleCount = frame->nb_samples * frame->ch_layout.nb_channels;
        sampler->collectSamples(samples, sampleCount);
        break;
    }

    case AV_SAMPLE_FMT_FLTP:
    {
        // Planar float格式（多个通道分开存储）
        // 对于多通道情况，需要交错处理
        int numSamples = frame->nb_samples;
        float **samples = (float **)frame->data;

        // 选项1: 只采集第一个声道（单声道分析）
        if (samples[0])
        {
            sampler->collectSamples(samples[0], numSamples);
        }
        break;
    }

    case AV_SAMPLE_FMT_S16:
    {
        // 16位有符号整数格式
        int16_t *samples = (int16_t *)frame->data[0];
        int sampleCount = frame->nb_samples * frame->ch_layout.nb_channels;
        sampler->collectSamples(samples, sampleCount);
        break;
    }

    case AV_SAMPLE_FMT_S16P:
    {
        // Planar 16位格式
        int numSamples = frame->nb_samples;
        int16_t **samples = (int16_t **)frame->data;

        if (samples[0])
        {
            sampler->collectSamples(samples[0], numSamples);
        }
        break;
    }

    default:
        qWarning() << "Unsupported audio format for sampling:" << frame->format;
        break;
    }
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
