#include "capture/autotest_report.h"
#include <iostream>

int main()
{
    const auto failure = autoTestRecordingReport(
        AutoTestCaptureError, QStringLiteral("D:/SubRec_test/run.mp4"), 123456,
        QStringLiteral("h264_nvenc"), true, QStringLiteral("1920x1080"),
        QStringLiteral("NVENC D3D11 passthrough"), QStringLiteral("Mux write failed"));
    const auto expected = QStringLiteral(
        "[AutoTest] result=FAIL file=D:/SubRec_test/run.mp4 bytes=123456 encoder=h264_nvenc "
        "hardware=yes resolution=1920x1080 encoderDetail=NVENC D3D11 passthrough captureError=Mux write failed");
    if (failure != expected) return 1;
    const auto success = autoTestRecordingReport(AutoTestOk, "ok.mp4", 1, "libx264", false,
                                                "1280x720", "software", "");
    if (!success.startsWith("[AutoTest] result=PASS file=ok.mp4 bytes=1 encoder=libx264 hardware=no ")
        || !success.endsWith("captureError=")) return 2;
    const auto missing = autoTestRecordingReport(AutoTestCaptureError, "missing.mp4", -1,
                                                "", false, "", "", "Encoder failed");
    if (!missing.contains("bytes=-1") || !missing.endsWith("captureError=Encoder failed")) return 3;
    std::cout << "PASS fatal and normal AutoTest result metadata\n";
}
