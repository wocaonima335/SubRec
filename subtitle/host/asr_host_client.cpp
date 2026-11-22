// M0 桩：AsrHostClient 与 DefaultAsrHostClientFactory。真实实现归工作包 D
// （QProcess、Job Object、优先级、JSON-lines 收发、append 节流）。
// 桩行为：不启动进程；命令方法为空操作；start 立即发出 failed(0, NotSupported)。

// Package D: the preceding comments describe the replaced M0 stub.
#include "subtitle/host/asr_host_client.h"
#include "subtitle/host/asr_host_client_factory.h"
#include "subtitle/host/asr_protocol.h"
#include "capture/test_hook_gate.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <map>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
using namespace subtitle;
// QObject ownership keeps all implementation state in this translation unit;
// the frozen headers have no implementation-owned private section.
struct ClientState : QObject {
    explicit ClientState(AsrHostClient *q) : QObject(q), q(q), process(this), appendTimer(this), stopTimer(this) {
        appendTimer.setSingleShot(true);
        stopTimer.setSingleShot(true);
        connect(&appendTimer, &QTimer::timeout, this, [this] { flush(); });
        connect(&stopTimer, &QTimer::timeout, &process, &QProcess::kill);
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] { read(); });
        connect(&process, &QProcess::readyReadStandardError, this, [this] {
            const auto bytes = process.readAllStandardError();
            if (!logWindow.isValid() || logWindow.elapsed() >= 1000) { logWindow.start(); logBytes = 0; }
            const auto keep = std::min<qsizetype>(bytes.size(), std::max(0, 4096 - logBytes));
            if (keep) { logBytes += int(keep); emit this->q->hostLog("stderr", QString::fromUtf8(bytes.constData(), keep)); }
        });
        connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            read(); stopTimer.stop(); appendTimer.stop(); pending.clear();
            emit this->q->exited(code, status == QProcess::CrashExit);
        });
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                emit this->q->failed(0, int(QFileInfo::exists(process.program()) ? ErrorCode::HostBlocked : ErrorCode::HostMissing), process.errorString());
        });
    }
    ~ClientState() override {
        process.disconnect(this);
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
#ifdef Q_OS_WIN
        if (job) CloseHandle(job);
#endif
    }
    AsrHostClient *q;
    QProcess process;
    QTimer appendTimer, stopTimer;
    QElapsedTimer logWindow, lastAppend;
    int logBytes = 0, throttle = 200;
    QByteArray input;
    bool invalid = false, greeted = false;
    std::map<JobId, qint64> pending;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
    qint64 assignedPid = 0;
