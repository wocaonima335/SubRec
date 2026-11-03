#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wtsapi32.h>
#endif
#include "capture/test_video_start_delay.h"
#include "ffscreengrabthread.h"
#include "capture/ddagrab_screen_source.h"
#include "capture/present_stamp.h"
#include "capture/qpc_session_clock.h"
#include "thread/stop_deadline.h"
#include <cstdlib>
#include <cstring>

#include "queue/ffvframequeue.h"
#include "queue/media_clock.h"

#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <algorithm>
#include <cstddef>
#include <utility>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/time.h>
}

namespace {
// Session lock state: 1 locked, 0 unlocked, -1 unknown. On Windows 10/11 the lock screen
// (LockApp) is drawn on the Default desktop, so DXGI duplication keeps working and would
// record it; the WTS session flags are the reliable signal. wtsapi32 is loaded lazily so
// the grab thread (also built into standalone tests) needs no extra link dependency.
#ifdef _WIN32
// Local mirrors of WTSINFOEXW (Windows 7+); MinGW's wtsapi32.h does not declare them.
constexpr int kWtsSessionInfoEx = 25; // WTS_INFO_CLASS::WTSSessionInfoEx
constexpr LONG kSessionStateLock = 0;   // WTS_SESSIONSTATE_LOCK
constexpr LONG kSessionStateUnlock = 1; // WTS_SESSIONSTATE_UNLOCK
struct WtsInfoExLevel1 {
    ULONG SessionId;
    int SessionState;
    LONG SessionFlags;
    WCHAR WinStationName[33];
    WCHAR UserName[21];
    WCHAR DomainName[18];
    LARGE_INTEGER LogonTime, ConnectTime, DisconnectTime, LastInputTime, CurrentTime;
    DWORD IncomingBytes, OutgoingBytes, IncomingFrames, OutgoingFrames,
          IncomingCompressedBytes, OutgoingCompressedBytes;
};
struct WtsInfoEx { DWORD Level; WtsInfoExLevel1 Data; };
#endif

int sessionLockState()
{
#ifdef _WIN32
    using QueryFn = BOOL(WINAPI *)(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR *, DWORD *);
    using FreeFn = void(WINAPI *)(PVOID);
    static HMODULE module = LoadLibraryW(L"wtsapi32.dll");
    static QueryFn query = module ? reinterpret_cast<QueryFn>(
        reinterpret_cast<void *>(GetProcAddress(module, "WTSQuerySessionInformationW"))) : nullptr;
    static FreeFn freeMemory = module ? reinterpret_cast<FreeFn>(
        reinterpret_cast<void *>(GetProcAddress(module, "WTSFreeMemory"))) : nullptr;
    if (!query || !freeMemory) return -1;
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (!query(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, static_cast<WTS_INFO_CLASS>(kWtsSessionInfoEx), &buffer, &bytes)
        || !buffer)
        return -1;
    int state = -1;
    const auto *info = reinterpret_cast<const WtsInfoEx *>(buffer);
    if (bytes >= sizeof(DWORD) + offsetof(WtsInfoExLevel1, WinStationName) && info->Level == 1) {
        const LONG flags = info->Data.SessionFlags;
        if (flags == kSessionStateLock) state = 1;
        else if (flags == kSessionStateUnlock) state = 0;
    }
    freeMemory(buffer);
    return state;
#else
    return 0;
#endif
}
} // namespace

