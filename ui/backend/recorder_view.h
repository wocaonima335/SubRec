#ifndef SUBREC_UI_RECORDER_VIEW_H
#define SUBREC_UI_RECORDER_VIEW_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module B1. GUI thread. QML singleton `RecorderView`: the ONLY way new-UI QML reads the
// recording engine. Replaces the old `recorder` context property for SubRec.Ui files.
//
// attach() takes FFRecorder as a plain QObject*: forwarding is by Qt property / method name
// (names below are identical to recorder/ffrecorder.h), so this header and its unit tests
// need no FFmpeg or recorder headers. Tests attach any QObject exposing the same properties
// (see tests/shell/fakes/observable_recorder.h for the pattern). Unattached = all defaults
// below, uiState "noDevice".
//
// START INVARIANT: no start/stop/pause/retry here. Recording control goes through
// ShellView -> WindowController::request* (shell/window_controller.h). Do not forward
// FFRecorder::retryCapture().
class RecorderView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ---- forwarded 1:1 from FFRecorder (same name, same meaning) ----
    // "Idle" | "Preparing" | "Running" | "Paused" | "Stopping" | "Failed". Default "Idle".
    Q_PROPERTY(QString captureState READ captureState NOTIFY captureStateChanged)
    Q_PROPERTY(bool canStartCapture READ canStartCapture NOTIFY captureStateChanged)
    Q_PROPERTY(QString captureError READ captureError NOTIFY captureErrorChanged)
    Q_PROPERTY(QString captureRemediation READ captureRemediation NOTIFY captureErrorChanged)
    Q_PROPERTY(QString captureTimeText READ captureTimeText NOTIFY captureTimeTextChanged)
    // Read-only here: P3-2 folder edits go through OutputNaming.outputDir, which writes
    // FFRecorder::outputPath itself.
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    // Written by OutputNaming right before each start (R1); read-only for QML.
    Q_PROPERTY(QString outputFileName READ outputFileName NOTIFY outputFileNameChanged)
    Q_PROPERTY(QString videoResolution READ videoResolution NOTIFY videoResolutionChanged)
    Q_PROPERTY(QString selectedVideoEncoder READ selectedVideoEncoder NOTIFY selectedVideoEncoderChanged)
    Q_PROPERTY(bool selectedVideoEncoderIsHardware READ selectedVideoEncoderIsHardware NOTIFY selectedVideoEncoderChanged)
    Q_PROPERTY(QString selectedVideoEncoderDetail READ selectedVideoEncoderDetail NOTIFY selectedVideoEncoderChanged)
    // "none" | "system" | "microphone" | "both".
    Q_PROPERTY(QString audioMode READ audioMode NOTIFY audioModeChanged)
    Q_PROPERTY(bool audioMixingAvailable READ audioMixingAvailable NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedMicrophoneDeviceId READ selectedMicrophoneDeviceId NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString mixMicrophoneDeviceId READ mixMicrophoneDeviceId NOTIFY audioModeChanged)
    // List of maps {id, displayName, description, backend, kind, available, unavailableReason};
    // kind: "screen" | "camera" | "system-audio" | "microphone" (capture/capture_types.cpp).
    Q_PROPERTY(QVariantList captureDevices READ captureDevices NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedVideoDeviceId READ selectedVideoDeviceId NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedAudioDeviceId READ selectedAudioDeviceId NOTIFY captureDevicesChanged)
    // "auto" | "ddagrab" | "dshow"; FromEnvironment = SUBREC_CAPTURE_BACKEND overrides (P3-6).
    Q_PROPERTY(QString screenCaptureBackend READ screenCaptureBackend NOTIFY captureDevicesChanged)
    Q_PROPERTY(bool screenCaptureBackendFromEnvironment READ screenCaptureBackendFromEnvironment NOTIFY captureDevicesChanged)
    // FFRecorder::audioSampler (FFAudioSampler*), for AudioLevelMonitor::attach only.
    Q_PROPERTY(QObject *audioSampler READ audioSampler NOTIFY audioSamplerChanged)

    // ---- derived for the new UI ----
    // UI_REDESIGN §3.2.3 S0–S6: "noDevice" (Idle and no available screen/camera) | "idle" |
    // "preparing" | "running" | "paused" | "stopping" | "failed". Single source of truth
    // for every recording control (§1.3 rule 1); never cache it in QML.
    Q_PROPERTY(QString uiState READ uiState NOTIFY uiStateChanged)
    // Preparing/Running/Paused/Stopping: configuration controls show the locked state (§3.2.5).
    Q_PROPERTY(bool capturing READ capturing NOTIFY uiStateChanged)
    Q_PROPERTY(bool hasVideoDevice READ hasVideoDevice NOTIFY captureDevicesChanged)
    // Kind of selectedVideoDeviceId: "screen" | "camera" | "" (fixes Q10).
    Q_PROPERTY(QString selectedVideoKind READ selectedVideoKind NOTIFY captureDevicesChanged)
    // captureTimeText normalised to "HH:MM:SS" for the timer, tray and floating bar.
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY captureTimeTextChanged)
    // Absolute path being written in this session (outputPath + outputFileName captured at
    // Preparing); "" when not capturing. Library L5 and P1 save area use it.
    Q_PROPERTY(QString currentOutputFile READ currentOutputFile NOTIFY currentOutputFileChanged)