#endif
    void send(const proto::Message &msg) {
        if (process.state() == QProcess::Running) {
            auto line = proto::encode(msg); line += '\n';
            process.write(line.data(), qint64(line.size()));
        }
    }
    void flush() {
        for (const auto &[id, n] : pending) send(proto::Append{id, n});
        pending.clear(); lastAppend.start(); appendTimer.stop();
    }
    void bad(ErrorCode code, const QString &message) {
        if (invalid) return;
        invalid = true; input.clear();
        emit q->failed(0, int(code), message);
        q->stop(100);
    }
    void read() {
        // Read bounded chunks: an attacker cannot grow the line accumulator without limit.
        process.setReadChannel(QProcess::StandardOutput);
        while (process.bytesAvailable() > 0) {
            auto chunk = process.read(16384);
            if (invalid) continue;
            input += chunk;
            for (;;) {
                auto e = input.indexOf('\n');
                if (e < 0) break;
                if (e > qsizetype(proto::kMaxLineBytes)) { bad(ErrorCode::HostProtocolError, "Protocol line exceeds 1 MiB"); break; }
                auto line = input.left(e); input.remove(0, e + 1);
                Status error;
                auto msg = proto::decode(std::string_view(line.constData(), size_t(line.size())), &error);
                if (!msg) {
                    if (error.code == ErrorCode::NotSupported) { emit q->hostLog("warn", QString::fromStdString(error.message)); continue; }
                    bad(ErrorCode::HostProtocolError, QString::fromStdString(error.message)); break;
                }
                if (auto h = std::get_if<proto::Hello>(&*msg)) {
                    if (h->protocol != proto::kProtocolVersion) { bad(ErrorCode::HostVersionMismatch, "ASR protocol version mismatch"); break; }
                    if (!greeted) { greeted = true; QStringList backends; for (auto &b : h->backends) backends << QString::fromStdString(b); emit q->ready(backends); }
                } else if (!greeted && !std::holds_alternative<proto::Error>(*msg) && !std::holds_alternative<proto::Log>(*msg)) {
                    bad(ErrorCode::HostProtocolError, "Message before hello"); break;
                } else if (auto v = std::get_if<proto::Loaded>(&*msg)) emit q->loaded(QString::fromStdString(v->backend), QString::fromStdString(v->fallbackReason));
                else if (auto v = std::get_if<proto::PartialMsg>(&*msg)) emit q->partialReady(v->seg);
                else if (auto v = std::get_if<proto::FinalMsg>(&*msg)) emit q->finalReady(v->seg);
                else if (auto v = std::get_if<proto::Heartbeat>(&*msg)) emit q->heartbeat(v->seq, v->rssMB);
                else if (auto v = std::get_if<proto::Busy>(&*msg)) emit q->busy(v->b16k, v->e16k, v->e16k - v->b16k);
                else if (auto v = std::get_if<proto::Progress>(&*msg)) emit q->progress(v->jobId, v->done16k, v->decodeMsTotal);
                else if (auto v = std::get_if<proto::Done>(&*msg)) emit q->jobDone(v->jobId);
                else if (auto v = std::get_if<proto::Cancelled>(&*msg)) emit q->jobCancelled(v->jobId);
                else if (auto v = std::get_if<proto::Error>(&*msg)) emit q->failed(v->jobId, v->code, QString::fromStdString(v->message));
                else if (auto v = std::get_if<proto::Log>(&*msg)) emit q->hostLog(QString::fromStdString(v->level), QString::fromStdString(v->text));
            }
            if (input.size() > qsizetype(proto::kMaxLineBytes)) bad(ErrorCode::HostProtocolError, "Protocol line exceeds 1 MiB");
        }
    }
};
ClientState *state(const AsrHostClient *q) {
    for (auto c : q->children()) if (auto s = dynamic_cast<ClientState *>(c)) return s;
    Q_UNREACHABLE();
}
}

AsrHostClient::AsrHostClient(QString exePath, QObject *parent)
    : QObject(parent), m_exePath(std::move(exePath)), m_role(QStringLiteral("live")) { new ClientState(this); }
