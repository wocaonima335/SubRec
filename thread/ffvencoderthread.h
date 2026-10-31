#ifndef FFVENCODERTHREAD_H
#define FFVENCODERTHREAD_H

#include "ffthread.h"
#include "queue/video_tick_scheduler.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
}

#include <mutex>

class FFVEncoder;
struct VideoEncoderSelection;
class FFVFrameQueue;
class FFMuxer;
class FFVFilter;
class FFGLItem;
class RecorderSyncQueue;
class FFRecorder;

class FFVEncoderThread : public FFThread
{
public:
    FFVEncoderThread();
    virtual ~FFVEncoderThread() override;

    void init(FFVFilter *vFilter_,
              FFVEncoder *vEncoder_,
              FFMuxer *muxer_,
              FFVFrameQueue *frmQueue_,
              RecorderSyncQueue *syncQueue_,
              FFRecorder *recorder_);
    void requestDrainStop();
    void requestAbort();
    void wakeAllThread();
    void close();

    // Configure before start; clock is session-owned and outlives this worker.
    void setMediaClock(MediaClock *clock) { mediaClock = clock; }
    // Future capture supervisor notification; requests are executed on this worker.
    void setCaptureStalled(bool stalled) { captureStalled.store(stalled); }
    uint64_t missingCaptureStamps() const { return missingStamps.load(); }
    void onPauseChanged(bool pausedFlag, int64_t ts_us);

protected:
    virtual void run() override;

private:
    bool initEncoder(AVFrame *frame);
    void failEncoding(int error);
    void failTimestamp(const char *reason);
    void publishEncoderSelection(const VideoEncoderSelection &selection);
    void sendPreviewData(AVFrame *frame);

private:
    FFVEncoder *vEncoder = nullptr;
    FFVFrameQueue *frmQueue = nullptr;
    FFMuxer *muxer = nullptr;
    FFVFilter *vFilter = nullptr;
    RecorderSyncQueue *syncQueue = nullptr;
    FFRecorder *recorder = nullptr;

    int streamIndex = -1;
    AVRational timeBase{};
    AVRational frameRate{};
    MediaClock *mediaClock = nullptr;
    VideoTickScheduler ticks;
    std::atomic<bool> captureStalled{false};
    std::atomic<uint64_t> missingStamps{0};
    std::atomic<int64_t> minimumCaptureUs{AV_NOPTS_VALUE};

    std::atomic<bool> paused{false};
    std::atomic<int64_t> pause_accum_us{0};
    std::atomic<bool> first_after_resume{false};
    std::mutex pause_mutex;
    int64_t pause_start_us = 0;
};

#endif // FFVENCODERTHREAD_H
