#ifndef CAPTURE_BACKEND_H
#define CAPTURE_BACKEND_H

#include "capture_types.h"

#include <QList>

class ICaptureBackend
{
public:
    virtual ~ICaptureBackend() = default;

    virtual CaptureBackend backend() const = 0;
    virtual QList<CaptureDevice> enumerate() const = 0;
    virtual CaptureStartResult validate(const InputRequest &request) const = 0;
    virtual CaptureStartResult makeInputRequest(CaptureDeviceKind kind,
                                                const QString &deviceId,
                                                InputRequest *request) const = 0;
};

#endif // CAPTURE_BACKEND_H
