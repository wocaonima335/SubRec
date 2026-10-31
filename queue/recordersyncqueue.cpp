#include "recordersyncqueue.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <QDebug>

#include <algorithm>
#include <cstring>

namespace {
constexpr AVRational kUsecTimeBase{1, AV_TIME_BASE};
}

RecorderSyncQueue::RecorderSyncQueue()
{
    av_channel_layout_uninit(&m_audioChannelLayout);
}

RecorderSyncQueue::~RecorderSyncQueue()
{
    reset();
}

void RecorderSyncQueue::reset(bool audioEnabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audioEnabled = audioEnabled;
    m_videoFrames = 0;
    clearReadyFramesLocked();
    for (auto &buffer : m_audioBuffers) {
        buffer.clear();
        buffer.shrink_to_fit();
    }
    m_audioBuffers.clear();
    m_audioBufferedSamples = 0;
    m_audioFinished = false;
    m_nextAudioPts = 0;
    m_audioClockUs = AV_NOPTS_VALUE;
    m_audioSampleFmt = AV_SAMPLE_FMT_NONE;
    m_audioSampleRate = 0;
    m_audioChannels = 0;
    m_audioFrameSamples = 0;
    m_audioTimeBase = AVRational{0, 1};
    m_audioPlanar = false;
    m_audioBytesPerSample = 0;
    av_channel_layout_uninit(&m_audioChannelLayout);

    m_videoFrameRate = AVRational{0, 1};
    m_videoTimeBase = AVRational{0, 1};
    m_videoFrameDurationUs = 0;
    m_nextVideoTick = 0;
    m_droppedVideoFrames = 0;
    m_videoCatchupCount = 0;
    m_paused = false;
}

void RecorderSyncQueue::onPauseChanged(bool paused, int64_t tsUs)
{
    Q_UNUSED(tsUs);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = paused;
}

void RecorderSyncQueue::configureAudio(AVSampleFormat sampleFmt,
                                       int sampleRate,
                                       const AVChannelLayout &channelLayout,
                                       int frameSamples,
                                       AVRational timeBase)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    clearReadyFramesLocked();
    av_channel_layout_uninit(&m_audioChannelLayout);
    av_channel_layout_copy(&m_audioChannelLayout, &channelLayout);

    m_audioSampleFmt = sampleFmt;
    m_audioSampleRate = sampleRate;
    m_audioChannels = channelLayout.nb_channels;
    m_audioFrameSamples = std::max(1, frameSamples);
    m_audioTimeBase = timeBase.num > 0 && timeBase.den > 0
                          ? timeBase
                          : AVRational{1, std::max(1, sampleRate)};
    m_audioPlanar = av_sample_fmt_is_planar(sampleFmt) != 0;
    m_audioBytesPerSample = av_get_bytes_per_sample(sampleFmt);
    m_audioBufferedSamples = 0;
    m_audioFinished = false;
    m_nextAudioPts = 0;
    m_audioClockUs = AV_NOPTS_VALUE;

    const int planes = audioPlaneCountLocked();
    m_audioBuffers.assign(std::max(planes, 1), {});
}

void RecorderSyncQueue::configureVideo(AVRational frameRate, AVRational timeBase)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (frameRate.num <= 0 || frameRate.den <= 0) {
        frameRate = AVRational{30, 1};
    }

    m_videoFrames = 0;
    m_videoFrameRate = frameRate;
    m_videoTimeBase = timeBase;
    m_videoFrameDurationUs = av_rescale_q(1, av_inv_q(frameRate), kUsecTimeBase);
    if (m_videoFrameDurationUs <= 0) {
        m_videoFrameDurationUs = 33333;
    }
    m_maxVideoLeadUs = std::max<int64_t>(m_videoFrameDurationUs * 2, 80000);
    m_targetVideoLagUs = std::max<int64_t>(m_videoFrameDurationUs, 20000);
    m_nextVideoTick = 0;
    m_droppedVideoFrames = 0;
    m_videoCatchupCount = 0;
}

MediaQueueResult RecorderSyncQueue::pushAudioFrame(AVFrame *frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!frame) {
        return MediaQueueResult::Again;
    }
    if (m_paused) {
        return MediaQueueResult::Again;
    }
    if (!audioConfiguredLocked()) {
        return MediaQueueResult::Error;
    }

    appendAudioFrameLocked(frame);

    while (m_audioBufferedSamples >= m_audioFrameSamples) {
        AVFrame *readyFrame = makeAudioFrameLocked(m_audioFrameSamples);
        if (!readyFrame) {
            return MediaQueueResult::Error;
        }
        consumeAudioSamplesLocked(m_audioFrameSamples);
        m_audioBufferedSamples -= m_audioFrameSamples;
        m_readyAudioFrames.push_back(readyFrame);
    }

    return MediaQueueResult::Ok;
}

