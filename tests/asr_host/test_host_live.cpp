// subrec-asr 运行时集成测试：合成 WAV spool + fake 引擎。
// 覆盖 §3 表 C 一行：splice 强制封口、maxSegment 强切、resumeFrom16k 续识别、setLevel 关 partial、
// stdout 重定向后协议不被污染（garbageStdout）、各进程级故障注入（crash/oom/hangAtSeg/hangAll/
// illegalInstr/loadFail）、协议错误路径（非 JSON / 超长行 / 版本不一致 / open-before-load）、refine 角色。

#include "host_driver.h"
#include "test_framework.h"
#include "wav_builder.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_types.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

using namespace subtitle;

namespace {

int g_spoolCounter = 0;

std::string newSpoolPath()
{
    return "asr-test-spool-" + std::to_string(++g_spoolCounter) + "-"
           + std::to_string(HostSession::nowMs()) + ".wav";
}

const FinalSegment &finalOf(const proto::Message &m)
{
    return std::get<proto::FinalMsg>(m).seg;
}

const PartialSegment &partialOf(const proto::Message &m)
{
    return std::get<proto::PartialMsg>(m).seg;
}

// 收集 final 直到 done（或超时）。返回是否等到 done。
bool collectFinalsUntilDone(HostSession &s, std::vector<proto::Message> &finals, int timeoutMs)
{
    const int64_t deadline = HostSession::nowMs() + timeoutMs;
    for (;;) {
        proto::Message m;
        const int remain = static_cast<int>(std::max<int64_t>(1, deadline - HostSession::nowMs()));
        if (!s.readMsg(m, remain))
            return false;
        const std::string t = proto::typeName(m);
        if (t == "final")
            finals.push_back(std::move(m));
        else if (t == "done")
            return true;
    }
}

} // namespace

// ---- 端点 final / partial / finish→done / quit ----

TEST(live_endpoint_final_and_done)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1200);
    wav.flush();
    const int64_t total = wav.writtenSamples();

    HostSession s;
    s.start();
    CHECK_MSG(s.loadFake(300, 10000), "load fake");
    s.openJob(spool, 0, total);
    proto::Message m;
    CHECK_MSG(s.waitFor("final", m, 8000), "endpoint final");
    const FinalSegment &f = finalOf(m);
    CHECK(f.job == s.job && f.id == 0 && f.pass == 1);
    CHECK(f.begin16k == 0);
    // 1 s 语音：端点 = 首个纯静音帧（~16384）+ pad 2400 ≈ 18784；留 ±1 帧余量
    CHECK(f.end16k > 18000 && f.end16k < 19500);
    CHECK(f.text == "第0段" && f.language == "zh" && f.modelId == "fake");
    CHECK(!f.forcedCut);

    s.finishJob(total);
    CHECK_MSG(s.waitFor("done", m, 8000), "done after finish");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && !exit.signaled && exit.code == 0);
}

TEST(live_partial_stream_and_seq)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(3000);
    wav.appendSilenceMs(1200);
    wav.flush();

    HostSession s;
    s.start();
    CHECK_MSG(s.loadFake(150, 10000), "load fake");
    s.openJob(spool, 0, 0);

    // 慢速喂音频（模拟实时），期间收集 partial（final 若在喂入期间到达也记下来）
    std::vector<proto::Message> partials;
    proto::Message finalDuringFeed;
    bool haveFinal = false;
    const int64_t total = wav.writtenSamples();
    int64_t committed = 0;
    while (committed < total) {
        committed = std::min<int64_t>(total, committed + 1600);   // 100 ms
        s.appendCommitted(committed);
        const int64_t till = HostSession::nowMs() + 120;
        while (HostSession::nowMs() < till) {
            proto::Message m;
            const int remain = static_cast<int>(std::max<int64_t>(1, till - HostSession::nowMs()));
            if (!s.readMsg(m, remain))
                break;
            const std::string t = proto::typeName(m);
            if (t == "partial")
                partials.push_back(std::move(m));
            else if (t == "final" && !haveFinal) {
                finalDuringFeed = std::move(m);
                haveFinal = true;
            }
        }
    }
    CHECK(partials.size() >= 2);
    for (size_t i = 0; i < partials.size(); ++i) {
        const PartialSegment &p = partialOf(partials[i]);
        CHECK(p.job == s.job && p.id == 0);
        CHECK(p.seq == i);                        // 本句内从 0 递增
        CHECK(p.upto16k > p.begin16k);
        CHECK(!p.tokens.empty());                 // partial 带 token 时间戳
    }
    for (size_t i = 1; i < partials.size(); ++i)
        CHECK(partialOf(partials[i]).upto16k >= partialOf(partials[i - 1]).upto16k);

    proto::Message fin;
    if (!haveFinal)
        CHECK_MSG(s.waitFor("final", fin, 8000), "final after silence");
    else
        fin = finalDuringFeed;
    const FinalSegment &f = finalOf(fin);
    CHECK(f.end16k > 49800 && f.end16k < 51200);   // 3 s 语音：首个纯静音帧 48128 + pad 2400
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- splice 强制封口 ----

