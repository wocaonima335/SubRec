#include "capture_session.h"

#include "event/ffclosesourceevent.h"
#include "event/ffopensourceevent.h"
#include "recorder/ffrecorder.h"
#include "thread/ffmuxerthread.h"
#include "thread/wincomscope.h"

#include <QDebug>
#include <memory>

using namespace FFRecordContextType;

CaptureSession::CaptureSession(FFRecorder &recorder)
    : m_recorder(recorder)
{}

CaptureSession::~CaptureSession()
{
    release();
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
        const CaptureStartResult result = openEvent->openAndStartResult([this]() {
            return !m_stopRequested.load(std::memory_order_acquire);
        });
        const bool cancelled = m_stopRequested.load(std::memory_order_acquire);
        qDebug() << "[CaptureSession] open task finished, ok=" << result.ok
                 << "cancelled=" << cancelled;
        if (!result.ok || cancelled) {
            qWarning() << "[CaptureSession] aborting partial capture setup";
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

    // Join all users of decoder/encoder contexts before closing any context.
    const auto waitWorker = [](auto *worker) { if (worker) worker->wait(); };
    for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
        waitWorker(pipeline->aDemuxerThread[i]);
        waitWorker(pipeline->aDecoderThread[i]);
    }
    for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
        waitWorker(pipeline->vDemuxerThread[i]);
        waitWorker(pipeline->vDecoderThread[i]);
    }
    waitWorker(pipeline->aFilterThread);
    waitWorker(pipeline->vFilterThread);
    waitWorker(pipeline->aEncoderThread);
    waitWorker(pipeline->vEncoderThread);
    waitWorker(pipeline->muxerThread);

    const auto closeWorker = [](auto *worker) { if (worker) worker->close(); };
    for (size_t i = 0; i < A_DECODER_SIZE; ++i) {
        closeWorker(pipeline->aDemuxerThread[i]);
        closeWorker(pipeline->aDecoderThread[i]);
    }
    for (size_t i = 0; i < V_DECODER_SIZE; ++i) {
        closeWorker(pipeline->vDemuxerThread[i]);
        closeWorker(pipeline->vDecoderThread[i]);
    }
    closeWorker(pipeline->aEncoderThread);
    closeWorker(pipeline->vEncoderThread);
    closeWorker(pipeline->muxerThread);
    if (pipeline->aFilterThread) {
        pipeline->aFilterThread->stopEncoder();
        pipeline->aFilterThread->closeAudioSource(AUDIO);
        pipeline->aFilterThread->closeAudioSource(MICROPHONE);
    }
    if (pipeline->vFilterThread) {
        pipeline->vFilterThread->stopEncoder();
        pipeline->vFilterThread->closeVideoSource(SCREEN);
        pipeline->vFilterThread->closeVideoSource(CAMERA);
    }
    m_cleanupNeeded = false;
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
}

void CaptureSession::cleanup()
{
    if (!m_cleanupNeeded) return;
    // Called by the opener on failure OR by release after joining the opener.
    // These are serialized by join, so resources cannot be closed twice.
    if (m_recorder.isRecording()) {
        qDebug() << "[CaptureSession] calling FFRecorder::stopRecord";
        m_recorder.stopRecord();
        qDebug() << "[CaptureSession] FFRecorder::stopRecord returned";
    }

    qDebug() << "[CaptureSession] closing video source";
    closeVideoSource();
    qDebug() << "[CaptureSession] video source closed";
    qDebug() << "[CaptureSession] closing audio source";
    closeAudioSource();
    qDebug() << "[CaptureSession] audio source closed";
    qDebug() << "[CaptureSession] finalizing muxer";
    finalizeMuxer();
    qDebug() << "[CaptureSession] muxer finalized";
    m_cleanupNeeded = false;
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
}

void CaptureSession::join()
{
    qDebug() << "[CaptureSession] join";
    if (m_openThread.joinable()) {
        m_openThread.join();
    }
}

void CaptureSession::release()
{
    requestStop();
    join();
    cleanup();
    m_started.store(false, std::memory_order_release);
    m_starting.store(false, std::memory_order_release);
    m_stopRequested.store(false, std::memory_order_release);
    m_created = false;
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

void CaptureSession::closeVideoSource()
{
    auto closeVideoEvent = std::make_unique<FFCloseSourceEvent>(
        &m_recorder, m_config.video.kind == CaptureDeviceKind::Screen
                                        ? demuxerType::SCREEN : demuxerType::CAMERA);
    closeVideoEvent->work();
}

void CaptureSession::closeAudioSource()
{
    auto closeAudioEvent = std::make_unique<FFCloseSourceEvent>(
        &m_recorder, m_config.audio.kind == CaptureDeviceKind::SystemAudio
                                             ? demuxerType::AUDIO : demuxerType::MICROPHONE);
    closeAudioEvent->work();
}

void CaptureSession::finalizeMuxer()
{
    auto *muxerThread = m_recorder.getMuxerThread();
    if (!muxerThread) {
        return;
    }

    qDebug() << "[CaptureSession] muxer wait";
    muxerThread->wait();
    qDebug() << "[CaptureSession] muxer close";
    muxerThread->close();
}
