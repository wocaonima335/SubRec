#include "ffafilterthread.h"

#include "filter/ffafilter.h"
#include "filter/audio_timeline_mixer.h"
#include "queue/ffaframequeue.h"
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
    mixPauses.clear();
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
        recordMixPause(false, av_gettime_relative());
        pauseTime += av_gettime_relative() - lastPauseTime;
        pauseFlag.store(false);
    } else {
        pauseFlag.store(true);
        lastPauseTime = av_gettime_relative();
        recordMixPause(true, lastPauseTime);
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
        recordMixPause(true, now);
        pauseFlag.store(true, std::memory_order_release);
        lastPauseTime.store(now, std::memory_order_release);
        qDebug() << "[FFAFilterThread] paused at" << now;
    } else {
        if (!pauseFlag.load(std::memory_order_acquire)) {
            return;
        }
        recordMixPause(false, now);
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

void FFAFilterThread::recordMixPause(bool paused, int64_t timestampUs)
{
    if (!mixing) return;
    if (paused) mixPauses.emplace_back(timestampUs, AV_NOPTS_VALUE);
    else if (!mixPauses.empty() && mixPauses.back().second == AV_NOPTS_VALUE)
        mixPauses.back().second = timestampUs;
}

void FFAFilterThread::runMixed()
{
    try {
        AudioTimelineMixer mixer(mixStartUs);
        bool eof[2]{};
        FFAFrameQueue *queues[] = {sysFrmQueue, micFrmQueue};
        qInfo() << "[AudioMix] enabled: WASAPI + microphone, 48k stereo FLTP, amix normalize=0, peak limiter=0.95";
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
                    mixer.push(source, *frame);
                }
            }
            const bool done = eof[0] && eof[1];
            const int64_t target = done ? mixer.end() :
                std::max<int64_t>(0, (av_gettime_relative() - mixStartUs - 200000) * 48000 / 1000000);
            while (!m_stop && mixer.position() < target) {
                int64_t until = target;
                bool discard = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    const auto sample = [this](int64_t us) {
                        return av_rescale_q(us - mixStartUs, AVRational{1,1000000}, AVRational{1,48000});
                    };
                    while (!mixPauses.empty() && mixPauses.front().second != AV_NOPTS_VALUE &&
                           sample(mixPauses.front().second) <= mixer.position()) mixPauses.pop_front();
                    if (!mixPauses.empty()) {
                        const auto &pause = mixPauses.front();
                        if (sample(pause.first) <= mixer.position()) {
                            discard = true;
                            if (pause.second != AV_NOPTS_VALUE) until = std::min(until, sample(pause.second));
                        } else until = std::min(until, sample(pause.first));
                    }
                }
                auto frame = mixer.render(until, systemGain, microphoneGain);
                if (encoderFlag && !discard) {
                    filter->forwardAudioFrame(frame.release(), mixStartUs, pauseTime);
                    ++forwardedFrameCount;
                } else ++droppedFrameCount;
            }
            if (done) {
                const auto &s = mixer.stats();
                qInfo() << "[AudioMix] finished system_frames=" << s.frames[0] << "mic_frames=" << s.frames[1]
                        << "system_silence_samples=" << s.silence[0] << "mic_silence_samples=" << s.silence[1]
                        << "trimmed_system=" << s.trimmed[0] << "trimmed_mic=" << s.trimmed[1]
                        << "limited_samples=" << s.limited;
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
