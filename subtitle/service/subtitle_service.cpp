// M0 桩：SubtitleService 与 SubtitleJobRunner。真实实现归工作包 D；实现时拆成
// subtitle_service.cpp / subtitle_job_runner.cpp 并删除本文件。
// 桩行为：available=false、state="unavailable"；beginRecording 返回 nullptr（录制照常，不做字幕）。
// Package D: the preceding comments describe the replaced M0 stub.
// ---- SubtitleService ----
#include "subtitle/service/subtitle_service.h"
#include "subtitle/service/subtitle_job_runner.h"
#include "subtitle/host/asr_host_client_factory.h"
#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/tap/audio_tap.h"
#include "capture/test_hook_gate.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <map>

namespace {
using namespace subtitle;
QString hook(const char *name) { auto value = capture::test::hookEnvironment(name); return value ? QString::fromUtf8(value) : QString{}; }
std::unique_ptr<QSettings> settings() {
    auto ini = hook("SUBREC_TEST_ASR_SETTINGS");
    if (!ini.isEmpty()) return std::make_unique<QSettings>(ini, QSettings::IniFormat);
    return std::make_unique<QSettings>("SubRec", "SubRec");
}
QString hostPath() {
    auto path = hook("SUBREC_TEST_ASR_EXE");
    if (!path.isEmpty()) return path;
    path = QCoreApplication::applicationDirPath() + "/asr/subrec-asr";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return path;
}
struct Ingest {
    std::shared_ptr<AudioTap> tap;
    std::shared_ptr<PcmSpool> spool;
    std::unique_ptr<IngestWorker> worker;
    bool ended = false, finalized = false, reportedError = false;
};
// Created and destroyed on the subtitle thread. All disk and ingest joins happen here.
struct ServiceWorker : QObject {
    ServiceWorker(SubtitleJobRunner *runner) : QObject(runner), runner(runner), timer(this) {
        timer.setInterval(20);
        connect(&timer, &QTimer::timeout, this, [this] { poll(); }); timer.start();
    }
    SubtitleJobRunner *runner;
    SteadyClock clock;
    QTimer timer;
    std::map<JobId, Ingest> ingests;
    void begin(JobId id, QString video, std::shared_ptr<AudioTap> tap, EngineRequest request, LiveConfig live, bool realtime) {
        Ingest in; in.tap = tap; in.spool = std::make_shared<PcmSpool>();
        auto path = std::filesystem::u8path(video.toUtf8().toStdString()); path.replace_extension(".subtitle.wav");
        auto st = in.spool->create(path);
        if (st.ok()) { in.worker = std::make_unique<IngestWorker>(tap, in.spool, clock); st = in.worker->start(); }
        if (!st.ok()) { tap->abort(); emit runner->jobFailed(id, int(st.code), QString::fromStdString(st.message)); return; }
        ingests.emplace(id, std::move(in)); runner->setLiveConfig(live);
        runner->submit(id, video, QString::fromStdString(path.u8string()), request, {}, realtime);
    }
    void poll() {
        for (auto it = ingests.begin(); it != ingests.end();) {
            auto &[id, in] = *it;
            runner->notifyCommitted(id, in.spool->committedSamples());
            const auto splice = in.spool->takeSplice(); if (splice >= 0) runner->notifySplice(id, splice);
            auto error = in.worker->lastError();
            if (!error.ok() && !in.reportedError) {
                in.reportedError = true; runner->cancelJob(id);
                emit runner->levelChanged(id, 4, "spool"); emit runner->jobFailed(id, int(error.code), QString::fromStdString(error.message));
            }
            if (in.ended && in.worker->join(std::chrono::milliseconds(0))) {
                // publish/finalize happened before the successful join.
                runner->notifyCommitted(id, in.spool->committedSamples());
                const auto lastSplice = in.spool->takeSplice(); if (lastSplice >= 0) runner->notifySplice(id, lastSplice);
                runner->finishJob(id, in.spool->committedSamples(), in.finalized && !in.reportedError);
                it = ingests.erase(it);
            } else ++it;
        }
    }
    void end(JobId id, bool finalized) {
        auto it = ingests.find(id); if (it == ingests.end()) return;
        it->second.ended = true; it->second.finalized = finalized;
        // RecordingAudioHook's end is after the producer has stopped.
        it->second.tap->finish(); poll();
    }
    void shutdown(int timeoutMs) {
        QElapsedTimer elapsed; elapsed.start();
        // An active recording has no successful MP4 finalization yet. Drain it
        // without inventing endRecording(false), so Runner marks it incomplete.
        for (auto &[id, in] : ingests) in.tap->finish();
        for (auto &[id, in] : ingests) {
            in.worker->join(std::chrono::milliseconds(std::max(0, timeoutMs - int(elapsed.elapsed()))));
            runner->notifyCommitted(id, in.spool->committedSamples());
            if (in.ended) runner->finishJob(id, in.spool->committedSamples(), in.finalized);
        }
        runner->shutdown(std::max(0, timeoutMs - int(elapsed.elapsed())));
        timer.stop();
        for (auto &[id, in] : ingests) { in.tap->abort(); in.worker->requestStop(); }
        ingests.clear();
    }
};
struct ServiceState : QObject {
    explicit ServiceState(SubtitleService *q) : QObject(q), q(q), lineTimer(this) {
        qRegisterSubtitleMetaTypes();
        auto args = QStringList{}; auto fake = hook("SUBREC_TEST_ASR_FAKE");
        if (!fake.isEmpty()) args << "--engine=fake" << "--fake=" + fake;
        factory = std::make_unique<DefaultAsrHostClientFactory>(hostPath(), args);
        runner = new SubtitleJobRunner(factory.get());
        worker = new ServiceWorker(runner);
        runner->moveToThread(&thread); thread.setObjectName("subtitle");
        connect(&thread, &QThread::finished, runner, &QObject::deleteLater);
        connect(runner, &SubtitleJobRunner::stateChanged, this, [this](quint64 id, int state, double progress, double lag) {
            const auto name = state == 2 ? "finishing" : state == 3 ? "done" : state == 4 ? "failed" : state == 5 ? "unavailable" : level == 2 ? "lagging" : "live";
            emit this->q->jobProgress(videos[id], name, int(progress * 100));
            if (id != current) return;
            this->state = name; this->progress = progress; this->lag = lag;
            if (state == 5) available = false;
            emit this->q->changed();
        });
        connect(runner, &SubtitleJobRunner::jobFinished, this, [this](quint64 id, QString srt) {
            emit this->q->jobProgress(videos[id], "done", 100);
            if (id != current) return;
            lastSrt = srt; state = "done"; progress = 1; lag = 0; pendingConfirmed.clear(); pendingTentative.clear(); emit this->q->changed();
        });
        connect(runner, &SubtitleJobRunner::jobFailed, this, [this](quint64 id, int code, const QString &) {
            if (id != current) return;
            if (code == int(ErrorCode::JobPoisonSegment)) { error = QStringLiteral("有一段音频识别失败，已跳过"); emit this->q->changed(); return; }
            if (code == int(ErrorCode::JobCancelled)) state = "idle";
            else if (code == 300 || code == 301 || code == 303 || code == 305 || code == 700 || code == 701 || code == 704 || code == 710 || code == 711) { state = "unavailable"; available = false; }
            else state = "failed";
            error = code == int(ErrorCode::ShutdownIncomplete) ? QStringLiteral("字幕未完成，已保存恢复数据") : QStringLiteral("字幕处理失败（%1）").arg(code);
            pendingConfirmed.clear(); pendingTentative.clear();
            emit this->q->jobProgress(videos[id], state, int(progress * 100)); emit this->q->changed();
        });
        connect(runner, &SubtitleJobRunner::currentSplitChanged, this, [this](JobId id, QString c, QString t) {
            if (id != current) return; pendingConfirmed = c; pendingTentative = t;
        });
        connect(runner, &SubtitleJobRunner::levelChanged, this, [this](JobId id, int l, QString why) {
            if (id != current) return; level = l;
            reason = why == "oom" ? QStringLiteral("内存不足") : why == "rec" ? QStringLiteral("优先保证录制流畅") : why == "spool" ? QStringLiteral("字幕音频写入失败") : l == 3 ? QStringLiteral("录完后生成") : l == 2 ? QStringLiteral("字幕正在追赶") : l == 1 ? QStringLiteral("仅显示完整句子") : QString{};
            if (l >= 2) { pendingConfirmed.clear(); pendingTentative.clear(); }
            if (state == "live" || state == "lagging") state = l == 2 ? "lagging" : l == 4 ? "failed" : "live";
            emit this->q->changed();
        });
        lineTimer.setTimerType(Qt::PreciseTimer); lineTimer.setInterval(250);
        connect(&lineTimer, &QTimer::timeout, this, [this] {
            if (confirmed == pendingConfirmed && tentative == pendingTentative) return;
            confirmed = pendingConfirmed; tentative = pendingTentative; emit this->q->currentLineChanged();
        });
        lineTimer.start(); thread.start();
    }
    ~ServiceState() override {
        if (thread.isRunning()) {
            if (!closing) {
                auto w = worker; auto t = &thread;
                QMetaObject::invokeMethod(runner, [w, t] { w->shutdown(3000); t->quit(); }, Qt::QueuedConnection);
            }
            thread.wait(); // teardown only; endRecording/shutdown never wait on the GUI thread
        }
    }
    SubtitleService *q;
    std::unique_ptr<DefaultAsrHostClientFactory> factory;
    QThread thread; SubtitleJobRunner *runner = nullptr; ServiceWorker *worker = nullptr;
    QTimer lineTimer;
    JobId current = 0;
    std::map<JobId, QString> videos;
    bool available = false, closing = false, recording = false;
    QString mode = "off", state = "off", lastSrt, error, video, confirmed, tentative, pendingConfirmed, pendingTentative, reason;
    double progress = 0, lag = 0; int level = 0;
    EngineRequest request; LiveConfig live;
    SubtitleService::EngineRequestProvider engineRequestProvider;
};
ServiceState *stateOf(const SubtitleService *q) { for (auto c : q->children()) if (auto s = dynamic_cast<ServiceState *>(c)) return s; Q_UNREACHABLE(); }
}
SubtitleService::SubtitleService(QObject *parent) : QObject(parent) { new ServiceState(this); reloadSettings(); }
SubtitleService::~SubtitleService() { delete stateOf(this); }
void SubtitleService::reloadSettings() {
    auto s = stateOf(this); if (s->closing) return;
    auto cfg = settings(); s->mode = cfg->value("asr/mode", "off").toString();
    s->request.engineId = cfg->value("asr/engineId", "sherpa-onnx").toString().toStdString();
    s->request.modelId = cfg->value("asr/modelId", "sensevoice-small").toString().toStdString();
    s->request.modelDir = std::filesystem::u8path(cfg->value("asr/modelDir").toString().toUtf8().toStdString());
    s->request.language = cfg->value("asr/language", "zh").toString().toStdString();
    s->request.vad.speechPadMs = std::clamp(cfg->value("asr/speechPadMs", 150).toInt(), 0, 1000);
    s->request.threads = std::clamp(cfg->value("asr/threads", 1).toInt(), 1, 4);
    s->live.partialIntervalMs = std::max(0, cfg->value("asr/partialIntervalMs", 600).toInt());
    s->live.maxSegmentMs = std::max(100, cfg->value("asr/maxSegmentMs", 10000).toInt());
    s->live.agreementN = std::clamp(cfg->value("asr/agreementN", 2).toInt(), 2, 3);
    s->live.confirmGuardMs = std::max(0, cfg->value("asr/confirmGuardMs", 300).toInt());
    s->live.threadsWhileRecording = std::min(s->request.threads, QThread::idealThreadCount() > 4 ? 2 : 1);
    s->available = QFileInfo::exists(hostPath());
    if (!s->recording && s->state != "finishing") s->state = s->mode == "off" ? "off" : s->available ? "idle" : "unavailable";
    auto runner = s->runner; auto live = s->live;
    QMetaObject::invokeMethod(runner, [runner, live] { runner->setLiveConfig(live); }, Qt::QueuedConnection);
    emit changed();
}
std::shared_ptr<AudioTap> SubtitleService::beginRecording(const std::filesystem::path &path) noexcept {
    try {
        auto s = stateOf(this); if (s->closing || s->mode == "off" || !s->available || s->recording) return {};
        auto request = s->request;
        if (s->engineRequestProvider) {
            if (auto provided = s->engineRequestProvider(QString::fromStdString(s->request.modelId))) {
                request = std::move(*provided);
                // ModelManager resolves the installed model; these existing user
                // settings remain owned by Service. Keep other provider fields.
                request.language = s->request.language;
                request.threads = s->request.threads;
                request.vad.speechPadMs = s->request.vad.speechPadMs;
            }
        }
        auto tap = std::make_shared<AudioTap>(); const auto id = ++s->current;
        s->video = QString::fromStdString(path.u8string()); s->videos[id] = s->video;
        s->recording = true; s->state = "live"; s->level = s->mode == "after" ? 3 : 0;
        s->progress = s->lag = 0; s->error.clear(); s->lastSrt.clear(); s->reason.clear();
        s->pendingConfirmed.clear(); s->pendingTentative.clear();
        auto w = s->worker; auto video = s->video; auto live = s->live; const bool realtime = s->mode == "live";
        QMetaObject::invokeMethod(s->runner, [w, id, video, tap, request, live, realtime] { w->begin(id, video, tap, request, live, realtime); }, Qt::QueuedConnection);
        emit changed(); return tap;
    } catch (...) { return {}; }
}
void SubtitleService::endRecording(bool finalized) noexcept {
    try {
        auto s = stateOf(this); if (!s->recording || s->closing) return;
        s->recording = false; s->state = finalized ? "finishing" : "idle";
        auto w = s->worker; auto id = s->current;
        QMetaObject::invokeMethod(s->runner, [w, id, finalized] { w->end(id, finalized); }, Qt::QueuedConnection); emit changed();
    } catch (...) {}
}
void SubtitleService::onRecorderHealth(const subtitle::RecorderHealth &health) {
    auto s = stateOf(this); if (s->closing) return;
    auto runner = s->runner;
    QMetaObject::invokeMethod(runner, [runner, health] { runner->onHealthSample(health); }, Qt::QueuedConnection);
}
void SubtitleService::setEngineRequestProvider(EngineRequestProvider provider) {
    auto s = stateOf(this); if (s->closing) return;
    s->engineRequestProvider = std::move(provider);
}
bool SubtitleService::available() const { return stateOf(this)->available; }
QString SubtitleService::state() const { return stateOf(this)->state; }
qreal SubtitleService::progress() const { return stateOf(this)->progress; }
qreal SubtitleService::lagSeconds() const { return stateOf(this)->lag; }
QString SubtitleService::lastSrtPath() const { return stateOf(this)->lastSrt; }
QString SubtitleService::lastError() const { return stateOf(this)->error; }
QString SubtitleService::currentJobVideoPath() const { return stateOf(this)->video; }
QString SubtitleService::currentLine() const { return currentConfirmed() + currentTentative(); }
QString SubtitleService::currentConfirmed() const { return stateOf(this)->confirmed; }
QString SubtitleService::currentTentative() const { return stateOf(this)->tentative; }
int SubtitleService::degradeLevel() const { return stateOf(this)->level; }
QString SubtitleService::degradeReason() const { return stateOf(this)->reason; }
QString SubtitleService::refineState() const { return {}; }
qreal SubtitleService::refineProgress() const { return 0; }
void SubtitleService::openLastSrtFolder() {
    const auto folder = QFileInfo(lastSrtPath()).absolutePath(); if (lastSrtPath().isEmpty()) return;
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(folder)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {folder});
#else
    QProcess::startDetached("xdg-open", {folder});
#endif
}
bool SubtitleService::regenerate(const QString &) { return false; } // M3
void SubtitleService::cancelRefine() {} // M2
void SubtitleService::cancelCurrent() { auto s = stateOf(this); if (s->closing) return; auto r = s->runner; auto id = s->current; QMetaObject::invokeMethod(r, [r, id] { r->cancelJob(id); }, Qt::QueuedConnection); }
void SubtitleService::restoreLive() { auto s = stateOf(this); if (s->closing) return; auto r = s->runner; auto id = s->current; QMetaObject::invokeMethod(r, [r, id] { r->restoreLive(id); }, Qt::QueuedConnection); }
void SubtitleService::shutdown(int timeoutMs) {
    auto s = stateOf(this); if (s->closing) return; s->closing = true; s->recording = false;
    auto w = s->worker; auto t = &s->thread;
    QMetaObject::invokeMethod(s->runner, [w, t, timeoutMs] { w->shutdown(std::max(0, timeoutMs)); t->quit(); }, Qt::QueuedConnection);
}
