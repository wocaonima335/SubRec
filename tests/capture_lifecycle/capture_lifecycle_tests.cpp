// Ten fake control-plane groups plus one real early-failure subset; see README.md.
// This class must remain global to match the existing production friend seam.
#include "recorder/capture_scheduler.h"
#include "recorder/capture_session.h"
#include "recorder/ffrecorder.h"
#include "capture/ddagrab_screen_source.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QPointer>
#include <QFileInfo>
#include <QUuid>
#include <QTemporaryDir>

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__,          \
                         __LINE__, #condition);                                 \
            return false;                                                      \
        }                                                                      \
    } while (false)

namespace {
using namespace std::chrono_literals;
using Completion = std::function<void(const CaptureStartResult &)>;

struct Probe {
    std::atomic<bool> release{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
    std::atomic<int> posted{0};
    std::atomic<bool> queried{false};
    CaptureSchedulerState observed = CaptureSchedulerState::Idle;
};

enum class Mode { Result, HoldUntilReleased, QueryAfterCancel };

CaptureStartResult failure(const QString &message = QStringLiteral("probe failure"))
{
    CaptureError error;
    error.stage = CaptureErrorStage::InputOpen;
    error.userMessage = message;
    return CaptureStartResult::failure(std::move(error));
}

// Never dispatch events here: posted == 1 means the real startup callback is
// queued, not yet delivered. The timeout diagnoses missing worker progress;
// only CTest can bound a deadlock in stop/join/destruction on this thread.
bool waitFor(const std::function<bool()> &ready, std::chrono::milliseconds limit = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void deliver(QObject *receiver)
{
    QCoreApplication::sendPostedEvents(receiver, QEvent::MetaCall);
}

class MetaCallCounter : public QObject {
public:
    int calls = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MetaCall) ++calls;
        return false; // Observe, never suppress the production callback.
    }
};
} // namespace

class CaptureLifecycleTest {
public:
    static void installConfig(CaptureScheduler &scheduler)
    {
        // Seed valid selectable devices through the existing friend access.
        // Otherwise Paused selection rejection could pass merely because the
        // requested IDs do not exist; refresh must preserve this exact list.
        CaptureDevice video;
        video.id = QStringLiteral("probe-video");
        video.kind = CaptureDeviceKind::Screen;
        video.available = true;
        CaptureDevice audio;
        audio.id = QStringLiteral("probe-audio");
        audio.kind = CaptureDeviceKind::SystemAudio;
        audio.available = true;
        scheduler.m_devices = {video, audio};
        scheduler.m_selectedVideoDeviceId = video.id;
        scheduler.m_selectedAudioDeviceId = audio.id;
        scheduler.m_testConfigHook = [](CaptureConfig *config) {
            // Fake opener does not inspect URLs or write this file.
            config->outputPath = QStringLiteral(".");
            config->outputFileName = QStringLiteral("not-created-lifecycle.mp4");
            config->video = {CaptureBackend::DirectShow, CaptureDeviceKind::Screen,
                             QStringLiteral("probe-video")};
            config->audio = {CaptureBackend::DirectShow, CaptureDeviceKind::SystemAudio,
                             QStringLiteral("probe-audio")};
            return CaptureStartResult::success();
        };
    }

    static void installRealFormatFailure(CaptureScheduler &scheduler,
                                         const QString &outputName,
                                         const QString &missingFormat)
    {
        scheduler.m_testOpenerHook = {}; // Exercise the real Session opener.
        scheduler.m_testConfigHook = [outputName, missingFormat](CaptureConfig *config) {
            config->outputPath = QStringLiteral(".");
            config->outputFileName = outputName;
            config->video = {CaptureBackend::DirectShow, CaptureDeviceKind::Screen,
                             QStringLiteral("probe-video")};
            config->audio = {CaptureBackend::DirectShow, CaptureDeviceKind::SystemAudio,
                             QStringLiteral("probe-audio")};
            config->videoInput = {CaptureBackend::DirectShow, CaptureDeviceKind::Screen,
                                  QStringLiteral("probe-video"), QStringLiteral("No device video"),
                                  QStringLiteral("video=never-open"), missingFormat};
            config->audioInput = {CaptureBackend::DirectShow, CaptureDeviceKind::SystemAudio,
                                  QStringLiteral("probe-audio"), QStringLiteral("No device audio"),
                                  QStringLiteral("audio=never-open"), missingFormat};
            return CaptureStartResult::success();
        };
    }

