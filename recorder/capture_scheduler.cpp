#include "capture_scheduler.h"

#include "capture/dshow_capture_backend.h"
#include "recorder/ffrecorder.h"

extern "C" {
#include <libavutil/time.h>
}

#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QThread>

#include <algorithm>

QString captureSchedulerStateName(CaptureSchedulerState state)
{
    switch (state) {
    case CaptureSchedulerState::Idle: return QStringLiteral("Idle");
    case CaptureSchedulerState::Preparing: return QStringLiteral("Preparing");
    case CaptureSchedulerState::Running: return QStringLiteral("Running");
    case CaptureSchedulerState::Paused: return QStringLiteral("Paused");
    case CaptureSchedulerState::Stopping: return QStringLiteral("Stopping");
    case CaptureSchedulerState::Failed: return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

CaptureScheduler::CaptureScheduler(FFRecorder &recorder)
    : QObject(nullptr), m_recorder(recorder)
{}

CaptureScheduler::~CaptureScheduler()
{
    stop();
}

void CaptureScheduler::initialize()
{
    if (!onControlThread("initialize")) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recorder.initialize();
    }
    refreshDevices();
    qDebug() << "[CaptureScheduler] initialize finished";
}

bool CaptureScheduler::onControlThread(const char *operation) const
{
    if (QThread::currentThread() == thread()) return true;
    qWarning() << "[CaptureScheduler] rejected off-thread control operation" << operation;
    return false;
}

bool CaptureScheduler::start()
{
    if (!onControlThread("start")) return false;
    std::unique_ptr<CaptureSession> previous;
    std::uint64_t sessionId;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != CaptureSchedulerState::Idle && m_state != CaptureSchedulerState::Failed) {
            qDebug() << "[CaptureScheduler] start ignored in state" << captureSchedulerStateName(m_state);
            return false;
        }
        sessionId = ++m_sessionId;
        setStateLocked(CaptureSchedulerState::Preparing, QStringLiteral("start requested"));
        previous = std::move(m_session);
    }
    // A failed previous opener may still be returning after posting its result.
    // Never join it while holding the state mutex, and never reuse its session.
    if (previous) previous->release();

    CaptureConfig config;
    const CaptureStartResult configResult =
        m_testConfigHook ? m_testConfigHook(&config) : buildCurrentConfig(&config);
    if (!configResult.ok) {
        onSessionStarted(sessionId, configResult);
        return false;
    }

    auto next = std::make_unique<CaptureSession>(m_recorder);
    next->testOpenerOverride = m_testOpenerHook;
    if (!next->create()) {
        CaptureError error;
        error.stage = CaptureErrorStage::Configuration;
        error.userMessage = QStringLiteral("Unable to initialize the capture session.");
        error.remediation = QStringLiteral("Restart the application.");
        onSessionStarted(sessionId, CaptureStartResult::failure(std::move(error)));
        return false;
    }
    auto *session = next.get();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_session = std::move(next);
    }
    if (!session->start(config, [this, sessionId](const CaptureStartResult &result) {
            // The scheduler outlives the opener: its destructor joins the session.
            // QObject removes queued calls on destruction. No worker takes m_mutex.
            QMetaObject::invokeMethod(this, [this, sessionId, result]() {
                onSessionStarted(sessionId, result);
            }, Qt::QueuedConnection);
        })) {
        CaptureError error;
        error.stage = CaptureErrorStage::ThreadStart;
        error.userMessage = QStringLiteral("Unable to launch the capture session.");
        error.remediation = QStringLiteral("Retry after refreshing devices.");
        onSessionStarted(sessionId, CaptureStartResult::failure(std::move(error)));
        return false;
    }
    return true;
}

void CaptureScheduler::stop()
{
    if (!onControlThread("stop")) return;
    std::unique_ptr<CaptureSession> stopping;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_sessionId; // Invalidate already posted startup results, including failures.
        if (!m_session) {
            setStateLocked(CaptureSchedulerState::Idle, QStringLiteral("stop without session"));
            return;
        }
        setStateLocked(CaptureSchedulerState::Stopping, QStringLiteral("stop requested"));
        stopping = std::move(m_session);
    }
    // Unique ownership remains here until opener + cleanup have completed.
    // stop is intentionally synchronous for aboutToQuit; requestStop is not.
    stopping->requestStop();
    stopping->release();
    stopping.reset();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        setStateLocked(CaptureSchedulerState::Idle, QStringLiteral("session released"));
    }
}

void CaptureScheduler::pause()
{
    if (!onControlThread("pause")) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != CaptureSchedulerState::Running) {
        qDebug() << "[CaptureScheduler] pause ignored in state" << captureSchedulerStateName(m_state);
        return;
    }
    m_recorder.applyPauseState(true, av_gettime_relative());
    setStateLocked(CaptureSchedulerState::Paused, QStringLiteral("pause requested"));
}