struct FFScreenGrabThread::State {
    DdagrabScreenSource m_source;
    InputRequest m_request;
    std::shared_ptr<FFVFrameQueue::State> m_queue;
    QObject *m_recorder = nullptr; // Revoked under m_mutex before recorder destruction.
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<bool> m_stop{false};
    bool m_openDone = false, m_resumeRequested = false, m_active = false;
    bool m_abortRequested = false;
    CaptureStartResult m_openResult;
    bool m_gpuFrames = false;
    int m_width = 0, m_height = 0;
    QString m_description;
    std::function<bool(const AVFrame *)> m_previewConsumer;
    // Per-session captureUs stamp failures; State is recreated per session and the
    // counter is also cleared when grabbing starts. Atomic for future diagnostics readers.
    std::atomic<std::uint64_t> m_stampFailures{0};
    AVFrame *m_lastFrame = nullptr;
    AVBufferRef *m_originalHwFramesCtx = nullptr;
    std::uint64_t m_heldFrames = 0;
    std::uint64_t m_recoveryAttempts = 0;
    std::chrono::steady_clock::time_point m_lossStarted{};
    PresentStamper m_presentStamper{kRecordFrameRate}; // grab thread only
    bool m_fallbackWarned = false;
    int64_t captureStamp(const AVFrame *frame);
    // Stamp for a re-queued held frame (lock hold / recovery): goes through the
    // same PresentStamper so stamps stay strictly increasing across the hold.
    int64_t heldStamp();
    void logPresentStamps(const char *what);
    void run();
    void grabLoop();
    bool reopenAfterLoss();
    void reportLoss(const QString &reason);
    bool restoreOriginalGpuFrame(AVFrame *frame);
    // Lock-screen hold: keep the last pre-lock frame instead of recording the lock screen.
    AVFrame *m_candidateFrame = nullptr; // newest snapshot (refreshed every 500 ms)
    AVFrame *m_stableFrame = nullptr;    // previous snapshot: 0.5-1 s old, predates a lock
    std::chrono::steady_clock::time_point m_candidateAt{};
    std::chrono::steady_clock::time_point m_lockPolledAt{};
    std::chrono::steady_clock::time_point m_lockStarted{};
    bool m_lockedCached = false;
    bool m_lockHold = false;
    std::uint64_t m_lockHeldFrames = 0;
    std::chrono::steady_clock::time_point m_nextLockHold{};
    bool sessionLocked();
    void rememberFrame(const AVFrame *frame);
    void enterLockHold(const char *where);
    void leaveLockHold();
    bool enqueueHeld(const std::shared_ptr<FFVFrameQueue::State> &queue, std::uint64_t &counter);
    void lockHoldTick(const std::shared_ptr<FFVFrameQueue::State> &queue);
};

FFScreenGrabThread::FFScreenGrabThread() : state_(std::make_shared<State>()) {}

FFScreenGrabThread::~FFScreenGrabThread()
{
    requestAbort();
    // Normal session teardown has already waited against its shared deadline.
    // Never wait again on a driver that has been abandoned.
    abandon();
}

CaptureStartResult FFScreenGrabThread::prepareOpenAndWait(const InputRequest &request,
                                                          FFVFrameQueue *queue,
                                                          QObject *recorder,
                                                          int timeoutMs,
                                                          const std::function<bool()> &shouldOpen,
                                                          std::function<bool(const AVFrame *)> previewConsumer)
{
    requestAbort();
    abandon();
    auto state = std::make_shared<State>();
    state_ = state;
    state->m_request = request;
    state->m_previewConsumer = std::move(previewConsumer);
    state->m_queue = queue ? queue->sharedState() : nullptr;
    state->m_recorder = recorder;
    // Evaluate the session-borrowing guard HERE; never keep it in a detached task.
    state->m_stop.store(shouldOpen && !shouldOpen());
    lifetime_ = std::make_unique<capture::wasapi::WorkerLifetime>();
    lifetime_->start([state] { state->run(); });

    std::unique_lock<std::mutex> lock(state->m_mutex);
    const bool done = state->m_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                            [&] { return state->m_openDone; });
    if (done) return state->m_openResult;
    lock.unlock();
    requestAbort();
    qWarning() << "[FFScreenGrabThread] open timed out after" << timeoutMs << "ms";
    CaptureError error;
    error.stage = CaptureErrorStage::InputOpen;
    error.role = CaptureDeviceKind::Screen;
    error.backend = CaptureBackend::DesktopDuplication;
    error.deviceId = request.deviceId;
    error.deviceName = request.displayName;
    error.ffmpegError = AVERROR(ETIMEDOUT);
    error.userMessage = QStringLiteral("Timed out waiting for Desktop Duplication to start.");
    error.remediation = QStringLiteral("Retry, or set SUBREC_CAPTURE_BACKEND=dshow.");
    return CaptureStartResult::failure(std::move(error));
}

bool FFScreenGrabThread::waitFor(std::chrono::milliseconds timeout)
{
    return !lifetime_ || lifetime_->joinFor(timeout);
}

void FFScreenGrabThread::wait()
{
    if (!waitFor(capture_stop::timeout())) {
        qWarning() << "[FFScreenGrabThread] stop deadline exceeded; revoking session access";
        requestAbort();
        abandon();
    }
}

void FFScreenGrabThread::abandon()
{
    if (lifetime_) {
        lifetime_->abandon();
        lifetime_.reset();
    }
}