    static void installRealVideoTimeout(CaptureScheduler &scheduler,
                                        const QString &directory, const QString &audioFile)
    {
        scheduler.m_testOpenerHook = {};
        scheduler.m_testConfigHook = [directory, audioFile](CaptureConfig *config) {
            config->outputPath = directory;
            config->outputFileName = QStringLiteral("must-not-exist.mp4");
            config->video = {CaptureBackend::DesktopDuplication, CaptureDeviceKind::Screen,
                             QStringLiteral("injected-screen")};
            config->audio = {CaptureBackend::DirectShow, CaptureDeviceKind::Microphone,
                             QStringLiteral("test-file")};
            config->videoInput = {CaptureBackend::DesktopDuplication, CaptureDeviceKind::Screen,
                                  QStringLiteral("injected-screen"), QStringLiteral("Injected screen"),
                                  QStringLiteral("output_idx=0"), QString()};
            config->audioInput = {CaptureBackend::DirectShow, CaptureDeviceKind::Microphone,
                                  QStringLiteral("test-file"), QStringLiteral("Test file"),
                                  audioFile, QStringLiteral("nut")};
            return CaptureStartResult::success();
        };
    }

    static void installOpener(CaptureScheduler &scheduler,
                              const std::shared_ptr<Probe> &probe,
                              bool succeed, Mode mode)
    {
        scheduler.m_testOpenerHook = [probe, succeed, mode, &scheduler](
            CaptureSession &session, Completion onStarted) {
            session.m_openThread = std::thread([probe, succeed, mode, &scheduler,
                                                &session, onStarted = std::move(onStarted)] {
                probe->entered.store(true);
                if (mode == Mode::QueryAfterCancel) {
                    // The query cannot run before stop requests cancellation.
                    // Joining under m_mutex now necessarily deadlocks, rather
                    // than occasionally passing due to worker scheduling.
                    while (!cancelRequested(session)) std::this_thread::sleep_for(1ms);
                    probe->observed = scheduler.state();
                    probe->queried.store(true);
                } else {
                    while (!probe->release.load()) {
                        // For requestStop testing, cancellation alone MUST NOT
                        // release the worker. A joining requestStop deadlocks.
                        if (mode != Mode::HoldUntilReleased && cancelRequested(session)) break;
                        std::this_thread::sleep_for(1ms);
                    }
                }
                const bool cancelled = cancelRequested(session);
                session.m_started.store(succeed && !cancelled);
                session.m_starting.store(false);
                if (!cancelled && onStarted) {
                    onStarted(succeed ? CaptureStartResult::success() : failure());
                    probe->posted.fetch_add(1); // invokeMethod has returned.
                }
                probe->finished.store(true);
            });
        };
    }

    static CaptureSession *session(CaptureScheduler &scheduler)
    { return scheduler.m_session.get(); }
    static std::uint64_t generation(CaptureScheduler &scheduler)
    { return scheduler.m_sessionId; }
    static bool cancelRequested(const CaptureSession &session)
    { return session.m_stopRequested.load(); }

    // Replays use Qt delivery too, but only the cross-restart test below is
    // evidence of a genuinely old production callback/generation.
    static bool queueResult(CaptureScheduler &scheduler, std::uint64_t id,
                            const CaptureStartResult &result)
    {
        return QMetaObject::invokeMethod(&scheduler, [&scheduler, id, result] {
            scheduler.onSessionStarted(id, result);
        }, Qt::QueuedConnection);
    }

    static CaptureStartResult resolveConfig(CaptureScheduler &scheduler, CaptureConfig *config)
    {
        return scheduler.buildAudioConfig(config);
    }

