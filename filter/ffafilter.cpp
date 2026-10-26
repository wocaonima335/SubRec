#include "ffafilter.h"
#include "decoder/ffadecoder.h"
#include "queue/ffaframequeue.h"

#include <QDebug>

#include <sstream>
#include <stdexcept>

FFAFilter::FFAFilter() {}

FFAFilter::~FFAFilter()
{
    resetDualAudioFilter();
    for (auto &graph : m_individualFilterGraphs) {
        avfilter_graph_free(&graph);
    }
}

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
    Q_UNUSED(startTime);
    Q_UNUSED(pauseTime);

    std::lock_guard<std::mutex> lock(m_processingMutex);
    // Like processSingleAudioFrame(), this takes ownership of both input frames on every path.
    auto releaseInputs = [&audioFrame, &microphoneFrame]() {
        av_frame_free(&audioFrame);
        av_frame_free(&microphoneFrame);
    };

    if (!audioFrame || !microphoneFrame) {
        releaseInputs();
        return 0;
    }

    if (!m_encoderFrmQueue) {
        releaseInputs();
        return -1;
    }

    if (!m_dualFilterGraph) {
        if (!m_systemAudioDecoder || !m_microphoneAudioDecoder) {
            qDebug() << "Dual audio filter has neither a graph nor decoders to build one";
            releaseInputs();
            return -1;
        }
        try {
            initializeDualAudioFilter(m_systemAudioDecoder->getCodecCtx(),
                                      m_microphoneAudioDecoder->getCodecCtx(),
                                      m_systemAudioDecoder->getStream(),
                                      m_microphoneAudioDecoder->getStream());
        } catch (const std::exception &error) {
            qDebug() << "Failed to initialize dual audio filter:" << error.what();
            releaseInputs();
            return -1;
        }
    }

    int ret = av_buffersrc_add_frame_flags(m_audioBufferContext, audioFrame, 0);
    if (ret < 0) {
        qDebug() << "Failed to send frame to buffer1";
        logError(ret);
        releaseInputs();
        return -1;
    }

    ret = av_buffersrc_add_frame_flags(m_microphoneBufferContext, microphoneFrame, 0);
    releaseInputs();
    if (ret < 0) {
        qDebug() << "Failed to send frame to buffer2";
        logError(ret);
        return -1;
    }

    while (true) {
        AVFrame *filterFrame = av_frame_alloc();
        if (!filterFrame) {
            return -1;
        }
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
        // enqueue() moves the reference out of filterFrame. The pts is left as the graph made
        // it, like on the single-source path; RecorderSyncQueue/FFAEncoderThread own the
        // output timeline. (The old wall-clock pts was written after enqueue: never used.)
        m_encoderFrmQueue->enqueue(filterFrame);
        av_frame_free(&filterFrame);
    }

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

    applyVolume(m_systemaudioVolumeContext, volume);
    applyVolume(m_individualVolumeContexts[static_cast<size_t>(AudioSourceType::AUDIO_SOURCE_SYSTEM)],
                volume);
}

void FFAFilter::setMicrophoneAudioVolume(double volume)
{
    std::lock_guard<std::mutex> lock(m_processingMutex);
    m_microphoneVolume = volume;

    applyVolume(m_microphoneaudioVolumeContext, volume);
    applyVolume(m_individualVolumeContexts[static_cast<size_t>(
                    AudioSourceType::AUDIO_SOURCE_MICROPHONE)],
                volume);
}

void FFAFilter::applyVolume(AVFilterContext *volumeContext, double volume)
{
    if (!volumeContext) {
        return;
    }
    // "volume" is a string expression of the volume filter's private context, so
    // av_opt_set_double() on the AVFilterContext never reached it. The filter applies runtime
    // changes through its "volume" command (eval=frame: from the next frame on).
    char value[32];
    snprintf(value, sizeof(value), "%.4f", volume);
    int result = avfilter_process_command(volumeContext, "volume", value, nullptr, 0, 0);
    if (result < 0) {
        logError(result);
    }
}

