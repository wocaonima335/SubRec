#include "ffaframequeue.h"

#define MAX_FRAME_SIZE 3

FFAFrameQueue::FFAFrameQueue()
    : impl(new FFBoundedQueue<AVFrame, AVFrameTraits>(30))
{}

FFAFrameQueue::~FFAFrameQueue()
{
    close();
    delete impl;
}

MediaQueueReadResult<AVFrame> FFAFrameQueue::receive()
{
    return impl->receive();
}

MediaQueueReadResult<AVFrame> FFAFrameQueue::tryReceive() { return impl->tryReceive(); }

void FFAFrameQueue::enqueue(AVFrame *srcFrame)
{
    impl->enqueueFromSrc(srcFrame);
}

MediaQueueResult FFAFrameQueue::sendFinish()
{
    qDebug() << "[FFAFrameQueue] sendFinish";
    return impl->sendFinish();
}

void FFAFrameQueue::receiveFinish()
{
    qDebug() << "[FFAFrameQueue] receiveFinish";
    impl->receiveFinish();
}

void FFAFrameQueue::enqueueNull()
{
    sendFinish();
}

AVFrame *FFAFrameQueue::dequeue()
{
    return receive().item;
}

void FFAFrameQueue::wakeAllThread()
{
    impl->wakeAllThread();
}

void FFAFrameQueue::clearQueue()
{
    impl->clearQueue();
}

void FFAFrameQueue::flushQueue()
{
    impl->flushQueue();
}

void FFAFrameQueue::close(MediaQueueCloseReason reason, int errorCode)
{
    qDebug() << "[FFAFrameQueue] close reason=" << mediaQueueCloseReasonName(reason)
             << "errorCode=" << errorCode;
    impl->close(reason, errorCode);
}

void FFAFrameQueue::start()
{
    impl->start();
}

bool FFAFrameQueue::peekEmpty()
{
    return impl->peekEmpty();
}

bool FFAFrameQueue::isFinished() const
{
    return impl->isFinished();
}

bool FFAFrameQueue::isClosed() const
{
    return impl->isClosed();
}

MediaQueueCloseReason FFAFrameQueue::closeReason() const
{
    return impl->closeReason();
}

int FFAFrameQueue::errorCode() const
{
    return impl->errorCode();
}

AVFrame *FFAFrameQueue::peekQueue()
{
    return impl->peekQueue();
}
