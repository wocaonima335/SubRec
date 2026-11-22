#include "subtitle/host/asr_host_client_factory.h"
#include "subtitle/live/host_watchdog.h"
#include "subtitle/host/asr_protocol.h"
#include <QtTest>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace subtitle;
class HostTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString script(const QByteArray &bytes) {
        QString p = dir.filePath("script.txt"); QFile f(p); if (!f.open(QIODevice::WriteOnly)) return {}; f.write(bytes); return p;
    }
    void malformed(const QByteArray &body, int code) {
        AsrHostClient c(FAKEHOST_EXE); c.setExtraArguments({"--script=" + script("expect hello\n" + body)});
        QSignalSpy errors(&c, &AsrHostClient::failed), ready(&c, &AsrHostClient::ready), exits(&c, &AsrHostClient::exited);
        c.start(true); QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 3000); QCOMPARE(errors[0][1].toInt(), code);
        if (code == 704) QCOMPARE(ready.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(exits.size(), 1, 3000); QVERIFY(!c.isRunning());
    }
private slots:
    void init() { qRegisterSubtitleMetaTypes(); qunsetenv("SUBREC_AUTOTEST_SCENARIO"); qunsetenv("SUBREC_ASR_TEST_HOOKS"); }
    void version_mismatch() { malformed("reply-hello 1\n", 704); }
    void overlong_line() { malformed("reply-hello\nlongline 1048577\n", 703); }
    void non_json() { malformed("reply-hello\ngarbage broken\n", 703); }
    void unknown_type() {
        AsrHostClient c(FAKEHOST_EXE); c.setExtraArguments({"--script=" + script("expect hello\nreply-hello\nsend {\"type\":\"future-message\"}\n")});
        QSignalSpy ready(&c, &AsrHostClient::ready), errors(&c, &AsrHostClient::failed), exits(&c, &AsrHostClient::exited);
        c.start(false); QTRY_COMPARE(ready.size(), 1); c.stop(500); QTRY_COMPARE(exits.size(), 1); QCOMPARE(errors.size(), 0); QCOMPARE(exits[0][0].toInt(), 0);
    }
    void crash_restart_plan() {
        AsrHostClient c(FAKEHOST_EXE); c.setExtraArguments({"--script=" + script("expect hello\nreply-hello\ncrash\n")});
        QSignalSpy exits(&c, &AsrHostClient::exited); c.start(false); QTRY_COMPARE(exits.size(), 1); QVERIFY(exits[0][1].toBool());
        ManualClock clock; HostWatchdog watchdog({}, clock); watchdog.onProcessStarted(); watchdog.onBusy(16, 1600, 1584);
        auto plan = watchdog.onExit(exits[0][0].toInt(), exits[0][1].toBool(), 16);
        QVERIFY(plan.allowed); QCOMPARE(plan.delayMs, 1000); QVERIFY(!plan.poison);
    }
    void quit_kill_hang() {
        AsrHostClient c(FAKEHOST_EXE); c.setExtraArguments({"--script=" + script("expect hello\nreply-hello\nhang\n")});
        QSignalSpy ready(&c, &AsrHostClient::ready), exits(&c, &AsrHostClient::exited);
        c.start(false); QTRY_COMPARE(ready.size(), 1); QElapsedTimer elapsed; elapsed.start(); c.stop(120);
        QTest::qWait(50); QVERIFY(c.isRunning()); QTRY_COMPARE_WITH_TIMEOUT(exits.size(), 1, 1500);
        QVERIFY(elapsed.elapsed() >= 110); QVERIFY(elapsed.elapsed() < 1500); QVERIFY(exits[0][1].toBool());
    }
    void hooks_enabled() { hooks(true); }
    void hooks_disabled() { hooks(false); }
    void hooks(bool enabled) {
        // Deliberately inherit the child unlock in BOTH cases: disabled must scrub it.
        qputenv("SUBREC_ASR_TEST_HOOKS", "1");
        if (enabled) qputenv("SUBREC_AUTOTEST_SCENARIO", "subtitle-d");
        AsrHostClient c(ASR_EXE); c.setExtraArguments({"--engine=fake"});
        QSignalSpy ready(&c, &AsrHostClient::ready), loaded(&c, &AsrHostClient::loaded), errors(&c, &AsrHostClient::failed);
        c.start(false); QTRY_COMPARE(ready.size(), 1); EngineRequest r; r.engineId = "fake"; c.load(r);
        if (enabled) { QTRY_COMPARE(loaded.size(), 1); QCOMPARE(errors.size(), 0); }
        else { QTRY_COMPARE(errors.size(), 1); QCOMPARE(errors[0][1].toInt(), 300); QCOMPARE(loaded.size(), 0); }
        c.stop(200); QTRY_VERIFY(!c.isRunning());
        QCOMPARE(qgetenv("SUBREC_ASR_TEST_HOOKS"), QByteArray("1"));
    }
    void commands_append() {
        AsrHostClient c(FAKEHOST_EXE);
        c.setExtraArguments({"--script=" + script("expect hello\nreply-hello\nexpect load\nsend {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":0}\nexpect open\nexpect append\nexpect splice\nexpect setLevel\nexpect pause\nexpect resume\nexpect finish\nsend {\"type\":\"done\",\"jobId\":7}\nexpect cancel\nsend {\"type\":\"cancelled\",\"jobId\":7}\n")});
        QSignalSpy ready(&c, &AsrHostClient::ready), loaded(&c, &AsrHostClient::loaded), done(&c, &AsrHostClient::jobDone), cancelled(&c, &AsrHostClient::jobCancelled);
        c.start(false); QTRY_COMPARE(ready.size(), 1); c.load({}); QTRY_COMPARE(loaded.size(), 1);
        c.open(7, "spool", 0, 0, true); c.append(7, 16); c.append(7, 32); c.splice(7, 32);
        c.setLevel(7, DegradeLevel::L1FinalOnly, 1, 0); c.pauseJob(7); c.resumeJob(7); c.finish(7, 32);
        QTRY_COMPARE(done.size(), 1); c.cancel(7); QTRY_COMPARE(cancelled.size(), 1); c.stop(200); QTRY_VERIFY(!c.isRunning());
    }
