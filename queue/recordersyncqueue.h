#ifndef RECORDERSYNCQUEUE_H
#define RECORDERSYNCQUEUE_H

#include "media_queue_common.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class RecorderSyncQueue
{
public:
    RecorderSyncQueue();
    ~RecorderSyncQueue();

    void reset(bool audioEnabled = true);

    void configureAudio(AVSampleFormat sampleFmt,
                        int sampleRate,
                        const AVChannelLayout &channelLayout,
                        int frameSamples,
                        AVRational timeBase);
    // Diagnostic only; never changes video or audio timestamps.
    void observeVideoTick(int64_t tick, AVRational frameRate);
    int64_t avDeltaUs() const;

    MediaQueueResult pushAudioFrame(AVFrame *frame);
    MediaQueueResult finishAudio();
    MediaQueueReadResult<AVFrame> takeReadyAudioFrame();


    int64_t audioClockUs() const;
    // Legacy P0 stop-summary ABI; policy moved to VideoTickScheduler.
    uint64_t droppedVideoFrames() const { return 0; }
    uint64_t videoCatchupCount() const { return 0; }

private:
    void clearReadyFramesLocked();
    int audioPlaneCountLocked() const;
    bool audioConfiguredLocked() const;
    void appendAudioFrameLocked(const AVFrame *frame);
    AVFrame *makeAudioFrameLocked(int validSamples);
    void consumeAudioSamplesLocked(int samples);

private:
    mutable std::mutex m_mutex;

    bool m_audioEnabled = true;
    int64_t m_videoUs = AV_NOPTS_VALUE;
    int64_t m_lastDiagnosticUs = AV_NOPTS_VALUE;
    void logDeltaLocked();

    AVSampleFormat m_audioSampleFmt = AV_SAMPLE_FMT_NONE;
    int m_audioSampleRate = 0;
    AVChannelLayout m_audioChannelLayout{};
    int m_audioChannels = 0;
    int m_audioFrameSamples = 0;
    AVRational m_audioTimeBase{0, 1};
    bool m_audioPlanar = false;
    int m_audioBytesPerSample = 0;
    int m_audioBufferedSamples = 0;
    int64_t m_nextAudioPts = 0;
    int64_t m_audioClockUs = AV_NOPTS_VALUE;
    bool m_audioFinished = false;
    std::vector<std::vector<uint8_t>> m_audioBuffers;
    std::deque<AVFrame *> m_readyAudioFrames;

};

#endif // RECORDERSYNCQUEUE_H
