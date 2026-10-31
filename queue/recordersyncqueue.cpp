#include "recordersyncqueue.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
}

#include <QDebug>

#include <algorithm>
#include <cstring>
#include <climits>

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
    m_videoUs = m_lastDiagnosticUs = AV_NOPTS_VALUE;
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
    const int copiedLayout = av_channel_layout_copy(&m_audioChannelLayout, &channelLayout);

    m_audioSampleFmt = sampleFmt;
    m_audioSampleRate = sampleRate;
    m_audioChannels = copiedLayout < 0 ? 0 : channelLayout.nb_channels;
    m_audioFrameSamples = std::max(1, frameSamples);
    m_audioTimeBase = timeBase;
    if (sampleRate <= 0 || av_cmp_q(timeBase, AVRational{1, std::max(1, sampleRate)}) != 0 ||
        !av_channel_layout_check(&channelLayout)) m_audioTimeBase = AVRational{0,1};
    m_audioPlanar = av_sample_fmt_is_planar(sampleFmt) != 0;
    m_audioBytesPerSample = av_get_bytes_per_sample(sampleFmt);
    m_audioBufferedSamples = 0;
    m_audioFinished = false;
    m_nextAudioPts = 0;
    m_audioClockUs = AV_NOPTS_VALUE;

    const int planes = audioPlaneCountLocked();
    m_audioBuffers.assign(std::max(planes, 1), {});
}

MediaQueueResult RecorderSyncQueue::pushAudioFrame(AVFrame *frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!frame) {
        return MediaQueueResult::Again;
    }
    if (!audioConfiguredLocked() || m_audioFinished || !m_audioEnabled ||
        frame->format != m_audioSampleFmt || frame->sample_rate != m_audioSampleRate ||
        frame->nb_samples <= 0 || frame->nb_samples > INT_MAX - m_audioBufferedSamples ||
        !av_channel_layout_check(&frame->ch_layout) ||
        av_channel_layout_compare(&frame->ch_layout, &m_audioChannelLayout) != 0) {
        return MediaQueueResult::Error;
    }
    const int planes = audioPlaneCountLocked();
    if (!frame->extended_data) return MediaQueueResult::Error;
    for (int plane = 0; plane < planes; ++plane)
        if (!frame->extended_data[plane]) return MediaQueueResult::Error;

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

    logDeltaLocked();
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

void RecorderSyncQueue::observeVideoTick(int64_t tick, AVRational fps)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (tick < 0 || fps.num <= 0 || fps.den <= 0) return;
    m_videoUs = av_rescale_q(tick, av_inv_q(fps), kUsecTimeBase);
    logDeltaLocked();
}

int64_t RecorderSyncQueue::avDeltaUs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_audioEnabled || m_audioClockUs == AV_NOPTS_VALUE || m_videoUs == AV_NOPTS_VALUE
        ? AV_NOPTS_VALUE : m_audioClockUs - m_videoUs;
}

void RecorderSyncQueue::logDeltaLocked()
{
    if (!m_audioEnabled || m_audioClockUs == AV_NOPTS_VALUE || m_videoUs == AV_NOPTS_VALUE) return;
    const int64_t now = av_gettime_relative(); // Log cadence only, never a timestamp source.
    if (m_lastDiagnosticUs != AV_NOPTS_VALUE && now - m_lastDiagnosticUs < 1000000) return;
    m_lastDiagnosticUs = now;
    const auto delta = m_audioClockUs - m_videoUs;
    // This compares production cursors; the mixer's holdback can show here even
    // with perfectly aligned capture timestamps. It is NOT a beacon offset.
    if (delta > 100000 || delta < -100000)
        qWarning() << "[AVSync] Degraded avDeltaUs=" << delta << "audioUs=" << m_audioClockUs << "videoUs=" << m_videoUs;
    else qInfo() << "[AVSync] avDeltaUs=" << delta << "audioUs=" << m_audioClockUs << "videoUs=" << m_videoUs;
}

int64_t RecorderSyncQueue::audioClockUs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_audioClockUs;
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

    frame->time_base = m_audioTimeBase;
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
