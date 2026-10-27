#ifndef FFVPACKETQUEUE_H
#define FFVPACKETQUEUE_H

#include "ffboundedqueue.h"

extern "C" {
#include <libavformat/avformat.h>
}

class FFVPacketQueue
{
public:
    explicit FFVPacketQueue();
    ~FFVPacketQueue();

    MediaQueueReadResult<FFPacket> receive();
    FFPacket *dequeue();
    FFPacket *peekQueue() const;
    FFPacket *peekBack() const;
    void enqueue(AVPacket *pkt);
    void enqueueFlush();
    MediaQueueResult sendFinish();
    void receiveFinish();
    void enqueueNull();
    void flushQueue();
    bool tryEnqueue(AVPacket *pkt);
    MediaQueueReadResult<FFPacket> tryReceive();
    FFPacket *tryDequeue();

    size_t getSerial();

    bool isFinished() const;
    bool isClosed() const;
    MediaQueueCloseReason closeReason() const;
    int errorCode() const;
    void wakeAllThread();
    void clearQueue();
    void close(MediaQueueCloseReason reason = MediaQueueCloseReason::RequestedStop,
               int errorCode = 0);
    void start();
    int length();
    MediaQueueDiagnostics diagnostics() const { return impl->diagnostics(); }

private:
    std::atomic<size_t> serial;
    FFBoundedQueue<FFPacket, FFPacketTraits> *impl;
};

#endif
