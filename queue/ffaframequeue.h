#ifndef FFAFRAMEQUEUE_H
#define FFAFRAMEQUEUE_H
#include "ffboundedqueue.h"
#include <memory>

class FFAFrameQueue
{
public:
    explicit FFAFrameQueue();
    ~FFAFrameQueue();
    FFAFrameQueue(const FFAFrameQueue &) = delete;
    FFAFrameQueue &operator=(const FFAFrameQueue &) = delete;
    // A timed-out WASAPI producer retains the storage, never this recorder-owned wrapper.
    using State = FFBoundedQueue<AVFrame, AVFrameTraits>;
    std::shared_ptr<State> sharedState() const { return impl; }

    MediaQueueReadResult<AVFrame> receive();
    MediaQueueReadResult<AVFrame> tryReceive();
    void enqueue(AVFrame *srcFrame);
    MediaQueueResult sendFinish();
    void receiveFinish();
    void enqueueNull();
    AVFrame *dequeue();

    void wakeAllThread();
    void clearQueue();
    void flushQueue();
    void close(MediaQueueCloseReason reason = MediaQueueCloseReason::RequestedStop,
               int errorCode = 0);
    void start();
    bool peekEmpty();
    bool isFinished() const;
    bool isClosed() const;
    MediaQueueCloseReason closeReason() const;
    int errorCode() const;

private:
    AVFrame *peekQueue();
    std::shared_ptr<State> impl;
};
#endif