    static void installAudioDevices(CaptureScheduler &scheduler,
                                    const CaptureDevice &system,
                                    const CaptureDevice &microphone)
    {
        scheduler.m_devices.append(system);
        scheduler.m_devices.append(microphone);
        scheduler.m_selectedAudioDeviceId = system.id;
        scheduler.m_selectedSystemAudioId = system.id;
        scheduler.m_systemAudioBackendPreference = SystemAudioBackendPreference::Auto;
    }
};

namespace {
// Every group owns a new scheduler and all worker gates. CHECK early returns
// destroy this fixture: unblock first, then cancel/join, discard scheduler
// callbacks, drain recorder notifications, and reset shared recorder UI state.
struct Fixture {
    FFRecorder &recorder = FFRecorder::getInstance();
    std::vector<std::shared_ptr<Probe>> probes;
    std::unique_ptr<CaptureScheduler> scheduler = std::make_unique<CaptureScheduler>(recorder);

    Fixture() { CaptureLifecycleTest::installConfig(*scheduler); }
    ~Fixture()
    {
        for (const auto &probe : probes) probe->release.store(true);
        if (scheduler && scheduler->state() == CaptureSchedulerState::Paused) scheduler->resume();
        scheduler.reset();
        deliver(&recorder);
        recorder.clearCaptureError();
    }
    std::shared_ptr<Probe> opener(bool succeed = true, Mode mode = Mode::Result,
                                  bool released = true)
    {
        auto probe = std::make_shared<Probe>();
        probe->release.store(released);
        probes.push_back(probe);
        CaptureLifecycleTest::installOpener(*scheduler, probe, succeed, mode);
        return probe;
    }
};

bool cancelDuringPreparing()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto probe = f.opener(true, Mode::HoldUntilReleased, false);
    CHECK(s.start());
    CHECK(waitFor([&] { return probe->entered.load(); }));
    CHECK(s.state() == CaptureSchedulerState::Preparing);
    auto *session = CaptureLifecycleTest::session(s);
    CHECK(session);
    session->requestStop(); // Worker cannot finish until THIS thread opens gate.
    CHECK(CaptureLifecycleTest::cancelRequested(*session));
    CHECK(!probe->finished.load());
    CHECK(s.state() == CaptureSchedulerState::Preparing);
    probe->release.store(true);
    s.stop(); // Join proves the zero-completion assertion is not premature.
    CHECK(probe->finished.load());
    CHECK(probe->posted.load() == 0);
    CHECK(CaptureLifecycleTest::session(s) == nullptr);
    CHECK(s.state() == CaptureSchedulerState::Idle);
    CHECK(s.lastError().isEmpty());
    return true;
}

bool stopJoinsOutsideLock()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto probe = f.opener(true, Mode::QueryAfterCancel, false);
    CHECK(s.start());
    CHECK(waitFor([&] { return probe->entered.load(); }));
    CHECK(!probe->queried.load());
    s.stop();
    CHECK(probe->finished.load());
    CHECK(probe->queried.load());
    CHECK(probe->observed == CaptureSchedulerState::Stopping);
    CHECK(probe->posted.load() == 0);
    CHECK(s.state() == CaptureSchedulerState::Idle);
    return true;
}

bool queuedOldGenerationAcrossRestart()
{
    // Both a stale success (incorrect promotion) and stale failure (incorrect
    // failure/error) must be ignored while the next generation is Preparing.
    for (const bool oldSuccess : {true, false}) {
        Fixture f;
        auto &s = *f.scheduler;
        MetaCallCounter counter;
        s.installEventFilter(&counter);
        const auto old = f.opener(oldSuccess);
        CHECK(s.start());
        const auto oldId = CaptureLifecycleTest::generation(s);
        CHECK(waitFor([&] { return old->posted.load() == 1; }));
        CHECK(counter.calls == 0);
        CHECK(s.state() == CaptureSchedulerState::Preparing);
        s.stop(); // Does not dispatch the old scheduler MetaCall.
        CHECK(old->finished.load());
        CHECK(CaptureLifecycleTest::session(s) == nullptr);
        const auto next = f.opener(true, Mode::Result, false);
        CHECK(s.start());
        const auto nextId = CaptureLifecycleTest::generation(s);
        CHECK(nextId > oldId);
        CHECK(waitFor([&] { return next->entered.load(); }));
        deliver(&s);
        CHECK(counter.calls == 1); // Old production callback actually ran.
        CHECK(s.state() == CaptureSchedulerState::Preparing);
        CHECK(s.lastError().isEmpty());
        CHECK(CaptureLifecycleTest::generation(s) == nextId);
        next->release.store(true);
        CHECK(waitFor([&] { return next->posted.load() == 1; }));
        deliver(&s);
        CHECK(counter.calls == 2);
        CHECK(s.state() == CaptureSchedulerState::Running);
    }
    return true;
}

