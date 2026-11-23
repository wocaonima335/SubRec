// subrec-asr 运行时（工作包 C，design-live-v2.md §9.1 右侧框）。
//
// 线程
// - IO（main）：ProtocolIo 读命令、命令分发、load；stdin EOF / 协议错误 → 退出。
// - 心跳：每 kHeartbeatIntervalMs 发 hb{seq, state, rssMB}；fakeHangAllActive 置位后停发（§11.1 hangAll）。
// - 解码：SpoolTailReader 尾随 spool → 能量 VAD → SegmentStateMachine → IAsrEngine；
//   每次解码前发 busy，partial/final 后发 progress；finish 全部 final 发完 → done。
// - 父看门：ParentWatch（独立实现文件）。
//
// 命令路由：append 只推进 committed（原子）；splice/setLevel/pause/resume/finish/cancel 入共享队列，
// 由解码线程在下一个循环应用。role=refine 的实例只接受 hello/load/refine/cancel/quit（acceptedByRole）。

#include "host_runtime.h"

#include "asr_engine.h"
#include "engine_fake.h"
#include "fake_hooks.h"
#include "host_options.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <cstdlib>
#endif

#ifndef SUBREC_ASR_VERSION
#define SUBREC_ASR_VERSION "0.0.0-dev"
#endif

namespace asrhost {

namespace {

enum HostState { kStateIdle = 0, kStateLoading = 1, kStateDecoding = 2, kStatePaused = 3 };

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int currentRssMb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pm{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm))
        return static_cast<int>(pm.WorkingSetSize / (1024 * 1024));
    return 0;
#elif defined(__linux__)
    std::FILE *f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    char line[256];
    int rss = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "VmRSS: %d kB", &rss) == 1)
            break;
    }
    std::fclose(f);
    return rss / 1024;
#else
    return 0;
#endif
}

// 假引擎可能携带 illegalInstr（启动即退出）；--fake 与 load.fakeOptions 两处入口都检查。
bool fakeOptionsRequestIllegalInstr(const HostOptions &opt)
{
    if (opt.engine != "fake" || !fakeEngineAllowed() || opt.fakeOptions.empty())
        return false;
    FakeEngineOptions fo;
    subtitle::Status st;
    return parseFakeOptions(opt.fakeOptions, fo, &st) && fo.illegalInstr;
}

class HostRuntime {
public:
    explicit HostRuntime(const HostOptions &opt) : m_opt(opt) {}

    int run();

private:
    struct JobInfo {
        subtitle::JobId id = 0;
        bool live = true;
        int64_t resumeFrom16k = 0;
        std::string spoolPath;
    };

    // IO 线程写、解码线程读的控制面。
    struct Shared {
        std::mutex mu;
        std::condition_variable cv;
        struct Cmd {
            enum Kind { Splice, SetPartial } kind;
            int64_t a = 0;
        };
        std::vector<Cmd> cmds;
        bool paused = false;
        bool cancelled = false;
        bool finishing = false;
        int64_t finishTotal = 0;
        bool stop = false;
    };

    void handleLoad(const subtitle::proto::Load &ld);
    void handleOpen(const subtitle::proto::Open &op);
    void handleRefine(const subtitle::proto::Refine &rf);
    void startHeartbeat();
    void stopHeartbeat();
    void startDecodeLive(JobInfo job);
    void startDecodeRefine(subtitle::proto::Refine rf);
    void stopDecodeThread();
    void decodeLoopLive(JobInfo job);
    void decodeLoopRefine(subtitle::proto::Refine rf);
    void sendError(subtitle::JobId job, subtitle::Status st);
    void setState(int s) { m_state.store(s, std::memory_order_relaxed); }

    HostOptions m_opt;
    ProtocolIo m_io;
    bool m_ioProtocolError = false;

    std::unique_ptr<IAsrEngine> m_engine;
    std::unique_ptr<IVad> m_vad;
    subtitle::LiveConfig m_live;

    Shared m_sh;
    std::thread m_decodeThread;
    bool m_decodeRefineMode = false;
    std::unique_ptr<SpoolTailReader> m_reader;

    std::thread m_hbThread;
    std::atomic<bool> m_hbStop{false};
    std::atomic<int> m_state{kStateIdle};
    std::atomic<double> m_decodeMsTotal{0.0};
    std::atomic<bool> m_jobActive{false};
};