void FFScreenGrabThread::resume()
{
    {
        std::lock_guard<std::mutex> lock(state_->m_mutex);
        state_->m_resumeRequested = true;
    }
    state_->m_cond.notify_all();
}

void FFScreenGrabThread::requestDrainStop()
{
    state_->m_stop.store(true, std::memory_order_release);
    state_->m_cond.notify_all();
}

void FFScreenGrabThread::requestAbort()
{
    std::shared_ptr<FFVFrameQueue::State> queue;
    {
        std::lock_guard<std::mutex> lock(state_->m_mutex);
        state_->m_abortRequested = true;
        state_->m_recorder = nullptr;
        queue = state_->m_queue;
    }
    state_->m_stop.store(true, std::memory_order_release);
    state_->m_cond.notify_all();
    // Unblocks an enqueue() waiting for space.
    if (queue) {
        queue->close(MediaQueueCloseReason::RequestedStop);
    }
}

void FFScreenGrabThread::wakeAllThread()
{
    requestAbort();
}

void FFScreenGrabThread::close()
{
    // Native graph destruction is always on its owner thread, including timeout.
    if (!waitFor(std::chrono::milliseconds(0))) {
        requestAbort();
    } else {
        // The finished producer has sent EOF. Keep its queued tail for the
        // downstream filter; abort would clear it and replace EOF with Closed.
        std::lock_guard<std::mutex> lock(state_->m_mutex);
        state_->m_recorder = nullptr;
    }
    abandon();
    state_ = std::make_shared<State>();
}

bool FFScreenGrabThread::isActive() const
{
    std::lock_guard<std::mutex> lock(state_->m_mutex);
    return state_->m_active;
}

bool FFScreenGrabThread::gpuFrames() const
{
    std::lock_guard<std::mutex> lock(state_->m_mutex);
    return state_->m_gpuFrames;
}

int FFScreenGrabThread::width() const
{
    std::lock_guard<std::mutex> lock(state_->m_mutex);
    return state_->m_width;
}

int FFScreenGrabThread::height() const
{
    std::lock_guard<std::mutex> lock(state_->m_mutex);
    return state_->m_height;
}

QString FFScreenGrabThread::describe() const
{
    std::lock_guard<std::mutex> lock(state_->m_mutex);
    return state_->m_description;
}

void FFScreenGrabThread::State::run()
{
    InputRequest request;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        request = m_request;
    }

    // This guard runs even if open/read throws. DXGI is closed on its owner thread.
    struct CloseSource { DdagrabScreenSource &source; AVFrame *&last; AVFrame *&cand; AVFrame *&stable; AVBufferRef *&ctx; ~CloseSource() { source.close(); av_frame_free(&last); av_frame_free(&cand); av_frame_free(&stable); av_buffer_unref(&ctx); } } closeSource{m_source, m_lastFrame, m_candidateFrame, m_stableFrame, m_originalHwFramesCtx};
    CaptureStartResult result = CaptureStartResult::success();
    if (m_stop.load(std::memory_order_acquire)) {
        CaptureError error;
        error.stage = CaptureErrorStage::Configuration;
        error.role = CaptureDeviceKind::Screen;
        error.backend = CaptureBackend::DesktopDuplication;
        error.ffmpegError = AVERROR_EXIT;
        error.userMessage = QStringLiteral("Capture start was cancelled.");
        result = CaptureStartResult::failure(std::move(error));
    } else {
#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
        if (capture::test::videoOpenTimeoutRequested()) {
            qWarning() << "[TestHook] SUBREC_TEST_VIDEO_OPEN_TIMEOUT enabled";
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this] { return m_stop.load(); });
            CaptureError error;
            error.stage = CaptureErrorStage::InputOpen;
            error.role = CaptureDeviceKind::Screen;
            error.backend = CaptureBackend::DesktopDuplication;
            error.ffmpegError = AVERROR(ETIMEDOUT);
            error.userMessage = QStringLiteral("Injected video startup timeout.");
            result = CaptureStartResult::failure(std::move(error));
        } else {
#endif
            m_source.setPreviewConsumer(m_previewConsumer);
            result = m_source.open(request);
#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
        }