TEST(live_splice_forces_cut)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));

    HostSession s;
    s.start();
    CHECK_MSG(s.loadFake(0, 10000), "load fake");   // 关 partial，聚焦 final 序列

    // 语音 0-1.5 s；在 1.5 s（拼接点 = 暂停开始）强制封口
    wav.appendSpeechMs(1500);
    wav.flush();
    s.openJob(spool, 0, wav.writtenSamples());
    s.spliceAt(24000);

    proto::Message m;
    CHECK_MSG(s.waitFor("final", m, 8000), "splice final");
    const FinalSegment &f1 = finalOf(m);
    CHECK(f1.begin16k == 0);
    CHECK(f1.end16k == 24000);      // 强制封口位置精确
    CHECK(f1.forcedCut);

    // 恢复：1.7 s 起继续语音到 3 s，再静音到 4.2 s → 自然端点 final（不 forced）
    wav.appendSilenceMs(200);
    wav.appendSpeechMs(1300);
    wav.appendSilenceMs(1200);
    wav.flush();
    s.appendCommitted(wav.writtenSamples());
    CHECK_MSG(s.waitFor("final", m, 8000), "second final after resume");
    const FinalSegment &f2 = finalOf(m);
    CHECK(!f2.forcedCut);
    CHECK(f2.begin16k >= 24000);    // 不跨拼接点
    CHECK(f2.begin16k <= 27200);    // 恢复后语音起点 1.7 s − pad
    CHECK(f2.end16k > 49800 && f2.end16k < 51200);
    CHECK(f2.id == 1);

    s.finishJob(wav.writtenSamples());
    CHECK_MSG(s.waitFor("done", m, 8000), "done");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- maxSegment 强切 ----

TEST(live_max_segment_force_cut)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(25000);   // 连续语音
    wav.appendSilenceMs(1500);
    wav.flush();

    HostSession s;
    s.start();
    CHECK_MSG(s.loadFake(0, 2000), "load fake with maxSegmentMs=2000");
    s.openJob(spool, 0, wav.writtenSamples());
    s.finishJob(wav.writtenSamples());

    std::vector<proto::Message> finals;
    CHECK_MSG(collectFinalsUntilDone(s, finals, 10000), "done after finish");
    // 25 s 连续语音 × 2 s 强切 = 12 个 forced final + 末尾自然端点 1 个
    CHECK(finals.size() == 13);
    for (int k = 0; k < 12; ++k) {
        const FinalSegment &f = finalOf(finals[static_cast<size_t>(k)]);
        CHECK(f.forcedCut);
        CHECK(f.begin16k == 32000 * k);
        CHECK(f.end16k == 32000 * (k + 1));   // 精确切在倍数位置
        CHECK(f.id == k);
    }
    const FinalSegment &last = finalOf(finals[12]);
    CHECK(!last.forcedCut);
    CHECK(last.begin16k == 32000 * 12);
    CHECK(last.end16k > 400000);

    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- resumeFrom16k 断点续识别 ----

