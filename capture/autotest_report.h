#pragma once
#include "autotest_result.h"
#include <QString>

inline QString autoTestRecordingReport(AutoTestExit result, const QString &file,
                                       qint64 bytes, const QString &encoder,
                                       bool hardware, const QString &resolution,
                                       const QString &encoderDetail, const QString &captureError)
{
    return QStringLiteral("[AutoTest] result=%1 file=%2 bytes=%3 encoder=%4 "
                          "hardware=%5 resolution=%6 encoderDetail=%7 captureError=%8")
        .arg(result == AutoTestOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             file, QString::number(bytes), encoder,
             hardware ? QStringLiteral("yes") : QStringLiteral("no"),
             resolution, encoderDetail, captureError);
}
