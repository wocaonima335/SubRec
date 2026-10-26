#include "dshow_capture_backend.h"

#include <QDebug>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace {
CaptureError makeError(CaptureErrorStage stage,
                       CaptureDeviceKind kind,
                       const QString &deviceId,
                       const QString &deviceName,
                       const QString &message,
                       const QString &remediation,
                       int ffmpegError = 0)
{
    CaptureError error;
    error.stage = stage;
    error.role = kind;
    error.backend = CaptureBackend::DirectShow;
    error.deviceId = deviceId;
    error.deviceName = deviceName;
    error.ffmpegError = ffmpegError;
    error.ffmpegMessage = message;
    error.userMessage = message;
    error.remediation = remediation;
    error.retryable = true;
    return error;
}

QString ffmpegErrorText(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("unknown FFmpeg error");
    }
    return QString::fromLocal8Bit(buffer);
}
}

CaptureBackend DirectShowCaptureBackend::backend() const
{
    return CaptureBackend::DirectShow;
}

QList<CaptureDevice> DirectShowCaptureBackend::enumerate() const
{
    return enumerateDevices();
}

QList<CaptureDevice> DirectShowCaptureBackend::enumerateDevices() const
{
    QList<CaptureDevice> result;
    const AVInputFormat *format = av_find_input_format("dshow");
    if (!format) {
        qWarning() << "[CaptureDevice] backend=dshow result=unavailable reason=input-format-not-found";
        return result;
    }

    AVDeviceInfoList *list = nullptr;
    const int count = avdevice_list_input_sources(format, nullptr, nullptr, &list);
    if (count < 0) {
        qWarning().noquote() << QStringLiteral("[CaptureDevice] backend=dshow result=enumeration-failed fferr=%1 error=%2")
                                         .arg(count)
                                         .arg(ffmpegErrorText(count));
        avdevice_free_list_devices(&list);
        return result;
    }

    if (!list || list->nb_devices == 0) {
        qWarning() << "[CaptureDevice] backend=dshow result=empty";
        avdevice_free_list_devices(&list);
        return result;
    }

    for (int index = 0; index < list->nb_devices; ++index) {
        const AVDeviceInfo *info = list->devices[index];
        if (!info || !info->device_name) {
            continue;
        }

        QList<int> mediaTypes;
        for (int mediaIndex = 0; mediaIndex < info->nb_media_types; ++mediaIndex) {
            mediaTypes.append(static_cast<int>(info->media_types[mediaIndex]));
        }

        CaptureDevice device;
        device.id = QString::fromUtf8(info->device_name);
        device.displayName = info->device_description
            ? QString::fromUtf8(info->device_description)
            : device.id;
        device.description = device.displayName;
        device.backend = CaptureBackend::DirectShow;
        device.kind = kindForDevice(device.id, mediaTypes);
        device.available = true;
        result.append(device);
        qInfo().noquote() << QStringLiteral("[CaptureDevice] backend=dshow result=found id=%1 kind=%2 name=%3")
                                  .arg(device.id,
                                       captureDeviceKindName(device.kind),
                                       device.displayName);
    }

    avdevice_free_list_devices(&list);
    return result;
}

CaptureStartResult DirectShowCaptureBackend::makeInputRequest(CaptureDeviceKind kind,
                                                               const QString &deviceId,
                                                               InputRequest *request) const
{
    if (!request) {
        return CaptureStartResult::failure(makeError(
            CaptureErrorStage::Configuration, kind, deviceId, {},
            QStringLiteral("Internal capture request target is missing."),
            QStringLiteral("Retry the recording request.")));
    }

    if (deviceId.trimmed().isEmpty()) {
        return CaptureStartResult::failure(makeError(
            CaptureErrorStage::DeviceValidation, kind, deviceId, {},
            QStringLiteral("No %1 device is selected.").arg(captureDeviceKindName(kind)),
            QStringLiteral("Refresh devices and select an available input.")));
    }

    const QList<CaptureDevice> devices = enumerateDevices();
    const auto it = std::find_if(devices.cbegin(), devices.cend(),
                                 [&deviceId](const CaptureDevice &device) {
                                     return device.id == deviceId;
                                 });
    if (it == devices.cend()) {
        return CaptureStartResult::failure(makeError(
            CaptureErrorStage::DeviceValidation, kind, deviceId, deviceId,
            QStringLiteral("The selected DirectShow device is unavailable: %1").arg(deviceId),
            QStringLiteral("Refresh devices, then choose an available input.")));
    }

    request->backend = CaptureBackend::DirectShow;
    request->kind = kind;
    request->deviceId = it->id;
    request->displayName = it->displayName;
    request->url = makeUrl(kind, it->id);
    request->format = QStringLiteral("dshow");
    return CaptureStartResult::success();
}

CaptureStartResult DirectShowCaptureBackend::validate(const InputRequest &request) const
{
    if (request.backend != CaptureBackend::DirectShow) {
        return CaptureStartResult::failure(makeError(
            CaptureErrorStage::Configuration, request.kind, request.deviceId,
            request.displayName, QStringLiteral("Unsupported capture backend."),
            QStringLiteral("Select a DirectShow input.")));
    }

    if (!av_find_input_format("dshow")) {
        return CaptureStartResult::failure(makeError(
            CaptureErrorStage::InputFormat, request.kind, request.deviceId,
            request.displayName, QStringLiteral("FFmpeg does not provide the DirectShow input backend."),
            QStringLiteral("Deploy an FFmpeg build with DirectShow support.")));
    }

    InputRequest resolved;
    return makeInputRequest(request.kind, request.deviceId, &resolved);
}

CaptureDeviceKind DirectShowCaptureBackend::kindForDevice(const QString &name,
                                                           const QList<int> &mediaTypes)
{
    const bool hasAudio = mediaTypes.contains(AVMEDIA_TYPE_AUDIO);
    const bool hasVideo = mediaTypes.contains(AVMEDIA_TYPE_VIDEO);
    const QString lowerName = name.toLower();
    if (hasAudio) {
        if (lowerName.contains(QStringLiteral("virtual-audio-capturer"))) {
            return CaptureDeviceKind::SystemAudio;
        }
        return CaptureDeviceKind::Microphone;
    }
    if (hasVideo) {
        if (lowerName.contains(QStringLiteral("screen-capture-recorder"))) {
            return CaptureDeviceKind::Screen;
        }
        return CaptureDeviceKind::Camera;
    }
    return CaptureDeviceKind::Camera;
}

QString DirectShowCaptureBackend::makeUrl(CaptureDeviceKind kind, const QString &deviceId)
{
    const QString prefix = (kind == CaptureDeviceKind::SystemAudio
                            || kind == CaptureDeviceKind::Microphone)
        ? QStringLiteral("audio=")
        : QStringLiteral("video=");
    return prefix + deviceId;
}
