#pragma once

#include <QObject>
#include <QString>
#include <QThread>

// Real Qt delivery, with only the recorder/UI boundary replaced. Do not stop
// the worker here: the test independently checks that it exits on encode error.
class FFRecorder : public QObject
{
    Q_OBJECT
public:
    QString captureError;
    int errorCalls = 0;
    int stopRequests = 0;
    bool errorBeforeStop = false;
    bool deliveredOnRecorderThread = true;

public slots:
    void setCaptureError(const QString &error, const QString &)
    {
        deliveredOnRecorderThread &= QThread::currentThread() == thread();
        captureError = error;
        ++errorCalls;
    }
    void stopOnCaptureError()
    {
        deliveredOnRecorderThread &= QThread::currentThread() == thread();
        errorBeforeStop = !captureError.isEmpty();
        ++stopRequests;
    }
    void setSelectedVideoEncoder(const QString &, bool, const QString &) {}
};
