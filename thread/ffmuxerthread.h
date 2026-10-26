#ifndef FFMUXERTHREAD_H
#define FFMUXERTHREAD_H

#include "ffthread.h"

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
    int lastError() const { return errorCode.load(); }

protected:
    virtual void run() override;

private:
    void sendCaptureProcessEvent(double seconds);
    void fail(int error);

private:
    FFAPacketQueue *aPktQueue = nullptr;
    FFVPacketQueue *vPktQueue = nullptr;

    FFMuxer *muxer = nullptr;

    std::atomic<int> errorCode{0};

    double lastProcessTime = 0;

    FFRecorder *recorderCtx = nullptr;
};

#endif // FFMUXERTHREAD_H
