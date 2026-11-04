#include <QtTest>
#include "ui/backend/audio_level_monitor.h"
using namespace subrec::ui;

class Sampler : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volumeLevel MEMBER level NOTIFY volumeLevelChanged)
    Q_PROPERTY(bool isActive MEMBER active NOTIFY activeStateChanged)
public:
    int level = 0;
    bool active = false;
signals:
    void volumeLevelChanged(int value);
    void activeStateChanged(bool active);
};

class AudioLevelMonitorTests : public QObject {
    Q_OBJECT
private slots:

    void levelsAndThrottle()
    {
        AudioLevelMonitor view(nullptr);
        QCOMPARE(view.mixLevelDb(), -60.0);
        QVERIFY(!view.active());
        QVERIFY(!view.perSourceAvailable());
        Sampler source;
        source.active = true;
        view.attach(&source);
        for (int level : {0, 1, 50, 100}) {
            source.level = level;
            emit source.volumeLevelChanged(level);
            QTRY_COMPARE(view.mixLevelDb(), level == 0 ? -60.0 : -40.0 + level * 0.4);
            QCOMPARE(view.systemLevelDb(), view.mixLevelDb());
            QCOMPARE(view.microphoneLevelDb(), view.mixLevelDb());
        }
        QSignalSpy spy(&view, &AudioLevelMonitor::levelsChanged);
        for (int n = 0; n < 100; ++n) {
            source.level = n;
            emit source.volumeLevelChanged(n);
        }
        QVERIFY(spy.count() <= 1);
        QTRY_VERIFY(qAbs(view.mixLevelDb() + 0.4) < 0.000001);
        QVERIFY(spy.count() <= 2);
        source.active = false;
        emit source.activeStateChanged(false);
        QTRY_VERIFY(!view.active());
        QCOMPARE(view.mixLevelDb(), -60.0);
        view.attach(nullptr);
        QCOMPARE(view.mixLevelDb(), -60.0);
    }

};

QTEST_MAIN(AudioLevelMonitorTests)
#include "audio_level_monitor_tests.moc"