TEST(live_resume_from_16k)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);    // 0-1 s
    wav.appendSilenceMs(1000);   // 1-2 s
    wav.appendSpeechMs(1500);    // 2-3.5 s
    wav.appendSilenceMs(1500);   // 3.5-5 s
    wav.flush();
    const int64_t total = wav.writtenSamples();

    // 第一轮：只喂到 2 s，得到 final1 [0, ~1.15 s)
    int64_t resume16k = 0;
    {
        HostSession s;
        s.start();
        CHECK_MSG(s.loadFake(0, 10000), "load fake");
        s.openJob(spool, 0, 32000);
        proto::Message m;
        CHECK_MSG(s.waitFor("final", m, 8000), "round1 final");
        const FinalSegment &f1 = finalOf(m);
        CHECK(f1.begin16k == 0 && f1.end16k > 18000 && f1.end16k < 19500);
        resume16k = f1.end16k;
        s.finishJob(32000);
        CHECK_MSG(s.waitFor("done", m, 8000), "round1 done");
        s.quit();
        auto exit = s.proc.waitForExit(5000);
        CHECK(!exit.timedOut && exit.code == 0);
    }

    // 第二轮：模拟主进程重启后 resume（resume = final1.e16k），新进程、新 jobId
    {
        HostSession s2;
        s2.start();
        CHECK_MSG(s2.loadFake(0, 10000), "load fake round2");
        s2.openJob(spool, resume16k, total, 2);
        s2.finishJob(total);
        std::vector<proto::Message> finals2;
        CHECK_MSG(collectFinalsUntilDone(s2, finals2, 10000), "round2 done");
        CHECK(finals2.size() == 1);   // 只有 2-3.5 s 的那句，resume 之前不重复识别
        const FinalSegment &f2 = finalOf(finals2[0]);
        CHECK(f2.job == 2);
        CHECK(f2.begin16k >= resume16k);
        CHECK(f2.begin16k <= 32000);       // 恢复后语音起点 2 s − pad
        CHECK(f2.end16k > 57600 && f2.end16k < 59500);
        CHECK(!f2.forcedCut);
        s2.quit();
        auto exit2 = s2.proc.waitForExit(5000);
        CHECK(!exit2.timedOut && exit2.code == 0);
    }
}

// ---- setLevel 关 partial ----

TEST(live_set_level_disables_partial)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));

    HostSession s;
    s.start();
    CHECK_MSG(s.loadFake(120, 10000), "load fake");
    s.openJob(spool, 0, 0);

    std::vector<proto::Message> partialsBefore;
    int64_t committed = 0;
    while (committed < 24000) {   // 1.5 s 语音，慢速喂
        committed += 1600;
        wav.appendSpeechMs(100);
        wav.flush();
        s.appendCommitted(committed);
        const int64_t till = HostSession::nowMs() + 60;
        while (HostSession::nowMs() < till) {
            proto::Message m;
            const int remain = static_cast<int>(std::max<int64_t>(1, till - HostSession::nowMs()));
            if (!s.readMsg(m, remain))
                break;
            if (std::string(proto::typeName(m)) == "partial")
                partialsBefore.push_back(std::move(m));
        }
    }
    CHECK(partialsBefore.size() >= 1);

    // L1：setLevel{partialIntervalMs=0}
    s.setPartialLevel(0, 1);
    size_t afterMark = 0;
    proto::Message fin;
    bool haveFinal = false;
    while (committed < 24000 + 32000) {   // 再喂 1 s 语音 + 1.2 s 静音
        committed += 1600;
        if (wav.writtenSamples() < 40000)
            wav.appendSpeechMs(100);
        else
            wav.appendSilenceMs(100);
        wav.flush();
        s.appendCommitted(committed);
        const int64_t till = HostSession::nowMs() + 60;
        while (HostSession::nowMs() < till) {
            proto::Message m;
            const int remain = static_cast<int>(std::max<int64_t>(1, till - HostSession::nowMs()));
            if (!s.readMsg(m, remain))
                break;
            const std::string t = proto::typeName(m);
            if (t == "partial")
                ++afterMark;
            else if (t == "final" && !haveFinal) {
                fin = std::move(m);
                haveFinal = true;
            }
        }
    }
    CHECK(afterMark <= 1);   // setLevel 之后不再有 partial（≤1 容忍已调度在途的最后一个；
                             // 若 setLevel 失效，120 ms 间隔在 ~4 s 内会流出几十个）

    if (!haveFinal)
        CHECK_MSG(s.waitFor("final", fin, 8000), "final still works with partial off");
    CHECK(finalOf(fin).begin16k == 0);
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- stdout 重定向：协议通道不被 garbage 污染 ----

