#ifndef CAPTURE_SCHEDULER_H
#define CAPTURE_SCHEDULER_H

#include "capture/capture_types.h"
#include "capture_session.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

class FFRecorder;

enum class CaptureSchedulerState
{
    Idle,
    Preparing,
    Running,
    Paused,
    Stopping,
    Failed
};

QString captureSchedulerStateName(CaptureSchedulerState state);

// Control operations run on the creating (Recorder/UI) thread. Worker results
// are queued to this QObject, so destruction also discards pending callbacks.
class CaptureScheduler : public QObject
{
public:
    explicit CaptureScheduler(FFRecorder &recorder);
    ~CaptureScheduler();

    void initialize();
    bool start();
    void stop();
    void pause();
    void resume();
    bool changeVideoSource(bool useScreen);
    bool changeAudioSource(bool useSystemAudio);
    bool setAudioMode(const QString &mode);
    bool setMixMicrophone(const QString &deviceId);
    bool clearMix();
    void refreshDevices();
    bool selectVideoDevice(const QString &deviceId);
    bool selectAudioDevice(const QString &deviceId);
    // Select a DirectShow microphone in addition to the WASAPI system source.
    bool selectMixMicrophone(const QString &deviceId);
    // Screen backend: SUBREC_CAPTURE_BACKEND (auto|ddagrab|dshow) wins over the saved setting
    // (QSettings "capture/screenBackend"); setScreenBackendPreference() saves the setting.
    bool setScreenBackendPreference(ScreenBackendPreference preference);
    ScreenBackendPreference screenBackendPreference() const;
    bool screenBackendFromEnvironment() const;

    CaptureSchedulerState state() const;
    QString lastError() const;
    bool useSystemAudio() const;
    QString audioMode() const;
    bool mixingEnabled() const;
    bool audioMixingAvailable() const;
    QString mixMicrophoneId() const;
    QString selectedMicrophoneId() const;
    bool selectMicrophoneDevice(const QString &deviceId);
    QVariantList devices() const;
    QString selectedVideoDeviceId() const;
    QString selectedAudioDeviceId() const;

private:
    CaptureStartResult buildCurrentConfig(CaptureConfig *config) const;
    CaptureStartResult buildAudioConfig(CaptureConfig *config) const;
    bool configurationMutableLocked() const;
    bool audioMixingAvailableLocked() const;
    QString microphoneIdLocked(const QString &requested = {}) const;
    void notifyAudioConfigurationLocked();
    void selectDefaultDevicesLocked();
    CaptureBackend effectiveScreenBackendLocked() const;
    void onSessionStarted(std::uint64_t sessionId, const CaptureStartResult &result);
    bool onControlThread(const char *operation) const;
    friend class CaptureLifecycleTest;

    // Lifecycle-harness seams. Both stay null in production; the regression
    // harness swaps the config resolution and the FFmpeg opener so the
    // scheduler/session control state machine can be tested without media.
    std::function<CaptureStartResult(CaptureConfig *)> m_testConfigHook;
    std::function<void(CaptureSession &,
                       std::function<void(const CaptureStartResult &)>)>
        m_testOpenerHook;
    void setStateLocked(CaptureSchedulerState newState, const QString &reason);
    void failLocked(const QString &message);

private:
    FFRecorder &m_recorder;
    mutable std::mutex m_mutex;
    std::unique_ptr<CaptureSession> m_session;
    std::uint64_t m_sessionId = 0;
    CaptureSchedulerState m_state = CaptureSchedulerState::Idle;
    QString m_lastError;
    QList<CaptureDevice> m_devices;
    QString m_selectedVideoDeviceId;
    QString m_selectedAudioDeviceId;
    QString m_selectedSystemAudioId;
    QString m_selectedMicrophoneId;
    QString m_mixMicrophoneId;
    bool m_audioEnabled = true;
    bool m_useScreen = true;
    bool m_useSystemAudio = true;
    ScreenBackendPreference m_screenBackendPreference = ScreenBackendPreference::Auto;
    bool m_screenBackendFromEnv = false;
    SystemAudioBackendPreference m_systemAudioBackendPreference = SystemAudioBackendPreference::Auto;
};

#endif // CAPTURE_SCHEDULER_H