public:
    explicit RecorderView(QObject *parent); // no default: see singleton_provider.h
    ~RecorderView() override;

    static void setInstance(RecorderView *instance);
    static RecorderView *create(QQmlEngine *engine, QJSEngine *);

    // GUI thread, before QML loads. Borrowed; may be called again with nullptr on shutdown.
    // changeVideoSource wraps captureScheduler().changeVideoSource(useScreen) (main.cpp
    // openSourceByControl); the scheduler is not reachable through QObject properties.
    void attach(QObject *recorder, std::function<void(bool useScreen)> changeVideoSource);

    QString captureState() const;
    bool canStartCapture() const;
    QString captureError() const;
    QString captureRemediation() const;
    QString captureTimeText() const;
    QString outputPath() const;
    QString outputFileName() const;
    QString videoResolution() const;
    QString selectedVideoEncoder() const;
    bool selectedVideoEncoderIsHardware() const;
    QString selectedVideoEncoderDetail() const;
    QString audioMode() const;
    bool audioMixingAvailable() const;
    QString selectedMicrophoneDeviceId() const;
    QString mixMicrophoneDeviceId() const;
    QVariantList captureDevices() const;
    QString selectedVideoDeviceId() const;
    QString selectedAudioDeviceId() const;
    QString screenCaptureBackend() const;
    bool screenCaptureBackendFromEnvironment() const;
    QObject *audioSampler() const;
    QString uiState() const;
    bool capturing() const;
    bool hasVideoDevice() const;
    QString selectedVideoKind() const;
    QString elapsedText() const;
    QString currentOutputFile() const;

    // Devices of one kind, in captureDevices order, unavailable ones included (UI shows them
    // disabled with unavailableReason).
    Q_INVOKABLE QVariantList devicesOfKind(const QString &kind) const;
    // Forwarded to FFRecorder; same return values. Callers must not invoke them while
    // `capturing` (the UI locks those controls); the engine rejects them anyway.
    Q_INVOKABLE void refreshCaptureDevices();
    Q_INVOKABLE bool selectVideoCaptureDevice(const QString &deviceId);
    Q_INVOKABLE bool selectAudioCaptureDevice(const QString &deviceId);
    Q_INVOKABLE bool selectMicrophoneCaptureDevice(const QString &deviceId);
    Q_INVOKABLE bool setAudioMode(const QString &mode);
    Q_INVOKABLE bool setScreenCaptureBackend(const QString &name);
    // Source segmented control (P1 D panel), kind "screen" | "camera". Same behaviour as the
    // old openScreen/openCamera root signals: calls the injected changeVideoSource.
    Q_INVOKABLE void openSource(const QString &kind);
    // "Dismiss" on a non-fatal error alert: FFRecorder::clearCaptureError().
    Q_INVOKABLE void dismissCaptureError();

signals:
    void captureStateChanged();
    void captureErrorChanged();
    void captureTimeTextChanged();
    void outputPathChanged();
    void outputFileNameChanged();
    void videoResolutionChanged();
    void selectedVideoEncoderChanged();
    void audioModeChanged();
    void captureDevicesChanged();
    void audioSamplerChanged();
    void uiStateChanged();
    void currentOutputFileChanged();
    // R8. Stopping -> Idle with no error: T1 / N2 / afterRecordAction.
    void recordingSaved(const QString &filePath, const QString &durationText);
    // R8. Transition into Failed. partialFilePath is the session file if it exists on disk
    // (S6 extra line "已录制的部分可能已保存到 …"), else "".
    void recordingFailed(const QString &error, const QString &partialFilePath);

private slots:
    void onCaptureStateChanged();
    void onCaptureErrorChanged();
    void onCaptureTimeChanged();
    void onOutputPathChanged();
    void onOutputFileNameChanged();
    void onCaptureDevicesChanged();
    void onVideoResolutionChanged();
    void onEncoderChanged();
    void onAudioModeChanged();
    void onAudioSamplerChanged();
private:
    QPointer<QObject> recorder_;
    std::function<void(bool)> changeVideoSource_;
    QString previousState_;
    QString previousOutputFile_;
    QString previousTime_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_RECORDER_VIEW_H
