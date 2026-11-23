#include "subtitle/service/subtitle_job_runner.h"
#include "subtitle/service/subtitle_service.h"
#include "subtitle/host/asr_host_client_factory.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/core/subtitle_writers.h"
#include "subtitle/tap/audio_tap.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#if SUBREC_TAP_HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#endif
using namespace subtitle;
namespace {
std::filesystem::path pathOf(QString s) { return std::filesystem::u8path(s.toUtf8().toStdString()); }
QByteArray read(QString p) { QFile f(p); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
struct Factory : AsrHostClientFactory {
    int launches = 0;
    std::function<QString(int)> options = [](int) { return QString("rtf=0.1"); };
    QString script;
    std::unique_ptr<AsrHostClient> create(const QString &role) override {
        auto c = std::make_unique<AsrHostClient>(script.isEmpty() ? ASR_EXE : FAKEHOST_EXE);
        c->setRole(role);
        c->setExtraArguments(script.isEmpty() ? QStringList{"--engine=fake", "--fake=" + options(launches)} : QStringList{"--script=" + script});
        ++launches; return c;
    }
};
struct Fixture {
    QTemporaryDir dir;
    PcmSpool spool;
    Factory factory;
    SubtitleJobRunner runner{&factory};
    QSignalSpy finished{&runner, &SubtitleJobRunner::jobFinished};
    QSignalSpy failed{&runner, &SubtitleJobRunner::jobFailed};
    QSignalSpy levels{&runner, &SubtitleJobRunner::levelChanged};
    QSignalSpy restarts{&runner, &SubtitleJobRunner::hostRestarting};
    QSignalSpy splits{&runner, &SubtitleJobRunner::currentSplitChanged};
    QString wav = dir.filePath("record.wav"), video = dir.filePath("record.mp4"), srt = dir.filePath("record.srt");
    qint64 total = 0;
    Fixture(int segments = 5, int speech = 5120) {
        if (!spool.create(pathOf(wav)).ok()) qFatal("create spool");
        std::vector<float> pcm(speech + 5120);
        for (int seg = 0; seg < segments; ++seg) {
            for (int n = 0; n < speech; ++n) pcm[n] = float(0.8 * std::sin(6.283185307 * 440 * n / 16000));
            std::fill(pcm.begin() + speech, pcm.end(), 0.f);
            if (!spool.append(pcm.data(), pcm.size()).ok()) qFatal("append spool");
        }
        total = spool.writtenSamples(); spool.publish(total); spool.finalize();
    }
    ~Fixture() { runner.shutdown(0); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void submit(bool live = true, LiveConfig config = {}) {
        config.partialIntervalMs = 0; runner.setLiveConfig(config);
        EngineRequest request; request.engineId = "fake"; request.vad.minSpeechMs = 64; request.vad.minSilenceMs = 64; request.vad.speechPadMs = 0;
        runner.submit(1, video, wav, request, {}, live); runner.notifyCommitted(1, total);
    }
    int cues(QString file = {}) {
        if (file.isEmpty()) file = srt;
        std::vector<Cue> out; auto bytes = read(file); if (bytes.isEmpty() || !parseSrt(bytes.toStdString(), out)) return 0;
        return int(out.size());
    }
    bool hasLevel(int l) { for (auto &args : levels) if (args[1].toInt() == l) return true; return false; }
    bool hasError(int c) { for (auto &args : failed) if (args[1].toInt() == c) return true; return false; }
    void finish() { runner.finishJob(1, total, true); }
};
}
class ServiceTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir;
    QString settingsPath;
    void serviceSettings(QString fake = "rtf=0.1") {
        settingsPath = settingsDir.filePath("settings.ini");
        QSettings settings(settingsPath, QSettings::IniFormat); settings.clear();
        settings.setValue("asr/mode", "live"); settings.setValue("asr/engineId", "fake"); settings.setValue("asr/partialIntervalMs", 0); settings.sync();
        qputenv("SUBREC_TEST_ASR_SETTINGS", settingsPath.toUtf8()); qputenv("SUBREC_TEST_ASR_EXE", ASR_EXE); qputenv("SUBREC_TEST_ASR_FAKE", fake.toUtf8());
    }
    QString replay(QByteArray body) {
        auto p = settingsDir.filePath("replay"); QFile f(p); if (!f.open(QIODevice::WriteOnly)) qFatal("script"); f.write(body); f.close(); return p;
    }
    void verifyIntervals(Fixture &f, int count, bool poison = false) {
        auto doc = QJsonDocument::fromJson(read(f.dir.filePath("record.subrec.json"))).object();
        QVERIFY(!doc["incomplete"].toBool());
        auto segments = doc["segments"].toArray();
        // The sidecar also retains poison intervals; SRT contains only recognized speech.
        QCOMPARE(f.cues(), count);
        std::vector<Cue> cues; QVERIFY(parseSrt(read(f.srt).toStdString(), cues));
        qint64 previous = -1; int index = 0;
        for (const auto &cue : cues) {
            const int expected = poison && index >= 3 ? index + 1 : index;
            QCOMPARE(cue.beginMs, int64_t(expected * 640)); QCOMPARE(cue.endMs, int64_t(expected * 640 + 320));
            QVERIFY(cue.beginMs >= previous); previous = cue.endMs; ++index;
        }
        QVERIFY(segments.size() >= count);
    }
private slots:
    void init() {
        qRegisterSubtitleMetaTypes(); qputenv("SUBREC_AUTOTEST_SCENARIO", "subtitle-d"); qputenv("SUBREC_TEST_ASR_TIME_SCALE", "0.02");
        qunsetenv("SUBREC_D_FAULT_ATTEMPTS"); qunsetenv("SUBREC_D_REPLAY_SCRIPT"); qunsetenv("SUBREC_TEST_ASR_EXE"); qunsetenv("SUBREC_TEST_ASR_FAKE"); qunsetenv("SUBREC_TEST_ASR_SETTINGS");
    }
    void realtime_finalize() {
        Fixture f; QElapsedTimer elapsed; elapsed.start(); f.submit();
        QTRY_VERIFY_WITH_TIMEOUT(f.cues(f.srt + ".part") >= 1, 600);
        QVERIFY(elapsed.elapsed() <= 600);
        QTRY_COMPARE_WITH_TIMEOUT(f.cues(f.srt + ".part"), 5, 2000); QVERIFY(elapsed.elapsed() < 2000);
        QVERIFY(!QFileInfo::exists(f.srt)); f.finish(); QTRY_COMPARE(f.finished.size(), 1); verifyIntervals(f, 5);
    }
    void rtf_l1() {
        Fixture f; f.factory.options = [](int) { return "rtf=0.8"; }; f.submit();
        QTRY_VERIFY_WITH_TIMEOUT(f.hasLevel(1), 4000); QVERIFY(!f.hasLevel(2)); f.finish(); QTRY_COMPARE(f.finished.size(), 1); verifyIntervals(f, 5);
    }
    void rtf_l2() {
        Fixture f; f.factory.options = [](int) { return "rtf=1.5"; }; f.submit();
        QTRY_VERIFY_WITH_TIMEOUT(f.hasLevel(2), 5000); f.finish(); QTRY_COMPARE(f.finished.size(), 1); verifyIntervals(f, 5);
    }
    void crash_recovery() {
        Fixture f; f.factory.options = [](int attempt) { return attempt == 0 ? "crashAtSeg=3" : "rtf=0"; }; f.submit(); f.finish();
        QTRY_COMPARE_WITH_TIMEOUT(f.finished.size(), 1, 5000); QCOMPARE(f.factory.launches, 2); QCOMPARE(f.restarts.size(), 1); verifyIntervals(f, 5);
    }
    void hang_recovery() {
        Fixture f; f.factory.options = [](int attempt) { return attempt == 0 ? "hangAtSeg=3" : "rtf=0"; }; f.submit(); f.finish();
        QTRY_COMPARE_WITH_TIMEOUT(f.finished.size(), 1, 6000); QCOMPARE(f.factory.launches, 2); QCOMPARE(f.restarts.size(), 1); verifyIntervals(f, 5);
    }
    void poison_segment() {
        Fixture f; f.factory.options = [](int attempt) { return attempt == 0 ? "crashAtSeg=3" : attempt == 1 ? "crashAtSeg=0" : "rtf=0"; };
        f.submit(); f.finish(); QTRY_COMPARE_WITH_TIMEOUT(f.finished.size(), 1, 5000); QCOMPARE(f.factory.launches, 3); QVERIFY(f.hasError(611)); verifyIntervals(f, 4, true);
    }
    void oom_l1_l3() {
        Fixture f; f.factory.options = [](int attempt) { return attempt == 0 ? "oomAtSeg=3" : attempt == 1 ? "oomAtSeg=0" : "rtf=0"; };
        f.submit(); QTRY_VERIFY(f.hasLevel(3)); QVERIFY(f.hasLevel(1)); QCOMPARE(f.factory.launches, 2);
        f.finish(); QTRY_COMPARE(f.finished.size(), 1); verifyIntervals(f, 5);
    }
    void illegal_instruction() {
        Fixture f; f.factory.options = [](int) { return "illegalInstr"; }; f.submit();
        QTRY_VERIFY(f.hasError(710)); QTest::qWait(150); QCOMPARE(f.factory.launches, 1); QCOMPARE(f.restarts.size(), 0);
    }
    void protocol_exit_191() {
        Fixture f; f.factory.script = replay("expect hello\nreply-hello\nexit 703\n"); f.submit();
        QTRY_VERIFY(f.restarts.size() >= 1); QVERIFY(!f.hasError(305));
        // A new child reads the new script, so a protocol exit recovers normally.
        f.factory.script = replay("expect hello\nreply-hello\nexpect load\nsend {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":0}\nexpect finish\nsend {\"type\":\"done\",\"jobId\":1}\n");
        f.finish(); QTRY_COMPARE(f.finished.size(), 1);
    }
    void end_false() { Fixture f; f.submit(); QTRY_COMPARE(f.cues(f.srt + ".part"), 5); f.runner.finishJob(1, f.total, false); QTRY_VERIFY(f.hasError(600)); QVERIFY(!QFileInfo::exists(f.srt)); QVERIFY(QFileInfo::exists(f.srt + ".part")); }
    void shutdown_incomplete() {
        Fixture f; f.factory.options = [](int) { return "hangAtSeg=3"; }; f.submit();
        QTRY_COMPARE(f.cues(f.srt + ".part"), 3); f.finish(); QElapsedTimer t; t.start(); f.runner.shutdown(80); QVERIFY(t.elapsed() < 700);
        QVERIFY(f.hasError(602)); QCOMPARE(f.cues(), 3); QVERIFY(QJsonDocument::fromJson(read(f.dir.filePath("record.subrec.json"))).object()["incomplete"].toBool());
    }
    void current_split() {
        Fixture f;
        f.factory.script = replay("expect hello\nreply-hello\nexpect load\nsend {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":0}\nexpect open\nsend {\"type\":\"partial\",\"jobId\":1,\"id\":0,\"b16k\":0,\"upto16k\":16000,\"text\":\"hello\",\"seq\":0}\nsend {\"type\":\"partial\",\"jobId\":1,\"id\":0,\"b16k\":0,\"upto16k\":16000,\"text\":\"hello\",\"seq\":1}\nsend {\"type\":\"partial\",\"jobId\":1,\"id\":0,\"b16k\":0,\"upto16k\":16000,\"text\":\"hello world\",\"seq\":2}\n");
        LiveConfig config; config.agreementN = 3; f.submit(true, config);
        QTRY_VERIFY(f.splits.size() >= 2);
        QCOMPARE(f.splits[0][1].toString(), QString{}); QCOMPARE(f.splits[0][2].toString(), QString("hello"));
        QCOMPARE(f.splits.last()[1].toString(), QString("hello")); QCOMPARE(f.splits.last()[2].toString(), QString(" world"));
        f.runner.onPauseSplice(1); QCOMPARE(f.splits.last()[1].toString(), QString{}); QCOMPARE(f.splits.last()[2].toString(), QString{});
    }
    void live_config() {
        Fixture f(1, 10240); LiveConfig config; config.maxSegmentMs = 320; config.revisableCues = 0; f.submit(true, config); f.finish();
        QTRY_COMPARE(f.finished.size(), 1); QCOMPARE(f.cues(), 2);
        std::vector<Cue> cues; QVERIFY(parseSrt(read(f.srt).toStdString(), cues)); QCOMPARE(cues[0].endMs, int64_t(320)); QCOMPARE(cues[1].beginMs, int64_t(320));
    }
    void restore_live() {
        Fixture f; f.submit(false); QVERIFY(f.hasLevel(3)); QCOMPARE(f.factory.launches, 0);
        f.runner.restoreLive(999); QCOMPARE(f.factory.launches, 0);
        f.runner.restoreLive(1); QVERIFY(f.hasLevel(2)); QTRY_COMPARE(f.cues(f.srt + ".part"), 5);
        const auto count = f.levels.size(); f.runner.restoreLive(1); QCOMPARE(f.levels.size(), count);
        f.finish(); QTRY_COMPARE(f.finished.size(), 1); verifyIntervals(f, 5);
    }
    void fifo_preemption() {
        Fixture f; f.factory.options = [](int) { return "rtf=0.8"; }; f.submit(); f.finish();
        QTRY_VERIFY(f.cues(f.srt + ".part") >= 1);
        EngineRequest request; request.engineId = "fake"; request.vad.minSpeechMs = 64; request.vad.minSilenceMs = 64; request.vad.speechPadMs = 0;
        auto video2 = f.dir.filePath("second.mp4"); f.runner.submit(2, video2, f.wav, request, {}, true);
        f.runner.notifyCommitted(2, f.total); f.runner.finishJob(2, f.total, true);
        QTRY_COMPARE_WITH_TIMEOUT(f.finished.size(), 2, 7000); QCOMPARE(f.finished[0][0].toULongLong(), 2ull); QCOMPARE(f.finished[1][0].toULongLong(), 1ull); verifyIntervals(f, 5);
    }
    void service_end_false() {
        serviceSettings();
        {
            SubtitleService service; auto video = settingsDir.filePath("false.mp4"); auto tap = service.beginRecording(pathOf(video)); QVERIFY(tap);
            QElapsedTimer timer; timer.start(); service.endRecording(false); QVERIFY(timer.elapsed() < 50);
            QTRY_VERIFY(QFileInfo::exists(settingsDir.filePath("false.srt.part")));
            QTRY_COMPARE(service.state(), QString("idle")); service.shutdown(100);
        }
        QVERIFY(!QFileInfo::exists(settingsDir.filePath("false.srt")));
    }
    void service_shutdown() {
        serviceSettings(); SubtitleService service; auto tap = service.beginRecording(pathOf(settingsDir.filePath("shutdown.mp4"))); QVERIFY(tap);
        QTest::qWait(100); QElapsedTimer timer; timer.start(); service.shutdown(20); QVERIFY(timer.elapsed() < 50);
        QTRY_VERIFY(service.lastError().contains(QStringLiteral("未完成")));
    }
    void service_throttle() {
        // Drive the actual queued Service mirror with 50 Hz scripted partials.
        serviceSettings(); QElapsedTimer elapsed;
        auto script = replay("expect hello\nreply-hello\nexpect load\nsend {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":0}\nexpect open\n" + partialBurst());
        // A test-only relay supplies the script to C's unchanged fakehost.
        qputenv("SUBREC_TEST_ASR_EXE", QCoreApplication::applicationFilePath().toUtf8()); qputenv("SUBREC_D_REPLAY_SCRIPT", script.toUtf8());
        SubtitleService replayService; auto replayTap = replayService.beginRecording(pathOf(settingsDir.filePath("throttle2.mp4"))); QVERIFY(replayTap);
        QList<qint64> updates; elapsed.restart();
        connect(&replayService, &SubtitleService::currentLineChanged, &replayService, [&] { updates << elapsed.elapsed(); QCOMPARE(replayService.currentLine(), replayService.currentConfirmed() + replayService.currentTentative()); });
        // LocalAgreement freezes "word 1", so "word 59" displays as "word 19"
        // with tentative "9". Earlier partials can match too: require the display
        // to stay unchanged for more than one refresh tick before settling.
        QTRY_VERIFY_WITH_TIMEOUT(replayService.currentLine() == QStringLiteral("word 19")
                                 && replayService.currentTentative() == QStringLiteral("9")
                                 && !updates.isEmpty() && elapsed.elapsed() - updates.last() >= 300, 5000);
        QTest::qWait(250);
        QStringList timestamps;
        for (const auto update : updates) timestamps << QString::number(update);
        const auto diagnostics = QString("currentLine update timestamps (ms): [%1]").arg(timestamps.join(", ")).toUtf8();
        QVERIFY2(updates.size() >= 3, diagnostics.constData());
        const qint64 window = updates.last() - updates.first();
        // 4 Hz means 250 ms between updates; allow 20 ms scheduling jitter per gap.
        // N updates span N - 1 gaps, starting at the first observed update.
        QVERIFY2(updates.size() <= window / 230 + 1, diagnostics.constData());
        for (int n = 1; n < updates.size(); ++n)
            QVERIFY2(updates[n] - updates[n-1] >= 230, diagnostics.constData());
        replayService.endRecording(false); replayService.shutdown(100);
    }
    QByteArray partialBurst() {
        QByteArray body;
        for (int n = 0; n < 60; ++n) {
            QJsonObject p{{"type", "partial"}, {"jobId", 1}, {"id", 0}, {"b16k", 0}, {"upto16k", 16000}, {"text", QString("word %1").arg(n)}, {"seq", n}};
            body += "send " + QJsonDocument(p).toJson(QJsonDocument::Compact) + "\nsleep 20\n";
        }
        return body;
    }
#if SUBREC_TAP_HAS_FFMPEG
    void tap_realtime() { tapScenario("rtf=0.1", 0); }
    void tap_rtf_l1() { tapScenario("rtf=0.8", 1); }
    void tap_rtf_l2() { tapScenario("rtf=1.5", 2); }
    void tap_crash() { tapScenario("crashAtSeg=3", 0); }
    void tap_hang() { tapScenario("hangAtSeg=3", 0); }
    void tap_poison() { tapScenario("crashAtSeg=3", 0, true); }
    void tap_oom() { tapScenario("oomAtSeg=3", 3); }
    void tap_illegal() {
        serviceSettings("illegalInstr"); SubtitleService service; auto tap = service.beginRecording(pathOf(settingsDir.filePath("illegal.mp4"))); QVERIFY(tap);
        QTRY_COMPARE(service.state(), QString("unavailable")); QVERIFY(!service.available()); service.endRecording(false); service.shutdown(100);
    }
    void tapScenario(QString options, int expectedLevel, bool poison = false) {
        serviceSettings(options);
        if (poison) {
            QSettings cfg(settingsPath, QSettings::IniFormat); cfg.setValue("asr/speechPadMs", 0); cfg.sync();
            qputenv("SUBREC_TEST_ASR_EXE", QCoreApplication::applicationFilePath().toUtf8());
            qputenv("SUBREC_D_FAULT_ATTEMPTS", settingsDir.filePath("attempts").toUtf8());
        }
        SubtitleService service;
        auto video = settingsDir.filePath("tap.mp4"); auto tap = service.beginRecording(pathOf(video)); QVERIFY(tap);
        int maxLevel = 0; connect(&service, &SubtitleService::changed, &service, [&] { maxLevel = std::max(maxLevel, service.degradeLevel()); });
        // 8 segments: 320 ms speech, 640 ms silence. Default VAD endpoint is 500 ms.
        // The 30 s tap ring holds the entire synthetic recording.
        const int segments = 8;
        std::vector<float> pcm(480);
        AVFrame frame{}; frame.sample_rate = 48000; frame.nb_samples = 480; frame.ch_layout.nb_channels = 1; frame.format = AV_SAMPLE_FMT_FLT;
        frame.data[0] = reinterpret_cast<uint8_t *>(pcm.data()); frame.extended_data = frame.data;
        for (int block = 0; block < segments * 96; ++block) {
            for (int n = 0; n < 480; ++n) pcm[n] = block % 96 < 32 ? float(0.8 * std::sin(6.283185307 * 440 * (block * 480 + n) / 48000)) : 0.f;
            frame.pts = block * 480; tap->push(frame);
        }
        QCOMPARE(tap->stats().droppedBlocks, uint64_t(0));
        if (expectedLevel) QTRY_VERIFY_WITH_TIMEOUT(maxLevel >= expectedLevel, 10000);
        else if (!poison) QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(settingsDir.filePath("tap.srt.part")), 5000);
        service.endRecording(true); QTRY_COMPARE_WITH_TIMEOUT(service.state(), QString("done"), 15000);
        auto document = QJsonDocument::fromJson(read(settingsDir.filePath("tap.subrec.json"))).object(); QVERIFY(!document["incomplete"].toBool());
        std::vector<Cue> cues; QVERIFY(parseSrt(read(settingsDir.filePath("tap.srt")).toStdString(), cues));
        if (!poison) QCOMPARE(int(cues.size()), segments);
        else {
            QCOMPARE(int(cues.size()), segments - 1);
            for (int n = 0; n < int(cues.size()); ++n) {
                const int original = n >= 3 ? n + 1 : n;
                QVERIFY(std::abs(cues[n].beginMs - original * 960) < 40);
                QVERIFY(std::abs(cues[n].endMs - (original * 960 + 320)) < 40);
            }
            QVERIFY(service.lastError().contains(QStringLiteral("跳过")));
        }
        service.shutdown(100);
    }
#endif
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().filter("--role=").size()) {
        // Test-only relay: forwards stdin/stdout to C's unchanged scripted host.
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.setInputChannelMode(QProcess::ForwardedInputChannel);
        const auto attemptsPath = QString::fromUtf8(qgetenv("SUBREC_D_FAULT_ATTEMPTS"));
        if (!attemptsPath.isEmpty()) {
            int attempt = read(attemptsPath).toInt();
            QFile file(attemptsPath); if (!file.open(QIODevice::WriteOnly)) return 3;
            file.write(QByteArray::number(attempt + 1)); file.close();
            // C1 restarts IDs at zero: inject the SAME absolute interval twice.
            const QString options = attempt == 0 ? "crashAtSeg=3" : attempt == 1 ? "crashAtSeg=0" : "rtf=0";
            child.start(ASR_EXE, {"--engine=fake", "--fake=" + options, "--parent-pid=" + QString::number(app.applicationPid())});
        } else child.start(FAKEHOST_EXE, {"--script=" + QString::fromUtf8(qgetenv("SUBREC_D_REPLAY_SCRIPT"))});
        if (!child.waitForStarted()) return 2;
        QObject::connect(&child, &QProcess::finished, &app, [&](int code) { app.exit(code); });
        return app.exec();
    }
    ServiceTests test; return QTest::qExec(&test, argc, argv);
}
#include "service_tests.moc"
