#ifndef FFPROCESSEVENT_H
#define FFPROCESSEVENT_H

#include <QString>
#include "ffevent.h"

class FFCaptureProcessEvent : public FFEvent
{
public:
    FFCaptureProcessEvent(FFRecorder *recordCtx, double seconds_);

    virtual void work() override;

private:
    QString formatTime(double seconds);
    void updateCaptureTimeText(const QString &timeText);

private:
    double seconds;
};

#endif // FFPROCESSEVENT_H
