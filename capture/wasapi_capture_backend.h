#pragma once
#include "capture_backend.h"
class WasapiCaptureBackend final : public ICaptureBackend {
public:
    CaptureBackend backend() const override { return CaptureBackend::Wasapi; }
    QList<CaptureDevice> enumerate() const override;
    CaptureStartResult validate(const InputRequest &request) const override;
    CaptureStartResult makeInputRequest(CaptureDeviceKind kind, const QString &id, InputRequest *request) const override;
};
