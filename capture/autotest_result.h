#pragma once

// Exit 6 is reserved for the stop watchdog.
enum AutoTestExit {
    AutoTestOk = 0,
    AutoTestBadEnvironment = 2,
    AutoTestNotRunning = 3,
    AutoTestNoOutput = 4,
    AutoTestStopTimeout = 5,
    AutoTestCaptureError = 7
};

inline AutoTestExit autoTestRecordingResult(bool exists, long long bytes, bool captureErrorEmpty)
{
    if (!captureErrorEmpty) return AutoTestCaptureError;
    return exists && bytes > 0 ? AutoTestOk : AutoTestNoOutput;
}

inline bool autoTestCaptureFailureReady(bool finalized, bool captureErrorEmpty)
{
    return finalized && !captureErrorEmpty;
}
