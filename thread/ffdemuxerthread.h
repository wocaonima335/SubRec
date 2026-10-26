#ifndef FFDEMUXERTHREAD_H
#define FFDEMUXERTHREAD_H

#include "ffthread.h"

#include <condition_variable>
#include <mutex>
#include <functional>

class Demuxer;
class FFAPacketQueue;
class FFVPacketQueue;
class FFPlayerContext;

class FFDemuxerThread : public FFThread
{
public:
    FFDemuxerThread();
    virtual ~FFDemuxerThread() override;

    void init(Demuxer *demuxer_);
    bool prepareOpenAndWait(int timeoutMs = 5000,
                            const std::function<bool()> &shouldOpen = {});
    void resumePreparedDemux();
    void requestDrainStop();
    void requestAbort();
    void wakeAllThread();

    void close();

    bool peekStop();

protected:
    virtual void run() override;

private:
    Demuxer *demuxer = nullptr;
    FFPlayerContext *playerCtx = nullptr;
    std::condition_variable cond;
    std::mutex mutex;
    std::atomic<bool> stopFlag;
    std::function<bool()> openGuard;
    bool prepareOpenRequested = false;
    bool prepareOpenReady = false;
    bool prepareOpenSuccess = false;
    bool waitForResume = false;
    bool resumeRequested = false;
};

#endif // FFDEMUXERTHREAD_H
