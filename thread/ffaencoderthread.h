#ifndef FFAENCODERTHREAD_H
#define FFAENCODERTHREAD_H

#include "ffthread.h"

#include <condition_variable>
#include <mutex>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

class FFAEncoder;
class FFAFrameQueue;
class FFMuxer;
class FFAFilter;
class RecorderSyncQueue;

class FFAEncoderThread : public FFThread
{
public:
    FFAEncoderThread();
    virtual ~FFAEncoderThread() override;

    void init(FFAFilter *aFilter_,
              FFAEncoder *aEncoder_,
              FFMuxer *muxer_,
              FFAFrameQueue *frmQueue_,
              RecorderSyncQueue *syncQueue_);

    void requestDrainStop();
    void requestAbort();
    void close();
    void wakeAllThread();
    void setStartTimeUs(int64_t us) { start_time_us = us; }

    void onPauseChanged(bool pauseFlag, int64_t ts_us);

protected:
    virtual void run() override;

private:
    void initEncoder(AVFrame *frame);
    void drainReadyAudioFrames();

private:
    FFAEncoder *aEncoder = nullptr;
    FFAFrameQueue *frmQueue = nullptr;
    FFMuxer *muxer = nullptr;
    AVRational audioTimeBase = {0, 1};
    int streamIndex = -1;
    FFAFilter *aFilter = nullptr;
    RecorderSyncQueue *syncQueue = nullptr;
    int64_t start_time_us = 0;
    int64_t last_apts = AV_NOPTS_VALUE;
    int64_t next_apts = AV_NOPTS_VALUE;
    std::atomic<bool> paused{false};
    int64_t pause_start_us{0};
    int64_t pause_accum_us{0};
    std::mutex pause_mutex;
    std::condition_variable pause_cv;
};

#endif // FFAENCODERTHREAD_H
