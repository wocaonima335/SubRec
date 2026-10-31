#include "capture/audio_timing.h"
#include "ffafilterthread.h"

#include "filter/ffafilter.h"
#include "filter/audio_timeline_mixer.h"
#include "queue/ffaframequeue.h"
#include "queue/media_clock.h"
#include "recorder/ffrecorder.h"

#include <QDebug>
#include <iostream>
#include <algorithm>
#include <stdexcept>

using namespace FFRecordContextType;

FFAFilterThread::FFAFilterThread()
{
    encoderFlag.store(false);
    audioFlag.store(false);
    microphoneFlag.store(false);
    pauseFlag.store(false);

    pauseTime = 0;
    lastPauseTime = 0;
}

FFAFilterThread::~FFAFilterThread()
{
    if (sysFrame) {
        av_frame_free(&sysFrame);
    }
    if (micFrame) {
        av_frame_free(&micFrame);
    }
}

void FFAFilterThread::openAudioSource(int audioType)
{
    cond.notify_all();
    enum demuxerType type = static_cast<demuxerType>(audioType);
    if (type == AUDIO) {
        audioFlag.store(true, std::memory_order_seq_cst);
    } else if (type == MICROPHONE) {
        microphoneFlag.store(true, std::memory_order_seq_cst);
    }
}

void FFAFilterThread::closeAudioSource(int audioType)
{
    cond.notify_all();
    enum demuxerType type = static_cast<demuxerType>(audioType);
    if (type == AUDIO) {
        audioFlag.store(false, std::memory_order_seq_cst);
    } else if (type == MICROPHONE) {
        microphoneFlag.store(false, std::memory_order_seq_cst);
    }
}

void FFAFilterThread::init(FFAFrameQueue *micFrmQueue_,
                           FFAFrameQueue *sysFrmQueue_,
                           FFAFilter *filter_)
{
    micFrmQueue = micFrmQueue_;
    sysFrmQueue = sysFrmQueue_;
    filter = filter_;
}

void FFAFilterThread::startEncoder()
{
    cond.notify_all();
    encoderFlag.store(true, std::memory_order_seq_cst);
    forwardedFrameCount.store(0, std::memory_order_release);
    droppedFrameCount.store(0, std::memory_order_release);
    muteFrameCount.store(0, std::memory_order_release);
    qDebug() << "[FFAFilterThread] encoder enabled";
}

void FFAFilterThread::stopEncoder()
{
    cond.notify_all();
    encoderFlag.store(false, std::memory_order_seq_cst);
    pauseFlag.store(false);
    pauseTime = 0;
    lastPauseTime = 0;
    qDebug() << "[FFAFilterThread] encoder disabled, forwarded="
             << forwardedFrameCount.load(std::memory_order_acquire)
             << "dropped=" << droppedFrameCount.load(std::memory_order_acquire)
             << "mute=" << muteFrameCount.load(std::memory_order_acquire);
}

void FFAFilterThread::pauseEncoder()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (pauseFlag.load()) {

        pauseTime += av_gettime_relative() - lastPauseTime;
        pauseFlag.store(false);
    } else {
        pauseFlag.store(true);
        lastPauseTime = av_gettime_relative();

    }
}

void FFAFilterThread::onPauseChanged(bool pausedFlag, int64_t ts_us)
{
    std::lock_guard<std::mutex> lock(mutex);
    const int64_t now = ts_us > 0 ? ts_us : av_gettime_relative();
    if (pausedFlag) {
        if (pauseFlag.load(std::memory_order_acquire)) {
            return;
        }

        pauseFlag.store(true, std::memory_order_release);
        lastPauseTime.store(now, std::memory_order_release);
        qDebug() << "[FFAFilterThread] paused at" << now;
    } else {
        if (!pauseFlag.load(std::memory_order_acquire)) {
            return;
        }

        pauseTime.store(pauseTime.load(std::memory_order_acquire)
                            + (now - lastPauseTime.load(std::memory_order_acquire)),
                        std::memory_order_release);
        pauseFlag.store(false, std::memory_order_release);
        qDebug() << "[FFAFilterThread] resumed at" << now
                 << "total_pause=" << pauseTime.load(std::memory_order_acquire);
    }
    cond.notify_all();
}