void HostRuntime::sendError(subtitle::JobId job, subtitle::Status st)
{
    m_io.write(subtitle::proto::Error{job, static_cast<int>(st.code), st.message});
}

void HostRuntime::handleLoad(const subtitle::proto::Load &ld)
{
    setState(kStateLoading);
    stopDecodeThread();   // 引擎/vad 将被替换，旧解码线程不得再触碰

    const std::string engineId = ld.request.engineId.empty() ? m_opt.engine : ld.request.engineId;
    std::string fakeOpts = ld.fakeOptions.empty() ? m_opt.fakeOptions : ld.fakeOptions;
    if (!fakeOpts.empty() && engineId == "fake" && fakeEngineAllowed()) {
        FakeEngineOptions fo;
        subtitle::Status st;
        if (parseFakeOptions(fakeOpts, fo, &st) && fo.illegalInstr)
            injectIllegalInstrExit();
    }

    subtitle::Status st;
    auto engine = createEngine(engineId, fakeOpts, &st);
    if (!engine) {
        sendError(0, st);
        setState(kStateIdle);
        return;
    }
    auto vad = createVad(ld.request, &st);
    if (!vad) {
        sendError(0, st);
        setState(kStateIdle);
        return;
    }

    const int64_t t0 = nowMs();
    std::string backend;
    st = engine->load(ld.request, &backend);
    if (!st.ok()) {
        sendError(0, st);
        setState(kStateIdle);
        return;
    }
    const int64_t loadMs = nowMs() - t0;

    m_engine = std::move(engine);
    m_vad = std::move(vad);
    m_live = ld.live;
    setState(kStateIdle);
    m_io.write(subtitle::proto::Loaded{backend.empty() ? std::string("cpu") : backend, loadMs, ""});
}

void HostRuntime::handleOpen(const subtitle::proto::Open &op)
{
    stopDecodeThread();
    if (!m_engine || !m_vad) {
        sendError(op.jobId,
                  subtitle::Status::error(subtitle::ErrorCode::NotReady, "open before load"));
        return;
    }
    m_reader = std::make_unique<SpoolTailReader>();
    subtitle::Status st = m_reader->open(op.spoolPath);
    if (!st.ok()) {
        m_reader.reset();
        sendError(op.jobId, st);
        return;
    }
    m_reader->setCommitted(op.committed16k);

    JobInfo job;
    job.id = op.jobId;
    job.live = op.live;
    job.resumeFrom16k = std::max<int64_t>(0, op.resumeFrom16k);
    job.spoolPath = op.spoolPath;
    m_decodeMsTotal.store(0.0, std::memory_order_relaxed);
    m_jobActive.store(true, std::memory_order_relaxed);
    setState(kStateDecoding);
    startDecodeLive(job);
}

void HostRuntime::handleRefine(const subtitle::proto::Refine &rf)
{
    stopDecodeThread();
    if (!m_engine) {
        sendError(rf.jobId, subtitle::Status::error(subtitle::ErrorCode::NotReady, "refine before load"));
        return;
    }
    m_reader = std::make_unique<SpoolTailReader>();
    subtitle::Status st = m_reader->open(rf.spoolPath);
    if (!st.ok()) {
        m_reader.reset();
        sendError(rf.jobId, st);
        return;
    }
    m_reader->setCommitted(INT64_MAX);   // 精修读的是已定稿的完整 spool
    m_jobActive.store(true, std::memory_order_relaxed);
    setState(kStateDecoding);
    startDecodeRefine(rf);
}

void HostRuntime::startHeartbeat()
{
    m_hbThread = std::thread([this] {
        int64_t seq = 0;
        int64_t nextAt = nowMs();
        while (!m_hbStop.load(std::memory_order_relaxed)) {
            const int64_t now = nowMs();
            if (now < nextAt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    static_cast<int>(std::min<int64_t>(nextAt - now, 20))));
                continue;
            }
            nextAt = now + subtitle::proto::kHeartbeatIntervalMs;
            if (m_hbStop.load(std::memory_order_relaxed))
                break;
            if (fakeHangAllActive().load(std::memory_order_relaxed))
                continue;   // hangAll 注入：hb 停
            const int s = m_state.load(std::memory_order_relaxed);
            const char *state = s == kStateLoading ? "loading"
                                : s == kStateDecoding ? "decoding"
                                : s == kStatePaused ? "paused"
                                                    : "idle";
            m_io.write(subtitle::proto::Heartbeat{seq++, state, currentRssMb()});
        }
    });
}