void FFAFilter::setAudioVolume(double systemVolume, double microphoneVolume)
{
    if (systemVolume >= 0.0) {
        setSystemAudioVolume(systemVolume);
    }
    if (microphoneVolume >= 0.0) {
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
    resetDualAudioFilter();
    if (!systemAudioDecoderCtx || !microphoneAudioDecoderCtx || !sysAudioStream
        || !micAudioStream) {
        throw std::invalid_argument("Dual audio filter needs both codec contexts and streams");
    }
    m_systemAudioDecoderContext = systemAudioDecoderCtx;
    m_microphoneAudioDecoderContext = microphoneAudioDecoderCtx;

    m_dualFilterGraph = avfilter_graph_alloc();
    if (!m_dualFilterGraph) {
        throw std::runtime_error("Failed to allocate filter graph");
    }

    try {
        createAudioBufferFilter(m_dualFilterGraph,
                                &m_audioBufferContext,
                                m_systemAudioDecoderContext,
                                sysAudioStream,
                                "in_system");
        createAudioBufferFilter(m_dualFilterGraph,
                                &m_microphoneBufferContext,
                                m_microphoneAudioDecoderContext,
                                micAudioStream,
                                "in_microphone");

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

        // abuffer -> volume -> amix input N. Each source needs its own amix input pad:
        // linking both volumes to pad 0 made avfilter_link() fail with EINVAL, and the
        // abuffer -> volume links were missing entirely.
        linkFilters(m_audioBufferContext, 0, m_systemaudioVolumeContext, 0);
        linkFilters(m_microphoneBufferContext, 0, m_microphoneaudioVolumeContext, 0);
        linkFilters(m_systemaudioVolumeContext, 0, m_audioMixerContext, 0);
        linkFilters(m_microphoneaudioVolumeContext, 0, m_audioMixerContext, 1);
        linkFilters(m_audioMixerContext, 0, m_bufferSinkContext, 0);

        // 配置滤镜图
        avfilter_graph_set_auto_convert(m_dualFilterGraph, AVFILTER_AUTO_CONVERT_ALL);
        int ret = avfilter_graph_config(m_dualFilterGraph, nullptr);
        if (ret < 0) {
            logError(ret);
            throw std::runtime_error("Filter graph config failed");
        }
    } catch (...) {
        // Never leave a half-built graph behind: processDualtAudioFrames() would use it.
        resetDualAudioFilter();
        throw;
    }
}

void FFAFilter::resetDualAudioFilter()
{
    avfilter_graph_free(&m_dualFilterGraph);
    m_audioBufferContext = nullptr;
    m_microphoneBufferContext = nullptr;
    m_bufferSinkContext = nullptr;
    m_audioMixerContext = nullptr;
    m_noiseReductionContext = nullptr;
    m_systemaudioVolumeContext = nullptr;
    m_microphoneaudioVolumeContext = nullptr;
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
    if (!amixFilter) {
        throw std::runtime_error("amix filter not found");
    }
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
    if (!sinkFilter) {
        throw std::runtime_error("abuffersink filter not found");
    }

    // FFmpeg 8 exposes sample_formats as a private SAMPLE_FMT array option.
    // Set it before avfilter_init_str(): init appends the list terminator.
    AVFilterContext *sink = avfilter_graph_alloc_filter(filterGraph, sinkFilter, name);
    if (!sink) {
        throw std::runtime_error(std::string("Failed to allocate filter: ") + name);
    }

    const AVSampleFormat formats[] = {AUDIO_SAMPLE_FORMAT};
    int result = av_opt_set_array(sink, "sample_formats", AV_OPT_SEARCH_CHILDREN,
                                  0, 1, AV_OPT_TYPE_SAMPLE_FMT, formats);
    if (result >= 0) {
        result = avfilter_init_str(sink, nullptr);
    }
    if (result < 0) {
        logError(result);
        avfilter_free(sink);
        throw std::runtime_error(std::string("Failed to create filter: ") + name);
    }
    *context = sink;
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