void FFAFilterThread::setAudioVolume(double value)
{
    if (value >= 0) systemGain = value;
    if (filter) {
        if (audioFlag.load() && microphoneFlag.load()) {
            filter->setAudioVolume(value, -1);
        } else {
            filter->setSystemAudioVolume(value);
        }
    }
}

void FFAFilterThread::setMicrophoneVolume(double value)
{
    if (value >= 0) microphoneGain = value;
    if (filter) {
        if (audioFlag.load() && microphoneFlag.load()) {
            filter->setAudioVolume(-1, value);
        } else {
            filter->setMicrophoneAudioVolume(value);
        }
    }
}

bool FFAFilterThread::peekStart()
{
    return encoderFlag.load();
}

void FFAFilterThread::wakeAllThread()
{
    if (sysFrmQueue) {
        sysFrmQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    if (micFrmQueue) {
        micFrmQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    cond.notify_all();
}

void FFAFilterThread::run()
{
    if (mixing) { runMixed(); return; }
    while (!m_stop) {
        bool microphoneActive = microphoneFlag.load(std::memory_order_seq_cst);
        bool audioActive = audioFlag.load(std::memory_order_seq_cst);
        bool encoderActive = encoderFlag.load(std::memory_order_seq_cst);
        bool pauseActive = pauseFlag.load(std::memory_order_seq_cst);

        if (!microphoneActive && !audioActive && !encoderActive) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait_for(lock, std::chrono::milliseconds(100));
            continue; // 重新读取所有标志
        }

        int64_t start = av_gettime_relative() * 10;

        if (audioActive) {
            auto received = sysFrmQueue->receive();
            if (received.result == MediaQueueResult::Eof) {
                if (filter) {
                    qDebug() << "[FFAFilterThread] system queue finished, forwarding finish";
                    filter->sendFinish();
                }
                break;
            }
            if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
                qDebug() << "[FFAFilterThread] system receive exit result="
                         << mediaQueueResultName(received.result);
                break;
            }
            sysFrame = received.item;
            if (encoderActive && !pauseActive) {
                filter->processSingleAudioFrame(sysFrame,
                                                start,
                                                pauseTime,
                                                AudioSourceType::AUDIO_SOURCE_SYSTEM);
                sysFrame = nullptr;
                const auto forwarded =
                    forwardedFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (forwarded == 1 || forwarded % 30 == 0) {
                    qDebug() << "[FFAFilterThread] forwarded system frame count="
                             << forwarded;
                }
            } else {
                const auto dropped =
                    droppedFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (dropped == 1 || dropped % 30 == 0) {
                    qDebug() << "[FFAFilterThread] dropped system frame count="
                             << dropped << "encoder=" << encoderActive
                             << "pause=" << pauseActive;
                }
                if (sysFrame) {
                    av_frame_unref(sysFrame);
                    av_frame_free(&sysFrame);
                }
            }
        } else if (microphoneActive) {
            auto received = micFrmQueue->receive();
            if (received.result == MediaQueueResult::Eof) {
                if (filter) {
                    qDebug() << "[FFAFilterThread] microphone queue finished, forwarding finish";
                    filter->sendFinish();
                }
                break;
            }
            if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
                qDebug() << "[FFAFilterThread] microphone receive exit result="
                         << mediaQueueResultName(received.result);
                break;
            }
            micFrame = received.item;
            if (encoderActive && !pauseActive) {
                filter->processSingleAudioFrame(micFrame,
                                                start,
                                                pauseTime,
                                                AudioSourceType::AUDIO_SOURCE_MICROPHONE);
                micFrame = nullptr;
                const auto forwarded =
                    forwardedFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (forwarded == 1 || forwarded % 30 == 0) {
                    qDebug() << "[FFAFilterThread] forwarded microphone frame count="
                             << forwarded;
                }
            } else {
                const auto dropped =
                    droppedFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (dropped == 1 || dropped % 30 == 0) {
                    qDebug() << "[FFAFilterThread] dropped microphone frame count="
                             << dropped << "encoder=" << encoderActive
                             << "pause=" << pauseActive;
                }
                if (micFrame) {
                    av_frame_unref(micFrame);
                    av_frame_free(&micFrame);
                }
            }
        } else {
            if (encoderActive && !pauseActive) {
                AVFrame *muteFrame = generateMuteFrame();
                filter->forwardAudioFrame(muteFrame, start, pauseTime);
                const auto muteCount =
                    muteFrameCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (muteCount == 1 || muteCount % 30 == 0) {
                    qDebug() << "[FFAFilterThread] forwarded mute frame count="
                             << muteCount;
                }
            }
        }
    }
}