#endif
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_openResult = result;
        m_openDone = true;
        m_active = result.ok;
        m_gpuFrames = m_source.gpuFrames();
        m_width = m_source.width();
        m_height = m_source.height();
        m_description = m_source.describe();
    }
    m_cond.notify_all();
    if (!result.ok) return; // CloseSource releases the graph on this thread.

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] {
            return m_resumeRequested || m_stop.load(std::memory_order_acquire);
        });
    }
    if (!m_stop.load(std::memory_order_acquire)) {
        grabLoop();
    }

    bool aborted = false;
    std::shared_ptr<FFVFrameQueue::State> queue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        aborted = m_abortRequested;
        queue = m_queue;
    }
    // Drain stop or lost duplication: EOF lets FFVFilterThread and the encoder finish the
    // file normally. On abort the queue was already closed.
    if (!aborted && queue) {
        queue->sendFinish();
    }
    qDebug() << "[FFScreenGrabThread] leave, aborted=" << aborted;
}

void FFScreenGrabThread::State::grabLoop()
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        reportLoss(QStringLiteral("out of memory"));
        return;
    }
    std::shared_ptr<FFVFrameQueue::State> queue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        queue = m_queue;
    }

#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
    capture::test::VideoStartDelay testDelay;
    const bool ready = testDelay.beforeFirstPacket([&](std::chrono::milliseconds delay) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cond.wait_for(lock, delay, [this] { return m_stop.load(); });
    }, [](int64_t delayMs) {
        qInfo() << "[TestHook] SUBREC_TEST_VIDEO_START_DELAY_MS enabled ms=" << delayMs;
    });
    if (!ready) { av_frame_free(&frame); return; }
#endif

    std::uint64_t frames = 0;
    m_stampFailures.store(0, std::memory_order_relaxed); // recording start: new session count
    m_presentStamper.reset();
    m_fallbackWarned = false;
    QpcSessionClock::instance().refresh(true);   // first anchor before the first frame
    while (!m_stop.load(std::memory_order_acquire)) {
        if (sessionLocked()) {
            enterLockHold("grab");
            lockHoldTick(queue);
            continue;
        }
        if (m_lockHold) leaveLockHold();
        // ddagrab paces itself to the frame rate (it sleeps until the next frame time and
        // duplicates the last frame on a static desktop), so this loop does not spin.
        const int ret = m_source.read(frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret >= 0) {
            if (m_gpuFrames && !m_originalHwFramesCtx && frame->hw_frames_ctx)
                m_originalHwFramesCtx = av_buffer_ref(frame->hw_frames_ctx);
            // The GPU encoder (h264_nvenc) dereferences frame->hw_frames_ctx: every frame it
            // gets must come from the original pool. Reopened sources may deliver frames of
            // another D3D11 frames context, or system-memory frames (#30 crash, r25).
            if (m_gpuFrames && m_originalHwFramesCtx
                && (!frame->hw_frames_ctx || frame->hw_frames_ctx->data != m_originalHwFramesCtx->data)
                && !restoreOriginalGpuFrame(frame)) {
                reportLoss(QStringLiteral("unable to restore frame to encoder GPU context"));
                break;
            }
            // P3a: stamp on the session clock (av_gettime_relative domain, see
            // queue/media_clock.h) with the time the desktop image was presented:
            // DXGI LastPresentTime exported by the patched ddagrab, mapped from QPC by
            // QpcSessionClock. Duplicates are placed on the frame grid after the last new
            // image; without the metadata the receive time is used and counted
            // (present_fallback). ddagrab's own pts (origin = its first_pts) is untouched.
            const int stampRet = setFrameCaptureUs(frame, captureStamp(frame));
            if (stampRet < 0) {
                // Never substitute a later time downstream; the consumer reports the gap.
                const auto n = m_stampFailures.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n == 1 || n % 300 == 0)
                    qWarning() << "[FFScreenGrabThread] captureUs stamp failed ret=" << stampRet
                               << "count=" << n;
            }
            rememberFrame(frame);
            queue->enqueueFromSrc(frame, &m_stop); // moves the reference; blocks while the queue is full
            ++frames;
            if (frames == 1 || frames % 300 == 0) {
                qDebug() << "[FFScreenGrabThread] frames=" << frames;
            }
            if (frames == 1 || frames % 1800 == 0) logPresentStamps("progress");
            continue;
        }

        char text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(ret, text, sizeof(text));
        qWarning() << "[FFScreenGrabThread] ddagrab read failed:" << text
                   << "(desktop switch, UAC prompt, lock screen or resolution change)";
        if (!reopenAfterLoss()) {
            reportLoss(QString::fromLocal8Bit(text));
            break;
        }
    }
    av_frame_free(&frame);
    qInfo() << "[FFScreenGrabThread] grab loop finished, frames=" << frames;
    logPresentStamps("summary");
}

