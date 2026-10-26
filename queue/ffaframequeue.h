#ifndef FFAFRAMEQUEUE_H
#define FFAFRAMEQUEUE_H
#include "ffboundedqueue.h"

class FFAFrameQueue
{
public:
    explicit FFAFrameQueue();
    ~FFAFrameQueue();

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
    FFBoundedQueue<AVFrame, AVFrameTraits> *impl;
};
#endif
