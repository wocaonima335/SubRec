#include "recorder/capture_session.h"
#include "recorder/ffrecorder.h"
#include "capture/ddagrab_screen_source.h"
#include "thread/failure_cleanup_deadline.h"
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <stdexcept>

using namespace std::chrono_literals;
#define REQUIRE(c) do { if (!(c)) throw std::runtime_error(#c); } while (false)
namespace {
std::mutex driverMutex;
std::condition_variable driverWake;
bool driverReleased = false;
bool failImmediately = false;
bool nativeSuccess = false;
std::atomic<int> liveSources{0}, liveOpens{0};
void setEnv(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (*value) setenv(name, value, 1); else unsetenv(name);
#endif
}
void releaseDriver() {
    { std::lock_guard<std::mutex> lock(driverMutex); driverReleased = true; }
    driverWake.notify_all();
}
void awaitReclaimed() {
    const auto end = std::chrono::steady_clock::now() + 2s;
    while (liveSources || liveOpens) {
        REQUIRE(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(1ms);
    }
}
}
// Fake only the native driver: emulate an open that ignores cancellation. This
// must outlive session/queue destruction without touching borrowed objects.
DdagrabScreenSource::DdagrabScreenSource() { ++liveSources; }
DdagrabScreenSource::~DdagrabScreenSource() { --liveSources; }
CaptureStartResult DdagrabScreenSource::open(const InputRequest &, bool) {
    ++liveOpens;
    if (!failImmediately) {
        std::unique_lock<std::mutex> lock(driverMutex);
        driverWake.wait(lock, [] { return driverReleased; });
    }
    --liveOpens;
    if (nativeSuccess) return CaptureStartResult::success();
    CaptureError error;
    error.stage = CaptureErrorStage::InputOpen;
    error.role = CaptureDeviceKind::Screen;
    error.userMessage = QStringLiteral("fake video driver failed");
    return CaptureStartResult::failure(error);
}
void DdagrabScreenSource::close() {}
int DdagrabScreenSource::read(AVFrame *) { std::abort(); }
QString DdagrabScreenSource::describe() const { return {}; }
QString captureDeviceKindName(CaptureDeviceKind) { return {}; }

static void runFailure(bool cancellableHook, bool immediate) {
    failImmediately = immediate;
    nativeSuccess = false;
    driverReleased = false;
    setEnv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", cancellableHook ? "1" : "");
    setEnv("SUBREC_STOP_TIMEOUT_MS", "120");
    std::promise<CaptureStartResult> reported;
    auto report = reported.get_future();
    std::chrono::steady_clock::time_point failedAt;
    long stopMs = 0;
    {
        FFRecorder recorder;
        CaptureSession session(recorder);
        CaptureConfig config;
        config.videoInput.backend = CaptureBackend::DesktopDuplication;
        config.videoInput.kind = CaptureDeviceKind::Screen;
        REQUIRE(session.start(config, [&](const CaptureStartResult &result) {
            failedAt = std::chrono::steady_clock::now();
            reported.set_value(result);
        }));
        REQUIRE(report.wait_for(2s) == std::future_status::ready);
        const auto result = report.get();
        REQUIRE(!result.ok && result.error.has_value());
        REQUIRE(result.error->stage == CaptureErrorStage::InputOpen);
        // A delayed native open eventually returns within the cleanup budget.
        // Permanently stuck native open is checked in a separate child process.
        std::thread driverReturn;
        if (!cancellableHook && !immediate) driverReturn = std::thread([] {
            std::this_thread::sleep_for(60ms);
            releaseDriver();
        });
        const bool released = session.release();
        if (driverReturn.joinable()) driverReturn.join();
        REQUIRE(released); // Joins REAL opener + runs REAL failure cleanup.
        stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - failedAt).count();
        REQUIRE(stopMs < 600); // 120 ms single budget + scheduling tolerance.
        REQUIRE(!recorder.isRecording());
        REQUIRE(!session.isStarted());
        REQUIRE(FakeDeviceWorker::live == 0);
        for (const auto &worker : recorder.audio) {
            REQUIRE(worker->exited && worker->closed);
            REQUIRE(worker->queue.isClosed());
        }
        REQUIRE(recorder.videoQueue.isClosed());
        REQUIRE(liveOpens == 0);
    } // All device/processing workers joined before session destruction.
    releaseDriver();
    awaitReclaimed();
    REQUIRE(FakeDeviceWorker::live == 0);
    std::printf("PASS video-start-failure hook=%d immediate=%d cleanup_ms=%ld live_audio=0 live_sources=0\n",
                cancellableHook, immediate, stopMs);
}
static void destroyedScreenWhileOpening() {
    driverReleased = false;
    failImmediately = false;
    nativeSuccess = true; // Late successful open must never read/enqueue after abort.
    setEnv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "");
    int guardCalls = 0;
    {
        FFVFrameQueue queue;
        FFScreenGrabThread screen;
        queue.start();
        REQUIRE(!screen.prepareOpenAndWait(InputRequest{}, &queue, nullptr, 30, [&] {
            ++guardCalls;
            return true;
        }).ok);
        REQUIRE(liveOpens == 1);
    } // Worker owns State/queue storage, not these destroyed wrappers.
    releaseDriver();
    awaitReclaimed();
    REQUIRE(guardCalls == 1);
    std::puts("PASS screen-owner-destruction: native open reclaimed, no borrowed access");
}
static void normalScreenDrain() {
    failImmediately = false;
    nativeSuccess = true;
    releaseDriver();
    setEnv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "");
    for (const int frameCount : {0, 1, 30}) {
        FFVFrameQueue queue;
        queue.start();
        {
            FFScreenGrabThread screen;
            REQUIRE(screen.prepareOpenAndWait(InputRequest{}, &queue, nullptr, 1000, {}).ok);
            // Hold the prepared producer before resume and model its buffered tail.
            // No consumer runs until after close(), including at the queue's capacity.
            for (int i = 0; i < frameCount; ++i) {
                AVFrame *frame = av_frame_alloc();
                REQUIRE(frame);
                frame->pts = i;
                queue.enqueue(frame);
                av_frame_free(&frame);
            }
            screen.requestDrainStop();
            REQUIRE(screen.waitFor(1s)); // Real worker sends EOF before completing.
            screen.close();
            REQUIRE(!screen.isActive());
            screen.close(); // Repeated close/destruction must also preserve the tail.
        }
        REQUIRE(!queue.isClosed());
        for (int i = 0; i < frameCount; ++i) {
            auto result = queue.receive();
            REQUIRE(result.result == MediaQueueResult::Ok && result.item);
            const auto pts = result.item->pts;
            av_frame_free(&result.item);
            REQUIRE(pts == i);
        }
        const auto end = queue.receive();
        REQUIRE(end.result == MediaQueueResult::Eof && !end.item);
        awaitReclaimed();
        std::printf("PASS screen-normal-drain: frames=%d ordered, EOF (not Closed), live_sources=0\n",
                    frameCount);
    }
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--autotest-exit") {
        capture_stop::armAutoTestExitWatchdog(3, 40ms);
        std::this_thread::sleep_for(5s); // Models a blocked Qt aboutToQuit callback.
        return 99;
    }
    if (argc == 2 && (std::string(argv[1]) == "--stuck-audio"
                     || std::string(argv[1]) == "--stuck-video")) {
        setEnv("SUBREC_STOP_TIMEOUT_MS", "120");
        setEnv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "");
        failImmediately = std::string(argv[1]) == "--stuck-audio";
        FFRecorder recorder;
        recorder.audio[0]->stuck = failImmediately;
        CaptureSession session(recorder);
        std::promise<void> failure;
        session.start(CaptureConfig{}, [&](const CaptureStartResult &result) {
            if (!result.ok) failure.set_value();
        });
        if (failure.get_future().wait_for(1s) != std::future_status::ready) return 98;
        session.release(); // Must exit 6 on the single cleanup deadline, never UAF.
        return 99;
    }

    try {
        if (argc == 2 && std::string(argv[1]) == "--normal-drain") {
            normalScreenDrain();
            return 0;
        }
        runFailure(true, false);
        runFailure(false, false);
        runFailure(false, true);
        destroyedScreenWhileOpening();
        // Deadline must not restart a full timeout for each worker.
        capture_stop::Deadline budget(25ms);
        std::this_thread::sleep_for(30ms);
        REQUIRE(budget.remaining() == 0ms);
        setEnv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "");
        setEnv("SUBREC_STOP_TIMEOUT_MS", "");
        return 0;
    } catch (const std::exception &e) {
        releaseDriver();
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
}
