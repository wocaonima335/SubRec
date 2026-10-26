#include "ffafilter.h"
#include "decoder/ffadecoder.h"
#include "queue/ffaframequeue.h"
#include "recorder/ffrecorder.h"

#include <sstream>

FFAFilter::FFAFilter() {}

FFAFilter::~FFAFilter() {}

void FFAFilter::initalize(FFAFrameQueue *encoderFrameQueue,
                          FFADecoder *systemAudioDecoder,
                          FFADecoder *microphoneAudioDecoder)
{
    m_encoderFrmQueue = encoderFrameQueue;
    m_systemAudioDecoder = systemAudioDecoder;
    m_microphoneAudioDecoder = microphoneAudioDecoder;
}

int FFAFilter::processDualtAudioFrames(AVFrame *audioFrame,
                                       AVFrame *microphoneFrame,
                                       int64_t startTime,
                                       int64_t pauseTime)
{
    std::lock_guard<std::mutex> lock(m_processingMutex);
    if (!audioFrame || !microphoneFrame) {
        if (audioFrame) {
            av_frame_unref(audioFrame);
            av_frame_free(&audioFrame);
        }
        if (microphoneFrame) {
            av_frame_unref(microphoneFrame);
            av_frame_free(&microphoneFrame);
        }
        return 0;
    }

    if (!m_audioBufferContext || !m_microphoneBufferContext) {
        initializeDualAudioFilter(m_systemAudioDecoder->getCodecCtx(),
                                  m_microphoneAudioDecoder->getCodecCtx(),
                                  m_systemAudioDecoder->getStream(),
                                  m_microphoneAudioDecoder->getStream());
    }

    int ret = av_buffersrc_add_frame_flags(m_audioBufferContext,
                                           audioFrame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        qDebug() << "Failed to send frame to buffer1";
        logError(ret);
        return -1;
    }

    ret = av_buffersrc_add_frame_flags(m_microphoneBufferContext,
                                       microphoneFrame,
                                       AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        qDebug() << "Failed to send frame to buffer2";
        logError(ret);
        return -1;
    }

    while (true) {
        AVFrame *filterFrame = av_frame_alloc();
        ret = av_buffersink_get_frame(m_bufferSinkContext, filterFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&filterFrame);
            break;
        }
        if (ret < 0) {
            qDebug() << "Failed to get frame from sink";
            logError(ret);
            av_frame_free(&filterFrame);
            return -1;
        }
        m_encoderFrmQueue->enqueue(filterFrame);

        //调整时间戳
        auto end = av_gettime_relative();
        int64_t duration = 10 * (end - startTime);
        filterFrame->pts = av_gettime_relative() * 10 + duration - pauseTime * 10;

        av_frame_unref(filterFrame);
        av_frame_free(&filterFrame);
    };

    return 0;
}

void FFAFilter::processSingleAudioFrame(AVFrame *frame,
                                        int64_t startTime,
                                        int64_t pauseTime,
                                        AudioSourceType sourceType)
{
    Q_UNUSED(startTime);
    Q_UNUSED(pauseTime);
    Q_UNUSED(sourceType);

    std::lock_guard<std::mutex> lock(m_processingMutex);

    if (!frame) {
        return;
    }

    if (!m_encoderFrmQueue) {
        av_frame_unref(frame);
        av_frame_free(&frame);
        return;
    }

    m_encoderFrmQueue->enqueue(frame);
    av_frame_unref(frame);
    av_frame_free(&frame);
}

void FFAFilter::forwardAudioFrame(AVFrame *frame, int64_t startTime, int64_t pauseTime)
{
    Q_UNUSED(startTime);
    Q_UNUSED(pauseTime);
    if (frame == nullptr) {
        return;
    }

    m_encoderFrmQueue->enqueue(frame);
    av_frame_free(&frame);
}

void FFAFilter::sendFinish()
{
    if (m_encoderFrmQueue) {
        m_encoderFrmQueue->sendFinish();
    }
}

void FFAFilter::setSystemAudioVolume(double volume)
{
    std::lock_guard<std::mutex> lock(m_processingMutex);
    m_audioVolume = volume;

    if (m_systemaudioVolumeContext) {
        av_opt_set_double(m_systemaudioVolumeContext, "volume", volume, 0);
    }

    if (m_individualVolumeContexts[static_cast<size_t>(AudioSourceType::AUDIO_SOURCE_SYSTEM)]) {
        av_opt_set_double(m_individualVolumeContexts[static_cast<size_t>(
                              AudioSourceType::AUDIO_SOURCE_SYSTEM)],
                          "volume",
                          volume,
                          0);
    }
}

