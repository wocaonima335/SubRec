#include "capture/autotest_result.h"
#include <cassert>
int main() {
    // Use explicit returns so Release builds exercise every verdict.
    if (autoTestRecordingResult(true, 1, true) != AutoTestOk) return 1;
    if (autoTestRecordingResult(true, 10000, false) != AutoTestCaptureError) return 2;
    if (autoTestRecordingResult(false, -1, false) != AutoTestCaptureError) return 3;
    if (autoTestRecordingResult(false, -1, true) != AutoTestNoOutput) return 4;
    if (autoTestRecordingResult(true, 0, true) != AutoTestNoOutput) return 5;
    if (!autoTestCaptureFailureReady(true, false)) return 6;
    if (autoTestCaptureFailureReady(false, false)) return 7;
    if (autoTestCaptureFailureReady(true, true)) return 8;
}
