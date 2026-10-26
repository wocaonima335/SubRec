#ifndef FFAPACKETQUEUE_H
#define FFAPACKETQUEUE_H

#include "ffboundedqueue.h"

extern "C" {
#include <libavformat/avformat.h>
}

class FFAPacketQueue
{
public:
    explicit FFAPacketQueue();
    ~FFAPacketQueue();

    MediaQueueReadResult<FFPacket> receive();
    FFPacket *dequeue();
    FFPacket *peekQueue() const;
    FFPacket *peekBack() const;
    MediaQueueReadResult<FFPacket> tryReceive();
    FFPacket *tryDequeue();
    void enqueue(AVPacket *pkt);
    void enqueueFlush();
    MediaQueueResult sendFinish();
    void receiveFinish();
    void enqueueNull();
    void flushQueue();

    size_t getSerial();
    void clearQueue();
    bool isFinished() const;
    bool isClosed() const;
    MediaQueueCloseReason closeReason() const;
    int errorCode() const;
    void wakeAllThread();
    void close(MediaQueueCloseReason reason = MediaQueueCloseReason::RequestedStop,
               int errorCode = 0);
    void start();
    int length();

private:
    std::atomic<size_t> serial;
    FFBoundedQueue<FFPacket, FFPacketTraits> *impl;
};
#endif
