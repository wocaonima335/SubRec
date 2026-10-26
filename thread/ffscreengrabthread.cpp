#include "ffscreengrabthread.h"

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

FFScreenGrabThread::FFScreenGrabThread() = default;

FFScreenGrabThread::~FFScreenGrabThread()
{
    requestAbort();
    wait();
}

CaptureStartResult FFScreenGrabThread::prepareOpenAndWait(const InputRequest &request,
                                                          FFVFrameQueue *queue,
                                                          QObject *recorder,
                                                          int timeoutMs,
                                                          const std::function<bool()> &shouldOpen)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_request = request;
        m_queue = queue;
        m_recorder = recorder;
        m_openGuard = shouldOpen;
        m_openDone = false;
        m_resumeRequested = false;
        m_abortRequested = false;
        m_active = false;
        m_openResult = CaptureStartResult::success();
    }

    start();

    std::unique_lock<std::mutex> lock(m_mutex);
    const bool done = m_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                      [this] { return m_openDone; });
    if (!done) {
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
    return m_openResult;
}

void FFScreenGrabThread::resume()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resumeRequested = true;
    }
    m_cond.notify_all();
}

void FFScreenGrabThread::requestDrainStop()
{
    m_stop.store(true, std::memory_order_release);
    m_cond.notify_all();
}

void FFScreenGrabThread::requestAbort()
{
    FFVFrameQueue *queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_abortRequested = true;
        queue = m_queue;
    }
    m_stop.store(true, std::memory_order_release);
    m_cond.notify_all();
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
    // Only after wait(): the source is owned by the (finished) grab thread.
    m_source.close();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active = false;
    m_queue = nullptr;
    m_recorder = nullptr;
    m_openGuard = nullptr;
}

bool FFScreenGrabThread::isActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

bool FFScreenGrabThread::gpuFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_gpuFrames;
}

int FFScreenGrabThread::width() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_width;
}

int FFScreenGrabThread::height() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_height;
}

QString FFScreenGrabThread::describe() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_description;
}

void FFScreenGrabThread::run()
{
    InputRequest request;
    std::function<bool()> shouldOpen;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        request = m_request;
        shouldOpen = m_openGuard;
    }

    CaptureStartResult result = CaptureStartResult::success();
    if (shouldOpen && !shouldOpen()) {
        CaptureError error;
        error.stage = CaptureErrorStage::Configuration;
        error.role = CaptureDeviceKind::Screen;
        error.backend = CaptureBackend::DesktopDuplication;
        error.ffmpegError = AVERROR_EXIT;
        error.userMessage = QStringLiteral("Capture start was cancelled.");
        result = CaptureStartResult::failure(std::move(error));
    } else {
        result = m_source.open(request);
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
    if (!result.ok) {
        m_source.close();
        return;
    }

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
    FFVFrameQueue *queue = nullptr;
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

void FFScreenGrabThread::grabLoop()
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        reportLoss(QStringLiteral("out of memory"));
        return;
    }
    FFVFrameQueue *queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        queue = m_queue;
    }

    std::uint64_t frames = 0;
    while (!m_stop.load(std::memory_order_acquire)) {
        // ddagrab paces itself to the frame rate (it sleeps until the next frame time and
        // duplicates the last frame on a static desktop), so this loop does not spin.
        const int ret = m_source.read(frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret >= 0) {
            queue->enqueue(frame); // moves the reference; blocks while the queue is full
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

bool FFScreenGrabThread::reopenAfterLoss()
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

void FFScreenGrabThread::reportLoss(const QString &reason)
{
    QObject *recorder = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        recorder = m_recorder;
    }
    if (!recorder || m_stop.load(std::memory_order_acquire)) {
        return;
    }
    QMetaObject::invokeMethod(recorder, "setCaptureError", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("Screen capture stopped: %1").arg(reason)),
                              Q_ARG(QString, QStringLiteral("Stop the recording (the file is kept up to "
                                                            "this point). For capture across UAC/lock "
                                                            "screen, set SUBREC_DDAGRAB_MODE=cpu or "
                                                            "SUBREC_CAPTURE_BACKEND=dshow.")));
}