void FFAFilter::setMicrophoneAudioVolume(double volume)
{
    std::lock_guard<std::mutex> lock(m_processingMutex);
    m_microphoneVolume = volume;

    if (m_microphoneaudioVolumeContext) {
        av_opt_set_double(m_microphoneaudioVolumeContext, "volume", volume, 0);
    }

    if (m_individualVolumeContexts[static_cast<size_t>(AudioSourceType::AUDIO_SOURCE_MICROPHONE)]) {
        av_opt_set_double(m_individualVolumeContexts[static_cast<size_t>(
                              AudioSourceType::AUDIO_SOURCE_MICROPHONE)],
                          "volume",
                          volume,
                          0);
    }
}

void FFAFilter::setAudioVolume(double systemVolume, double microphoneVolume)
{
    if (m_audioVolume >= 0.0) {
        setSystemAudioVolume(systemVolume);
    }
    if (m_microphoneVolume >= 0.0) {
        setMicrophoneAudioVolume(microphoneVolume);
    }
}

AVRational FFAFilter::getTimeBase()
{
    return AUDIO_TIME_BASE;
}

AVMediaType FFAFilter::getMediaType()
{
    return AVMEDIA_TYPE_AUDIO;
}

void FFAFilter::initializeDualAudioFilter(AVCodecContext *systemAudioDecoderCtx,
                                          AVCodecContext *microphoneAudioDecoderCtx,
                                          AVStream *sysAudioStream,
                                          AVStream *micAudioStream)
{
    m_systemAudioDecoderContext = systemAudioDecoderCtx;
    m_microphoneAudioDecoderContext = microphoneAudioDecoderCtx;

    m_dualFilterGraph = avfilter_graph_alloc();
    if (!m_dualFilterGraph) {
        throw std::runtime_error("Failed to allocate filter graph");
    }

    createAudioBufferFilter(m_dualFilterGraph,
                            &m_audioBufferContext,
                            m_systemAudioDecoderContext,
                            sysAudioStream,
                            "in2");
    createAudioBufferFilter(m_dualFilterGraph,
                            &m_microphoneBufferContext,
                            m_microphoneAudioDecoderContext,
                            micAudioStream,
                            "in2");

    createVolumeControlFilter(m_dualFilterGraph,
                              &m_systemaudioVolumeContext,
                              m_audioVolume,
                              "volume1");

    createVolumeControlFilter(m_dualFilterGraph,
                              &m_microphoneaudioVolumeContext,
                              m_microphoneVolume,
                              "volume2");

    createAudioMixerFilter(m_dualFilterGraph);

    createBufferSinkFilter(m_dualFilterGraph, &m_bufferSinkContext, "out");

    linkFilters(m_systemaudioVolumeContext, 0, m_audioMixerContext, 0);
    linkFilters(m_microphoneaudioVolumeContext, 0, m_audioMixerContext, 0);
    linkFilters(m_audioMixerContext, 0, m_bufferSinkContext, 0);

    // 配置滤镜图
    avfilter_graph_set_auto_convert(m_dualFilterGraph, AVFILTER_AUTO_CONVERT_ALL);
    int ret = avfilter_graph_config(m_dualFilterGraph, nullptr);
    if (ret < 0) {
        logError(ret);
        throw std::runtime_error("Filter graph config failed");
    }
}

void FFAFilter::initializeIndividualFilter(AVCodecContext *codecCtx,
                                           AVStream *audioStream,
                                           AudioSourceType sourceType)
{
    const size_t sourceIndex = static_cast<size_t>(sourceType);
    m_individualFilterGraphs[sourceIndex] = avfilter_graph_alloc();
    if (!m_individualFilterGraphs[sourceIndex]) {
        throw std::runtime_error("Failed to allocate individual filter graph");
    }

    createAudioBufferFilter(m_individualFilterGraphs[sourceIndex],
                            &m_individualBufferContexts[sourceIndex],
                            codecCtx,
                            audioStream,
                            "in");
    double volume = (sourceType == AudioSourceType::AUDIO_SOURCE_SYSTEM) ? m_audioVolume
                                                                         : m_microphoneVolume;

    createVolumeControlFilter(m_individualFilterGraphs[sourceIndex],
                              &m_individualVolumeContexts[sourceIndex],
                              volume,
                              "volume");

    createBufferSinkFilter(m_individualFilterGraphs[sourceIndex],
                           &m_individualSinkContexts[sourceIndex],
                           "out");
    linkFilters(m_individualBufferContexts[sourceIndex],
                0,
                m_individualVolumeContexts[sourceIndex],
                0);
    linkFilters(m_individualVolumeContexts[sourceIndex],
                0,
                m_individualSinkContexts[sourceIndex],
                0);

    avfilter_graph_set_auto_convert(m_individualFilterGraphs[sourceIndex],
                                    AVFILTER_AUTO_CONVERT_ALL);
    int result = avfilter_graph_config(m_individualFilterGraphs[sourceIndex], nullptr);
    if (result < 0) {
        logError(result);
        throw std::runtime_error("Individual filter graph configuration failed");
    }
}