bool sameGenerationGuards()
{
    for (const bool succeed : {true, false}) {
        Fixture f;
        auto &s = *f.scheduler;
        const auto probe = f.opener(succeed);
        CHECK(s.start());
        const auto id = CaptureLifecycleTest::generation(s);
        CHECK(waitFor([&] { return probe->posted.load() == 1; }));
        deliver(&s);
        const auto expected = succeed ? CaptureSchedulerState::Running : CaptureSchedulerState::Failed;
        CHECK(s.state() == expected);
        const auto error = succeed ? QString() : QStringLiteral("probe failure");
        CHECK(s.lastError() == error);
        CHECK(CaptureLifecycleTest::queueResult(s, id, failure(QStringLiteral("replay failure"))));
        deliver(&s);
        CHECK(s.state() == expected);
        CHECK(s.lastError() == error);
        CHECK(CaptureLifecycleTest::queueResult(s, id, CaptureStartResult::success()));
        deliver(&s);
        CHECK(s.state() == expected);
        CHECK(s.lastError() == error);
        CHECK(CaptureLifecycleTest::generation(s) == id);
        s.stop();
        CHECK(s.state() == CaptureSchedulerState::Idle);
        CHECK(s.lastError().isEmpty());
    }
    return true;
}

bool restartDirectlyFromFailed()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto first = f.opener(false);
    CHECK(s.start());
    const auto firstId = CaptureLifecycleTest::generation(s);
    CHECK(waitFor([&] { return first->posted.load() == 1; }));
    deliver(&s);
    CHECK(s.state() == CaptureSchedulerState::Failed);
    const auto second = f.opener();
    CHECK(s.start()); // Exercises previous->release(), without an explicit stop.
    CHECK(first->finished.load());
    CHECK(CaptureLifecycleTest::generation(s) > firstId);
    CHECK(waitFor([&] { return second->posted.load() == 1; }));
    deliver(&s);
    CHECK(s.state() == CaptureSchedulerState::Running);
    CHECK(s.lastError().isEmpty());
    return true;
}

bool doubleStop()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto probe = f.opener(true, Mode::Result, false);
    CHECK(s.start());
    s.stop();
    s.stop();
    s.stop();
    CHECK(probe->finished.load());
    CHECK(probe->posted.load() == 0);
    CHECK(CaptureLifecycleTest::session(s) == nullptr);
    CHECK(s.state() == CaptureSchedulerState::Idle);
    return true;
}

bool pausedRejectsStartAndConfiguration()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto probe = f.opener();
    CHECK(s.start());
    CHECK(waitFor([&] { return probe->posted.load() == 1; }));
    deliver(&s);
    CHECK(s.state() == CaptureSchedulerState::Running);
    s.pause();
    CHECK(s.state() == CaptureSchedulerState::Paused);
    const auto id = CaptureLifecycleTest::generation(s);
    auto *session = CaptureLifecycleTest::session(s);
    const auto devices = s.devices();
    const auto video = s.selectedVideoDeviceId();
    const auto audio = s.selectedAudioDeviceId();
    const auto systemAudio = s.useSystemAudio();
    CHECK(!s.start());
    CHECK(!s.changeVideoSource(false));
    CHECK(!s.changeAudioSource(!systemAudio));
    CHECK(!s.selectVideoDevice(QStringLiteral("probe-video")));
    CHECK(!s.selectAudioDevice(QStringLiteral("probe-audio")));
    s.refreshDevices();
    CHECK(s.devices() == devices);
    CHECK(s.selectedVideoDeviceId() == video);
    CHECK(s.selectedAudioDeviceId() == audio);
    CHECK(s.useSystemAudio() == systemAudio);
    CHECK(s.state() == CaptureSchedulerState::Paused);
    CHECK(CaptureLifecycleTest::session(s) == session);
    CHECK(CaptureLifecycleTest::generation(s) == id);
    s.resume();
    CHECK(s.state() == CaptureSchedulerState::Running);
    return true;
}

