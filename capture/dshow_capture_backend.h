#ifndef DSHOW_CAPTURE_BACKEND_H
#define DSHOW_CAPTURE_BACKEND_H

#include "capture_backend.h"

class DirectShowCaptureBackend final : public ICaptureBackend
{
public:
    CaptureBackend backend() const override;
    QList<CaptureDevice> enumerate() const override;
    CaptureStartResult validate(const InputRequest &request) const override;
    CaptureStartResult makeInputRequest(CaptureDeviceKind kind,
                                        const QString &deviceId,
                                        InputRequest *request) const override;

private:
    QList<CaptureDevice> enumerateDevices() const;
    static CaptureDeviceKind kindForDevice(const QString &name,
                                           const QList<int> &mediaTypes);
    static QString makeUrl(CaptureDeviceKind kind, const QString &deviceId);
};

#endif // DSHOW_CAPTURE_BACKEND_H
