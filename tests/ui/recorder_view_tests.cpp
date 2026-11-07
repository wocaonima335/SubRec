#include <QtTest>
#include "ui/backend/recorder_view.h"
#include <QFile>
#include <QTemporaryDir>
using namespace subrec::ui;
class FakeRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString captureState MEMBER captureState NOTIFY captureStateChanged)
    Q_PROPERTY(bool canStartCapture MEMBER canStartCapture NOTIFY canStartCaptureChanged)
    Q_PROPERTY(QString captureError MEMBER captureError NOTIFY captureErrorChanged)
    Q_PROPERTY(QString captureRemediation MEMBER captureRemediation NOTIFY captureRemediationChanged)
    Q_PROPERTY(QString captureTimeText MEMBER captureTimeText NOTIFY captureTimeTextChanged)
    Q_PROPERTY(QString outputPath MEMBER outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(QString outputFileName MEMBER outputFileName NOTIFY outputFileNameChanged)
    Q_PROPERTY(QString videoResolution MEMBER videoResolution NOTIFY videoResolutionChanged)
    Q_PROPERTY(QString selectedVideoEncoder MEMBER selectedVideoEncoder NOTIFY selectedVideoEncoderChanged)
    Q_PROPERTY(bool selectedVideoEncoderIsHardware MEMBER selectedVideoEncoderIsHardware NOTIFY selectedVideoEncoderIsHardwareChanged)
    Q_PROPERTY(QString selectedVideoEncoderDetail MEMBER selectedVideoEncoderDetail NOTIFY selectedVideoEncoderDetailChanged)
    Q_PROPERTY(QString audioMode MEMBER audioMode NOTIFY audioModeChanged)
    Q_PROPERTY(bool audioMixingAvailable MEMBER audioMixingAvailable NOTIFY audioMixingAvailableChanged)
    Q_PROPERTY(QString selectedMicrophoneDeviceId MEMBER selectedMicrophoneDeviceId NOTIFY selectedMicrophoneDeviceIdChanged)
    Q_PROPERTY(QString mixMicrophoneDeviceId MEMBER mixMicrophoneDeviceId NOTIFY mixMicrophoneDeviceIdChanged)
    Q_PROPERTY(QVariantList captureDevices MEMBER captureDevices NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedVideoDeviceId MEMBER selectedVideoDeviceId NOTIFY selectedVideoDeviceIdChanged)
    Q_PROPERTY(QString selectedAudioDeviceId MEMBER selectedAudioDeviceId NOTIFY selectedAudioDeviceIdChanged)
    Q_PROPERTY(QString screenCaptureBackend MEMBER screenCaptureBackend NOTIFY screenCaptureBackendChanged)
    Q_PROPERTY(bool screenCaptureBackendFromEnvironment MEMBER screenCaptureBackendFromEnvironment NOTIFY screenCaptureBackendFromEnvironmentChanged)
    Q_PROPERTY(QObject* audioSampler MEMBER audioSampler NOTIFY audioSamplerChanged)
public:
    QString captureState = "Idle";
    bool canStartCapture = true;
    QString captureError = {};
    QString captureRemediation = {};
    QString captureTimeText = "00:01.0";
    QString outputPath = "/tmp";
    QString outputFileName = "session.mp4";
    QString videoResolution = "1920x1080";
    QString selectedVideoEncoder = "h264";
    bool selectedVideoEncoderIsHardware = true;
    QString selectedVideoEncoderDetail = "test";
    QString audioMode = "both";
    bool audioMixingAvailable = true;
    QString selectedMicrophoneDeviceId = "mic";
    QString mixMicrophoneDeviceId = "mix";
    QVariantList captureDevices = {};
    QString selectedVideoDeviceId = "screen";
    QString selectedAudioDeviceId = "audio";
    QString screenCaptureBackend = "auto";
    bool screenCaptureBackendFromEnvironment = true;
    QObject* audioSampler = nullptr;
    QString lastMethod;
    QString lastValue;
    Q_INVOKABLE void refreshCaptureDevices()
    {
        lastMethod = "refresh";
    }
    Q_INVOKABLE void clearCaptureError()
    {
        lastMethod = "clear";
    }
    Q_INVOKABLE bool selectVideoCaptureDevice(const QString &value)
    {
        lastMethod = "selectVideoCaptureDevice";
        lastValue = value;
        return true;
    }
    Q_INVOKABLE bool selectAudioCaptureDevice(const QString &value)
    {
        lastMethod = "selectAudioCaptureDevice";
        lastValue = value;
        return true;
    }
    Q_INVOKABLE bool selectMicrophoneCaptureDevice(const QString &value)
    {
        lastMethod = "selectMicrophoneCaptureDevice";
        lastValue = value;
        return true;
    }
    Q_INVOKABLE bool setAudioMode(const QString &value)
    {
        lastMethod = "setAudioMode";
        lastValue = value;
        return true;
    }
    Q_INVOKABLE bool setScreenCaptureBackend(const QString &value)
    {
        lastMethod = "setScreenCaptureBackend";
        lastValue = value;
        return true;
    }
signals:
    void captureStateChanged(QString value);
    void canStartCaptureChanged(bool value);
    void captureErrorChanged(QString value);
    void captureRemediationChanged(QString value);
    void captureTimeTextChanged(QString value);
    void outputPathChanged(QString value);
    void outputFileNameChanged(QString value);
    void videoResolutionChanged(QString value);
    void selectedVideoEncoderChanged(QString value);
    void selectedVideoEncoderIsHardwareChanged(bool value);
    void selectedVideoEncoderDetailChanged(QString value);
    void audioModeChanged(QString value);
    void audioMixingAvailableChanged(bool value);
    void selectedMicrophoneDeviceIdChanged(QString value);
    void mixMicrophoneDeviceIdChanged(QString value);
    void captureDevicesChanged(QVariantList value);
    void selectedVideoDeviceIdChanged(QString value);
    void selectedAudioDeviceIdChanged(QString value);
    void screenCaptureBackendChanged(QString value);
    void screenCaptureBackendFromEnvironmentChanged(bool value);
    void audioSamplerChanged(QObject* value);
};

