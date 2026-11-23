// Uses the existing lifecycle friend seam; no capture devices or codecs are opened.
#include "recorder/capture_scheduler.h"
#include "recorder/capture_session.h"
#include "recorder/ffrecorder.h"
#include "subtitle/tap/audio_tap.h"
#include <QtTest>
#include <QTemporaryDir>

class CaptureLifecycleTest {
public:
    static void configure(CaptureScheduler &scheduler, QString directory, bool rejectConfig,
                          bool rejectOpen, bool finalized, bool audioEnabled = true)
    {
        scheduler.m_testConfigHook = [=](CaptureConfig *config) {
            config->outputPath = directory;
            config->outputFileName = QStringLiteral("hook.mp4");
            config->audioEnabled = audioEnabled;
            return rejectConfig ? failure() : CaptureStartResult::success();
        };
        scheduler.m_testOpenerHook = [=](CaptureSession &session, auto complete) {
            session.m_startFailed = rejectOpen;
            session.m_starting = false;
            session.m_started = !rejectOpen;
            session.m_subtitleMuxerFinalized = finalized;
            complete(rejectOpen ? failure() : CaptureStartResult::success());
        };
    }
    static CaptureStartResult failure()
    {
        CaptureError error;
        error.userMessage = QStringLiteral("injected subtitle hook startup failure");
        return CaptureStartResult::failure(error);
    }
};
namespace {
struct Hook : subtitle::RecordingAudioHook {
    CaptureScheduler &scheduler;
    int begins = 0, ends = 0;
    bool finalized = false;
    std::filesystem::path path;
    explicit Hook(CaptureScheduler &s) : scheduler(s) {}
    ~Hook() override { scheduler.stop(); scheduler.setRecordingAudioHook(nullptr); }
    std::shared_ptr<subtitle::AudioTap> beginRecording(const std::filesystem::path &video) noexcept override
    {
        // Reentrant state() would deadlock if begin/end ran under m_mutex.
        (void)scheduler.state();
        ++begins; path = video;
        return std::make_shared<subtitle::AudioTap>();
    }
    void endRecording(bool result) noexcept override
    {
        (void)scheduler.state();
        ++ends; finalized = result;
    }
};
}
class SchedulerHookTests : public QObject {
    Q_OBJECT
private slots:
    void lifecycle()
    {
        QTemporaryDir directory;
        auto &recorder = FFRecorder::getInstance();
        auto &scheduler = recorder.captureScheduler();
        Hook hook(scheduler);
        scheduler.setRecordingAudioHook(&hook);
        for (bool finalized : {true, false}) {
            CaptureLifecycleTest::configure(scheduler, directory.path(), false, false, finalized);
            QVERIFY(scheduler.start());
            QTRY_COMPARE(scheduler.state(), CaptureSchedulerState::Running);
            scheduler.stop();
            QCOMPARE(hook.finalized, finalized);
            QCOMPARE(hook.begins, hook.ends);
        }
        QCOMPARE(hook.begins, 2);
        QCOMPARE(QString::fromStdString(hook.path.u8string()), directory.filePath("hook.mp4"));
        scheduler.stop(); // double stop must not end the subtitle job twice
        QCOMPARE(hook.ends, 2);
        CaptureLifecycleTest::configure(scheduler, directory.path(), true, false, false);
        QVERIFY(!scheduler.start());
        QCOMPARE(hook.begins, 2); QCOMPARE(hook.ends, 2);
        CaptureLifecycleTest::configure(scheduler, directory.path(), false, true, false);
        QVERIFY(scheduler.start());
        QTRY_COMPARE(scheduler.state(), CaptureSchedulerState::Failed);
        QCOMPARE(hook.begins, 3); QCOMPARE(hook.ends, 3); QVERIFY(!hook.finalized);
        scheduler.stop();
        QCOMPARE(hook.ends, 3);
        CaptureLifecycleTest::configure(scheduler, directory.path(), false, false, true, false);
        QVERIFY(scheduler.start());
        QTRY_COMPARE(scheduler.state(), CaptureSchedulerState::Running);
        scheduler.stop();
        QCOMPARE(hook.begins, 3); QCOMPARE(hook.ends, 3); // video-only
        scheduler.setRecordingAudioHook(nullptr);
    }
};
QTEST_GUILESS_MAIN(SchedulerHookTests)
#include "scheduler_hooks.moc"
