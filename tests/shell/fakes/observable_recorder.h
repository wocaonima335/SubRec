#pragma once
// TEST ONLY: the production ShellStateSource is compiled against this observable recorder.
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
class FFRecorder : public QObject {
    Q_OBJECT
public:
    QString state = "Idle", time = "00:12:34", error, path = "/recordings", file = "example.mp4";
    QVariantList devices{QVariantMap{{"kind", "screen"}, {"available", true}}};
    QString captureState() const { return state; }
    QString captureTimeText() const { return time; }
    QString captureError() const { return error; }
    QVariantList captureDevices() const { return devices; }
    QString outputPath() const { return path; }
    QString outputFileName() const { return file; }
    void setState(const QString &s) { state = s; emit captureStateChanged(); }
signals:
    void captureStateChanged();
    void captureTimeTextChanged();
    void captureErrorChanged();
    void captureDevicesChanged();
    void outputPathChanged();
    void outputFileNameChanged();
};