int64_t FFScreenGrabThread::State::captureStamp(const AVFrame *frame)
{
    const int64_t receiveUs = av_gettime_relative();
    PresentStamper::Input input;
    input.receiveUs = receiveUs;
    // ddagrab.present_qpc / present_new come from the SubRec FFmpeg patch
    // (packaging/ffmpeg/patches/0001-ddagrab-export-present-time.patch).
    const AVDictionaryEntry *qpc = av_dict_get(frame->metadata, "ddagrab.present_qpc", nullptr, 0);
    const AVDictionaryEntry *fresh = av_dict_get(frame->metadata, "ddagrab.present_new", nullptr, 0);
    if (qpc && qpc->value) {
        char *end = nullptr;
        const long long ticks = std::strtoll(qpc->value, &end, 10);
        const int64_t presentUs = (end && *end == '\0' && ticks > 0)
            ? QpcSessionClock::instance().fromQpcTicks(ticks) : AV_NOPTS_VALUE;
        if (presentUs != AV_NOPTS_VALUE) {
            input.hasPresent = true;
            input.presentUs = presentUs;
            input.isNew = fresh && fresh->value && std::strcmp(fresh->value, "1") == 0;
        }
    }
    const int64_t stamp = m_presentStamper.stamp(input);
    const auto &c = m_presentStamper.counters();
    if (c.fallback == 1 && c.present == 0 && !m_fallbackWarned) {
        m_fallbackWarned = true;
        qWarning() << "[FFScreenGrabThread] no ddagrab presentation time; using receive time"
                   << "(FFmpeg without the SubRec ddagrab patch?)";
    }
    return stamp;
}

int64_t FFScreenGrabThread::State::heldStamp()
{
    // A held frame repeats an older picture (pre-lock snapshot or last frame before a
    // loss), so its presentation time no longer applies: it is placed at the receive
    // time, never before the previous stamp, and ends duplicate synthesis from the
    // last new image. After the hold, a new image whose presentation time is not after
    // the last held stamp gets its receive time (counted after_hold), so presentation
    // time never moves backwards into frames that were already queued.
    return m_presentStamper.stampHeld(av_gettime_relative());
}

void FFScreenGrabThread::State::logPresentStamps(const char *what)
{
    const auto &c = m_presentStamper.counters();
    const auto q = QpcSessionClock::instance().stats();
    qInfo() << "[FFScreenGrabThread] present_stamp" << what << "present=" << c.present
            << "synthesized=" << c.synthesized << "held=" << c.held << "after_hold=" << c.afterHold
            << "present_fallback=" << c.fallback
            << "rejected_future=" << c.rejectedFuture << "rejected_stale=" << c.rejectedStale
            << "clamped=" << c.clamped << "latency_mean_us=" << m_presentStamper.meanLatencyUs()
            << "latency_max_us=" << c.maxLatencyUs << "qpc_anchors=" << q.anchors
            << "qpc_slope_ppm=" << q.slopePpm << "qpc_residual_us=" << q.maxResidualUs
            << "qpc_drift_us=" << q.driftUs << "qpc_uncertainty_us=" << q.lastUncertaintyUs;
}

