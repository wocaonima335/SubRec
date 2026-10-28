#pragma once
#include "stop_deadline.h"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace capture_stop {
// Native read/close calls cannot always be interrupted. Failed preparation has
// already been reported as Failed; if borrowed workers still cannot be stopped,
// do not free their contexts or leave readers running. Fail the process explicitly.
[[noreturn]] inline void failedCleanupExit() {
    std::fputs("[CaptureSession] FATAL: failed-start cleanup exceeded SUBREC_STOP_TIMEOUT_MS; "
               "native device did not stop. Recording FAILED; exiting with code 6.\n", stderr);
    std::fflush(stderr);
    std::_Exit(6);
}
class FailureCleanupDeadline {
    struct State { std::mutex mutex; std::condition_variable cv; bool done = false; };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::thread watchdog_;
public:
    explicit FailureCleanupDeadline(std::chrono::milliseconds limit = timeout()) {
        const auto end = std::chrono::steady_clock::now() + limit;
        watchdog_ = std::thread([state = state_, end] {
            std::unique_lock<std::mutex> lock(state->mutex);
            if (!state->cv.wait_until(lock, end, [&] { return state->done; })) failedCleanupExit();
        });
    }
    ~FailureCleanupDeadline() {
        { std::lock_guard<std::mutex> lock(state_->mutex); state_->done = true; }
        state_->cv.notify_all();
        watchdog_.join();
    }
    FailureCleanupDeadline(const FailureCleanupDeadline &) = delete;
    FailureCleanupDeadline &operator=(const FailureCleanupDeadline &) = delete;
};

// AutoTest exit must remain bounded even if aboutToQuit blocks the Qt event loop.
// Arm BEFORE QCoreApplication::exit(), which may synchronously call shutdown code.
inline void armAutoTestExitWatchdog(int code, std::chrono::milliseconds grace) {
    std::thread([code, grace] {
        std::this_thread::sleep_for(grace);
        std::fputs("[AutoTest] teardown deadline exceeded, force exit\n", stderr);
        std::_Exit(code == 0 ? 5 : code);
    }).detach();
}
}