TEST(live_garbage_stdout_keeps_protocol_clean)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1200);
    wav.flush();
    const int64_t total = wav.writtenSamples();

    // 引擎持续向 fd 1 打印非 JSON；fd 1 已被 install() 重定向到 stderr，
    // 协议通道（dup 出的原 fd）必须只出现合法协议行。从 hello 起手动数行。
    HostSession s;
    s.start("garbageStdout");
    proto::Message m;
    int lines = 0;
    bool sawLoaded = false;
    bool sawFinal = false;

    proto::Hello h;
    h.protocol = proto::kProtocolVersion;
    s.send(h);
    ++lines;
    proto::Load ld;
    ld.request.engineId = "fake";
    ld.live.partialIntervalMs = 0;
    ld.live.maxSegmentMs = 10000;
    s.send(ld);
    proto::Open op;
    op.jobId = 3;
    op.spoolPath = spool;
    op.committed16k = total;
    s.send(op);

    const int64_t deadline = HostSession::nowMs() + 8000;
    while (HostSession::nowMs() < deadline) {
        std::string line;
        if (!s.proc.readLine(line, 1500))
            break;
        ++lines;
        Status st;
        auto msg = proto::decode(line, &st);
        CHECK_MSG(msg.has_value(), "stdout 出现非协议行: " + line.substr(0, 120));
        const std::string t = proto::typeName(*msg);
        if (t == "loaded")
            sawLoaded = true;
        if (t == "final") {
            sawFinal = true;
            s.quit();
        }
    }
    CHECK(lines >= 4);   // hello 回复 + loaded + busy + final
    CHECK(sawLoaded && sawFinal);
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- 进程级故障注入 ----

TEST(fault_crash_at_seg)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1000);
    wav.flush();

    HostSession s;
    s.start("crashAtSeg=0");
    CHECK_MSG(s.loadFake(0, 10000), "load fake");
    s.openJob(spool, 0, wav.writtenSamples());
    auto exit = s.proc.waitForExit(8000);
    CHECK(!exit.timedOut);
    CHECK(exit.signaled);   // 崩溃退出（Windows：NTSTATUS 崩溃码区间）
#ifdef _WIN32
    // 空指针写 → STATUS_ACCESS_VIOLATION；GetExitCodeProcess 的 32 位码按 int 带回（负值）
    CHECK(static_cast<unsigned>(exit.code) == 0xC0000005u);
#else
    CHECK(exit.code == SIGSEGV);
#endif
}

TEST(fault_oom_at_seg)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1000);
    wav.flush();

    HostSession s;
    s.start("oomAtSeg=0");
    CHECK_MSG(s.loadFake(0, 10000), "load fake");
    s.openJob(spool, 0, wav.writtenSamples());
    auto exit = s.proc.waitForExit(8000);
    CHECK(!exit.timedOut);
    CHECK(!exit.signaled);
    CHECK(exit.code == proto::kExitOutOfMemory);
}

