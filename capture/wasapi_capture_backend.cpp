#include "wasapi_capture_backend.h"
QList<CaptureDevice> WasapiCaptureBackend::enumerate() const {
    CaptureDevice device;
    device.id = QStringLiteral("wasapi:default");
    device.displayName = QStringLiteral("System audio (WASAPI)");
    device.description = QStringLiteral("Default playback device; no additional driver required");
    device.kind = CaptureDeviceKind::SystemAudio;
    device.backend = CaptureBackend::Wasapi;
    // Resolve on the capture thread at open time so device changes remain recoverable.
    device.available = true;
    return {device};
}
CaptureStartResult WasapiCaptureBackend::validate(const InputRequest &request) const {
    if (request.backend == backend() && request.kind == CaptureDeviceKind::SystemAudio && request.deviceId == "wasapi:default")
        return CaptureStartResult::success();
    CaptureError error;
    error.backend = backend(); error.role = request.kind;
    error.stage = CaptureErrorStage::DeviceValidation;
    error.userMessage = QStringLiteral("Invalid WASAPI system audio device.");
    return CaptureStartResult::failure(std::move(error));
}
CaptureStartResult WasapiCaptureBackend::makeInputRequest(CaptureDeviceKind kind, const QString &id, InputRequest *request) const {
    InputRequest input;
    input.backend = backend(); input.kind = kind; input.deviceId = id;
    input.displayName = QStringLiteral("System audio (WASAPI)");
    input.format = QStringLiteral("wasapi"); input.url = id;
    auto result = validate(input);
    if (!request) { input.deviceId.clear(); return validate(input); }
    if (result.ok) *request = input;
    return result;
}