void HostRuntime::stopHeartbeat()
{
    m_hbStop.store(true, std::memory_order_relaxed);
    if (m_hbThread.joinable())
        m_hbThread.join();
}

void HostRuntime::startDecodeLive(JobInfo job)
{
    m_decodeRefineMode = false;
    m_decodeThread = std::thread([this, job] { decodeLoopLive(job); });
}

void HostRuntime::startDecodeRefine(subtitle::proto::Refine rf)
{
    m_decodeRefineMode = true;
    m_decodeThread = std::thread([this, rf = std::move(rf)] { decodeLoopRefine(rf); });
}

void HostRuntime::stopDecodeThread()
{
    {
        std::lock_guard<std::mutex> lock(m_sh.mu);
        m_sh.stop = true;
    }
    m_sh.cv.notify_all();
    if (m_engine)
        m_engine->requestCancel();
    if (m_decodeThread.joinable())
        m_decodeThread.join();
    std::lock_guard<std::mutex> lock(m_sh.mu);
    m_sh.cmds.clear();
    m_sh.paused = false;
    m_sh.cancelled = false;
    m_sh.finishing = false;
    m_sh.finishTotal = 0;
    m_sh.stop = false;
}

void HostRuntime::decodeLoopLive(JobInfo job)
{
    // 段 ID 接续（subtitle_types.h SegId 契约「重启后从 resume 处继续编号」）：不加协议字段，
    // 由 open.resumeFrom16k 确定性推出起始编号 firstId = resumeFrom16k。依据：
    // - 单调 / 不回退：同一次 open 内 id 在 [firstId, firstId+c) 连续分配，每个已终结段占
    //   [b,e) ≥ 1 个不重叠样本，故下一次 open 的 resumeFrom16k（= 已接受 final 的 end16k）
    //   严格大于本次 open 用过的全部 id；跨多次重启归纳成立。
    // - 同段不冲突：重启后 VAD 从静音初始化，段起点 ≥ resumeFrom16k（minBegin 钳制），
    //   新 id ≥ resumeFrom16k > 历史全部 id；与重启前同一段的编号相比只增不减。
    // - 故障注入（crashAtSeg 等）保持按本地段号计数，见 fakeInjectionSegBase。
    SegmentStateMachine ssm;
    ssm.configure(m_live, job.resumeFrom16k, job.resumeFrom16k);
    fakeInjectionSegBase().store(job.resumeFrom16k, std::memory_order_relaxed);
    m_vad->reset(job.resumeFrom16k);

    int64_t fedUpto = job.resumeFrom16k;
    bool finishApplied = false;
    bool aborted = false;
    subtitle::SegId partialSeg = -1;
    uint32_t partialSeq = 0;
    std::vector<float> pcm;
    std::vector<std::pair<int64_t, int64_t>> closed;
    // splice 按位置排队：先把音频喂到拼接点、再强制封口（IO 线程可能在解码线程观察到语音
    // 开始之前就已送达 splice，若立即应用会是 no-op）。到达顺序即位置顺序（主进程按序发送）。
    std::deque<int64_t> pendingSplices;

    for (; !aborted;) {
        bool finishing = false;
        int64_t finishTotal = 0;
        {
            std::unique_lock<std::mutex> lock(m_sh.mu);
            m_sh.cv.wait_for(lock, std::chrono::milliseconds(20), [this] {
                return m_sh.stop || m_sh.cancelled || m_sh.finishing || !m_sh.cmds.empty();
            });
            if (m_sh.stop || m_sh.cancelled)
                break;
            for (const Shared::Cmd &cmd : m_sh.cmds) {
                if (cmd.kind == Shared::Cmd::Splice)
                    pendingSplices.push_back(cmd.a);
                else
                    ssm.setPartialIntervalMs(static_cast<int>(cmd.a));
            }
            m_sh.cmds.clear();
            if (m_sh.paused) {
                setState(kStatePaused);
                m_sh.cv.wait(lock, [this] {
                    return m_sh.stop || m_sh.cancelled || m_sh.finishing || !m_sh.paused
                        || !m_sh.cmds.empty();
                });
                if (m_sh.stop || m_sh.cancelled)
                    break;
                for (const Shared::Cmd &cmd : m_sh.cmds) {
                    if (cmd.kind == Shared::Cmd::Splice)
                        pendingSplices.push_back(cmd.a);
                    else
                        ssm.setPartialIntervalMs(static_cast<int>(cmd.a));
                }
                m_sh.cmds.clear();
                setState(kStateDecoding);
            }
            finishing = m_sh.finishing;
            finishTotal = m_sh.finishTotal;
        }

        // ---- 尾随读取新增音频并喂 VAD（不越过最近的拼接点）----
        // 先喂后 finish：finish 生效前，已提交（committed16k / total16k 以内）样本必须全部
        // 过 VAD，否则 finish 抢跑会把末尾语音整段丢掉（C2 问题 2）。
        int64_t committed = m_reader->committed();
        if (finishing)
            committed = std::min(committed, finishTotal);
        if (!pendingSplices.empty())
            committed = std::min(committed, pendingSplices.front());
        if (fedUpto < committed) {
            subtitle::Status st = m_reader->read(fedUpto, committed, pcm);
            if (!st.ok()) {
                sendError(job.id, st);
                break;
            }
            m_vad->feed(pcm.data(), pcm.size(), fedUpto, closed);
            fedUpto = committed;
            // closed 段自带 [b, e)，成对通知（段可能在一批 feed 内完整开合，不能只靠 inSpeech() 补开段）。
            for (const auto &seg : closed) {
                ssm.onSpeechStart(seg.first);
                ssm.onSpeechEnd(seg.second);
            }
            closed.clear();
            // 仍未封口的段在此开段（partial 调度 / 后续 splice 封口都需要 ssm 已开段）；
            // ssm 已开段时 onSpeechStart 是 no-op（maxSegment 强切后 ssm 已在切点重开）。
            if (m_vad->inSpeech())
                ssm.onSpeechStart(m_vad->openSegmentBegin16k());
        }
        // 拼接点：音频已喂到该位置、VAD 状态已反映 [0, at)，现在强制封口。
        while (!pendingSplices.empty() && fedUpto >= pendingSplices.front()) {
            const int64_t at = pendingSplices.front();
            pendingSplices.pop_front();
            m_vad->forceCut(at);
            ssm.onSplice(at);
        }

        // ---- finish：音频已喂到 committed / total16k，现在封口（自然端点，不标 forcedCut）----
        if (finishing && !finishApplied) {
            m_vad->finish(closed);
            for (const auto &seg : closed) {
                ssm.onSpeechStart(seg.first);
                ssm.onSpeechEnd(seg.second);
            }
            closed.clear();
            ssm.onFinish(finishTotal);
            finishApplied = true;
        }

        // ---- 出 partial / final ----
        for (;;) {
            const auto act = ssm.next(committed, nowMs(), false);
            if (act.kind == SegmentStateMachine::Action::None)
                break;
            if (act.e16k <= act.b16k)
                continue;
            if (act.kind == SegmentStateMachine::Action::Partial) {
                if (act.id != partialSeg) {
                    partialSeg = act.id;
                    partialSeq = 0;
                } else {
                    ++partialSeq;
                }
                subtitle::Status st = m_reader->read(act.b16k, act.e16k, pcm);
                if (!st.ok()) {
                    sendError(job.id, st);
                    aborted = true;
                    break;
                }
                m_io.write(subtitle::proto::Busy{job.id, "partial", act.b16k, act.e16k});
                subtitle::FinalSegment tmp;
                tmp.id = act.id;
                tmp.begin16k = act.b16k;
                tmp.end16k = act.e16k;
                st = m_engine->recognize(pcm.data(), pcm.size(), RecognizeKind::Partial, tmp);
                if (!st.ok()) {
                    sendError(job.id, st);
                    aborted = true;
                    break;
                }
                // partial 也是解码开销：decodeMsTotal 是总解码耗时（供 RTF EWMA，协议 §4 progress），
                // 只计 final 会低估 RTF（C2 问题 4）。
                m_decodeMsTotal.store(m_decodeMsTotal.load(std::memory_order_relaxed) + tmp.decodeMs,
                                      std::memory_order_relaxed);
                subtitle::PartialSegment p;
                p.job = job.id;
                p.id = act.id;
                p.begin16k = act.b16k;
                p.upto16k = act.e16k;
                p.text = tmp.text;
                p.tokens = tmp.words;
                p.seq = partialSeq;
                m_io.write(subtitle::proto::PartialMsg{p});
            } else {
                subtitle::Status st = m_reader->read(act.b16k, act.e16k, pcm);
                if (!st.ok()) {
                    sendError(job.id, st);
                    aborted = true;
                    break;
                }
                m_io.write(subtitle::proto::Busy{job.id, "final", act.b16k, act.e16k});
                subtitle::FinalSegment f;
                f.job = job.id;
                f.id = act.id;
                f.begin16k = act.b16k;
                f.end16k = act.e16k;
                f.forcedCut = act.forcedCut;
                f.pass = 1;
                st = m_engine->recognize(pcm.data(), pcm.size(), RecognizeKind::Final, f);
                if (!st.ok()) {
                    sendError(job.id, st);
                    aborted = true;
                    break;
                }
                m_decodeMsTotal.store(m_decodeMsTotal.load(std::memory_order_relaxed) + f.decodeMs,
                                      std::memory_order_relaxed);
                m_io.write(subtitle::proto::FinalMsg{f});
                m_io.write(subtitle::proto::Progress{job.id, act.e16k,
                                                     m_decodeMsTotal.load(std::memory_order_relaxed)});
            }
        }
        if (aborted)
            break;

        if (finishing && finishApplied && fedUpto >= finishTotal && ssm.idle() && !m_vad->inSpeech()) {
            m_io.write(subtitle::proto::Done{job.id});
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_sh.mu);
        if (m_sh.cancelled)
            m_io.write(subtitle::proto::Cancelled{job.id});
    }
    m_jobActive.store(false, std::memory_order_relaxed);
    setState(kStateIdle);
}