bool audioModeTransitions()
{
    Fixture f;
    auto &s = *f.scheduler;
    CaptureDevice wasapi;
    wasapi.id = QStringLiteral("probe-wasapi");
    wasapi.kind = CaptureDeviceKind::SystemAudio;
    wasapi.backend = CaptureBackend::Wasapi;
    wasapi.available = true;
    CaptureDevice microphone;
    microphone.id = QStringLiteral("probe-mic");
    microphone.kind = CaptureDeviceKind::Microphone;
    microphone.backend = CaptureBackend::DirectShow;
    microphone.available = true;
    CaptureLifecycleTest::installAudioDevices(s, wasapi, microphone);
    CHECK(s.audioMode() == QStringLiteral("system"));
    CHECK(s.setAudioMode(QStringLiteral("both")));
    CHECK(s.audioMode() == QStringLiteral("both"));
    CHECK(s.mixingEnabled());
    CHECK(s.setAudioMode(QStringLiteral("microphone")));
    CHECK(s.audioMode() == QStringLiteral("microphone"));
    CHECK(!s.mixingEnabled());
    CHECK(s.setAudioMode(QStringLiteral("none")));
    CHECK(s.audioMode() == QStringLiteral("none"));
    CaptureConfig config;
    CHECK(CaptureLifecycleTest::resolveConfig(s, &config).ok);
    CHECK(!config.audioEnabled);
    CHECK(config.audioInput.deviceId.isEmpty());
    CHECK(!config.microphoneMixInput && !config.audioFallbackInput);
    CHECK(s.setMixMicrophone(microphone.id));
    CHECK(!s.setMixMicrophone(QStringLiteral("missing")));
    CHECK(s.audioMode() == QStringLiteral("both"));
    CHECK(s.clearMix());
    CHECK(s.audioMode() == QStringLiteral("system"));
    CHECK(s.setMixMicrophone(microphone.id));
    CHECK(s.selectAudioDevice(QStringLiteral("probe-audio"))); // DirectShow clears mix.
    CHECK(s.audioMode() == QStringLiteral("system"));
    CHECK(!s.audioMixingAvailable());
    CHECK(!s.setMixMicrophone(microphone.id));
    CHECK(s.selectAudioDevice(wasapi.id));
    CHECK(s.audioMixingAvailable());
    return true;
}

bool audioModeRefusedWhileActive()
{
    Fixture f;
    auto &s = *f.scheduler;
    CaptureDevice system, mic;
    system.id = "wasapi"; system.kind = CaptureDeviceKind::SystemAudio;
    system.backend = CaptureBackend::Wasapi; system.available = true;
    mic.id = "mic"; mic.kind = CaptureDeviceKind::Microphone; mic.available = true;
    CaptureLifecycleTest::installAudioDevices(s, system, mic);
    CHECK(s.setMixMicrophone(mic.id));
    const auto opener = f.opener(true, Mode::Result, false);
    CHECK(s.start());
    const auto refuses = [&] {
        return !s.setAudioMode("none") && !s.clearMix() && !s.setMixMicrophone(mic.id)
            && !s.selectMicrophoneDevice(mic.id) && !s.changeAudioSource(false)
            && s.audioMode() == "both" && s.mixMicrophoneId() == mic.id;
    };
    CHECK(refuses()); // Preparing
    opener->release.store(true);
    CHECK(waitFor([&] { return opener->posted.load() == 1; }));
    deliver(&s);
    CHECK(s.state() == CaptureSchedulerState::Running && refuses());
    s.pause();
    CHECK(s.state() == CaptureSchedulerState::Paused && refuses());
    s.stop();
    CHECK(s.setAudioMode("none"));
    return true;
}