void CaptureScheduler::resume()
{
    if (!onControlThread("resume")) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != CaptureSchedulerState::Paused) {
        qDebug() << "[CaptureScheduler] resume ignored in state" << captureSchedulerStateName(m_state);
        return;
    }
    m_recorder.applyPauseState(false, av_gettime_relative());
    setStateLocked(CaptureSchedulerState::Running, QStringLiteral("resume requested"));
}

bool CaptureScheduler::changeVideoSource(bool useScreen)
{
    if (!onControlThread("changeVideoSource")) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!configurationMutableLocked()) {
        qDebug() << "[CaptureScheduler] video source change ignored while capture is active";
        return false;
    }
    m_useScreen = useScreen;
    m_selectedVideoDeviceId.clear();
    qDebug() << "[CaptureScheduler] video source set to" << (useScreen ? "screen" : "camera");
    QMetaObject::invokeMethod(&m_recorder, "notifyDeviceConfigurationChanged", Qt::QueuedConnection);
    return true;
}

bool CaptureScheduler::changeAudioSource(bool useSystemAudio)
{
    if (!onControlThread("changeAudioSource")) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!configurationMutableLocked()) {
        qDebug() << "[CaptureScheduler] audio source change ignored while capture is active";
        return false;
    }
    m_useSystemAudio = useSystemAudio;
    m_selectedAudioDeviceId.clear();
    qDebug() << "[CaptureScheduler] audio source set to" << (useSystemAudio ? "system" : "microphone");
    QMetaObject::invokeMethod(&m_recorder, "notifyAudioSourceChanged", Qt::QueuedConnection);
    QMetaObject::invokeMethod(&m_recorder, "notifyDeviceConfigurationChanged", Qt::QueuedConnection);
    return true;
}

void CaptureScheduler::refreshDevices()
{
    if (!onControlThread("refreshDevices")) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!configurationMutableLocked()) return;
    }
    DirectShowCaptureBackend backend;
    const QList<CaptureDevice> devices = backend.enumerate();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_devices = devices;
        const auto selectFirst = [this](CaptureDeviceKind kind, QString &selectedId) {
            const auto existing = std::find_if(m_devices.cbegin(), m_devices.cend(),
                                               [&selectedId, kind](const CaptureDevice &device) {
                                                   return device.id == selectedId && device.kind == kind && device.available;
                                               });
            if (existing != m_devices.cend()) return;
            const auto fallback = std::find_if(m_devices.cbegin(), m_devices.cend(),
                                               [kind](const CaptureDevice &device) {
                                                   return device.kind == kind && device.available;
                                               });
            selectedId = fallback == m_devices.cend() ? QString() : fallback->id;
        };
        selectFirst(m_useScreen ? CaptureDeviceKind::Screen : CaptureDeviceKind::Camera,
                    m_selectedVideoDeviceId);
        selectFirst(m_useSystemAudio ? CaptureDeviceKind::SystemAudio : CaptureDeviceKind::Microphone,
                    m_selectedAudioDeviceId);
    }
    qInfo() << "[CaptureScheduler] device refresh completed count=" << devices.size();
    QMetaObject::invokeMethod(&m_recorder, "notifyDeviceConfigurationChanged", Qt::QueuedConnection);
}

bool CaptureScheduler::selectVideoDevice(const QString &deviceId)
{
    if (!onControlThread("selectVideoDevice")) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!configurationMutableLocked()) return false;
    const CaptureDeviceKind kind = m_useScreen ? CaptureDeviceKind::Screen : CaptureDeviceKind::Camera;
    const auto match = std::find_if(m_devices.cbegin(), m_devices.cend(), [&deviceId, kind](const CaptureDevice &device) {
        return device.id == deviceId && device.kind == kind && device.available;
    });
    if (match == m_devices.cend()) return false;
    m_selectedVideoDeviceId = deviceId;
    QMetaObject::invokeMethod(&m_recorder, "notifyDeviceConfigurationChanged", Qt::QueuedConnection);
    return true;
}

bool CaptureScheduler::selectAudioDevice(const QString &deviceId)
{
    if (!onControlThread("selectAudioDevice")) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!configurationMutableLocked()) return false;
    const CaptureDeviceKind kind = m_useSystemAudio ? CaptureDeviceKind::SystemAudio : CaptureDeviceKind::Microphone;
    const auto match = std::find_if(m_devices.cbegin(), m_devices.cend(), [&deviceId, kind](const CaptureDevice &device) {
        return device.id == deviceId && device.kind == kind && device.available;
    });
    if (match == m_devices.cend()) return false;
    m_selectedAudioDeviceId = deviceId;
    QMetaObject::invokeMethod(&m_recorder, "notifyDeviceConfigurationChanged", Qt::QueuedConnection);
    return true;
}

