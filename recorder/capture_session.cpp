#include "capture_session.h"

#include "event/ffclosesourceevent.h"
#include "event/ffopensourceevent.h"
#include "recorder/ffrecorder.h"
#include "thread/ffmuxerthread.h"
#include "thread/wincomscope.h"
#include "thread/failure_cleanup_deadline.h"
#include "queue/recordersyncqueue.h"

extern "C" {
#include <libavutil/error.h>
}

#include <QDebug>
#include <memory>

using namespace FFRecordContextType;

CaptureSession::CaptureSession(FFRecorder &recorder)
    : m_recorder(recorder)
{}

CaptureSession::~CaptureSession()
{
    if (!release()) {
        // A retained, borrowed worker cannot safely outlive Recorder destruction.
        // Final cancellation is bounded too; an uninterruptible driver is fatal.
        abortPipeline();
    }
}

bool CaptureSession::create()
{
    if (m_created) {
        return true;
    }

    qDebug() << "[CaptureSession] create";
    m_recorder.initialize();
    m_created = true;
    return true;
}

bool CaptureSession::start(const CaptureConfig &config,
                           std::function<void(const CaptureStartResult &)> onStarted)
{
    if (!create()) {
        return false;
    }

    if (m_started.load(std::memory_order_acquire)
        || m_starting.load(std::memory_order_acquire) || m_openThread.joinable()
        || m_cleanupNeeded) {
        qWarning() << "[CaptureSession] start rejected: release the previous run first";
        return false;
    }

    if (testOpenerOverride) {
        // Harness path: skip FFRecorder::startRecord and the real opener. The
        // synthetic opener owns all state transitions and never touches media.
        m_config = config;
        m_stopRequested.store(false, std::memory_order_release);
        m_starting.store(true, std::memory_order_release);
        testOpenerOverride(*this, std::move(onStarted));
        return true;
    }

    // Called on the scheduler control thread before launching the opener.
    m_config = config;
    m_stopRequested.store(false, std::memory_order_release);

    qDebug() << "[CaptureSession] start"
             << "videoSource=" << captureDeviceKindName(m_config.video.kind)
             << "videoDevice=" << m_config.videoInput.deviceId
             << "audioSource=" << captureDeviceKindName(m_config.audio.kind)
             << "audioDevice=" << m_config.audioInput.deviceId
             << "outputPath=" << m_config.outputPath
             << "outputFileName=" << m_config.outputFileName;

    qDebug() << "[CaptureSession] calling FFRecorder::startRecord";
    m_videoClosed = m_audioClosed = m_muxerClosed = false;
    m_startFailed = false;
    m_cleanupNeeded = true;
    m_recorder.startRecord(m_config);
    qDebug() << "[CaptureSession] FFRecorder::startRecord returned";

    m_starting.store(true, std::memory_order_release);

    const CaptureConfig startConfig = m_config;
    qDebug() << "[CaptureSession] launching dedicated open thread";
    m_openThread = std::thread([this, startConfig, onStarted = std::move(onStarted)]() mutable {
        ScopedWinComInitializer comInit("CaptureSession::openThread");
        qDebug() << "[CaptureSession] open thread COM"
                 << "tried=" << comInit.tried()
                 << "ready=" << comInit.ready()
                 << "hr=0x" << QString::number(static_cast<qulonglong>(static_cast<unsigned long>(comInit.result())), 16);
        qDebug() << "[CaptureSession] open task begin";
        auto openEvent = std::make_unique<FFOpenSourceEvent>(
            &m_recorder, startConfig.videoInput, startConfig.audioInput);
        openEvent->setMixMicrophone(startConfig.microphoneMixInput);
        openEvent->setAudioEnabled(startConfig.audioEnabled);
        openEvent->setAudioFallback(startConfig.audioFallbackInput);
        openEvent->setVideoFallback(startConfig.videoFallbackInput);
        const CaptureStartResult result = openEvent->openAndStartResult([this]() {
            return !m_stopRequested.load(std::memory_order_acquire);
        });
        const bool cancelled = m_stopRequested.load(std::memory_order_acquire);
        qDebug() << "[CaptureSession] open task finished, ok=" << result.ok
                 << "cancelled=" << cancelled;
        if (!result.ok || cancelled) {
            qWarning() << "[CaptureSession] aborting partial capture setup";
            m_startFailed = true;
            // Publish failure before waiting on native devices. The queued scheduler
            // callback exposes Failed/setCaptureError even if a driver is stuck.
            if (!cancelled && onStarted) {
                onStarted(result);
                onStarted = {};
            }
            abortPipeline();
        }

        m_started.store(result.ok && !cancelled, std::memory_order_release);
        m_starting.store(false, std::memory_order_release);

        if (m_stopRequested.load(std::memory_order_acquire)) {
            qDebug() << "[CaptureSession] open finished after stop request";
            return;
        }

        if (onStarted) {
            onStarted(result);
        }
    });
    qDebug() << "[CaptureSession] open thread launched";

    return true;
}