TEST(fault_illegal_instr_exit)
{
    // illegalInstr 启动即退出（握手之前），不能用 HostSession::start（它要求 hello 回复）
    ChildProcess proc;
    CHECK(proc.spawn(SUBREC_ASR_EXE, {"--engine=fake", "--fake=illegalInstr"},
                     {{"SUBREC_ASR_TEST_HOOKS", "1"}}));
    auto exit = proc.waitForExit(8000);
    CHECK(!exit.timedOut);
#ifndef _WIN32
    CHECK(!exit.signaled);
    CHECK(exit.code == normalizedExitCode(proto::kExitCpuUnsupported));   // 305（POSIX 截断为 49）
#else
    // injectIllegalInstrExit 用 RaiseException(STATUS_ILLEGAL_INSTRUCTION)：未处理异常 → 进程以
    // 该 NTSTATUS 退出（0xC000001D；GetExitCodeProcess 的 32 位码按 int 带回，用无符号比较）
    CHECK(exit.signaled);
    CHECK(static_cast<unsigned>(exit.code) == proto::kStatusIllegalInstruction);
#endif
}

TEST(fault_hang_at_seg_keeps_hb)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1000);
    wav.flush();

    HostSession s;
    s.start("hangAtSeg=0");
    CHECK_MSG(s.loadFake(0, 10000), "load fake");
    s.openJob(spool, 0, wav.writtenSamples());

    // 无 final；hb 继续（IO 线程活着）
    int hbCount = 0;
    const int64_t deadline = HostSession::nowMs() + 3000;
    while (HostSession::nowMs() < deadline) {
        std::string line;
        if (!s.proc.readLine(line, 500))
            continue;
        Status st;
        auto msg = proto::decode(line, &st);
        if (!msg.has_value())
            continue;
        const std::string t = proto::typeName(*msg);
        CHECK_MSG(t != "final", "hangAtSeg 不应出 final");
        if (t == "hb")
            ++hbCount;
    }
    CHECK(hbCount >= 2);   // ~3 s 内至少 2 条 hb
    CHECK(s.proc.alive());
}

TEST(fault_hang_all_stops_hb)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1000);
    wav.flush();

    HostSession s;
    s.start("hangAll=0");
    CHECK_MSG(s.loadFake(0, 10000), "load fake");
    s.openJob(spool, 0, 0);

    // 先确认 hb 在流
    proto::Message m;
    CHECK_MSG(s.waitFor("hb", m, 3000), "hb before hang");
    // 触发解码（hangAll 在第 0 段解码时生效：解码线程与 hb 同时停）
    s.appendCommitted(wav.writtenSamples());
    int hbAfterTrigger = 0;
    const int64_t deadline = HostSession::nowMs() + 3000;
    while (HostSession::nowMs() < deadline) {
        std::string line;
        if (!s.proc.readLine(line, 500))
            continue;
        Status st;
        auto msg = proto::decode(line, &st);
        if (msg.has_value() && std::string(proto::typeName(*msg)) == "hb")
            ++hbAfterTrigger;
    }
    CHECK(hbAfterTrigger <= 1);   // 触发后 hb 停（至多一条在途）
    CHECK(s.proc.alive());
}

TEST(fault_load_fail_error_and_stay_alive)
{
    HostSession s;
    s.start("loadFail=303");
    proto::Message m;
    CHECK_MSG(s.loadFake(0, 10000, "", &m) == false, "load should fail");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK_MSG(err != nullptr, "expected error message");
    CHECK(err->code == 303);
    // 宿主仍活着，可正常退出
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

// ---- 协议错误与角色 ----

TEST(host_unknown_type_ignored)
{
    HostSession s;
    s.start();
    s.sendText("{\"type\":\"brand-new-command\",\"payload\":123}");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);   // 未知 type：记日志忽略，不视为协议错误
}

TEST(host_bad_json_exits_703)
{
    HostSession s;
    s.start();
    s.sendText("this is not json");
    proto::Message m;
    CHECK_MSG(s.waitFor("error", m, 5000), "error before exit");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK(err && err->code == 703);
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut);
    CHECK(exit.code == normalizedExitCode(703));
}

TEST(host_too_long_line_exits_703)
{
    HostSession s;
    s.start();
    s.sendText("{\"type\":\"append\",\"jobId\":1,\"committed16k\":0,\"pad\":\""
               + std::string(1100000, 'x') + "\"}");
    proto::Message m;
    CHECK_MSG(s.waitFor("error", m, 5000), "error before exit");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK(err && err->code == 703);
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut);
    CHECK(exit.code == normalizedExitCode(703));
}