MediaQueueResult RecorderSyncQueue::finishAudio()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!audioConfiguredLocked()) {
        m_audioFinished = true;
        return MediaQueueResult::Eof;
    }

    if (m_audioBufferedSamples > 0) {
        AVFrame *finalFrame = makeAudioFrameLocked(m_audioBufferedSamples);
        if (!finalFrame) {
            return MediaQueueResult::Error;
        }
        consumeAudioSamplesLocked(m_audioBufferedSamples);
        m_audioBufferedSamples = 0;
        m_readyAudioFrames.push_back(finalFrame);
    }

    m_audioFinished = true;
    return MediaQueueResult::Eof;
}

MediaQueueReadResult<AVFrame> RecorderSyncQueue::takeReadyAudioFrame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_readyAudioFrames.empty()) {
        AVFrame *frame = m_readyAudioFrames.front();
        m_readyAudioFrames.pop_front();
        return MediaQueueReadResult<AVFrame>{
            MediaQueueResult::Ok, frame, MediaQueueCloseReason::None, 0};
    }

    if (m_audioFinished) {
        return MediaQueueReadResult<AVFrame>{
            MediaQueueResult::Eof, nullptr, MediaQueueCloseReason::None, 0};
    }

    return MediaQueueReadResult<AVFrame>{
        MediaQueueResult::Again, nullptr, MediaQueueCloseReason::None, 0};
}

RecorderSyncVideoDecision RecorderSyncQueue::reserveVideoFrame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RecorderSyncVideoDecision decision;

    if (m_paused) {
        decision.drop = true;
        return decision;
    }

    decision.audioClockUs = m_audioClockUs;
    decision.audioClockReady = m_audioClockUs != AV_NOPTS_VALUE;

    if (m_videoTimeBase.num <= 0 || m_videoTimeBase.den <= 0) {
        decision.drop = true;
        return decision;
    }

    if (!m_audioEnabled) {
        // Exact rational frame counting avoids rounding drift and has no absent audio clock
        // to wait on. Paused frames were rejected above, so resume is contiguous.
        decision.pts = av_rescale_q(m_videoFrames, av_inv_q(m_videoFrameRate), m_videoTimeBase);
        decision.ptsUs = av_rescale_q(m_videoFrames++, av_inv_q(m_videoFrameRate), kUsecTimeBase);
        return decision;
    }

    int64_t candidateTick = m_nextVideoTick;
    if (decision.audioClockReady) {
        const int64_t minPtsUs =
            std::max<int64_t>(0, decision.audioClockUs - m_targetVideoLagUs);
        const int64_t targetTick = av_rescale_rnd(
            minPtsUs, m_videoFrameRate.num,
            int64_t(AV_TIME_BASE) * m_videoFrameRate.den, AV_ROUND_UP);
        if (candidateTick < targetTick) {
            candidateTick = targetTick;
            ++m_videoCatchupCount;
            if (m_videoCatchupCount == 1 || m_videoCatchupCount % 30 == 0) {
                qDebug() << "[RecorderSyncQueue] catch up video pts count="
                         << m_videoCatchupCount
                         << "candidateUs=" << av_rescale_q(candidateTick, av_inv_q(m_videoFrameRate), kUsecTimeBase)
                         << "audioClockUs=" << decision.audioClockUs
                         << "targetLagUs=" << m_targetVideoLagUs;
            }
        }
    }

    decision.ptsUs = av_rescale_q(candidateTick, av_inv_q(m_videoFrameRate), kUsecTimeBase);

    if (!decision.audioClockReady) {
        if (decision.ptsUs > m_startupMaxLeadUs) {
            decision.drop = true;
            ++m_droppedVideoFrames;
            return decision;
        }
    } else if (decision.ptsUs > decision.audioClockUs + m_maxVideoLeadUs) {
        decision.drop = true;
        ++m_droppedVideoFrames;
        if (m_droppedVideoFrames == 1 || m_droppedVideoFrames % 30 == 0) {
            qDebug() << "[RecorderSyncQueue] drop video frame count="
                     << m_droppedVideoFrames
                     << "candidateUs=" << decision.ptsUs
                     << "audioClockUs=" << decision.audioClockUs
                     << "leadUs=" << (decision.ptsUs - decision.audioClockUs);
        }
        return decision;
    }

    decision.pts = av_rescale_q(candidateTick, av_inv_q(m_videoFrameRate), m_videoTimeBase);
    m_nextVideoTick = candidateTick + 1;
    return decision;
}

int64_t RecorderSyncQueue::audioClockUs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_audioClockUs;
}

uint64_t RecorderSyncQueue::droppedVideoFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_droppedVideoFrames;
}

uint64_t RecorderSyncQueue::videoCatchupCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_videoCatchupCount;
}

void RecorderSyncQueue::clearReadyFramesLocked()
{
    while (!m_readyAudioFrames.empty()) {
        AVFrame *frame = m_readyAudioFrames.front();
        m_readyAudioFrames.pop_front();
        if (frame) {
            av_frame_unref(frame);
            av_frame_free(&frame);
        }
    }
}