void FFAFilter::createAudioBufferFilter(AVFilterGraph *filterGraph,
                                        AVFilterContext **context,
                                        AVCodecContext *codecCtx,
                                        AVStream *audioStream,
                                        const char *name)
{
    const AVFilter *bufferFilter = avfilter_get_by_name("abuffer");
    if (!bufferFilter) {
        throw std::runtime_error("abuffer filter ont found");
    }

    AVRational timeBase = audioStream->time_base;
    AVChannelLayout channelLayout = {};
    uint64_t channelMask = 0;

    av_channel_layout_default(&channelLayout, codecCtx->ch_layout.nb_channels);
    if (channelLayout.order == AV_CHANNEL_ORDER_NATIVE) {
        channelMask = channelLayout.u.mask;
    }

    std::ostringstream filterArgs;
    filterArgs << "time_base=" << timeBase.num << "/" << timeBase.den
               << ":sample_rate=" << AUDIO_SAMPLE_RATE
               << ":sample_fmt=" << av_get_sample_fmt_name(AUDIO_SAMPLE_FORMAT)
               << ":channel_layout=0x" << std::hex << channelMask;

    int result = avfilter_graph_create_filter(context,
                                              bufferFilter,
                                              name,
                                              filterArgs.str().c_str(),
                                              nullptr,
                                              filterGraph);
    if (result < 0) {
        logError(result);
        throw std::runtime_error("Failed to create abuffer filter");
    }
}

void FFAFilter::createVolumeControlFilter(AVFilterGraph *filterGraph,
                                          AVFilterContext **context,
                                          double gain,
                                          const char *name)
{
    const AVFilter *volumeFilter = avfilter_get_by_name("volume");
    if (!volumeFilter) {
        throw std::runtime_error("volume filter not found");
    }

    char filterArgs[64];
    snprintf(filterArgs, sizeof(filterArgs), "volume=%.2f:eval=frame", gain);

    int result = avfilter_graph_create_filter(context,
                                              volumeFilter,
                                              name,
                                              filterArgs,
                                              nullptr,
                                              filterGraph);

    if (result < 0) {
        logError(result);
        throw std::runtime_error("Failed to create volume filter: " + std::string(name));
    }
}

void FFAFilter::createAudioMixerFilter(AVFilterGraph *filterGraph)
{
    const AVFilter *amixFilter = avfilter_get_by_name("amix");
    m_audioMixerContext = createFilter(filterGraph, amixFilter, "amix", "inputs=2:duration=longest");
}

void FFAFilter::createNoiseReductionFilter(AVFilterGraph *filterGraph)
{
    const AVFilter *afftdnFilter = avfilter_get_by_name("afftdn");
    m_noiseReductionContext = createFilter(filterGraph,
                                           afftdnFilter,
                                           "afftdn",
                                           "nf=-60:track_noise=1");
}

void FFAFilter::createBufferSinkFilter(AVFilterGraph *filterGraph,
                                       AVFilterContext **context,
                                       const char *name)
{
    const AVFilter *sinkFilter = avfilter_get_by_name("abuffersink");
    *context = createFilter(filterGraph, sinkFilter, name, nullptr);

    // 设置输出格式 - 使用新版 API
    char formatList[128];
    snprintf(formatList, sizeof(formatList), "%d|%d", AUDIO_SAMPLE_FORMAT, AV_SAMPLE_FMT_FLTP);

    av_opt_set(*context, "sample_fmts", formatList, AV_OPT_SEARCH_CHILDREN);
}

AVFilterContext *FFAFilter::createFilter(AVFilterGraph *filterGraph,
                                         const AVFilter *filter,
                                         const char *name,
                                         const char *filterArgs)
{
    AVFilterContext *context = nullptr;
    int result
        = avfilter_graph_create_filter(&context, filter, name, filterArgs, nullptr, filterGraph);

    if (result < 0) {
        logError(result);
        throw std::runtime_error(std::string("Failed to create filter: ") + name);
    }
    return context;
}

void FFAFilter::linkFilters(AVFilterContext *source,
                            int sourcePad,
                            AVFilterContext *destination,
                            int destinationPad)
{
    int result = avfilter_link(source, sourcePad, destination, destinationPad);
    if (result < 0) {
        logError(result);
        throw std::runtime_error("filter linking failed");
    }
}

void FFAFilter::logError(int errorCode) const
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    int result = av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
    if (result < 0) {
        qDebug() << "Unknown Error!";
    } else {
        qDebug() << "Error: " << errorBuffer;
    }
}
