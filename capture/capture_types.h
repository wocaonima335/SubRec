#ifndef CAPTURE_TYPES_H
#define CAPTURE_TYPES_H

#include <QString>
#include <QVariantList>

#include <optional>
#include <utility>

enum class CaptureBackend
{
    DirectShow
};

enum class CaptureDeviceKind
{
    Screen,
    Camera,
    SystemAudio,
    Microphone
};

enum class CaptureErrorStage
{
    None,
    Configuration,
    DeviceEnumeration,
    DeviceValidation,
    OutputValidation,
    InputFormat,
    InputOpen,
    StreamInfo,
    Encoder,
    Muxer,
    ThreadStart
};

struct CaptureDevice
{
    QString id;
    QString displayName;
    QString description;
    CaptureBackend backend = CaptureBackend::DirectShow;
    CaptureDeviceKind kind = CaptureDeviceKind::Camera;
    bool available = false;
    QString unavailableReason;

    QVariantMap toVariantMap() const;
};

struct InputRequest
{
    CaptureBackend backend = CaptureBackend::DirectShow;
    CaptureDeviceKind kind = CaptureDeviceKind::Camera;
    QString deviceId;
    QString displayName;
    QString url;
    QString format;
};

struct VideoCaptureConfig
{
    CaptureBackend backend = CaptureBackend::DirectShow;
    CaptureDeviceKind kind = CaptureDeviceKind::Screen;
    QString deviceId;
};

struct AudioCaptureConfig
{
    CaptureBackend backend = CaptureBackend::DirectShow;
    CaptureDeviceKind kind = CaptureDeviceKind::SystemAudio;
    QString deviceId;
};

struct CaptureConfig
{
    VideoCaptureConfig video;
    AudioCaptureConfig audio;
    InputRequest videoInput;
    InputRequest audioInput;
    QString outputPath;
    QString outputFileName;
};

struct CaptureError
{
    CaptureErrorStage stage = CaptureErrorStage::None;
    CaptureDeviceKind role = CaptureDeviceKind::Camera;
    CaptureBackend backend = CaptureBackend::DirectShow;
    QString deviceId;
    QString deviceName;
    int ffmpegError = 0;
    QString ffmpegMessage;
    QString userMessage;
    QString remediation;
    bool retryable = true;

    bool isValid() const { return stage != CaptureErrorStage::None; }
    QVariantMap toVariantMap() const;
};

struct CaptureStartResult
{
    bool ok = false;
    std::optional<CaptureError> error;

    static CaptureStartResult success() { return {true, std::nullopt}; }
    static CaptureStartResult failure(CaptureError error) { return {false, std::move(error)}; }
};

QString captureBackendName(CaptureBackend backend);
QString captureDeviceKindName(CaptureDeviceKind kind);
QString captureErrorStageName(CaptureErrorStage stage);

#endif // CAPTURE_TYPES_H