void CaptureSession::requestStop()
{
    // No joins, source destruction, or Recorder/UI access on the cancel path.
    // Underlying device-open interruption is handled separately by R-02/R-03.
    m_stopRequested.store(true, std::memory_order_release);
}

void CaptureSession::abortPipeline()
{
    if (!m_cleanupNeeded) return;
    capture_stop::Deadline deadline;
    // Also bounds native close(), not just std::thread waits. Failed preparation
    // never returns to normal recording with device readers still alive.
    capture_stop::FailureCleanupDeadline watchdog;
    auto *pipeline = m_recorder.getContext();
    m_recorder.stopRecord();

    // Failed preparation has no reliable EOF chain. Close EVERY queue first:
    // a worker can be blocked on either its input or its downstream output.
    const auto closeQueue = [](auto *queue) {
        if (queue) queue->close(MediaQueueCloseReason::RequestedStop);
    };
    for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
        closeQueue(pipeline->aDecoderPktQueue[i]);
        closeQueue(pipeline->aDecoderFrmQueue[i]);
    }
    for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
        closeQueue(pipeline->vDecoderPktQueue[i]);
        closeQueue(pipeline->vDecoderFrmQueue[i]);
    }
    closeQueue(pipeline->aFilterEncoderFrmQueue);
    closeQueue(pipeline->vFilterEncoderFrmQueue);
    closeQueue(pipeline->aEncoderPktQueue);
    closeQueue(pipeline->vEncoderPktQueue);
#ifdef _WIN32
    if (pipeline->systemAudioCapture) {
        pipeline->systemAudioCapture->requestAbort();
    }
#endif
    if (pipeline->screenGrabThread) pipeline->screenGrabThread->requestAbort();

    const auto abortWorker = [](auto *worker) { if (worker) worker->requestAbort(); };
    for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
        abortWorker(pipeline->aDemuxerThread[i]);
        abortWorker(pipeline->aDecoderThread[i]);
    }
    for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
        abortWorker(pipeline->vDemuxerThread[i]);
        abortWorker(pipeline->vDecoderThread[i]);
    }
    abortWorker(pipeline->aEncoderThread);
    abortWorker(pipeline->vEncoderThread);
    const auto stopFilter = [](auto *worker) {
        if (worker) { worker->stop(); worker->wakeAllThread(); }
    };
    stopFilter(pipeline->aFilterThread);
    stopFilter(pipeline->vFilterThread);
    if (pipeline->muxerThread) pipeline->muxerThread->wakeAllThread();

    // Wait against ONE deadline, after all producers and consumers were cancelled.
    // Close the audio contexts before waiting for video: avformat_close_input stops
    // DirectShow's own device callbacks, even after its reader thread has exited.
    const auto waitWorker = [&deadline](auto *worker) {
        return !worker || worker->waitFor(deadline.remaining());
    };
    const auto closeWorker = [](auto *worker) { if (worker) worker->close(); };
    bool audioJoined = true;
#ifdef _WIN32
    if (pipeline->systemAudioCapture &&
        !pipeline->systemAudioCapture->joinFor(deadline.remaining())) {
        qWarning() << "[CaptureSession] WASAPI failed to stop before the startup cleanup deadline";
        capture_stop::failedCleanupExit();
    }
#endif
    for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
        audioJoined = waitWorker(pipeline->aDemuxerThread[i]) && audioJoined;
        audioJoined = waitWorker(pipeline->aDecoderThread[i]) && audioJoined;
    }
    audioJoined = waitWorker(pipeline->aFilterThread) && audioJoined;
    audioJoined = waitWorker(pipeline->aEncoderThread) && audioJoined;
    if (audioJoined) {
        for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
            closeWorker(pipeline->aDemuxerThread[i]);
            closeWorker(pipeline->aDecoderThread[i]);
        }
        closeWorker(pipeline->aEncoderThread);
        if (pipeline->aFilterThread) {
            pipeline->aFilterThread->stopEncoder();
            pipeline->aFilterThread->closeAudioSource(AUDIO);
            pipeline->aFilterThread->closeAudioSource(MICROPHONE);
        }
    }
    bool videoJoined = true;
    for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
        videoJoined = waitWorker(pipeline->vDemuxerThread[i]) && videoJoined;
        videoJoined = waitWorker(pipeline->vDecoderThread[i]) && videoJoined;
    }
    videoJoined = waitWorker(pipeline->vFilterThread) && videoJoined;
    videoJoined = waitWorker(pipeline->vEncoderThread) && videoJoined;
    const bool muxerJoined = waitWorker(pipeline->muxerThread);
    if (pipeline->screenGrabThread && !pipeline->screenGrabThread->waitFor(
            deadline.remaining())) {
        qWarning() << "[CaptureSession] native screen open did not return before the cleanup deadline";
        capture_stop::failedCleanupExit();
    }
    closeWorker(pipeline->screenGrabThread);
    if (videoJoined) {
        for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
            closeWorker(pipeline->vDemuxerThread[i]);
            closeWorker(pipeline->vDecoderThread[i]);
        }
        closeWorker(pipeline->vEncoderThread);
        if (pipeline->vFilterThread) {
            pipeline->vFilterThread->stopEncoder();
            pipeline->vFilterThread->closeVideoSource(SCREEN);
            pipeline->vFilterThread->closeVideoSource(CAMERA);
        }
    }
    if (muxerJoined) closeWorker(pipeline->muxerThread);
    if (!audioJoined || !videoJoined || !muxerJoined) {
        qWarning() << "[CaptureSession] failed startup abort deadline exceeded"
                   << "audio=" << audioJoined << "video=" << videoJoined << "muxer=" << muxerJoined;
        capture_stop::failedCleanupExit();
    }
    qInfo() << "[CaptureSession] failed startup cleanup complete; audio devices closed";
    m_cleanupNeeded = false;
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
}