TEST(host_hello_version_mismatch_exits_704)
{
    // 直接发 protocol=1 的 hello（不做正常握手）
    ChildProcess proc;
    CHECK(proc.spawn(SUBREC_ASR_EXE, {"--engine=fake"}, {{"SUBREC_ASR_TEST_HOOKS", "1"}}));
    proto::Hello wrong;
    wrong.protocol = 1;
    proc.writeLine(proto::encode(proto::Message{wrong}));
    HostSession s;
    s.proc = std::move(proc);   // 复用读消息的辅助
    proto::Message m;
    CHECK_MSG(s.waitFor("error", m, 5000), "error before exit");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK(err && err->code == 704);
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut);
    CHECK(exit.code == normalizedExitCode(704));
}

TEST(host_open_before_load_not_ready)
{
    HostSession s;
    s.start();
    // 不 load 直接 open → error NotReady(107)
    s.openJob("whatever.wav", 0, 0);
    proto::Message m;
    CHECK_MSG(s.waitFor("error", m, 5000), "error expected");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK(err && err->code == 107);
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(host_eof_exits_zero)
{
    HostSession s;
    s.start();
    s.proc.closeStdin();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && !exit.signaled && exit.code == 0);
}

// ---- refine 角色 ----

TEST(host_refine_role_replay)
{
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSpeechMs(1000);
    wav.appendSilenceMs(1000);
    wav.flush();

    HostSession s;
    s.start("refineRtf=0.01", "refine");
    CHECK_MSG(s.loadFake(0, 10000), "load fake for refine");
    proto::Refine rf;
    rf.jobId = 9;
    rf.spoolPath = spool;
    rf.segments = {{0, 0, 16000}, {5, 16000, 32000}};
    rf.prompt = "热词";
    s.send(rf);

    int finals = 0;
    bool sawDone = false;
    proto::Message m;
    const int64_t deadline = HostSession::nowMs() + 8000;
    while (HostSession::nowMs() < deadline) {
        const int remain = static_cast<int>(std::max<int64_t>(1, deadline - HostSession::nowMs()));
        if (!s.readMsg(m, remain))
            break;
        const std::string t = proto::typeName(m);
        if (t != "final" && t != "done")
            continue;
        if (t == "done") {
            sawDone = true;
            break;
        }
        const FinalSegment &f = finalOf(m);
        CHECK(f.pass == 2);
        CHECK(f.job == 9);
        CHECK(finals == 0 ? f.id == 0 : f.id == 5);
        ++finals;
    }
    CHECK(finals == 2);
    CHECK_MSG(sawDone, "done after refine");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(host_refine_role_rejects_live_commands)
{
    HostSession s;
    s.start("", "refine");
    CHECK_MSG(s.loadFake(0, 10000), "load fake for refine role");
    // open 不被 refine 角色接受：只回一条 log（记日志后忽略），不产生 error / 状态变化
    s.openJob("whatever.wav", 0, 0);
    std::string line;
    const int64_t till = HostSession::nowMs() + 800;
    while (HostSession::nowMs() < till) {
        if (!s.proc.readLine(line, static_cast<int>(std::max<int64_t>(1, till - HostSession::nowMs()))))
            break;
        Status st;
        auto msg = proto::decode(line, &st);
        CHECK_MSG(msg.has_value(), "协议行必须可解析");
        const std::string t = proto::typeName(*msg);
        CHECK_MSG(t == "log" || t == "hb", "角色过滤只允许 log/hb，收到: " + t);
    }
    // refine 角色自己的命令照常工作（空 segments → 立即 done；spool 必须真实存在）
    std::string spool = newSpoolPath();
    WavSpoolBuilder wav;
    CHECK(wav.create(spool));
    wav.appendSilenceMs(10);
    wav.flush();
    proto::Refine rf;
    rf.jobId = 1;
    rf.spoolPath = spool;
    s.send(rf);
    proto::Message m;
    CHECK_MSG(s.waitFor("done", m, 8000), "done for empty refine");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}
