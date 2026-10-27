#ifndef FFMUXERTHREAD_H
#define FFMUXERTHREAD_H

#include "ffthread.h"
#include "stop_deadline.h"

extern "C" {
#include <libavformat/avformat.h>
}

class FFAPacketQueue;
class FFVPacketQueue;
class FFMuxer;
class FFAEncoder;
class FFVEncoder;
class FFPacket;
class FFRecorder;

class FFMuxerThread : public FFThread
{
public:
    FFMuxerThread();
    ~FFMuxerThread() override;

    void init(FFAPacketQueue *aPktQueue_,
              FFVPacketQueue *vPktQueue_,
              FFMuxer *muxer_,
              FFAEncoder *aEncoder_,
              FFVEncoder *vEncoder_,
              FFRecorder *recorder_);

    void close();
    void wakeAllThread();
    void requestAbort(); // No join; cancel header and packet-queue waits.
    void requestDrainStop(std::chrono::milliseconds timeout = capture_stop::timeout());
    void finishQueuedPackets(); // Seal producers, keep queued packets and write trailer.
    bool drainTimedOut() const { return drainExpired.load(); }
    struct Diagnostics {
        uint64_t bootstrapMaxPackets = 0, bootstrapHoldNs = 0;
        uint64_t enqueueBlockedNs = 0, droppedPackets = 0;
    };
    Diagnostics diagnostics() const;
    int lastError() const { return errorCode.load(); }

protected:
    virtual void run() override;

private:
    void sendCaptureProcessEvent(double seconds);
    void fail(int error);
    void checkDrainDeadline();
    template<class Queue> auto receivePacket(Queue *queue);

private:
    FFAPacketQueue *aPktQueue = nullptr;
    FFVPacketQueue *vPktQueue = nullptr;

    FFMuxer *muxer = nullptr;

    std::atomic<int> errorCode{0};
    std::atomic<int64_t> drainDeadlineMs{0};
    std::atomic<bool> drainExpired{false};

    std::atomic<uint64_t> bootstrapMaxPackets{0}, bootstrapHoldNs{0}, droppedPackets{0};
    bool diagnosticsLogged = false; // control thread, close after join
    double lastProcessTime = 0;

    FFRecorder *recorderCtx = nullptr;
};

#endif // FFMUXERTHREAD_H
