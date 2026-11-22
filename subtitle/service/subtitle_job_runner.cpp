// ---- SubtitleJobRunner ----
// Package D orchestration; public interfaces remain frozen.
#include "subtitle/service/subtitle_job_runner.h"
#include "subtitle/host/asr_host_client_factory.h"
#include "subtitle/host/asr_protocol.h"
#include "subtitle/live/host_watchdog.h"
#include "subtitle/live/degrade_controller.h"
#include "subtitle/live/subtitle_merger.h"
#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "capture/test_hook_gate.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
using namespace subtitle;
std::filesystem::path pathOf(const QString &s) { return std::filesystem::u8path(s.toUtf8().toStdString()); }
QString stringOf(const std::filesystem::path &p) { return QString::fromStdString(p.u8string()); }
// Private wire between Runner and Service: 0 queued, 1 live, 2 finishing,
// 3 done, 4 failed, 5 unavailable. No public enum is added to M0.
int normalizedExit(int code) {
    if (uint32_t(code) == proto::kStatusIllegalInstruction) return proto::kExitCpuUnsupported;
#ifndef Q_OS_WIN
    for (int full : {301, 303, 305, 700, 701, 703, 704, 711}) if (code == (full & 255)) return full;
#endif
    return code;
}
bool unavailableCode(int c) {
    return c == 300 || c == 301 || c == 303 || c == 305 || c == 700 || c == 701 || c == 704 || c == 710 || c == 711;
}
int testDuration(int ms) {
    const char *value = capture::test::hookEnvironment("SUBREC_TEST_ASR_TIME_SCALE");
    if (!value) return ms;
    bool ok = false; const double scale = QString::fromLatin1(value).toDouble(&ok);
    return ok && std::isfinite(scale) ? std::max(10, int(ms * std::clamp(scale, 0.001, 1.0))) : ms;
}
WatchdogConfig watchdogConfig() {
    WatchdogConfig c;
    c.minDecodeTimeoutMs = testDuration(c.minDecodeTimeoutMs);
    // Keep sample-derived RTF limit intact: slow legitimate decode must not be killed.
    c.backoffBaseMs = testDuration(c.backoffBaseMs); c.backoffMaxMs = testDuration(c.backoffMaxMs);
    return c;
}
DegradeThresholds thresholds() {
    DegradeThresholds c;
    c.l0l1LagHoldMs = testDuration(c.l0l1LagHoldMs); c.l0l1RtfHoldMs = testDuration(c.l0l1RtfHoldMs);
    c.l0l1SysCpuHoldMs = testDuration(c.l0l1SysCpuHoldMs); c.l1l2LagHoldMs = testDuration(c.l1l2LagHoldMs);
    c.l1l2RtfHoldMs = testDuration(c.l1l2RtfHoldMs); c.l1l0HoldMs = testDuration(c.l1l0HoldMs);
    c.l2l1HoldMs = testDuration(c.l2l1HoldMs); c.minTransitionIntervalMs = testDuration(c.minTransitionIntervalMs);
    c.recWindowMs = testDuration(c.recWindowMs); return c;
}
struct Job {
    Job(JobId id, QString video, QString spool, EngineRequest request, CueConfig cues, bool live, LiveConfig config, IClock &clock)
        : id(id), video(std::move(video)), spool(std::move(spool)), request(std::move(request)), live(live),
          agreement(config.agreementN, config.confirmGuardMs), fixer(FixerRules{}),
          merger(doc, cues, fixer, agreement, clock), degrade(thresholds()), watchdog(watchdogConfig(), clock) {
        srt = srtPathForMp4(pathOf(this->video)); merger.setLiveConfig(config);
        if (!live) degrade.forceLevel(DegradeLevel::L3Deferred, "after");
    }
    JobId id; QString video, spool; EngineRequest request; bool live;
    bool paused = false;
    bool finishing = false, writeSrt = true, terminal = false, unavailable = false, restored = false, deferredRetry = false, afterRetry = false;
    qint64 committed = 0, total = 0, resume = 0, busyBegin = 0, busyEnd = 0, done = 0;
    int attempts = 0, ooms = 0;
    double rtf = 0, decodeTotal = 0, partialDecodeMs = 0;
    qint64 busyAt = 0;
    qint64 decodedEnd = 0, rtfAt = 0;
    SegId nextId = 0;
    std::map<int64_t, SegId> segmentIds;
    std::filesystem::path srt;
    SubtitleDocument doc; LocalAgreement agreement; SentenceFixer fixer; SubtitleMerger merger;
    DegradeController degrade; HostWatchdog watchdog;
    SegId segmentId(int64_t begin) {
        auto [it, inserted] = segmentIds.emplace(begin, nextId);
        if (inserted) ++nextId;
        return it->second;
    }
};
struct RunnerState : QObject {
    explicit RunnerState(SubtitleJobRunner *q, AsrHostClientFactory *factory)
        : QObject(q), q(q), factory(factory), poll(this), retry(this) {
        if (!factory) { ownedFactory = std::make_unique<DefaultAsrHostClientFactory>(QString{}); this->factory = ownedFactory.get(); }
        poll.setInterval(std::min(1000, testDuration(1000))); poll.start(); retry.setSingleShot(true);
        connect(&poll, &QTimer::timeout, this, [this] { tick(); });
        connect(&retry, &QTimer::timeout, this, [this] { launch(); });
    }
    SubtitleJobRunner *q;
    AsrHostClientFactory *factory;
    std::unique_ptr<AsrHostClientFactory> ownedFactory;
    std::unique_ptr<AsrHostClient> host;
    SteadyClock clock;
    LiveConfig config;
    RecorderHealth health;
    QTimer poll, retry;
    std::map<JobId, std::unique_ptr<Job>> jobs;
    std::deque<JobId> queued;
    Job *active = nullptr;
    bool opened = false, closing = false, expectedExit = false;
    int exitOverride = 0;
    QString confirmed, tentative;
#ifdef Q_OS_WIN
    uint64_t cpuIdle = 0, cpuKernel = 0, cpuUser = 0, procCpu = 0;
    qint64 cpuAt = 0;
#endif
    Job *find(JobId id) { auto i = jobs.find(id); return i == jobs.end() ? nullptr : i->second.get(); }
    qint64 resume(Job &j) { return std::max<qint64>(j.resume, j.doc.resumePoint16k()); }
    void report(Job &j) {
        const int state = j.terminal ? (j.unavailable ? 5 : 4) : (j.finishing ? 2 : 1);
        emit q->stateChanged(j.id, state, j.committed ? std::clamp(double(j.done) / j.committed, 0.0, 1.0) : 0.0,
                             std::max<qint64>(0, j.committed - j.done) / 16000.0);
    }
    void display(Job &j, bool clear = false) {
        auto p = (clear || j.paused) ? std::optional<ConfirmedPrefix>{} : j.merger.current();
        QString c = p ? QString::fromStdString(p->confirmedText) : QString{};
        QString t = p ? QString::fromStdString(p->tentativeText) : QString{};
        if (c == confirmed && t == tentative) return;
        confirmed = c; tentative = t;
        emit q->currentSplitChanged(j.id, c, t); emit q->currentLineChanged(j.id, c + t);
    }
    void detachHost() {
        retry.stop(); opened = false;
        if (!host) return;
        host->disconnect(this); host->kill();
        // Defer deletion when called from a host signal. Its QProcess is reaped in its destructor.
        host.release()->deleteLater();
    }
    void fail(Job &j, int code, QString message) {
        if (j.terminal) return;
        j.terminal = true; j.unavailable = unavailableCode(code);
        j.degrade.forceLevel(DegradeLevel::L4Failed, "failure"); j.merger.onLevelChanged(DegradeLevel::L4Failed);
        j.merger.flushPart(srtPartPath(j.srt), {});
        emit q->levelChanged(j.id, 4, "failure"); display(j, true); report(j);
        emit q->jobFailed(j.id, code, message);
        if (&j == active) { detachHost(); active = nullptr; next(); }
    }
    void level(Job &j, DegradeLevel l, const std::string &reason) {
        j.degrade.forceLevel(l, reason); j.merger.onLevelChanged(l);
        if (&j == active && host && opened) {
            if (l == DegradeLevel::L3Deferred && !j.finishing) host->pauseJob(j.id);
            else {
                host->resumeJob(j.id);
                host->setLevel(j.id, l, j.finishing ? std::clamp(j.request.threads, 1, 4) : (l >= DegradeLevel::L2CatchUp ? 1 : std::clamp(config.threadsWhileRecording, 1, 2)),
                               l == DegradeLevel::L0Live ? config.partialIntervalMs : 0);
            }
        }
        emit q->levelChanged(j.id, int(l), QString::fromStdString(reason)); display(j); report(j);
    }
    void finishCommand(Job &j) {
        if (!host || !opened) return;
        host->setPriority(false); host->resumeJob(j.id);
        host->setLevel(j.id, j.degrade.level(), std::clamp(j.request.threads, 1, 4), 0);
        host->append(j.id, j.total);
        // C1 may apply finish before feeding the final append. A terminal splice
        // ensures the final input is closed even in that ordering (C issue in notes).
        host->splice(j.id, j.total);
        host->finish(j.id, j.total);
    }
    void launch() {
        if (!active || active->terminal || (closing && !active->finishing)) return;
        auto &j = *active;
        detachHost(); expectedExit = false; exitOverride = 0;
        host = factory->create("live");
        if (!host) { fail(j, int(ErrorCode::HostStartFailed), "Factory returned no host"); return; }
        j.watchdog.onProcessStarted(); j.merger.onHostRestarted(); display(j, true);
        connect(host.get(), &AsrHostClient::ready, this, [this](const QStringList &) {
            if (!active) return;
            active->watchdog.onLoadStarted(); auto req = active->request;
            if (!active->finishing && active->live) { req.backend = Backend::Cpu; req.threads = std::clamp(config.threadsWhileRecording, 1, QThread::idealThreadCount() > 4 ? 2 : 1); }
            host->load(req, config);
        });
        connect(host.get(), &AsrHostClient::loaded, this, [this](const QString &backend, const QString &) {
            if (!active) return; auto &j = *active;
            j.watchdog.onLoaded(); j.watchdog.setBackend(proto::parseBackend(backend.toStdString()).value_or(Backend::Cpu));
            j.decodeTotal = 0; j.decodedEnd = resume(j);
            host->open(j.id, j.spool, resume(j), j.committed, j.live && !j.finishing); opened = true;
            for (auto at : j.doc.hardBoundaries16k()) if (at > resume(j)) host->splice(j.id, at);
            level(j, j.degrade.level(), j.degrade.reason());
            if (j.finishing) finishCommand(j);
        });
        connect(host.get(), &AsrHostClient::heartbeat, this, [this](qint64 seq, int) { if (active) active->watchdog.onHeartbeat(seq); });
        connect(host.get(), &AsrHostClient::busy, this, [this](qint64 b, qint64 e, qint64 n) {
            if (!active) return; active->busyAt = clock.nowMs(); active->busyBegin = b; active->busyEnd = e; active->watchdog.onBusy(b, e, n);
        });
        connect(host.get(), &AsrHostClient::partialReady, this, [this](PartialSegment p) {
            if (!active || p.job != active->id) return;
            active->partialDecodeMs += std::max<qint64>(0, clock.nowMs() - active->busyAt);
            active->watchdog.onIdle(); p.id = active->segmentId(p.begin16k); active->merger.onPartial(p); display(*active);
        });
        connect(host.get(), &AsrHostClient::finalReady, this, [this](FinalSegment f) {
            if (!active || f.job != active->id) return; auto &j = *active;
            j.watchdog.onIdle();
            if (f.end16k > f.begin16k) {
                const auto now = clock.nowMs();
                const double r = (f.decodeMs + j.partialDecodeMs) / ((f.end16k - f.begin16k) / 16.0);
                const double weight = j.rtfAt ? 1 - std::exp(-double(now - j.rtfAt) / 30000.0) : 1;
                j.rtf += weight * (r - j.rtf); j.rtfAt = now; j.partialDecodeMs = 0;
            }
            f.id = j.segmentId(f.begin16k); j.merger.onFinal(f);
            j.done = std::max<qint64>(j.done, f.end16k); display(j);
            auto st = j.merger.flushPart(srtPartPath(j.srt), {});
            if (!st.ok()) fail(j, int(st.code), QString::fromStdString(st.message));
        });
        connect(host.get(), &AsrHostClient::progress, this, [this](JobId id, qint64 done, double decode) {
            if (!active || id != active->id) return; auto &j = *active;
            j.decodedEnd = done; j.decodeTotal = decode; j.done = std::max(j.done, done); report(j);
        });
        connect(host.get(), &AsrHostClient::jobDone, this, [this](JobId id) {
            if (!active || active->id != id || !active->finishing) return;
            complete(*active);
        });
        connect(host.get(), &AsrHostClient::failed, this, [this](JobId id, int code, const QString &message) {
            if (!active || (id && id != active->id)) return;
            if (code == int(ErrorCode::HostProtocolError)) { exitOverride = code; host->kill(); }
            else fail(*active, code, message);
        });
        connect(host.get(), &AsrHostClient::exited, this, [this](int raw, bool crashed) {
            opened = false;
            if (!active || expectedExit || active->terminal) return;
            auto &j = *active; int code = exitOverride ? exitOverride : normalizedExit(raw); exitOverride = 0;
            if (unavailableCode(code)) { fail(j, code == proto::kExitCpuUnsupported ? int(ErrorCode::HostCpuUnsupported) : code, "ASR component unavailable"); return; }
            const auto plan = j.watchdog.onExit(code, crashed, resume(j));
            j.merger.onHostRestarted(); display(j, true);
            if (code == proto::kExitOutOfMemory) {
                ++j.ooms; level(j, j.ooms == 1 ? DegradeLevel::L1FinalOnly : DegradeLevel::L3Deferred, "oom");
                if (j.ooms >= 2 && !j.finishing) { j.deferredRetry = true; return; }
            }
            if (plan.poison) {
                j.doc.markPoison(j.busyBegin, plan.skipTo16k); j.resume = plan.skipTo16k;
                emit q->jobFailed(j.id, int(ErrorCode::JobPoisonSegment), "Skipped repeatedly failing audio interval");
            }
            if (plan.forceCpu) j.request.backend = Backend::Cpu;
            if (!plan.allowed || j.afterRetry) {
                if (j.finishing) { fail(j, int(ErrorCode::HostRestartExhausted), "ASR recovery exhausted"); return; }
                j.deferredRetry = true; level(j, DegradeLevel::L3Deferred, "restartExhausted"); return;
            }
            emit q->hostRestarting(++j.attempts, plan.delayMs); retry.start(plan.delayMs);
        });
        host->start(j.live && !j.finishing);
    }
    Status sidecar(Job &j, bool incomplete) {
        QJsonObject object;
        object["incomplete"] = incomplete;
        object["segments"] = QJsonDocument::fromJson(QByteArray::fromStdString(toJson(j.doc.segmentsForSidecar()))).array();
        auto p = j.srt; p.replace_extension(".subrec.json");
        const auto bytes = QJsonDocument(object).toJson();
        return writeFileAtomic(p, std::string_view(bytes.constData(), size_t(bytes.size())));
    }
    void complete(Job &j) {
        Status st = j.writeSrt ? j.merger.finalize(j.srt, {}) : j.merger.flushPart(srtPartPath(j.srt), {});
        if (!st.ok()) { fail(j, int(st.code), QString::fromStdString(st.message)); return; }
        st = sidecar(j, false);
        if (!st.ok()) { fail(j, int(st.code), QString::fromStdString(st.message)); return; }
        j.terminal = true;
        if (j.writeSrt) { QFile::remove(stringOf(srtPartPath(j.srt))); emit q->stateChanged(j.id, 3, 1, 0); emit q->jobFinished(j.id, stringOf(j.srt)); }
        else emit q->jobFailed(j.id, int(ErrorCode::JobCancelled), "Recording was not finalized");
        display(j, true); detachHost(); active = nullptr; next();
    }
    void next() {
        if (active || closing) return;
        while (!queued.empty()) {
            auto j = find(queued.front()); queued.pop_front();
            if (!j || j->terminal) continue;
            active = j; launch(); return;
        }
    }
    void tick() {
        if (!active) return; auto &j = *active;
        if (host && host->isRunning() && !expectedExit) {
            const auto verdict = j.watchdog.poll(clock.nowMs());
            if (verdict != HostWatchdog::Verdict::Ok) {
                if (verdict == HostWatchdog::Verdict::KillLoadTimeout) { fail(j, int(ErrorCode::EngineLoadFailed), "ASR load timed out"); return; }
                exitOverride = int(verdict == HostWatchdog::Verdict::KillDecodeTimeout ? ErrorCode::HostDecodeTimeout : ErrorCode::HostHeartbeatLost);
                host->kill();
            }
        }
        if (!j.finishing) {
            LoadSample s; s.nowMs = clock.nowMs(); s.lagSec = std::max<qint64>(0, j.committed - j.done) / 16000.0; s.rtfEwma = j.rtf; s.health = health;
#ifdef Q_OS_WIN
            auto u64 = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            FILETIME idle{}, kernel{}, user{};
            if (GetSystemTimes(&idle, &kernel, &user)) {
                auto i = u64(idle), k = u64(kernel), u = u64(user), total = k - cpuKernel + u - cpuUser;
                if (cpuAt && total) s.sysCpu = 100.0 * (1.0 - double(i - cpuIdle) / total);
                cpuIdle = i; cpuKernel = k; cpuUser = u;
            }
            if (host) {
                auto process = host->findChild<QProcess *>();
                auto h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process ? DWORD(process->processId()) : 0);
                FILETIME created{}, ended{};
                if (h) {
                    if (GetProcessTimes(h, &created, &ended, &kernel, &user)) {
                        auto n = u64(kernel) + u64(user);
                        if (cpuAt && n >= procCpu && s.nowMs > cpuAt) s.hostCpuCores = double(n - procCpu) / ((s.nowMs - cpuAt) * 10000.0);
                        procCpu = n;
                    }
                    CloseHandle(h);
                }
            }
            cpuAt = s.nowMs;
#endif
            if (auto changed = j.degrade.tick(s)) level(j, changed->first, changed->second);
        }
        display(j); report(j);
        if (j.merger.shouldFlushPart(clock.nowMs())) {
            const auto st = j.merger.flushPart(srtPartPath(j.srt), {});
            if (!st.ok()) fail(j, int(st.code), QString::fromStdString(st.message));
        }
    }
};
RunnerState *state(const SubtitleJobRunner *q) {
    for (auto c : q->children()) if (auto s = dynamic_cast<RunnerState *>(c)) return s;
    Q_UNREACHABLE();
}
}
SubtitleJobRunner::SubtitleJobRunner(AsrHostClientFactory *factory, QObject *parent) : QObject(parent) { new RunnerState(this, factory); }
SubtitleJobRunner::~SubtitleJobRunner() {
    auto s = state(this); s->closing = true;
    if (s->host) s->host->disconnect(s);
    delete s; // unique_ptr destroys/reaps the final host even without an event loop
}
void SubtitleJobRunner::submit(quint64 id, QString video, QString spool, EngineRequest request, CueConfig cues, bool live) {
    auto s = state(this);
    // Completed jobs no longer need a document; keep only active/queued work.
    for (auto it = s->jobs.begin(); it != s->jobs.end();) {
        if (it->second->terminal && it->second.get() != s->active) it = s->jobs.erase(it); else ++it;
    }
    if (s->closing || s->find(id)) { emit jobFailed(id, int(ErrorCode::InvalidArgument), "Duplicate job or service shut down"); return; }
    auto j = std::make_unique<Job>(id, video, spool, request, cues, live, s->config, s->clock);
    if (s->active) { s->active->merger.onHostRestarted(); s->display(*s->active, true); s->queued.push_back(s->active->id); s->detachHost(); }
    s->active = j.get(); s->jobs.emplace(id, std::move(j));
    s->report(*s->active); if (live) s->launch(); else s->level(*s->active, DegradeLevel::L3Deferred, "after");
}
void SubtitleJobRunner::notifyCommitted(quint64 id, qint64 n) { auto s = state(this); if (auto j = s->find(id); j && !j->terminal) { if (n > j->committed) j->paused = false; j->committed = std::max(j->committed, n); if (j == s->active && s->opened) s->host->append(id, j->committed); } }
void SubtitleJobRunner::notifySplice(quint64 id, qint64 n) { auto s = state(this); if (auto j = s->find(id); j && !j->terminal) { j->paused = true; j->merger.onSplice(n); s->display(*j, true); if (j == s->active && s->opened) s->host->splice(id, n); } }
void SubtitleJobRunner::finishJob(quint64 id, qint64 total, bool writeSrt) {
    auto s = state(this); auto j = s->find(id); if (!j || j->terminal) return;
    j->paused = false; j->finishing = true; j->writeSrt = writeSrt; j->total = std::max<qint64>(0, total); j->committed = j->total; s->report(*j);
    if (!writeSrt) { if (j == s->active) s->complete(*j); else { j->terminal = true; j->merger.flushPart(srtPartPath(j->srt), {}); emit jobFailed(id, int(ErrorCode::JobCancelled), "Recording was not finalized"); } return; }
    if (j != s->active) return;
    if (j->deferredRetry) { j->afterRetry = true; j->watchdog.resetForJob(); s->launch(); }
    else if (!s->host) s->launch();
    else if (s->opened) s->finishCommand(*j);
}
void SubtitleJobRunner::cancelJob(quint64 id) { auto s = state(this); if (auto j = s->find(id)) s->fail(*j, int(ErrorCode::JobCancelled), "Subtitle job cancelled"); }
void SubtitleJobRunner::setLiveConfig(LiveConfig c) {
    auto s = state(this);
    const bool reload = c.maxSegmentMs != s->config.maxSegmentMs || c.endpointSilenceMs != s->config.endpointSilenceMs || c.appendThrottleMs != s->config.appendThrottleMs;
    s->config = c;
    for (auto &[id, j] : s->jobs) if (!j->terminal) { j->merger.setLiveConfig(c); if (j.get() == s->active) s->level(*j, j->degrade.level(), j->degrade.reason()); }
    if (reload && s->active && s->opened) s->launch();
}
void SubtitleJobRunner::onHealthSample(RecorderHealth h) { state(this)->health = h; }
void SubtitleJobRunner::onPauseSplice(JobId id) { auto s = state(this); if (auto j = s->find(id)) notifySplice(id, j->committed); }
void SubtitleJobRunner::restoreLive(JobId id) {
    auto s = state(this); auto j = s->find(id);
    if (!j || j->terminal || j->finishing || j->restored || j->degrade.level() != DegradeLevel::L3Deferred) return;
    j->restored = true; s->level(*j, DegradeLevel::L2CatchUp, "manual");
    if (j == s->active && (!s->host || !s->host->isRunning())) s->launch();
}
void SubtitleJobRunner::shutdown(int timeoutMs) {
    auto s = state(this); if (s->closing) return; s->closing = true;
    QElapsedTimer elapsed; elapsed.start();
    // Nested event processing is confined to the subtitle thread, allowing final/done
    // and quit/kill deadlines to complete without blocking the GUI's event loop.
    while (s->active && s->active->finishing && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    s->retry.stop(); s->poll.stop(); s->expectedExit = true;
    for (auto &[id, j] : s->jobs) if (!j->terminal) {
        j->merger.onHostRestarted();
        const auto st = j->writeSrt && j->finishing ? j->merger.finalize(j->srt, {}) : j->merger.flushPart(srtPartPath(j->srt), {});
        s->sidecar(*j, true); j->terminal = true;
        emit jobFailed(id, int(st.ok() ? ErrorCode::ShutdownIncomplete : st.code), "Subtitle output incomplete at shutdown");
    }
    if (s->host) {
        s->host->stop(std::max(0, timeoutMs - int(elapsed.elapsed())));
        while (s->host->isRunning() && elapsed.elapsed() < timeoutMs) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); }
        s->host->kill();
    }
}

// M0 puts Q_OBJECT on the class declaration line; explicitly request moc
// so CMake automoc does not miss that frozen spelling.
#include "moc_subtitle_job_runner.cpp"