int RecorderSyncQueue::audioPlaneCountLocked() const
{
    if (m_audioChannels <= 0) {
        return 0;
    }
    return m_audioPlanar ? m_audioChannels : 1;
}

bool RecorderSyncQueue::audioConfiguredLocked() const
{
    return m_audioSampleFmt != AV_SAMPLE_FMT_NONE
        && m_audioSampleRate > 0
        && m_audioChannels > 0
        && m_audioFrameSamples > 0
        && m_audioBytesPerSample > 0
        && m_audioTimeBase.num > 0
        && m_audioTimeBase.den > 0;
}

void RecorderSyncQueue::appendAudioFrameLocked(const AVFrame *frame)
{
    const int planes = audioPlaneCountLocked();
    if (planes <= 0) {
        return;
    }

    if (m_audioPlanar) {
        const size_t bytes = static_cast<size_t>(frame->nb_samples) * m_audioBytesPerSample;
        for (int channel = 0; channel < std::min(m_audioChannels, frame->ch_layout.nb_channels); ++channel) {
            const uint8_t *src = frame->extended_data ? frame->extended_data[channel] : frame->data[channel];
            if (!src) {
                continue;
            }
            auto &buffer = m_audioBuffers[channel];
            const size_t oldSize = buffer.size();
            buffer.resize(oldSize + bytes);
            std::memcpy(buffer.data() + oldSize, src, bytes);
        }
    } else {
        const size_t bytes = static_cast<size_t>(frame->nb_samples) * m_audioBytesPerSample * m_audioChannels;
        const uint8_t *src = frame->data[0];
        if (!src) {
            return;
        }
        auto &buffer = m_audioBuffers[0];
        const size_t oldSize = buffer.size();
        buffer.resize(oldSize + bytes);
        std::memcpy(buffer.data() + oldSize, src, bytes);
    }

    m_audioBufferedSamples += frame->nb_samples;
}

AVFrame *RecorderSyncQueue::makeAudioFrameLocked(int validSamples)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->format = m_audioSampleFmt;
    frame->sample_rate = m_audioSampleRate;
    frame->nb_samples = m_audioFrameSamples;
    if (av_channel_layout_copy(&frame->ch_layout, &m_audioChannelLayout) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    if (av_frame_get_buffer(frame, 0) < 0) {
        av_channel_layout_uninit(&frame->ch_layout);
        av_frame_free(&frame);
        return nullptr;
    }

    if (av_frame_make_writable(frame) < 0) {
        av_channel_layout_uninit(&frame->ch_layout);
        av_frame_free(&frame);
        return nullptr;
    }

    const int paddedSamples = std::min(validSamples, m_audioFrameSamples);
    if (m_audioPlanar) {
        const size_t validBytes = static_cast<size_t>(paddedSamples) * m_audioBytesPerSample;
        const size_t fullBytes = static_cast<size_t>(m_audioFrameSamples) * m_audioBytesPerSample;
        for (int channel = 0; channel < m_audioChannels; ++channel) {
            std::memset(frame->extended_data[channel], 0, fullBytes);
            if (validBytes == 0 || m_audioBuffers[channel].empty()) {
                continue;
            }
            std::memcpy(frame->extended_data[channel], m_audioBuffers[channel].data(), validBytes);
        }
    } else {
        const size_t validBytes =
            static_cast<size_t>(paddedSamples) * m_audioBytesPerSample * m_audioChannels;
        const size_t fullBytes =
            static_cast<size_t>(m_audioFrameSamples) * m_audioBytesPerSample * m_audioChannels;
        std::memset(frame->data[0], 0, fullBytes);
        if (validBytes > 0 && !m_audioBuffers[0].empty()) {
            std::memcpy(frame->data[0], m_audioBuffers[0].data(), validBytes);
        }
    }

    frame->pts = m_nextAudioPts;
    m_nextAudioPts += m_audioFrameSamples;
    m_audioClockUs = av_rescale_q(m_nextAudioPts, m_audioTimeBase, kUsecTimeBase);
    return frame;
}

void RecorderSyncQueue::consumeAudioSamplesLocked(int samples)
{
    if (samples <= 0) {
        return;
    }

    if (m_audioPlanar) {
        const size_t bytes = static_cast<size_t>(samples) * m_audioBytesPerSample;
        for (int channel = 0; channel < m_audioChannels; ++channel) {
            auto &buffer = m_audioBuffers[channel];
            const size_t consume = std::min(bytes, buffer.size());
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consume));
        }
    } else {
        const size_t bytes = static_cast<size_t>(samples) * m_audioBytesPerSample * m_audioChannels;
        auto &buffer = m_audioBuffers[0];
        const size_t consume = std::min(bytes, buffer.size());
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consume));
    }
}
