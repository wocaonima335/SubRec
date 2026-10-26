#ifndef FFADECODERTHREAD_H
#define FFADECODERTHREAD_H

#include "ffthread.h"

class FFAPacketQueue;
class FFADecoder;

class FFADecoderThread : public FFThread
{
public:
    FFADecoderThread();
    virtual ~FFADecoderThread() override;

    void init(FFADecoder *aDecoder_, FFAPacketQueue *aPktQueue_);
    void requestDrainStop();
    void requestAbort();
    void wakeAllThread();
    void close();

    bool peekStop();

protected:
    virtual void run() override;

private:
    FFAPacketQueue *aPktQueue = nullptr;
    FFADecoder *aDecoder = nullptr;
    std::atomic<bool> stopFlag;
};

#endif // FFADECODERTHREAD_H
