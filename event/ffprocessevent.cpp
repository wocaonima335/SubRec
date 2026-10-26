#include "ffprocessevent.h"

FFCaptureProcessEvent::FFCaptureProcessEvent(FFRecorder *recordCtx, double seconds_)
    : FFEvent(recordCtx)
    , seconds(seconds_)
{}

void FFCaptureProcessEvent::work()
{
    QString timeText = formatTime(seconds);
    updateCaptureTimeText(timeText);
}

QString FFCaptureProcessEvent::formatTime(double seconds)
{
    // 确保秒数不为负数
    if (seconds < 0) {
        seconds = 0;
    }

    int totalSeconds = static_cast<int>(seconds);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    int deciseconds = static_cast<int>((seconds - totalSeconds) * 10);

    // 格式化为 mm:ss.d 格式
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(deciseconds);
}

void FFCaptureProcessEvent::updateCaptureTimeText(const QString &timeText)
{
    QMetaObject::invokeMethod(recoderContext,
                              "setCaptureTimeText",
                              Qt::QueuedConnection,
                              Q_ARG(QString, timeText));
}
