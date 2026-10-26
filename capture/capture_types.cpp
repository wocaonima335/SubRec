#include "capture_types.h"

QString captureBackendName(CaptureBackend backend)
{
    switch (backend) {
    case CaptureBackend::DirectShow:
        return QStringLiteral("DirectShow");
    }
    return QStringLiteral("Unknown");
}

QString captureDeviceKindName(CaptureDeviceKind kind)
{
    switch (kind) {
    case CaptureDeviceKind::Screen:
        return QStringLiteral("screen");
    case CaptureDeviceKind::Camera:
        return QStringLiteral("camera");
    case CaptureDeviceKind::SystemAudio:
        return QStringLiteral("system-audio");
    case CaptureDeviceKind::Microphone:
        return QStringLiteral("microphone");
    }
    return QStringLiteral("unknown");
}

QString captureErrorStageName(CaptureErrorStage stage)
{
    switch (stage) {
    case CaptureErrorStage::None:
        return QStringLiteral("none");
    case CaptureErrorStage::Configuration:
        return QStringLiteral("configuration");
    case CaptureErrorStage::DeviceEnumeration:
        return QStringLiteral("device-enumeration");
    case CaptureErrorStage::DeviceValidation:
        return QStringLiteral("device-validation");
    case CaptureErrorStage::OutputValidation:
        return QStringLiteral("output-validation");
    case CaptureErrorStage::InputFormat:
        return QStringLiteral("input-format");
    case CaptureErrorStage::InputOpen:
        return QStringLiteral("input-open");
    case CaptureErrorStage::StreamInfo:
        return QStringLiteral("stream-info");
    case CaptureErrorStage::Encoder:
        return QStringLiteral("encoder");
    case CaptureErrorStage::Muxer:
        return QStringLiteral("muxer");
    case CaptureErrorStage::ThreadStart:
        return QStringLiteral("thread-start");
    }
    return QStringLiteral("unknown");
}

QVariantMap CaptureDevice::toVariantMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("description"), description},
        {QStringLiteral("backend"), captureBackendName(backend)},
        {QStringLiteral("kind"), captureDeviceKindName(kind)},
        {QStringLiteral("available"), available},
        {QStringLiteral("unavailableReason"), unavailableReason},
    };
}

QVariantMap CaptureError::toVariantMap() const
{
    return {
        {QStringLiteral("stage"), captureErrorStageName(stage)},
        {QStringLiteral("role"), captureDeviceKindName(role)},
        {QStringLiteral("backend"), captureBackendName(backend)},
        {QStringLiteral("deviceId"), deviceId},
        {QStringLiteral("deviceName"), deviceName},
        {QStringLiteral("ffmpegError"), ffmpegError},
        {QStringLiteral("ffmpegMessage"), ffmpegMessage},
        {QStringLiteral("userMessage"), userMessage},
        {QStringLiteral("remediation"), remediation},
        {QStringLiteral("retryable"), retryable},
    };
}