bool offThreadControlRejected()
{
    Fixture f;
    auto &s = *f.scheduler;
    const auto probe = f.opener(true, Mode::Result, false);
    CHECK(s.start());
    const auto id = CaptureLifecycleTest::generation(s);
    bool started = true;
    bool videoChanged = true;
    bool audioChanged = true;
    bool videoSelected = true;
    bool audioSelected = true;
    std::thread worker([&] {
        started = s.start();
        s.stop();
        s.pause();
        s.resume();
        videoChanged = s.changeVideoSource(false);
        audioChanged = s.changeAudioSource(false);
        videoSelected = s.selectVideoDevice(QStringLiteral("probe-video"));
        audioSelected = s.selectAudioDevice(QStringLiteral("probe-audio"));
        s.refreshDevices();
    });
    worker.join(); // No CHECK or other early return while worker is joinable.
    CHECK(!started && !videoChanged && !audioChanged && !videoSelected && !audioSelected);
    CHECK(s.state() == CaptureSchedulerState::Preparing);
    CHECK(CaptureLifecycleTest::generation(s) == id);
    CHECK(!CaptureLifecycleTest::cancelRequested(*CaptureLifecycleTest::session(s)));
    CHECK(probe->posted.load() == 0);
    return true;
}

bool destructorJoinsBlockedOpener()
{
    Fixture f;
    const auto probe = f.opener(true, Mode::Result, false);
    CHECK(f.scheduler->start());
    CHECK(waitFor([&] { return probe->entered.load(); }));
    CHECK(!probe->finished.load());
    f.scheduler.reset(); // Gate stays closed: only destructor cancellation helps.
    CHECK(probe->finished.load());
    CHECK(probe->posted.load() == 0);
    return true;
}