CaptureSchedulerState CaptureScheduler::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

QString CaptureScheduler::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

bool CaptureScheduler::useSystemAudio() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_useSystemAudio;
}

QVariantList CaptureScheduler::devices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantList result;
    for (const CaptureDevice &device : m_devices) result.append(device.toVariantMap());
    return result;
}

QString CaptureScheduler::selectedVideoDeviceId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedVideoDeviceId;
}

QString CaptureScheduler::selectedAudioDeviceId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedAudioDeviceId;
}

CaptureStartResult CaptureScheduler::buildCurrentConfig(CaptureConfig *config) const
{
    if (!config) {
        CaptureError error;
        error.stage = CaptureErrorStage::Configuration;
        error.userMessage = QStringLiteral("Internal capture configuration target is missing.");
        return CaptureStartResult::failure(std::move(error));
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        config->video = {CaptureBackend::DirectShow,
                         m_useScreen ? CaptureDeviceKind::Screen : CaptureDeviceKind::Camera,
                         m_selectedVideoDeviceId};
        config->audio = {CaptureBackend::DirectShow,
                         m_useSystemAudio ? CaptureDeviceKind::SystemAudio : CaptureDeviceKind::Microphone,
                         m_selectedAudioDeviceId};
    }
    const auto videoKind = config->video.kind;
    const auto audioKind = config->audio.kind;
    config->outputPath = m_recorder.outputPath();
    config->outputFileName = m_recorder.outputFileName();

    if (config->outputPath.trimmed().isEmpty() || config->outputFileName.trimmed().isEmpty()) {
        CaptureError error;
        error.stage = CaptureErrorStage::OutputValidation;
        error.userMessage = QStringLiteral("Choose an output folder and filename before recording.");
        error.remediation = QStringLiteral("Set a writable output path and filename in Settings.");
        return CaptureStartResult::failure(std::move(error));
    }
    QDir outputDirectory(config->outputPath);
    if (!outputDirectory.exists() && !QDir().mkpath(config->outputPath)) {
        CaptureError error;
        error.stage = CaptureErrorStage::OutputValidation;
        error.userMessage = QStringLiteral("The output folder cannot be created: %1").arg(config->outputPath);
        error.remediation = QStringLiteral("Choose a writable output folder.");
        return CaptureStartResult::failure(std::move(error));
    }

    DirectShowCaptureBackend backend;
    CaptureStartResult result = backend.makeInputRequest(videoKind, config->video.deviceId, &config->videoInput);
    if (!result.ok) return result;
    result = backend.makeInputRequest(audioKind, config->audio.deviceId, &config->audioInput);
    if (!result.ok) return result;
    return CaptureStartResult::success();
}

bool CaptureScheduler::configurationMutableLocked() const
{
    return m_state == CaptureSchedulerState::Idle || m_state == CaptureSchedulerState::Failed;
}

void CaptureScheduler::onSessionStarted(std::uint64_t sessionId, const CaptureStartResult &result)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sessionId != m_sessionId || m_state != CaptureSchedulerState::Preparing) {
        qDebug() << "[CaptureScheduler] ignored stale startup result session=" << sessionId;
        return;
    }
    if (result.ok) {
        setStateLocked(CaptureSchedulerState::Running, QStringLiteral("session started"));
        QMetaObject::invokeMethod(&m_recorder, "clearCaptureError", Qt::QueuedConnection);
        return;
    }
    CaptureError error = result.error.value_or(CaptureError{});
    failLocked(error.userMessage.isEmpty() ? QStringLiteral("Capture session failed.") : error.userMessage);
    QMetaObject::invokeMethod(&m_recorder, "setCaptureError", Qt::QueuedConnection,
                              Q_ARG(QString, m_lastError), Q_ARG(QString, error.remediation));
}

void CaptureScheduler::setStateLocked(CaptureSchedulerState newState, const QString &reason)
{
    if (m_state == newState) return;
    qDebug() << "[CaptureScheduler] state" << captureSchedulerStateName(m_state)
             << "->" << captureSchedulerStateName(newState) << reason;
    m_state = newState;
    if (newState != CaptureSchedulerState::Failed) m_lastError.clear();
    QMetaObject::invokeMethod(&m_recorder, "notifyCaptureStateChanged", Qt::QueuedConnection);
}

void CaptureScheduler::failLocked(const QString &message)
{
    m_lastError = message;
    qWarning() << "[CaptureScheduler] failed:" << message;
    setStateLocked(CaptureSchedulerState::Failed, message);
}