AVFrame *FFAFilterThread::generateMuteFrame()
{
    AVFrame *frame = av_frame_alloc();

    frame->format = AUDIO_SAMPLE_FORMAT;
    frame->sample_rate = AUDIO_SAMPLE_RATE;
    frame->nb_samples = 1024;
    av_channel_layout_default(&frame->ch_layout, 2);

    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        qDebug() << "get mute Frame buffer error";
        av_frame_free(&frame);
        return nullptr;
    }

    ret = av_samples_set_silence(frame->extended_data,
                                 0,
                                 frame->nb_samples,
                                 frame->ch_layout.nb_channels,
                                 AUDIO_SAMPLE_FORMAT);

    if (ret < 0) {
        qDebug() << "Set Samples Silence Fail !";
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

void FFAFilterThread::runMixed()
{
    try {
        if (!mediaClock) throw std::runtime_error("Audio session clock not configured");
        AudioTimelineMixer mixer(*mediaClock);
        mixer.setResyncObserver([](unsigned source, const char *reason, double errorUs,
                                   double before, double after) {
            qWarning() << "[AudioMix] resync source=" << (source ? "microphone" : "system")
                       << "reason=" << reason << "error_us=" << errorUs
                       << "ppm_before=" << before << "ppm_after=" << after;
        });
        // Also report on abort/error unwinding, before the mixer's stats disappear.
        struct ClockSummary {
            const AudioTimelineMixer &mixer;
            ~ClockSummary() {
                const auto &s = mixer.stats();
                for (unsigned source = 0; source < 2; ++source)
                    qInfo() << "[AudioMix] clock_summary source=" << (source ? "microphone" : "system")
                            << "resyncs=" << s.resyncs[source] << "ppm_min=" << s.ppmMin[source]
                            << "ppm_max=" << s.ppmMax[source] << "ppm_last=" << s.compensationPpm[source];
            }
        } clockSummary{mixer};
        int64_t nextDiagnosticUs = av_gettime_relative() + 60000000;
        bool eof[2]{!audioFlag.load(), !microphoneFlag.load()};
        uint64_t sourceDiagnostics[2]{};
        for (unsigned source = 0; source < 2; ++source) if (eof[source]) mixer.finish(source);
        FFAFrameQueue *queues[] = {sysFrmQueue, micFrmQueue};
        qInfo() << "[AudioMix] enabled: clock-aligned 48k stereo FLTP, amix normalize=0, peak limiter="
                << AudioTimelineMixer::limiterCeiling;
        while (!m_stop) {
            for (unsigned source = 0; source < 2; ++source) {
                // Bound each turn so a busy input cannot starve the other input.
                for (int n = 0; n < 30 && !eof[source]; ++n) {
                    auto received = queues[source]->tryReceive();
                    if (received.result == MediaQueueResult::Again) break;
                    if (received.result == MediaQueueResult::Eof) {
                        eof[source] = true; mixer.finish(source); break;
                    }
                    if (!received.ok()) {
                        if (received.closeReason == MediaQueueCloseReason::Error)
                            throw std::runtime_error("Audio mix input queue failed");
                        return; // abort: the session closes all downstream queues
                    }
                    AudioTimelineMixer::Frame frame(received.item);
                    const int64_t receiptUs = av_gettime_relative();
                    const int64_t sourceUs = av_rescale_q(frame->pts, frame->time_base,
                                                          AVRational{1, 1000000});
                    const auto diagnosticIndex = ++sourceDiagnostics[source];
                    if (diagnosticIndex == 1 || diagnosticIndex % 300 == 0) {
                        qInfo() << "[AudioClock] mixer_diagnostic source=" << source
                                << "index=" << diagnosticIndex
                                << "sourceUs=" << sourceUs
                                << "receiptUs=" << receiptUs
                                << "receiptDelayUs=" << (receiptUs - sourceUs)
                                << "samples=" << frame->nb_samples;
                    }
                    mixer.push(source, *frame);
                }
            }
            const bool done = eof[0] && eof[1];
            // The raw cutoff is only a holdback boundary. renderAt maps it
            // through MediaClock, excluding pauses and preserving capture origin.
            const int64_t cutoff = done ? mixer.captureEndUs() : av_gettime_relative() - capture::audio::mixHoldbackUs;
            while (!m_stop) {
                auto frame = mixer.renderAt(cutoff, systemGain, microphoneGain);
                if (!frame) break;
                if (encoderFlag) {
                    filter->forwardAudioFrame(frame.release(), 0, 0);
                    ++forwardedFrameCount;
                } else ++droppedFrameCount;
            }
            const int64_t nowUs = av_gettime_relative();
            if (nowUs >= nextDiagnosticUs) {
                nextDiagnosticUs = nowUs + 60000000;
                const auto &s = mixer.stats();
                for (unsigned source = 0; source < 2; ++source)
                    qInfo() << "[AudioMix] compensation source=" << (source ? "microphone" : "system")
                            << "ppm=" << s.compensationPpm[source] << "error_us=" << s.phaseErrorUs[source]
                            << "resyncs=" << s.resyncs[source];
                qInfo() << "[RecordingBuffers] mixer_system_frames=" << mixer.pendingFrames(0)
                        << "mixer_mic_frames=" << mixer.pendingFrames(1)
                        << "mixer_ahead_samples=" << mixer.end() - mixer.position()
                        << "system_queue=" << queues[0]->sharedState()->length()
                        << "mic_queue=" << queues[1]->sharedState()->length();
            }
            if (done) {
                const auto &s = mixer.stats();
                qInfo() << "[AudioMix] finished system_frames=" << s.frames[0] << "mic_frames=" << s.frames[1]
                        << "system_silence_samples=" << s.silence[0] << "mic_silence_samples=" << s.silence[1]
                        << "trimmed_system=" << s.trimmed[0] << "trimmed_mic=" << s.trimmed[1]
                        << "future_dropped_system=" << s.futurePacketsDropped[0]
                        << "future_dropped_mic=" << s.futurePacketsDropped[1]
                        << "over_ceiling_samples=" << s.limited << "limiter_samples=" << s.limiterSamples
                        << "over_ceiling_ratio=" << s.overCeilingRatio()
                        << "max_gain_reduction_db=" << s.maxGainReductionDb;
                filter->sendFinish();
                return;
            }
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait_for(lock, std::chrono::milliseconds(5));
        }
    } catch (const std::exception &error) {
        qWarning() << "[AudioMix] failed:" << error.what();
        sysFrmQueue->close(MediaQueueCloseReason::Error);
        micFrmQueue->close(MediaQueueCloseReason::Error);
        filter->sendFinish();
        auto *recorder = &FFRecorder::getInstance();
        QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromUtf8(error.what())),
            Q_ARG(QString, QStringLiteral("Check the microphone and system audio devices.")));
    }
}