bool FFScreenGrabThread::State::reopenAfterLoss()
{
    bool gpuFrames = false;
    int width = 0;
    int height = 0;
    InputRequest request;
    std::shared_ptr<FFVFrameQueue::State> queue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        gpuFrames = m_gpuFrames;
        width = m_width;
        height = m_height;
        request = m_request;
        queue = m_queue;
    }
    m_source.close();
    m_lossStarted = std::chrono::steady_clock::now();
    m_recoveryAttempts = 0;
    qWarning() << "[FFScreenGrabThread] capture lost, entering recovery mode="
               << (gpuFrames ? "gpu" : "cpu");
    auto nextHold = std::chrono::steady_clock::now();
    while (!m_stop.load(std::memory_order_acquire)) {
        if (sessionLocked()) {
            // Locked: reopening would duplicate the lock screen. Hold the pre-lock frame
            // and retry the open only after unlock.
            enterLockHold("recovery");
            lockHoldTick(queue);
            continue;
        }
        if (m_lockHold) leaveLockHold();
        ++m_recoveryAttempts;
        // Keep the audio/video timeline alive at the configured capture rate while DXGI is gone.
        const auto now = std::chrono::steady_clock::now();
        if (m_lastFrame && queue && now >= nextHold) {
            AVFrame *held = av_frame_alloc();
            if (held && av_frame_ref(held, m_lastFrame) >= 0) {
                if (setFrameCaptureUs(held, heldStamp()) >= 0 &&
                    queue->enqueueFromSrc(held, &m_stop) == MediaQueueResult::Ok) {
                    ++m_heldFrames;
                } else av_frame_free(&held);
            } else av_frame_free(&held);
            nextHold = now + std::chrono::milliseconds(33);
        }
        // Reopen with the same frame kind as the session's first open: GPU sessions need GPU
        // frames (restored into the encoder's pool below), CPU sessions forced CPU frames.
        // r24 passed gpuFrames as forceCpuFrames, so GPU sessions got system-memory frames
        // without hw_frames_ctx and nvenc crashed on frame->hw_frames_ctx->data.
        const CaptureStartResult result = m_source.open(request, !gpuFrames);
        if (result.ok) {
            if (m_source.width() != width || m_source.height() != height) {
                qWarning() << "[FFScreenGrabThread] screen size changed from" << width << "x" << height
                           << "to" << m_source.width() << "x" << m_source.height()
                           << "; the encoder cannot follow, ending the video stream";
                m_source.close();
                return false;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_lossStarted).count();
            qInfo() << "[FFScreenGrabThread] duplication restored after" << m_recoveryAttempts
                    << "attempts /" << elapsed << "ms mode=" << (gpuFrames ? "gpu" : "cpu")
                    << "held=" << m_heldFrames;
            return true;
        }
        if ((m_recoveryAttempts % 10) == 0)
            qInfo() << "[FFScreenGrabThread] recovery retry" << m_recoveryAttempts
                    << "mode=" << (gpuFrames ? "gpu" : "cpu");
        // Retry the open every ~300 ms, but keep holding the last frame at ~30 fps meanwhile.
        const auto retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (!m_stop.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < retryAt) {
            if (m_lastFrame && queue && std::chrono::steady_clock::now() >= nextHold) {
                AVFrame *held = av_frame_alloc();
                if (held && av_frame_ref(held, m_lastFrame) >= 0 &&
                    setFrameCaptureUs(held, heldStamp()) >= 0 &&
                    queue->enqueueFromSrc(held, &m_stop) == MediaQueueResult::Ok)
                    ++m_heldFrames;
                av_frame_free(&held);
                nextHold += std::chrono::milliseconds(33);
                if (nextHold < std::chrono::steady_clock::now() - std::chrono::milliseconds(100))
                    nextHold = std::chrono::steady_clock::now();
            }
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait_until(lock, std::min(retryAt, nextHold),
                              [this] { return m_stop.load(std::memory_order_acquire); });
        }
    }
    return false;
}

bool FFScreenGrabThread::State::sessionLocked()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lockPolledAt >= std::chrono::milliseconds(100)) {
        m_lockPolledAt = now;
        const int state = sessionLockState();
        if (state >= 0) m_lockedCached = state == 1; // unknown: keep the last answer
    }
    return m_lockedCached;
}

void FFScreenGrabThread::State::rememberFrame(const AVFrame *frame)
{
    if (!m_lastFrame) m_lastFrame = av_frame_alloc();
    if (m_lastFrame) { av_frame_unref(m_lastFrame); av_frame_ref(m_lastFrame, frame); }
    const auto now = std::chrono::steady_clock::now();
    if (!m_candidateFrame || now - m_candidateAt >= std::chrono::milliseconds(500)) {
        std::swap(m_stableFrame, m_candidateFrame);
        if (!m_candidateFrame) m_candidateFrame = av_frame_alloc();
        if (m_candidateFrame) { av_frame_unref(m_candidateFrame); av_frame_ref(m_candidateFrame, frame); }
        m_candidateAt = now;
    }
}

