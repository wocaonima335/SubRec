#ifndef DDAGRAB_CAPTURE_BACKEND_H
#define DDAGRAB_CAPTURE_BACKEND_H

#include "capture_backend.h"

// Screen devices of the Desktop Duplication (ddagrab) backend: one per DXGI output of the
// default adapter, ids "ddagrab:<output_idx>". Camera and audio are not offered here.
class DdagrabCaptureBackend final : public ICaptureBackend
{
public:
    CaptureBackend backend() const override;
    QList<CaptureDevice> enumerate() const override;
    CaptureStartResult validate(const InputRequest &request) const override;
    CaptureStartResult makeInputRequest(CaptureDeviceKind kind,
                                        const QString &deviceId,
                                        InputRequest *request) const override;

    static bool isDdagrabDeviceId(const QString &deviceId);
};

#endif // DDAGRAB_CAPTURE_BACKEND_H