void HostRuntime::decodeLoopRefine(subtitle::proto::Refine rf)
{
    fakeInjectionSegBase().store(0, std::memory_order_relaxed);   // refine 段号来自请求，本就是调用方编号
    std::vector<float> pcm;
    for (const subtitle::proto::RefineSegment &seg : rf.segments) {
        bool stopNow = false;
        {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            stopNow = m_sh.stop || m_sh.cancelled;
        }
        if (stopNow)
            break;
        subtitle::Status st = m_reader->read(seg.b16k, seg.e16k, pcm);
        if (!st.ok()) {
            sendError(rf.jobId, st);
            return;
        }
        m_io.write(subtitle::proto::Busy{rf.jobId, "refine", seg.b16k, seg.e16k});
        subtitle::FinalSegment f;
        f.job = rf.jobId;
        f.id = seg.id;
        f.begin16k = seg.b16k;
        f.end16k = seg.e16k;
        f.pass = 2;
        st = m_engine->recognize(pcm.data(), pcm.size(), RecognizeKind::Refine, f);
        if (!st.ok()) {
            sendError(rf.jobId, st);
            return;
        }
        m_io.write(subtitle::proto::FinalMsg{f});
    }
    {
        std::lock_guard<std::mutex> lock(m_sh.mu);
        if (m_sh.cancelled) {
            m_io.write(subtitle::proto::Cancelled{rf.jobId});
            m_jobActive.store(false, std::memory_order_relaxed);
            setState(kStateIdle);
            return;
        }
    }
    m_io.write(subtitle::proto::Done{rf.jobId});
    m_jobActive.store(false, std::memory_order_relaxed);
    setState(kStateIdle);
}