void FFScreenGrabThread::State::enterLockHold(const char *where)
{
    if (m_lockHold) return;
    m_lockHold = true;
    m_lockHeldFrames = 0;
    m_lockStarted = std::chrono::steady_clock::now();
    m_nextLockHold = m_lockStarted;
    // The session flag can trail the switch to the lock screen by a few frames, so hold the
    // 0.5-1 s old snapshot rather than the newest frame.
    AVFrame *source = m_stableFrame ? m_stableFrame : m_candidateFrame;
    if (source && m_lastFrame && source != m_lastFrame) {
        av_frame_unref(m_lastFrame);
        av_frame_ref(m_lastFrame, source);
    }
    qInfo() << "[FFScreenGrabThread] session locked (" << where
            << "), holding last pre-lock frame mode=" << (m_gpuFrames ? "gpu" : "cpu");
}

void FFScreenGrabThread::State::leaveLockHold()
{
    if (!m_lockHold) return;
    m_lockHold = false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_lockStarted).count();
    qInfo() << "[FFScreenGrabThread] session unlocked after" << ms << "ms, held="
            << m_lockHeldFrames << "mode=" << (m_gpuFrames ? "gpu" : "cpu");
}

bool FFScreenGrabThread::State::enqueueHeld(const std::shared_ptr<FFVFrameQueue::State> &queue,
                                            std::uint64_t &counter)
{
    if (!m_lastFrame || !queue) return false;
    AVFrame *held = av_frame_alloc();
    bool ok = held && av_frame_ref(held, m_lastFrame) >= 0 &&
              setFrameCaptureUs(held, heldStamp()) >= 0 &&
              queue->enqueueFromSrc(held, &m_stop) == MediaQueueResult::Ok;
    av_frame_free(&held);
    if (ok) ++counter;
    return ok;
}

void FFScreenGrabThread::State::lockHoldTick(const std::shared_ptr<FFVFrameQueue::State> &queue)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_nextLockHold) {
        enqueueHeld(queue, m_lockHeldFrames);
        m_nextLockHold += std::chrono::milliseconds(33);
        if (m_nextLockHold < now - std::chrono::milliseconds(100)) m_nextLockHold = now;
        if (m_lockHeldFrames % 300 == 0 && m_lockHeldFrames)
            qInfo() << "[FFScreenGrabThread] lock hold frames=" << m_lockHeldFrames;
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait_until(lock, std::min(m_nextLockHold, m_lockPolledAt + std::chrono::milliseconds(100)),
                      [this] { return m_stop.load(std::memory_order_acquire); });
}

// Reopened ddagrab owns a new D3D11 frames context. Download through system memory,
// then allocate/upload into the encoder's original pool; this avoids passing a surface
// tied to the lost duplication device into NVENC/AMF.
bool FFScreenGrabThread::State::restoreOriginalGpuFrame(AVFrame *frame)
{
    if (!m_originalHwFramesCtx || !frame) return false;
    AVFrame *system = av_frame_alloc();
    AVFrame *gpu = av_frame_alloc();
    if (!system || !gpu) { av_frame_free(&system); av_frame_free(&gpu); return false; }
    // GPU frame of another pool: download first. System-memory frame: upload directly.
    const int download = frame->hw_frames_ctx ? av_hwframe_transfer_data(system, frame, 0)
                                              : av_frame_ref(system, frame);
    int ret = download;
    if (ret >= 0) ret = av_hwframe_get_buffer(m_originalHwFramesCtx, gpu, 0);
    if (ret >= 0) ret = av_hwframe_transfer_data(gpu, system, 0);
    if (ret < 0) {
        char text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(ret, text, sizeof(text));
        qWarning() << "[FFScreenGrabThread] GPU frame restore failed:" << text
                   << "srcFormat=" << system->format;
        av_frame_free(&system); av_frame_free(&gpu); return false;
    }
    av_frame_copy_props(gpu, frame);
    av_buffer_unref(&gpu->hw_frames_ctx);
    gpu->hw_frames_ctx = av_buffer_ref(m_originalHwFramesCtx);
    av_frame_unref(frame);
    av_frame_move_ref(frame, gpu);
    av_frame_free(&system); av_frame_free(&gpu);
    return true;
}

void FFScreenGrabThread::State::reportLoss(const QString &reason)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QObject *recorder = m_recorder;
    if (!recorder || m_stop.load(std::memory_order_acquire)) return;
    QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("Screen capture stopped: %1").arg(reason)),
                              Q_ARG(QString, QStringLiteral("Stop the recording (the file is kept up to "
                                                            "this point). For capture across UAC/lock "
                                                            "screen, set SUBREC_DDAGRAB_MODE=cpu or "
                                                            "SUBREC_CAPTURE_BACKEND=dshow.")));
}