class RecorderViewTests : public QObject {
    Q_OBJECT
private slots:

    void forwardingAndDefaults()
    {
        RecorderView view(nullptr);
        QCOMPARE(view.uiState(), QString("noDevice"));
        QCOMPARE(view.elapsedText(), QString("00:00:00"));
        QVERIFY(!view.capturing());
        QVERIFY(!view.selectVideoCaptureDevice("screen"));
        FakeRecorder recorder;
        recorder.captureDevices = {QVariantMap{{"id", "screen"}, {"kind", "screen"}, {"available", true}},
                                   QVariantMap{{"id", "cam"}, {"kind", "camera"}, {"available", false}}};
        bool screen = false;
        view.attach(&recorder, [&](bool value) { screen = value; });
        for (int i = FakeRecorder::staticMetaObject.propertyOffset(); i < recorder.metaObject()->propertyCount(); ++i) {
            const char *name = recorder.metaObject()->property(i).name();
            QCOMPARE(view.property(name), recorder.property(name));
        }
        QCOMPARE(view.uiState(), QString("idle"));
        QCOMPARE(view.selectedVideoKind(), QString("screen"));
        QCOMPARE(view.devicesOfKind("camera").size(), 1);
        QCOMPARE(view.elapsedText(), QString("00:00:01"));
        view.openSource("screen");
        QVERIFY(screen);
        QVERIFY(view.selectVideoCaptureDevice("cam"));
        QCOMPARE(recorder.lastValue, QString("cam"));
        QVERIFY(view.selectAudioCaptureDevice("audio"));
        QVERIFY(view.selectMicrophoneCaptureDevice("mic"));
        QVERIFY(view.setAudioMode("both"));
        QVERIFY(view.setScreenCaptureBackend("auto"));
        view.refreshCaptureDevices();
        QCOMPARE(recorder.lastMethod, QString("refresh"));
        view.dismissCaptureError();
        QCOMPARE(recorder.lastMethod, QString("clear"));
        QSignalSpy devices(&view, &RecorderView::captureDevicesChanged);
        emit recorder.captureDevicesChanged(recorder.captureDevices);
        QCOMPARE(devices.count(), 1);
        view.attach(nullptr, {});
        QCOMPARE(view.uiState(), QString("noDevice"));
    }
    void stateEdgesAndSessionPath()
    {
        FakeRecorder recorder;
        QTemporaryDir directory;
        recorder.outputPath = directory.path();
        recorder.captureDevices = {QVariantMap{{"kind", "screen"}, {"available", true}}};
        RecorderView view(nullptr);
        view.attach(&recorder, {});
        QSignalSpy saved(&view, &RecorderView::recordingSaved);
        QSignalSpy failed(&view, &RecorderView::recordingFailed);
        auto state = [&](const QString &value) {
            recorder.captureState = value;
            emit recorder.captureStateChanged(value);
            QCOMPARE(view.uiState(), value.toLower());
        };
        state("Preparing");
        const auto path = view.currentOutputFile();
        recorder.outputFileName = "changed.mp4";
        emit recorder.outputFileNameChanged(recorder.outputFileName);
        QCOMPARE(view.currentOutputFile(), path);
        for (const auto &value : {"Running", "Paused", "Stopping"}) {
            state(value);
            QVERIFY(view.capturing());
            QVERIFY(!view.selectVideoCaptureDevice("cam"));
        }
        state("Idle");
        state("Idle");
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.at(0).at(0).toString(), path);
        QVERIFY(view.currentOutputFile().isEmpty());
        state("Preparing");
        QFile partial(view.currentOutputFile());
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.close();
        recorder.captureError = "test failure";
        state("Failed");
        state("Failed");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(1).toString(), partial.fileName());
    }

};

QTEST_MAIN(RecorderViewTests)
#include "recorder_view_tests.moc"
