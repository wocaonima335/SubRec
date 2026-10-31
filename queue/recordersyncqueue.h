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

struct RecorderSyncVideoDecision
{
    bool drop = false;
    int64_t pts = AV_NOPTS_VALUE;
    int64_t ptsUs = AV_NOPTS_VALUE;
    bool audioClockReady = false;
    int64_t audioClockUs = AV_NOPTS_VALUE;
};

class RecorderSyncQueue
{
public:
    RecorderSyncQueue();
    ~RecorderSyncQueue();

    void reset(bool audioEnabled = true);
    void setAudioClockDelayUs(int64_t delayUs);
    void onPauseChanged(bool paused, int64_t tsUs);

    void configureAudio(AVSampleFormat sampleFmt,
                        int sampleRate,
                        const AVChannelLayout &channelLayout,
                        int frameSamples,
                        AVRational timeBase);
    void configureVideo(AVRational frameRate, AVRational timeBase);

    MediaQueueResult pushAudioFrame(AVFrame *frame);
    MediaQueueResult finishAudio();
    MediaQueueReadResult<AVFrame> takeReadyAudioFrame();

    RecorderSyncVideoDecision reserveVideoFrame();

    int64_t audioClockUs() const;
    uint64_t droppedVideoFrames() const;
    uint64_t videoCatchupCount() const;

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
    int64_t m_videoFrames = 0;
    bool m_paused = false;

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
    int64_t m_audioClockDelayUs = 0;
    bool m_audioFinished = false;
    std::vector<std::vector<uint8_t>> m_audioBuffers;
    std::deque<AVFrame *> m_readyAudioFrames;

    AVRational m_videoFrameRate{0, 1};
    AVRational m_videoTimeBase{0, 1};
    int64_t m_videoFrameDurationUs = 0;
    int64_t m_nextVideoTick = 0;
    int64_t m_startupMaxLeadUs = 150000;
    int64_t m_maxVideoLeadUs = 80000;
    int64_t m_targetVideoLagUs = 33333;
    uint64_t m_droppedVideoFrames = 0;
    uint64_t m_videoCatchupCount = 0;
};

#endif // RECORDERSYNCQUEUE_H