#ifdef Q_OS_WIN
    void job_object_parent_death() {
        auto p = script("expect hello\nreply-hello\nhang\n"); auto pidFile = dir.filePath("pid");
        QProcess parent; parent.start(QCoreApplication::applicationFilePath(), {"--job-parent", p, pidFile});
        QVERIFY(parent.waitForStarted()); QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile), 5000);
        QFile file(pidFile); QVERIFY(file.open(QIODevice::ReadOnly)); auto pid = file.readAll().trimmed().toUInt(); QVERIFY(pid);
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid); QVERIFY(child);
        QCOMPARE(WaitForSingleObject(child, 0), DWORD(WAIT_TIMEOUT));
        parent.kill(); QVERIFY(parent.waitForFinished(2000));
        QCOMPARE(WaitForSingleObject(child, 3000), DWORD(WAIT_OBJECT_0)); CloseHandle(child);
    }
#endif
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (app.arguments().contains("--job-parent")) {
        AsrHostClient c(FAKEHOST_EXE); c.setExtraArguments({"--script=" + app.arguments().at(2)});
        QObject::connect(&c, &AsrHostClient::ready, &app, [&] {
            if (!c.assignJobObject(JobLimits{})) { app.exit(2); return; }
            QFile f(app.arguments().at(3)); if (!f.open(QIODevice::WriteOnly)) { app.exit(3); return; }
            f.write(QByteArray::number(c.findChild<QProcess *>()->processId())); f.close();
        });
        c.start(false); return app.exec();
    }
#endif
    HostTests test; return QTest::qExec(&test, argc, argv);
}
#include "host_tests.moc"
