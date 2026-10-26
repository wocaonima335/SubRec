#ifndef FFAFILTERTHREAD_H
#define FFAFILTERTHREAD_H

#include "ffthread.h"

extern "C" {
#include <libavutil/time.h>
}

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <deque>
#include <utility>

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

    void configureMix(bool enabled, int64_t startUs = 0) { mixing = enabled; mixStartUs = startUs; }
    bool isMixing() const { return mixing; }
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
    void runMixed();
    void recordMixPause(bool paused, int64_t timestampUs); // mutex held

private:
    std::atomic<bool> mixing{false};
    int64_t mixStartUs = 0;
    std::deque<std::pair<int64_t, int64_t>> mixPauses;
    std::atomic<double> systemGain{1}, microphoneGain{1};
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
