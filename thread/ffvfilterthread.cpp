#include "logging/frame_log.h"
#include "ffvfilterthread.h"

#include "filter/ffvfilter.h"
#include "queue/ffvframequeue.h"
#include "recorder/ffrecorder.h"

#include <iostream>

FFVFilterThread::FFVFilterThread()
{
    encoderFlag.store(false);
    screenFlag.store(false);
    pauseFlag.store(false);

    pauseTime = 0;
    lastPauseTime = 0;
}

FFVFilterThread::~FFVFilterThread()
{
    if (screenFrame) {
        AVFrameTraits::release(screenFrame);
    }

    if (lastVideoFrame) {
        AVFrameTraits::release(lastVideoFrame);
    }
}

void FFVFilterThread::init(FFVFrameQueue *frmQueue_, FFVFilter *filter_)
{
    frmQueue = frmQueue_;
    filter = filter_;
}

void FFVFilterThread::updateQueue(FFVFrameQueue *frmQueue_)
{
    std::lock_guard<std::mutex> lock(mutex);
    frmQueue = frmQueue_;
}

void FFVFilterThread::startEncoder()
{
    std::lock_guard<std::mutex> lock(mutex);
    encoderFlag.store(true);
    cond.notify_one();
}

void FFVFilterThread::stopEncoder()
{
    std::lock_guard<std::mutex> lock(mutex);
    encoderFlag.store(false);
    pauseFlag.store(false);
    pauseTime = 0;
    lastPauseTime = 0;
    cond.notify_one();
}

void FFVFilterThread::openVideoSource(int sourceType)
{
    std::lock_guard<std::mutex> lock(mutex);

    enum demuxerType type = static_cast<demuxerType>(sourceType);
    switch (type) {
    case SCREEN:
        screenFlag.store(true);
        break;
    case CAMERA:
        cameraFlag.store(true);
        break;
    default:
        break;
    }

    cond.notify_one();
}

void FFVFilterThread::closeVideoSource(int sourceType)
{
    std::lock_guard<std::mutex> lock(mutex);
    enum demuxerType type = static_cast<demuxerType>(sourceType);

    switch (type) {
    case SCREEN:
        screenFlag.store(false);
        break;
    case CAMERA:
        cameraFlag.store(false);
        break;

    default:
        break;
    }

    cond.notify_one();
}

void FFVFilterThread::pauseEncoder()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (pauseFlag.load()) {
        pauseTime += av_gettime_relative() - lastPauseTime;
        pauseFlag.store(false);
    } else {
        pauseFlag.store(true);
        lastPauseTime = av_gettime_relative();
    }
}

void FFVFilterThread::onPauseChanged(bool pausedFlag, int64_t ts_us)
{
    std::lock_guard<std::mutex> lock(mutex);
    const int64_t now = ts_us > 0 ? ts_us : av_gettime_relative();
    if (pausedFlag) {
        if (pauseFlag.load(std::memory_order_acquire)) {
            return;
        }
        pauseFlag.store(true, std::memory_order_release);
        lastPauseTime.store(now, std::memory_order_release);
    } else {
        if (!pauseFlag.load(std::memory_order_acquire)) {
            return;
        }
        pauseTime.store(pauseTime.load(std::memory_order_acquire)
                            + (now - lastPauseTime.load(std::memory_order_acquire)),
                        std::memory_order_release);
        pauseFlag.store(false, std::memory_order_release);
    }
    cond.notify_all();
}

void FFVFilterThread::peekStart() {}

void FFVFilterThread::wakeAllThread()
{
    if (frmQueue) {
        frmQueue->close(MediaQueueCloseReason::RequestedStop);
    }
    cond.notify_all();
}

void FFVFilterThread::run()
{
    while (!m_stop) {
        bool hasScreen = screenFlag.load();
        bool hasEncoder = encoderFlag.load();
        bool hasPause = pauseFlag.load();
        bool hascamera = cameraFlag.load();

        if ((!hasScreen && !hascamera) || !hasEncoder) {
            std::unique_lock<std::mutex> lock(mutex);
            qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] idle, waiting (camera=" << hascamera
                     << ", screen=" << hasScreen << ", encoder=" << hasEncoder
                     << ", pause=" << hasPause << ")";
            cond.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }

        auto start = av_gettime_relative();

        FFVFrameQueue *localQueue = nullptr;
        FFVFilter *localFilter = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            localQueue = frmQueue;
            localFilter = filter;
        }
        qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] ready to dequeue frame (encoder=1, pause="
                 << hasPause << ")";
        auto received = localQueue ? localQueue->receive()
                                   : MediaQueueReadResult<AVFrame>{
                                         MediaQueueResult::Closed,
                                         nullptr,
                                         MediaQueueCloseReason::RequestedStop,
                                         0};
        if (received.result == MediaQueueResult::Eof) {
            if (localFilter) {
                localFilter->sendFinish();
            }
            break;
        }
        if (received.result != MediaQueueResult::Ok || received.item == nullptr) {
            qDebug() << "[FFVFilterThread] receive exit result="
                     << mediaQueueResultName(received.result);
            break;
        }
        AVFrame *encodeFrame = received.item;
        auto end = av_gettime_relative();
        int64_t duration = (end - start) * 10;
        qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] dequeue done, frame=" << encodeFrame
                 << ", wait_us=" << (end - start);

        if (hasPause) {
            AVFrameTraits::release(encodeFrame);
            continue;
        }

        overplayPts = av_gettime_relative() * 10 + duration - pauseTime * 10;
        qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] computed pts (100ns units)=" << overplayPts
                 << ", pauseTime(100ns)=" << (pauseTime.load() * 10);

        encodeFrame->pts = overplayPts;
        qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] set frame->pts and forwarding to encoder queue";
        if (localFilter) {
            localFilter->sendEncodeFrame(encodeFrame);
            AVFrameTraits::release(encodeFrame);
        } else {
            AVFrameTraits::release(encodeFrame);
        }
        qCDebug(subrec::logging::frameLog) << "[FFVFilterThread] forwarded frame to encoderFrameQueue via "
                    "FFVFilter::sendEncodeFrame";
    }
}