AsrHostClient::~AsrHostClient() { delete state(this); }
void AsrHostClient::setRole(const QString &role) { m_role = role; }
void AsrHostClient::setExtraArguments(const QStringList &args) { m_extraArgs = args; }
void AsrHostClient::start(bool lowPriority) {
    auto s = state(this);
    if (isRunning()) return;
    s->input.clear(); s->invalid = s->greeted = false;
    s->pending.clear(); s->stopTimer.stop(); s->appendTimer.stop(); s->lastAppend.invalidate();
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove("SUBREC_ASR_TEST_HOOKS");
    if (capture::test::hooksEnabled()) env.insert("SUBREC_ASR_TEST_HOOKS", "1");
    s->process.setProcessEnvironment(env);
#ifdef Q_OS_WIN
    s->process.setCreateProcessArgumentsModifier([lowPriority](QProcess::CreateProcessArguments *a) {
        if (lowPriority) a->flags |= BELOW_NORMAL_PRIORITY_CLASS;
    });
#else
    Q_UNUSED(lowPriority);
#endif
    // Replace this connection on subsequent starts, without removing protocol handlers.
    s->process.disconnect(&s->process, &QProcess::started, this, nullptr);
    connect(&s->process, &QProcess::started, this, [this, s] {
        const auto limits = m_role == "refine" ? JobLimits::forRefine() : JobLimits::forLive(QThread::idealThreadCount());
#ifdef Q_OS_WIN
        if (!assignJobObject(limits)) { emit failed(0, int(ErrorCode::HostBlocked), "Cannot assign ASR Job Object"); kill(); return; }
#else
        Q_UNUSED(limits);
#endif
        proto::Hello hello; hello.role = m_role.toStdString(); s->send(hello);
    });
    s->process.start(m_exePath, QStringList{"--role=" + m_role, "--parent-pid=" + QString::number(QCoreApplication::applicationPid())} + m_extraArgs);
}
void AsrHostClient::stop(int graceMs) {
    auto s = state(this); s->pending.clear(); s->appendTimer.stop();
    if (!isRunning()) return;
    s->send(proto::Quit{}); s->process.closeWriteChannel();
    if (graceMs <= 0) kill(); else if (!s->stopTimer.isActive()) s->stopTimer.start(graceMs);
}
void AsrHostClient::kill() { state(this)->process.kill(); }
bool AsrHostClient::isRunning() const { return state(this)->process.state() != QProcess::NotRunning; }
void AsrHostClient::load(const EngineRequest &r, const LiveConfig &l) { auto s = state(this); s->throttle = std::max(0, l.appendThrottleMs); s->send(proto::Load{r, l, {}}); }
void AsrHostClient::open(JobId id, const QString &path, qint64 r, qint64 c, bool live) { state(this)->send(proto::Open{id, path.toUtf8().toStdString(), r, c, live}); }
void AsrHostClient::append(JobId id, qint64 n) {
    auto s = state(this); s->pending[id] = n;
    const auto remaining = s->lastAppend.isValid() ? s->throttle - s->lastAppend.elapsed() : 0;
    if (remaining <= 0) s->flush(); else if (!s->appendTimer.isActive()) s->appendTimer.start(int(remaining));
}
void AsrHostClient::splice(JobId id, qint64 n) { state(this)->flush(); state(this)->send(proto::Splice{id, n}); }
void AsrHostClient::setLevel(JobId id, DegradeLevel l, int t, int p) { state(this)->send(proto::SetLevel{id, int(l), t, p}); }
void AsrHostClient::pauseJob(JobId id) { state(this)->send(proto::Pause{id}); }
void AsrHostClient::resumeJob(JobId id) { state(this)->send(proto::Resume{id}); }
void AsrHostClient::finish(JobId id, qint64 n) { state(this)->flush(); state(this)->send(proto::Finish{id, n}); }
void AsrHostClient::cancel(JobId id) { state(this)->pending.erase(id); state(this)->send(proto::Cancel{id}); }
void AsrHostClient::refine(JobId id, const QString &path, const QVector<RefineRequestSegment> &segs, const QString &prompt) {
    proto::Refine r; r.jobId = id; r.spoolPath = path.toUtf8().toStdString(); r.prompt = prompt.toStdString();
    for (auto &s : segs) r.segments.push_back({s.id, s.b16k, s.e16k});
    state(this)->send(r);
}
void AsrHostClient::setPriority(bool belowNormal) {
#ifdef Q_OS_WIN
    auto h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, DWORD(state(this)->process.processId()));
    if (h) { SetPriorityClass(h, belowNormal ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS); CloseHandle(h); }
#else
    Q_UNUSED(belowNormal);
#endif
}
bool AsrHostClient::assignJobObject(const JobLimits &limits) {
#ifdef Q_OS_WIN
    auto s = state(this);
    const auto pid = s->process.processId();
    if (!pid) return false;
    if (s->assignedPid != pid) {
        if (s->job) CloseHandle(s->job);
        s->job = CreateJobObjectW(nullptr, nullptr); s->assignedPid = 0;
    }
    if (!s->job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (limits.killOnJobClose) info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (limits.dieOnUnhandledException) info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (limits.processMemoryBytes) { info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY; info.ProcessMemoryLimit = SIZE_T(limits.processMemoryBytes); }
    if (!SetInformationJobObject(s->job, JobObjectExtendedLimitInformation, &info, sizeof(info))) return false;
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    if (limits.cpuRate) { cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP; cpu.CpuRate = std::clamp<DWORD>(limits.cpuRate, 1, 10000); }
    if (!SetInformationJobObject(s->job, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu))) return false;
    if (s->assignedPid == pid) return true;
    auto h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!h) return false;
    const bool ok = AssignProcessToJobObject(s->job, h); CloseHandle(h);
    if (ok) s->assignedPid = pid;
    return ok;
#else
    Q_UNUSED(limits); return false;
#endif
}
DefaultAsrHostClientFactory::DefaultAsrHostClientFactory(QString path, QStringList args)
    : m_exePath(std::move(path)), m_extraArguments(std::move(args)) {
    if (m_exePath.isEmpty()) {
        m_exePath = QCoreApplication::applicationDirPath() + "/asr/subrec-asr";
#ifdef Q_OS_WIN
        m_exePath += ".exe";
#endif
    }
}
std::unique_ptr<AsrHostClient> DefaultAsrHostClientFactory::create(const QString &role) {
    auto client = std::make_unique<AsrHostClient>(m_exePath); client->setRole(role); client->setExtraArguments(m_extraArguments); return client;
}

// M0 puts Q_OBJECT on the class declaration line; explicitly request moc
// so CMake automoc does not miss that frozen spelling.
#include "moc_asr_host_client.cpp"
