#include "ddagrab_capture_backend.h"

#include "ddagrab_screen_source.h"

#include <QDebug>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#endif

namespace {
const QString kIdPrefix = QStringLiteral("ddagrab:");

CaptureStartResult failure(CaptureErrorStage stage,
                           CaptureDeviceKind kind,
                           const QString &deviceId,
                           const QString &message,
                           const QString &remediation)
{
    CaptureError error;
    error.stage = stage;
    error.role = kind;
    error.backend = CaptureBackend::DesktopDuplication;
    error.deviceId = deviceId;
    error.deviceName = deviceId;
    error.userMessage = message;
    error.remediation = remediation;
    return CaptureStartResult::failure(std::move(error));
}

CaptureDevice makeDevice(int outputIndex, const QString &detail, bool available,
                         const QString &unavailableReason)
{
    CaptureDevice device;
    device.id = kIdPrefix + QString::number(outputIndex);
    device.displayName = QStringLiteral("Display %1 (Desktop Duplication)").arg(outputIndex + 1);
    device.description = detail;
    device.backend = CaptureBackend::DesktopDuplication;
    device.kind = CaptureDeviceKind::Screen;
    device.available = available;
    device.unavailableReason = unavailableReason;
    return device;
}
} // namespace

CaptureBackend DdagrabCaptureBackend::backend() const
{
    return CaptureBackend::DesktopDuplication;
}

bool DdagrabCaptureBackend::isDdagrabDeviceId(const QString &deviceId)
{
    return deviceId.startsWith(kIdPrefix);
}

QList<CaptureDevice> DdagrabCaptureBackend::enumerate() const
{
    QList<CaptureDevice> devices;
    const bool available = DdagrabScreenSource::isAvailable();
    const QString reason = available
        ? QString()
        : QStringLiteral("this FFmpeg build has no ddagrab filter");

#ifdef _WIN32
    // ddagrab's internal D3D11 device uses the default adapter, so only its outputs can be
    // duplicated; output_idx counts the outputs of that adapter.
    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
        IDXGIAdapter1 *adapter = nullptr;
        if (factory->EnumAdapters1(0, &adapter) == S_OK) {
            for (UINT index = 0;; ++index) {
                IDXGIOutput *output = nullptr;
                if (adapter->EnumOutputs(index, &output) != S_OK) {
                    break;
                }
                DXGI_OUTPUT_DESC desc = {};
                QString detail;
                if (SUCCEEDED(output->GetDesc(&desc))) {
                    const RECT &r = desc.DesktopCoordinates;
                    detail = QStringLiteral("%1 %2x%3 at %4,%5")
                                 .arg(QString::fromWCharArray(desc.DeviceName))
                                 .arg(r.right - r.left)
                                 .arg(r.bottom - r.top)
                                 .arg(r.left)
                                 .arg(r.top);
                }
                output->Release();
                devices.append(makeDevice(int(index), detail, available, reason));
            }
            adapter->Release();
        }
        factory->Release();
    }
#endif
    if (devices.isEmpty()) {
        // Enumeration failed or no output on the default adapter: still offer output 0 and
        // let the open report the real reason.
        devices.append(makeDevice(0, QStringLiteral("default adapter, output 0"), available, reason));
    }
    qInfo() << "[DdagrabBackend] outputs=" << devices.size() << "ddagrab filter=" << available;
    return devices;
}

CaptureStartResult DdagrabCaptureBackend::makeInputRequest(CaptureDeviceKind kind,
                                                           const QString &deviceId,
                                                           InputRequest *request) const
{
    if (!request) {
        return failure(CaptureErrorStage::Configuration, kind, deviceId,
                       QStringLiteral("Internal capture request target is missing."),
                       QStringLiteral("Retry the recording request."));
    }
    if (kind != CaptureDeviceKind::Screen || !isDdagrabDeviceId(deviceId)) {
        return failure(CaptureErrorStage::Configuration, kind, deviceId,
                       QStringLiteral("Desktop Duplication only captures screens."),
                       QStringLiteral("Select a screen device."));
    }
    bool ok = false;
    const int outputIndex = deviceId.mid(kIdPrefix.size()).toInt(&ok);
    if (!ok || outputIndex < 0) {
        return failure(CaptureErrorStage::DeviceValidation, kind, deviceId,
                       QStringLiteral("Invalid Desktop Duplication device id: %1").arg(deviceId),
                       QStringLiteral("Refresh devices and select a screen."));
    }
    if (!DdagrabScreenSource::isAvailable()) {
        return failure(CaptureErrorStage::InputFormat, kind, deviceId,
                       QStringLiteral("This FFmpeg build has no ddagrab (Desktop Duplication) filter."),
                       QStringLiteral("Use the FFmpeg built by packaging/ffmpeg/build-ffmpeg.sh, or "
                                      "set SUBREC_CAPTURE_BACKEND=dshow."));
    }

    request->backend = CaptureBackend::DesktopDuplication;
    request->kind = kind;
    request->deviceId = deviceId;
    request->displayName = QStringLiteral("Display %1 (Desktop Duplication)").arg(outputIndex + 1);
    request->url = QStringLiteral("output_idx=%1").arg(outputIndex);
    request->format = QStringLiteral("ddagrab");
    return CaptureStartResult::success();
}

CaptureStartResult DdagrabCaptureBackend::validate(const InputRequest &request) const
{
    if (request.backend != CaptureBackend::DesktopDuplication) {
        return failure(CaptureErrorStage::Configuration, request.kind, request.deviceId,
                       QStringLiteral("Unsupported capture backend."),
                       QStringLiteral("Select a Desktop Duplication screen."));
    }
    InputRequest resolved;
    return makeInputRequest(request.kind, request.deviceId, &resolved);
}