int HostRuntime::run()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    subtitle::Status st = m_io.install();
    if (!st.ok()) {
        std::fprintf(stderr, "subrec-asr: %s\n", st.message.c_str());
        return static_cast<int>(subtitle::ErrorCode::HostStartFailed);
    }

    if (fakeOptionsRequestIllegalInstr(m_opt))
        injectIllegalInstrExit();

    // ---- 握手 ----
    auto first = m_io.readCommand();
    if (!first)
        return m_io.hadProtocolError() ? static_cast<int>(subtitle::ErrorCode::HostProtocolError)
                                       : subtitle::proto::kExitOk;
    if (const auto *hello = std::get_if<subtitle::proto::Hello>(&*first)) {
        if (hello->protocol != subtitle::proto::kProtocolVersion) {
            m_io.write(subtitle::proto::Error{0,
                                              static_cast<int>(subtitle::ErrorCode::HostVersionMismatch),
                                              "protocol version mismatch"});
            return subtitle::proto::kExitVersionMismatch;
        }
        if (hello->role != m_opt.role)
            std::fprintf(stderr, "subrec-asr: hello role '%s' != --role=%s (using --role)\n",
                         hello->role.c_str(), m_opt.role.c_str());
    } else {
        m_io.write(subtitle::proto::Error{
            0, static_cast<int>(subtitle::ErrorCode::HostProtocolError), "first message must be hello"});
        return static_cast<int>(subtitle::ErrorCode::HostProtocolError);
    }

    subtitle::proto::Hello reply;
    reply.role.clear();
    reply.version = SUBREC_ASR_VERSION;
    reply.backends = {"cpu"};
    m_io.write(reply);
    startHeartbeat();

    // ---- 命令循环（IO 线程）----
    for (;;) {
        auto msg = m_io.readCommand();
        if (!msg) {
            m_ioProtocolError = m_io.hadProtocolError();   // 区分 stdin EOF（退出码 0）与协议错误
            break;
        }
        subtitle::proto::Message &m = *msg;
        if (!subtitle::proto::acceptedByRole(m, m_opt.role)) {
            m_io.write(subtitle::proto::Log{"warn",
                                            std::string("ignored command in role=") + m_opt.role + ": "
                                                + subtitle::proto::typeName(m)});
            continue;
        }
        if (const auto *ld = std::get_if<subtitle::proto::Load>(&m)) {
            handleLoad(*ld);
        } else if (const auto *op = std::get_if<subtitle::proto::Open>(&m)) {
            handleOpen(*op);
        } else if (const auto *ap = std::get_if<subtitle::proto::Append>(&m)) {
            if (m_jobActive.load(std::memory_order_relaxed) && m_reader)
                m_reader->setCommitted(ap->committed16k);
        } else if (const auto *sp = std::get_if<subtitle::proto::Splice>(&m)) {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            m_sh.cmds.push_back({Shared::Cmd::Splice, sp->at16k});
            m_sh.cv.notify_all();
        } else if (const auto *sl = std::get_if<subtitle::proto::SetLevel>(&m)) {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            m_sh.cmds.push_back({Shared::Cmd::SetPartial, sl->partialIntervalMs});
            m_sh.cv.notify_all();
        } else if (std::holds_alternative<subtitle::proto::Pause>(m)) {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            m_sh.paused = true;
            m_sh.cv.notify_all();
        } else if (std::holds_alternative<subtitle::proto::Resume>(m)) {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            m_sh.paused = false;
            m_sh.cv.notify_all();
        } else if (const auto *fin = std::get_if<subtitle::proto::Finish>(&m)) {
            std::lock_guard<std::mutex> lock(m_sh.mu);
            m_sh.finishing = true;
            m_sh.finishTotal = fin->total16k;
            if (m_reader)
                m_reader->setCommitted(fin->total16k);
            m_sh.cv.notify_all();
        } else if (std::holds_alternative<subtitle::proto::Cancel>(m)) {
            {
                std::lock_guard<std::mutex> lock(m_sh.mu);
                m_sh.cancelled = true;
            }
            m_sh.cv.notify_all();
            if (m_engine)
                m_engine->requestCancel();
        } else if (const auto *rf = std::get_if<subtitle::proto::Refine>(&m)) {
            handleRefine(*rf);
        } else if (std::holds_alternative<subtitle::proto::Quit>(m)) {
            break;
        }
        // Hello 重复发送：忽略。
    }

    stopDecodeThread();
    stopHeartbeat();
    return m_ioProtocolError ? static_cast<int>(subtitle::ErrorCode::HostProtocolError)
                             : subtitle::proto::kExitOk;
}

} // namespace

int runHost(const HostOptions &options)
{
    HostRuntime runtime(options);
    return runtime.run();
}

} // namespace asrhost
