#ifndef FFCLOSESOURCEEVENT_H
#define FFCLOSESOURCEEVENT_H

#include "ffevent.h"
#include "thread/stop_deadline.h"
class FFThread;

class FFCloseSourceEvent : public FFEvent
{
public:
    FFCloseSourceEvent(FFRecorder *recordCtx, enum FFRecordContextType::demuxerType sourceType_);

    virtual void work() override;
    bool completed() const { return completed_; }

private:
    bool waitForWorker(FFThread *worker, const std::function<void()> &cancel, const char *name);
    void close();
    void closeAudioSourceChain();
    void closeVideoSourceChain();
    void drainAudioEncoderPath();
    void drainVideoEncoderPath();
    void stopAudioInputPath();
    void stopVideoInputPath();

private:
    bool completed_ = true;
    const std::chrono::milliseconds stopTimeout = capture_stop::timeout();
    int sourceType;
    int index;
};

#endif // FFCLOSESOURCEEVENT_H
