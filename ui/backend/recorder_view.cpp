#include "ui/backend/recorder_view.h"

#include "ui/backend/singleton_provider.h"

namespace subrec::ui {

namespace {
RecorderView *s_instance = nullptr;
}

RecorderView::RecorderView(QObject *parent) : QObject(parent) {}
RecorderView::~RecorderView()
{
    if (s_instance == this) s_instance = nullptr;
}

void RecorderView::setInstance(RecorderView *instance) { s_instance = instance; }
RecorderView *RecorderView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B1): connect the recorder's NOTIFY signals to ours, forward reads/writes by name,
// derive uiState/elapsedText/currentOutputFile, emit recordingSaved/recordingFailed.
void RecorderView::attach(QObject *recorder, std::function<void(bool)> changeVideoSource)
{
    recorder_ = recorder;
    changeVideoSource_ = std::move(changeVideoSource);
}

QString RecorderView::captureState() const { return QStringLiteral("Idle"); }
bool RecorderView::canStartCapture() const { return false; }
QString RecorderView::captureError() const { return {}; }
QString RecorderView::captureRemediation() const { return {}; }
QString RecorderView::captureTimeText() const { return QStringLiteral("00:00.0"); }
QString RecorderView::outputPath() const { return {}; }
QString RecorderView::outputFileName() const { return {}; }
QString RecorderView::videoResolution() const { return {}; }
QString RecorderView::selectedVideoEncoder() const { return {}; }
bool RecorderView::selectedVideoEncoderIsHardware() const { return false; }
QString RecorderView::selectedVideoEncoderDetail() const { return {}; }
QString RecorderView::audioMode() const { return QStringLiteral("none"); }
bool RecorderView::audioMixingAvailable() const { return false; }
QString RecorderView::selectedMicrophoneDeviceId() const { return {}; }
QString RecorderView::mixMicrophoneDeviceId() const { return {}; }
QVariantList RecorderView::captureDevices() const { return {}; }
QString RecorderView::selectedVideoDeviceId() const { return {}; }
QString RecorderView::selectedAudioDeviceId() const { return {}; }
QString RecorderView::screenCaptureBackend() const { return QStringLiteral("auto"); }
bool RecorderView::screenCaptureBackendFromEnvironment() const { return false; }
QObject *RecorderView::audioSampler() const { return nullptr; }
QString RecorderView::uiState() const { return QStringLiteral("noDevice"); }
bool RecorderView::capturing() const { return false; }
bool RecorderView::hasVideoDevice() const { return false; }
QString RecorderView::selectedVideoKind() const { return {}; }
QString RecorderView::elapsedText() const { return QStringLiteral("00:00:00"); }
QString RecorderView::currentOutputFile() const { return {}; }

QVariantList RecorderView::devicesOfKind(const QString &) const { return {}; }
void RecorderView::refreshCaptureDevices() {}
bool RecorderView::selectVideoCaptureDevice(const QString &) { return false; }
bool RecorderView::selectAudioCaptureDevice(const QString &) { return false; }
bool RecorderView::selectMicrophoneCaptureDevice(const QString &) { return false; }
bool RecorderView::setAudioMode(const QString &) { return false; }
bool RecorderView::setScreenCaptureBackend(const QString &) { return false; }
void RecorderView::openSource(const QString &) {}
void RecorderView::dismissCaptureError() {}

} // namespace subrec::ui
