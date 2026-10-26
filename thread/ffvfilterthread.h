#ifndef FFVFILTERTHREAD_H
#define FFVFILTERTHREAD_H

#include "ffthread.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}
#include <condition_variable>

class FFVFrameQueue;
class FFVFilter;

class FFVFilterThread : public FFThread
{
public:
    FFVFilterThread();
    virtual ~FFVFilterThread() override;

    void init(FFVFrameQueue *frmQueue_, FFVFilter *filter_);
    void updateQueue(FFVFrameQueue *frmQueue_);

    void startEncoder();
    void stopEncoder();

    void openVideoSource(int sourceType);
    void closeVideoSource(int sourceType);

    void pauseEncoder();
    void onPauseChanged(bool pausedFlag, int64_t ts_us);
    void peekStart();
    void wakeAllThread();

protected:
    virtual void run() override;

private:
    FFVFrameQueue *frmQueue = nullptr;

    FFVFilter *filter = nullptr;

    bool eofFlag = false;

    std::atomic<bool> encoderFlag;
    std::atomic<bool> screenFlag;
    std::atomic<bool> cameraFlag;

    AVFrame *screenFrame = nullptr;
    AVFrame *lastVideoFrame = nullptr;

    int64_t overplayPts;

    std::mutex mutex;

    std::condition_variable cond;

    int64_t lastVideoTime = 0;
    std::atomic<bool> pauseFlag;
    std::atomic<int64_t> pauseTime;
    std::atomic<int64_t> lastPauseTime;
};

#endif // FFVFILTERTHREAD_H
