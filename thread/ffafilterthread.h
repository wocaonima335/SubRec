#ifndef FFAFILTERTHREAD_H
#define FFAFILTERTHREAD_H

#include "ffthread.h"

extern "C" {
#include <libavutil/time.h>
}

#include <condition_variable>
#include <cstdint>
#include <mutex>

class FFAFrameQueue;
class FFAFilter;
class AVFrame;

class FFAFilterThread : public FFThread
{
public:
    FFAFilterThread();
    virtual ~FFAFilterThread() override;

    void openAudioSource(int audioType);
    void closeAudioSource(int audioType);
    void init(FFAFrameQueue *micFrmQueue_, FFAFrameQueue *sysFrmQueue_, FFAFilter *filter_);

    void startEncoder();
    void stopEncoder();
    void pauseEncoder();
    void onPauseChanged(bool pausedFlag, int64_t ts_us);

    void setAudioVolume(double value);
    void setMicrophoneVolume(double value);

    bool peekStart();
    void wakeAllThread();

protected:
    virtual void run() override;

private:
    AVFrame *generateMuteFrame();

private:
    FFAFrameQueue *micFrmQueue = nullptr;
    FFAFrameQueue *sysFrmQueue = nullptr;
    FFAFilter *filter = nullptr;

    AVFrame *sysFrame = nullptr;
    AVFrame *micFrame = nullptr;

    std::atomic<bool> encoderFlag;
    std::atomic<bool> audioFlag;
    std::atomic<bool> microphoneFlag;
    std::atomic<bool> pauseFlag;

    std::mutex mutex;
    std::condition_variable cond;
    std::atomic<int64_t> pauseTime;
    std::atomic<int64_t> lastPauseTime;
    std::atomic<uint64_t> forwardedFrameCount{0};
    std::atomic<uint64_t> droppedFrameCount{0};
    std::atomic<uint64_t> muteFrameCount{0};
};

#endif // FFAFILTERTHREAD_H
