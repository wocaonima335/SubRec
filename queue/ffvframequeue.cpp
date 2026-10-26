#include "ffvframequeue.h"

#define MAX_FRAME_SIZE 2

FFVFrameQueue::FFVFrameQueue()
    : impl(new FFBoundedQueue<AVFrame, AVFrameTraits>(30))
{
    m_lastStatsTime = std::chrono::steady_clock::now();
}

FFVFrameQueue::~FFVFrameQueue()
{
    close();
    delete impl;
}

MediaQueueReadResult<AVFrame> FFVFrameQueue::receive()
{
    return impl->receive();
}

void FFVFrameQueue::enqueue(AVFrame *srcFrame)
{
    // auto start = std::chrono::steady_clock::now();
    qDebug() << "[FFVFrameQueue] enqueue enter";
    impl->enqueueFromSrc(srcFrame);
    qDebug() << "[FFVFrameQueue] enqueue leave";
    // auto end = std::chrono::steady_clock::now();

    m_enqueueCount.fetch_add(1, std::memory_order_relaxed);
}

MediaQueueResult FFVFrameQueue::sendFinish()
{
    qDebug() << "[FFVFrameQueue] sendFinish";
    return impl->sendFinish();
}

void FFVFrameQueue::receiveFinish()
{
    qDebug() << "[FFVFrameQueue] receiveFinish";
    impl->receiveFinish();
}

AVFrame *FFVFrameQueue::dequeue()
{
    // auto start = std::chrono::steady_clock::now();
    AVFrame *frame = receive().item;
    // auto end = std::chrono::steady_clock::now();

    if (frame) {
        m_dequeueCount.fetch_add(1, std::memory_order_relaxed);
    }

    // auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // if (wait_ms > 50) { // 超过50ms警告
    //     std::cerr << "[VFrameQueue] dequeue blocked for " << wait_ms
    //               << "ms, queue_len=" << impl->length() << std::endl;
    // }

    return frame;
}

void FFVFrameQueue::wakeAllThread()
{
    impl->wakeAllThread();
}

void FFVFrameQueue::clearQueue()
{
    impl->clearQueue();
}

void FFVFrameQueue::enqueueNull()
{
    sendFinish();
}

void FFVFrameQueue::flushQueue()
{
    impl->flushQueue();
}

void FFVFrameQueue::close(MediaQueueCloseReason reason, int errorCode)
{
    qDebug() << "[FFVFrameQueue] close reason=" << mediaQueueCloseReasonName(reason)
             << "errorCode=" << errorCode;
    impl->close(reason, errorCode);
}

void FFVFrameQueue::start()
{
    impl->start();
}

bool FFVFrameQueue::peekEmpty()
{
    return impl->peekEmpty();
}

bool FFVFrameQueue::isFinished() const
{
    return impl->isFinished();
}

bool FFVFrameQueue::isClosed() const
{
    return impl->isClosed();
}

MediaQueueCloseReason FFVFrameQueue::closeReason() const
{
    return impl->closeReason();
}

int FFVFrameQueue::errorCode() const
{
    return impl->errorCode();
}

AVFrame *FFVFrameQueue::peekQueue()
{
    return impl->peekQueue();
}

void FFVFrameQueue::printQueueStats() const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastStatsTime).count();

    if (elapsed >= 5) { // 每5秒打印一次
        size_t enqueued = m_enqueueCount.load();
        size_t dequeued = m_dequeueCount.load();

        qDebug() << "[VFrameQueue] Stats: enqueued=" << enqueued << ", dequeued=" << dequeued
                 << ", queue_len=" << impl->length();

        m_lastStatsTime = now;
    }
}

void FFVFrameQueue::resetStats()
{
    m_enqueueCount.store(0, std::memory_order_relaxed);
    m_dequeueCount.store(0, std::memory_order_relaxed);
    m_lastStatsTime = std::chrono::steady_clock::now();
}
