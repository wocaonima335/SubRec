#ifndef FFCLOSESOURCEEVENT_H
#define FFCLOSESOURCEEVENT_H

#include "ffevent.h"

class FFCloseSourceEvent : public FFEvent
{
public:
    FFCloseSourceEvent(FFRecorder *recordCtx, enum FFRecordContextType::demuxerType sourceType_);

    virtual void work() override;

private:
    void close();
    void closeAudioSourceChain();
    void closeVideoSourceChain();
    void drainAudioEncoderPath();
    void drainVideoEncoderPath();
    void stopAudioInputPath();
    void stopVideoInputPath();

private:
    int sourceType;
    int index;
};

#endif // FFCLOSESOURCEEVENT_H
