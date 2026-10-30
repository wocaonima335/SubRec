#include "capture/test_video_start_delay.h"
#include "ffscreengrabthread.h"
#include "capture/ddagrab_screen_source.h"
#include "thread/stop_deadline.h"
#include <cstdlib>

#include "queue/ffvframequeue.h"

#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <chrono>
#include <cstdint>
#include <thread>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

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
    void run();
    void grabLoop();
    bool reopenAfterLoss();
    void reportLoss(const QString &reason);
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
    struct CloseSource { DdagrabScreenSource &source; ~CloseSource() { source.close(); } } closeSource{m_source};
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
    while (!m_stop.load(std::memory_order_acquire)) {
        // ddagrab paces itself to the frame rate (it sleeps until the next frame time and
        // duplicates the last frame on a static desktop), so this loop does not spin.
        const int ret = m_source.read(frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret >= 0) {
            queue->enqueueFromSrc(frame); // moves the reference; blocks while the queue is full
            ++frames;
            if (frames == 1 || frames % 300 == 0) {
                qDebug() << "[FFScreenGrabThread] frames=" << frames;
            }
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
}

bool FFScreenGrabThread::State::reopenAfterLoss()
{
    bool gpuFrames = false;
    int width = 0;
    int height = 0;
    InputRequest request;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        gpuFrames = m_gpuFrames;
        width = m_width;
        height = m_height;
        request = m_request;
    }
    // GPU frames: the encoder holds the old graph's hw_frames_ctx (h264_amf even asserts that
    // every frame uses it), so a new ddagrab device/frames context cannot be fed in.
    if (gpuFrames) {
        return false;
    }

    m_source.close();
    // Up to ~10 s, e.g. while a UAC prompt or the lock screen owns the desktop.
    for (int attempt = 0; attempt < 100 && !m_stop.load(std::memory_order_acquire); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const CaptureStartResult result = m_source.open(request, true);
        if (!result.ok) {
            continue;
        }
        if (m_source.width() != width || m_source.height() != height) {
            qWarning() << "[FFScreenGrabThread] screen size changed from" << width << "x" << height
                       << "to" << m_source.width() << "x" << m_source.height()
                       << "; the encoder cannot follow, ending the video stream";
            m_source.close();
            return false;
        }
        qInfo() << "[FFScreenGrabThread] duplication restored after" << (attempt + 1) << "attempts";
        return true;
    }
    return false;
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