bool destructorDiscardsPostedCallbacks()
{
    Fixture f;
    MetaCallCounter counter;
    f.scheduler->installEventFilter(&counter);
    QPointer<CaptureScheduler> lifetime(f.scheduler.get());
    const auto probe = f.opener();
    CHECK(f.scheduler->start());
    CHECK(waitFor([&] { return probe->posted.load() == 1; }));
    CHECK(f.scheduler->state() == CaptureSchedulerState::Preparing);
    CHECK(counter.calls == 0);

    int sentinelCalls = 0;
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> pendingToken = token;
    CHECK(QMetaObject::invokeMethod(f.scheduler.get(), [token, &sentinelCalls] {
        ++sentinelCalls;
    }, Qt::QueuedConnection));
    token.reset();
    CHECK(!pendingToken.expired());
    f.scheduler.reset(); // Real startup callback and sentinel are both queued.
    CHECK(lifetime.isNull());
    CHECK(probe->finished.load());
    CHECK(pendingToken.expired()); // Pending functor was destroyed, not leaked.
    QObject witness;
    bool queueDrained = false;
    CHECK(QMetaObject::invokeMethod(&witness, [&] { queueDrained = true; }, Qt::QueuedConnection));
    deliver(nullptr);
    CHECK(queueDrained);
    CHECK(counter.calls == 0);
    CHECK(sentinelCalls == 0);
    return true;
}
bool realSessionInputFormatFailureTwice()
{
    Fixture f;
    auto &s = *f.scheduler;
    const QString missingFormat = QStringLiteral("__capture_lifecycle_missing_format_7ac913__");
    // Fail closed if this name ever becomes a registered format: never open a
    // real device or reach mux output creation even in that environment.
    CHECK(av_find_input_format(missingFormat.toUtf8().constData()) == nullptr);
    const QString outputName = QStringLiteral("must-not-exist-%1.mp4")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    CHECK(!QFileInfo::exists(outputName));
    CaptureLifecycleTest::installRealFormatFailure(s, outputName, missingFormat);
    std::uint64_t previousId = CaptureLifecycleTest::generation(s);
    for (int attempt = 0; attempt < 2; ++attempt) {
        CHECK(!f.recorder.isRecording());
        CHECK(s.start()); // Real startRecord + FFOpenSourceEvent, no fake worker.
        CHECK(CaptureLifecycleTest::generation(s) > previousId);
        previousId = CaptureLifecycleTest::generation(s);
        CHECK(waitFor([&] {
            // Pump the real queued startup result and recorder notifications.
            deliver(nullptr);
            return s.state() == CaptureSchedulerState::Failed;
        }));
        CHECK(s.lastError() == QStringLiteral("Unable to prepare the selected capture input."));
        // Failed is published before bounded cleanup, so the UI/AutoTest can
        // report an unresponsive native driver. stop() below joins that cleanup.
        auto *session = CaptureLifecycleTest::session(s);
        CHECK(session != nullptr);
        CHECK(!session->isStarted());
        CHECK(session->config().audioInput.kind == CaptureDeviceKind::SystemAudio);
        CHECK(!QFileInfo::exists(outputName));
        s.stop(); // Joins the real opener; must not hang or rerun live cleanup.
        CHECK(s.state() == CaptureSchedulerState::Idle);
        CHECK(s.lastError().isEmpty());
        CHECK(CaptureLifecycleTest::session(s) == nullptr);
        CHECK(!f.recorder.isRecording());
        CHECK(!QFileInfo::exists(outputName));
        deliver(nullptr);
        f.recorder.clearCaptureError();
    }
    return true;
}
// Full production Session -> OpenSourceEvent -> ScreenGrab timeout -> scheduler
// notification -> abort path. A generated NUT input replaces the audio device;
// no desktop or microphone is opened, even on PC3.
bool realVideoTimeoutClosesOpenedAudio()
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString input = directory.path() + QStringLiteral("/input.nut");
    AVFormatContext *output = nullptr;
    CHECK(avformat_alloc_output_context2(&output, nullptr, "nut", input.toStdString().c_str()) >= 0);
    struct OutputGuard {
        AVFormatContext *ctx;
        ~OutputGuard() { if (ctx) { if (ctx->pb) avio_closep(&ctx->pb); avformat_free_context(ctx); } }
    } outputGuard{output};
    AVStream *stream = avformat_new_stream(output, nullptr);
    CHECK(stream);
    stream->time_base = AVRational{1, 48000};
    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
    stream->codecpar->format = AV_SAMPLE_FMT_S16;
    stream->codecpar->sample_rate = 48000;
    stream->codecpar->bits_per_coded_sample = 16;
    stream->codecpar->block_align = 4;
    av_channel_layout_default(&stream->codecpar->ch_layout, 2);
    CHECK(avio_open(&output->pb, input.toStdString().c_str(), AVIO_FLAG_WRITE) >= 0);
    CHECK(avformat_write_header(output, nullptr) >= 0);
    AVPacket *packet = av_packet_alloc();
    CHECK(packet);
    struct PacketGuard { AVPacket *pkt; ~PacketGuard() { av_packet_free(&pkt); } } packetGuard{packet};
    CHECK(av_new_packet(packet, 4800 * 4) >= 0);
    std::fill(packet->data, packet->data + packet->size, 0);
    packet->pts = packet->dts = 0;
    packet->duration = 4800;
    packet->stream_index = stream->index;
    CHECK(av_write_frame(output, packet) >= 0);
    CHECK(av_write_trailer(output) >= 0);
    CHECK(avio_closep(&output->pb) >= 0);

    struct EnvironmentGuard {
        QByteArray oldScenario = qgetenv("SUBREC_AUTOTEST_SCENARIO");
        QByteArray oldHook = qgetenv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT");
        QByteArray oldLimit = qgetenv("SUBREC_STOP_TIMEOUT_MS");
        EnvironmentGuard() {
            // Linked application modules retain the runtime AutoTest gate.
            qputenv("SUBREC_AUTOTEST_SCENARIO", "record_seconds");
            qputenv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "1");
            qputenv("SUBREC_STOP_TIMEOUT_MS", "500");
        }
        ~EnvironmentGuard() {
            qputenv("SUBREC_AUTOTEST_SCENARIO", oldScenario);
            qputenv("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", oldHook);
            qputenv("SUBREC_STOP_TIMEOUT_MS", oldLimit);
        }
    } environment;
    Fixture f;
    auto &scheduler = *f.scheduler;
    CaptureLifecycleTest::installRealVideoTimeout(scheduler, directory.path(), input);
    CHECK(scheduler.start());
    CHECK(waitFor([&] {
        deliver(nullptr);
        return scheduler.state() == CaptureSchedulerState::Failed;
    }, 15s));
    CHECK(scheduler.lastError().contains(QStringLiteral("Timed out waiting for Desktop Duplication")));
    const auto stopBegin = std::chrono::steady_clock::now();
    scheduler.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stopBegin).count();
    CHECK(stopMs < 1000);
    CHECK(!f.recorder.isRecording());
    CHECK(!QFileInfo::exists(directory.path() + QStringLiteral("/must-not-exist.mp4")));
    auto *pipeline = f.recorder.getContext();
    for (size_t i = 0; i < FFRecordContextType::A_DECODER_SIZE; ++i) {
        CHECK(!pipeline->aDemuxer[i]->isOpen());
        CHECK(pipeline->aDemuxerThread[i]->waitFor(0ms));
        CHECK(pipeline->aDecoderThread[i]->waitFor(0ms));
    }
    CHECK(pipeline->aFilterThread->waitFor(0ms));
    CHECK(pipeline->aEncoderThread->waitFor(0ms));
    CHECK(pipeline->screenGrabThread->waitFor(0ms));
    std::printf("video startup timeout: stop_ms=%lld, all audio closed, no output file\n",
                static_cast<long long>(stopMs));
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::vector<std::pair<const char *, bool (*)()>> groups = {
        {"cancel_during_preparing", cancelDuringPreparing},
        {"stop_joins_outside_lock", stopJoinsOutsideLock},
        {"queued_old_generation_across_restart", queuedOldGenerationAcrossRestart},
        {"same_generation_success_failure_guards", sameGenerationGuards},
        {"restart_directly_from_failed", restartDirectlyFromFailed},
        {"double_stop", doubleStop},
        {"paused_rejects_start_and_configuration", pausedRejectsStartAndConfiguration},
        {"audio_mode_transitions", audioModeTransitions},
        {"audio_mode_refused_while_active", audioModeRefusedWhileActive},
        {"off_thread_control_rejected", offThreadControlRejected},
        {"destructor_joins_blocked_opener", destructorJoinsBlockedOpener},
        {"destructor_discards_posted_callbacks", destructorDiscardsPostedCallbacks},
        {"real_session_input_format_failure_twice", realSessionInputFormatFailureTwice},
        {"real_video_timeout_closes_opened_audio", realVideoTimeoutClosesOpenedAudio},
    };
    if (argc > 1 && std::string(argv[1]) == "--audio-modes") {
        groups = {{"audio_mode_transitions", audioModeTransitions},
                  {"audio_mode_refused_while_active", audioModeRefusedWhileActive}};
    }
    int failures = 0;
    {
        struct RecorderLifetime {
            ~RecorderLifetime() { FFRecorder::destoryInstance(); }
        } recorderLifetime;
        for (const auto &group : groups) {
            std::printf("RUN %s\n", group.first);
            std::fflush(stdout);
            const bool ok = group.second(); // CHECK unwinds group fixture first.
            std::printf("%s %s\n", group.first, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            if (!ok) ++failures;
        }
        std::puts("TEARDOWN FFRecorder (must return before SUMMARY)");
        std::fflush(stdout);
    } // No watchdog, detach, forced exit, or skipped production destruction.
    std::printf("SUMMARY %zu/%zu groups passed; recorder destruction returned\n",
                groups.size() - static_cast<std::size_t>(failures), groups.size());
    return failures == 0 ? 0 : 1; // QCoreApplication also undergoes normal destruction.
}
