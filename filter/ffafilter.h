#ifndef FFAFILTER_H
#define FFAFILTER_H

#include <array>
#include <mutex>

extern "C" {
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

enum AudioSourceType { AUDIO_SOURCE_SYSTEM = 0, AUDIO_SOURCE_MICROPHONE = 1 };

// 音频处理标准配置
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_SAMPLE_FORMAT AV_SAMPLE_FMT_FLTP
#define AUDIO_TIME_BASE {1, 48000}

class FFAFrameQueue;
class FFADecoder;

class FFAFilter
{
public:
    FFAFilter();
    ~FFAFilter();

    void initalize(FFAFrameQueue *encoderFrameQueue,
                   FFADecoder *audioDecoder,
                   FFADecoder *microphoneDecoder);

    int processDualtAudioFrames(AVFrame *audioFrame,
                                AVFrame *microphoneFrame,
                                int64_t startTime,
                                int64_t endTime);

    void processSingleAudioFrame(AVFrame *frame,
                                 int64_t startTime,
                                 int64_t pauseTime,
                                 AudioSourceType sourceType);
    void forwardAudioFrame(AVFrame *frame, int64_t startTime, int64_t pauseTime);
    void sendFinish();

    void setSystemAudioVolume(double volume);
    void setMicrophoneAudioVolume(double volume);
    void setAudioVolume(double systemVolume, double microphoneVolume);

    AVRational getTimeBase();
    AVMediaType getMediaType();

    // 滤镜图初始化方法
    void initializeDualAudioFilter(AVCodecContext *systemAudioDecoderCtx,
                                   AVCodecContext *microphoneAudioDecoderCtx,
                                   AVStream *sysAudioStream,
                                   AVStream *micAudioStream);

    void initializeIndividualFilter(AVCodecContext *codecCtx,
                                    AVStream *stream,
                                    AudioSourceType sourceType);

    void createAudioBufferFilter(AVFilterGraph *filterGraph,
                                 AVFilterContext **context,
                                 AVCodecContext *codecCtx,
                                 AVStream *stream,
                                 const char *name);
    void createVolumeControlFilter(AVFilterGraph *filterGraph,
                                   AVFilterContext **context,
                                   double gain,
                                   const char *name);
    void createAudioMixerFilter(AVFilterGraph *filterGraph);
    void createNoiseReductionFilter(AVFilterGraph *filterGraph);
    void createBufferSinkFilter(AVFilterGraph *filterGraph,
                                AVFilterContext **context,
                                const char *name);

    // 通用滤镜操作方法
    AVFilterContext *createFilter(AVFilterGraph *filterGraph,
                                  const AVFilter *filter,
                                  const char *name,
                                  const char *filterArgs);

    void linkFilters(AVFilterContext *source,
                     int sourcePad,
                     AVFilterContext *destination,
                     int destinationPad);

    void logError(int errorCode) const;

private:
    FFAFrameQueue *m_encoderFrmQueue = nullptr;
    FFADecoder *m_systemAudioDecoder = nullptr;
    FFADecoder *m_microphoneAudioDecoder = nullptr;

    AVCodecContext *m_systemAudioDecoderContext = nullptr;
    AVCodecContext *m_microphoneAudioDecoderContext = nullptr;

    AVStream *m_audioStream = nullptr;
    AVStream *m_microphoneStream = nullptr;

    AVFilterGraph *m_dualFilterGraph = nullptr;
    AVFilterContext *m_audioBufferContext = nullptr;
    AVFilterContext *m_microphoneBufferContext = nullptr;
    AVFilterContext *m_bufferSinkContext = nullptr;
    AVFilterContext *m_audioMixerContext = nullptr;
    AVFilterContext *m_noiseReductionContext = nullptr;
    AVFilterContext *m_systemaudioVolumeContext = nullptr;
    AVFilterContext *m_microphoneaudioVolumeContext = nullptr;

    static constexpr size_t AUDIO_SOURCE_COUNT = 2;
    std::array<AVFilterGraph *, AUDIO_SOURCE_COUNT> m_individualFilterGraphs = {nullptr, nullptr};
    std::array<AVFilterContext *, AUDIO_SOURCE_COUNT> m_individualBufferContexts = {nullptr,
                                                                                    nullptr};
    std::array<AVFilterContext *, AUDIO_SOURCE_COUNT> m_individualSinkContexts = {nullptr, nullptr};
    std::array<AVFilterContext *, AUDIO_SOURCE_COUNT> m_individualVolumeContexts = {nullptr,
                                                                                    nullptr};

    double m_audioVolume = 1.0;
    double m_microphoneVolume = 1.0;

    mutable std::mutex m_processingMutex;
};

#endif // FFAFILTER_H