bool CaptureSession::cleanup()
{
    if (!m_cleanupNeeded) return true;
    if (m_startFailed) { abortPipeline(); return true; }
    // Called by the opener on failure OR by release after joining the opener.
    // These are serialized by join, so resources cannot be closed twice.
    if (m_recorder.isRecording()) {
        qDebug() << "[CaptureSession] calling FFRecorder::stopRecord";
        m_recorder.stopRecord();
        qDebug() << "[CaptureSession] FFRecorder::stopRecord returned";
    }

    if (auto *worker = m_recorder.getMuxerThread()) worker->requestDrainStop();
    qDebug() << "[CaptureSession] closing video source";
    if (!m_videoClosed) m_videoClosed = closeVideoSource();
    qDebug() << "[CaptureSession] video source closed";
    qDebug() << "[CaptureSession] closing audio source";
    if (!m_audioClosed) m_audioClosed = closeAudioSource();
    qDebug() << "[CaptureSession] audio source closed";
    qDebug() << "[CaptureSession] finalizing muxer";
    if (!m_muxerClosed && !finalizeMuxer())
        qWarning() << "[CaptureSession] muxer finalization failed";
    qDebug() << "[CaptureSession] cleanup joined video/audio/muxer="
             << m_videoClosed << m_audioClosed << m_muxerClosed;
    if (!m_videoClosed || !m_audioClosed || !m_muxerClosed) return false;
    m_cleanupNeeded = false;
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
    return true;
}

void CaptureSession::join()
{
    qDebug() << "[CaptureSession] join";
    if (m_openThread.joinable()) {
        m_openThread.join();
    }
}

bool CaptureSession::release()
{
    requestStop();
    join();
    if (!cleanup()) return false;
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
    m_stopRequested.store(false, std::memory_order_release);
    m_created = false;
    return true;
}

bool CaptureSession::isCreated() const
{
    return m_created;
}

bool CaptureSession::isStarted() const
{
    return m_started.load(std::memory_order_acquire);
}

const CaptureConfig &CaptureSession::config() const
{
    return m_config;
}

bool CaptureSession::closeVideoSource()
{
    auto closeVideoEvent = std::make_unique<FFCloseSourceEvent>(
        &m_recorder, m_config.video.kind == CaptureDeviceKind::Screen
                                        ? demuxerType::SCREEN : demuxerType::CAMERA);
    closeVideoEvent->work();
    return closeVideoEvent->completed();
}

bool CaptureSession::closeAudioSource()
{
    if (!m_config.audioEnabled) return true;
    auto closeAudioEvent = std::make_unique<FFCloseSourceEvent>(
        &m_recorder, m_config.audio.kind == CaptureDeviceKind::SystemAudio
                                             ? demuxerType::AUDIO : demuxerType::MICROPHONE);
    closeAudioEvent->work();
    return closeAudioEvent->completed();
}

bool CaptureSession::finalizeMuxer()
{
    auto *muxerThread = m_recorder.getMuxerThread();
    if (!muxerThread) {
        m_muxerClosed = true;
        return true;
    }

    qDebug() << "[CaptureSession] muxer wait";
    if (!capture_stop::drain(*muxerThread, [muxerThread] {
            muxerThread->finishQueuedPackets();
        }, [](const char *message) { qWarning() << "[CaptureSession] muxer" << message; }))
        return false;
    qDebug() << "[CaptureSession] muxer close";
    muxerThread->close();
    m_muxerClosed = true; // Joined/closed even if writing failed; release remains safe.
    const int muxError = muxerThread->lastError();
    if (muxError != 0) {
        m_recorder.setCaptureError(
            QStringLiteral("Muxer write failed; recording stopped."),
            QStringLiteral("Recording stopped; check the output file."));
    }
    if (auto *syncQueue = m_recorder.getSyncQueue()) {
        qInfo() << "[RecorderSyncQueue] summary dropped_video=" << syncQueue->droppedVideoFrames()
                << "catchups=" << syncQueue->videoCatchupCount();
    }
    return muxError == 0;
}
