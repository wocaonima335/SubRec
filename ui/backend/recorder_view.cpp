#include "ui/backend/recorder_view.h"

#include "ui/backend/singleton_provider.h"

#include <QDir>
#include <QVariant>
#include <QMetaProperty>
#include <QFileInfo>

namespace subrec::ui {

namespace {
RecorderView *s_instance = nullptr;

QVariant objectProperty(const QPointer<QObject> &object, const char *name,
                  const QVariant &fallback = {})
{
    return object ? object->property(name) : fallback;
}

QString stringProperty(const QPointer<QObject> &object, const char *name,
                       const QString &fallback = {})
{
    const auto value = objectProperty(object, name);
    return value.isValid() ? value.toString() : fallback;
}

bool boolProperty(const QPointer<QObject> &object, const char *name, bool fallback = false)
{
    const auto value = objectProperty(object, name);
    return value.isValid() ? value.toBool() : fallback;
}
} // namespace

RecorderView::RecorderView(QObject *parent)
    : QObject(parent)
{
}

RecorderView::~RecorderView()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void RecorderView::setInstance(RecorderView *instance)
{
    s_instance = instance;
}

RecorderView *RecorderView::create(QQmlEngine *engine, QJSEngine *)
{
    return detail::provideSingleton(s_instance, engine);
}

// TODO(B1): connect the recorder's NOTIFY signals to ours, forward reads/writes by name,
// derive uiState/elapsedText/currentOutputFile, emit recordingSaved/recordingFailed.
void RecorderView::attach(QObject *recorder, std::function<void(bool)> changeVideoSource)
{
    if (recorder_)
        disconnect(recorder_, nullptr, this, nullptr);
    recorder_ = recorder;
    changeVideoSource_ = std::move(changeVideoSource);
    previousState_ = captureState();
    previousOutputFile_ = capturing() ? QDir(outputPath()).absoluteFilePath(outputFileName()) : QString();
    previousTime_ = elapsedText();
    if (recorder_) {
        const auto connectProperty = [this](const char *name, const char *slot) {
            const auto index = recorder_->metaObject()->indexOfProperty(name);
            if (index < 0)
                return;
            const auto property = recorder_->metaObject()->property(index);
            if (property.hasNotifySignal())
                connect(recorder_, property.notifySignal(), this,
                        metaObject()->method(metaObject()->indexOfSlot(slot)), Qt::UniqueConnection);
        };
        connectProperty("captureState", "onCaptureStateChanged()");
        connectProperty("canStartCapture", "onCaptureStateChanged()");
        connectProperty("captureError", "onCaptureErrorChanged()");
        connectProperty("captureRemediation", "onCaptureErrorChanged()");
        connectProperty("captureTimeText", "onCaptureTimeChanged()");
        connectProperty("outputPath", "onOutputPathChanged()");
        connectProperty("outputFileName", "onOutputFileNameChanged()");
        connectProperty("videoResolution", "onVideoResolutionChanged()");
        for (const auto *name : {"selectedVideoEncoder", "selectedVideoEncoderIsHardware", "selectedVideoEncoderDetail"})
            connectProperty(name, "onEncoderChanged()");
        for (const auto *name : {"captureDevices", "selectedVideoDeviceId", "selectedAudioDeviceId",
                                 "selectedMicrophoneDeviceId", "audioMixingAvailable", "screenCaptureBackend",
                                 "screenCaptureBackendFromEnvironment"})
            connectProperty(name, "onCaptureDevicesChanged()");
        connectProperty("audioMode", "onAudioModeChanged()");
        connectProperty("mixMicrophoneDeviceId", "onAudioModeChanged()");
        connectProperty("audioSampler", "onAudioSamplerChanged()");
        connect(recorder_, &QObject::destroyed, this, [this] { attach(nullptr, {}); });
    }
    emit captureErrorChanged();
    emit captureTimeTextChanged();
    emit outputPathChanged();
    emit outputFileNameChanged();
    emit videoResolutionChanged();
    emit selectedVideoEncoderChanged();
    emit audioModeChanged();
    emit audioSamplerChanged();
    emit currentOutputFileChanged();
    emit captureStateChanged();
    emit captureDevicesChanged();
    emit uiStateChanged();
}

QString RecorderView::captureState() const
{
    return stringProperty(recorder_, "captureState", "Idle");
}
bool RecorderView::canStartCapture() const
{
    return boolProperty(recorder_, "canStartCapture");
}
QString RecorderView::captureError() const
{
    return stringProperty(recorder_, "captureError");
}
QString RecorderView::captureRemediation() const
{
    return stringProperty(recorder_, "captureRemediation");
}
QString RecorderView::captureTimeText() const
{
    return stringProperty(recorder_, "captureTimeText", "00:00.0");
}
QString RecorderView::outputPath() const
{
    return stringProperty(recorder_, "outputPath");
}
QString RecorderView::outputFileName() const
{
    return stringProperty(recorder_, "outputFileName");
}
QString RecorderView::videoResolution() const
{
    return stringProperty(recorder_, "videoResolution");
}
QString RecorderView::selectedVideoEncoder() const
{
    return stringProperty(recorder_, "selectedVideoEncoder");
}
bool RecorderView::selectedVideoEncoderIsHardware() const
{
    return boolProperty(recorder_, "selectedVideoEncoderIsHardware");
}
QString RecorderView::selectedVideoEncoderDetail() const
{
    return stringProperty(recorder_, "selectedVideoEncoderDetail");
}
QString RecorderView::audioMode() const
{
    return stringProperty(recorder_, "audioMode", "none");
}
bool RecorderView::audioMixingAvailable() const
{
    return boolProperty(recorder_, "audioMixingAvailable");
}
QString RecorderView::selectedMicrophoneDeviceId() const
{
    return stringProperty(recorder_, "selectedMicrophoneDeviceId");
}
QString RecorderView::mixMicrophoneDeviceId() const
{
    return stringProperty(recorder_, "mixMicrophoneDeviceId");
}
QVariantList RecorderView::captureDevices() const
{
    return objectProperty(recorder_, "captureDevices").toList();
}
QString RecorderView::selectedVideoDeviceId() const
{
    return stringProperty(recorder_, "selectedVideoDeviceId");
}
QString RecorderView::selectedAudioDeviceId() const
{
    return stringProperty(recorder_, "selectedAudioDeviceId");
}
QString RecorderView::screenCaptureBackend() const
{
    return stringProperty(recorder_, "screenCaptureBackend", "auto");
}
bool RecorderView::screenCaptureBackendFromEnvironment() const
{
    return boolProperty(recorder_, "screenCaptureBackendFromEnvironment");
}
QObject *RecorderView::audioSampler() const
{
    return qobject_cast<QObject *>(objectProperty(recorder_, "audioSampler").value<QObject *>());
}

QString RecorderView::uiState() const
{
    const auto state = captureState();
    if (state == QStringLiteral("Idle") && !hasVideoDevice())
        return QStringLiteral("noDevice");
    if (state == QStringLiteral("Idle"))
        return QStringLiteral("idle");
    if (state == QStringLiteral("Preparing"))
        return QStringLiteral("preparing");
    if (state == QStringLiteral("Running"))
        return QStringLiteral("running");
    if (state == QStringLiteral("Paused"))
        return QStringLiteral("paused");
    if (state == QStringLiteral("Stopping"))
        return QStringLiteral("stopping");
    return QStringLiteral("failed");
}

bool RecorderView::capturing() const
{
    const auto state = uiState();
    return state == QStringLiteral("preparing") || state == QStringLiteral("running")
        || state == QStringLiteral("paused") || state == QStringLiteral("stopping");
}

bool RecorderView::hasVideoDevice() const
{
    for (const auto &value : captureDevices()) {
        const auto map = value.toMap();
        const auto kind = map.value(QStringLiteral("kind")).toString();
        if ((kind == QStringLiteral("screen") || kind == QStringLiteral("camera"))
            && map.value(QStringLiteral("available"), true).toBool())
            return true;
    }
    return false;
}

QString RecorderView::selectedVideoKind() const
{
    const auto selected = selectedVideoDeviceId();
    for (const auto &value : captureDevices()) {
        const auto map = value.toMap();
        if (map.value(QStringLiteral("id")).toString() == selected)
            return map.value(QStringLiteral("kind")).toString();
    }
    return {};
}

QString RecorderView::elapsedText() const
{
    auto time = captureTimeText();
    if (time.count(':') == 1)
        time = QStringLiteral("00:") + time;
    if (time.contains('.'))
        time = time.left(time.indexOf('.'));
    return time.count(':') == 2 ? time : QStringLiteral("00:00:00");
}

QString RecorderView::currentOutputFile() const
{
    return capturing() ? previousOutputFile_ : QString();
}

QVariantList RecorderView::devicesOfKind(const QString &kind) const
{
    QVariantList result;
    for (const auto &value : captureDevices()) {
        if (value.toMap().value(QStringLiteral("kind")).toString() == kind)
            result << value;
    }
    return result;
}

void RecorderView::refreshCaptureDevices()
{
    if (recorder_)
        QMetaObject::invokeMethod(recorder_, "refreshCaptureDevices");
}

bool RecorderView::selectVideoCaptureDevice(const QString &id)
{
    bool result = false;
    if (recorder_ && !capturing())
        QMetaObject::invokeMethod(recorder_, "selectVideoCaptureDevice",
                                  Q_RETURN_ARG(bool, result), Q_ARG(QString, id));
    return result;
}

bool RecorderView::selectAudioCaptureDevice(const QString &id)
{
    bool result = false;
    if (recorder_ && !capturing())
        QMetaObject::invokeMethod(recorder_, "selectAudioCaptureDevice",
                                  Q_RETURN_ARG(bool, result), Q_ARG(QString, id));
    return result;
}

bool RecorderView::selectMicrophoneCaptureDevice(const QString &id)
{
    bool result = false;
    if (recorder_ && !capturing())
        QMetaObject::invokeMethod(recorder_, "selectMicrophoneCaptureDevice",
                                  Q_RETURN_ARG(bool, result), Q_ARG(QString, id));
    return result;
}

bool RecorderView::setAudioMode(const QString &mode)
{
    bool result = false;
    if (recorder_ && !capturing())
        QMetaObject::invokeMethod(recorder_, "setAudioMode",
                                  Q_RETURN_ARG(bool, result), Q_ARG(QString, mode));
    return result;
}

bool RecorderView::setScreenCaptureBackend(const QString &backend)
{
    bool result = false;
    if (recorder_ && !capturing())
        QMetaObject::invokeMethod(recorder_, "setScreenCaptureBackend",
                                  Q_RETURN_ARG(bool, result), Q_ARG(QString, backend));
    return result;
}

void RecorderView::openSource(const QString &kind)
{
    if (changeVideoSource_ && !capturing() && (kind == "screen" || kind == "camera"))
        changeVideoSource_(kind == QStringLiteral("screen"));
}

void RecorderView::dismissCaptureError()
{
    if (recorder_)
        QMetaObject::invokeMethod(recorder_, "clearCaptureError");
}

void RecorderView::onCaptureStateChanged()
{
    const auto now = captureState();
    const auto old = previousState_;
    const auto oldFile = previousOutputFile_;
    const auto duration = (now == "Idle" || now == "Failed") ? previousTime_ : elapsedText();
    // Publish the edge before emitting signals: receivers may synchronously reenter.
    previousState_ = now;
    if (now == "Preparing" && old != now)
        previousOutputFile_ = QDir(outputPath()).absoluteFilePath(outputFileName());
    else if (!capturing())
        previousOutputFile_.clear();
    previousTime_ = duration;
    emit captureStateChanged();
    emit uiStateChanged();
    emit currentOutputFileChanged();
    if (old == "Stopping" && now == "Idle" && captureError().isEmpty())
        emit recordingSaved(oldFile, duration);
    if (now == "Failed" && old != now)
        emit recordingFailed(captureError(), QFileInfo::exists(oldFile) ? oldFile : QString());
}

void RecorderView::onCaptureErrorChanged()
{
    emit captureErrorChanged();
    emit uiStateChanged();
}
void RecorderView::onCaptureTimeChanged()
{
    if (capturing())
        previousTime_ = elapsedText();
    emit captureTimeTextChanged();
}
void RecorderView::onOutputPathChanged()
{
    emit outputPathChanged();
    emit currentOutputFileChanged();
}
void RecorderView::onOutputFileNameChanged()
{
    emit outputFileNameChanged();
    emit currentOutputFileChanged();
}
void RecorderView::onCaptureDevicesChanged()
{
    emit captureDevicesChanged();
    emit uiStateChanged();
}
void RecorderView::onVideoResolutionChanged()
{
    emit videoResolutionChanged();
}
void RecorderView::onEncoderChanged()
{
    emit selectedVideoEncoderChanged();
}
void RecorderView::onAudioModeChanged()
{
    emit audioModeChanged();
}
void RecorderView::onAudioSamplerChanged()
{
    emit audioSamplerChanged();
}

} // namespace subrec::ui
