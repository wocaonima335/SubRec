// fake 宿主（subrec-asr-fakehost）脚本回放测试：起子进程按脚本驱动。

#include "host_driver.h"
#include "test_framework.h"

#include "fake_host/fake_host_script.h"
#include "subtitle/host/asr_protocol.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace subtitle;

namespace {

const char *kExe = SUBREC_ASR_FAKEHOST_EXE;
int g_tmpCounter = 0;

std::string writeScript(const std::string &content)
{
    const std::string path = "fakehost-" + std::to_string(++g_tmpCounter) + "-"
                             + std::to_string(HostSession::nowMs()) + ".script";
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    CHECK_MSG(out.good(), "write script failed: " + path);
    return path;
}

proto::Message decodeOrThrow(const std::string &line)
{
    Status st;
    auto m = proto::decode(line, &st);
    CHECK_MSG(m.has_value(), "undecodable line: " + line);
    return *m;
}

} // namespace

TEST(fakehost_script_parse_errors)
{
    std::vector<asrhost::fake::ScriptStep> steps;
    std::string error;
    CHECK(!asrhost::fake::parseScript("expect\n", steps, &error) && error.rfind("1:", 0) == 0);
    CHECK(!asrhost::fake::parseScript("bogus step here\n", steps, &error));
    CHECK(!asrhost::fake::parseScript("hb 0\n", steps, &error));
    CHECK(!asrhost::fake::parseScript("sleep -1\n", steps, &error));
    CHECK(!asrhost::fake::parseScript("crash now\n", steps, &error));
    CHECK(asrhost::fake::parseScript("# comment\n\n  \nreply-hello\nhb 100\nhb off\nexit 0\n", steps,
                                     &error)
          && steps.size() == 4);
    CHECK(steps[0].op == asrhost::fake::ScriptStep::Op::ReplyHello);
    CHECK(steps[1].op == asrhost::fake::ScriptStep::Op::Hb && steps[1].value == 100);
    CHECK(steps[2].op == asrhost::fake::ScriptStep::Op::Hb && steps[2].value == 0);
    CHECK(steps[3].op == asrhost::fake::ScriptStep::Op::Exit && steps[3].value == 0);
}

TEST(fakehost_handshake_and_loaded)
{
    const std::string script = writeScript(
        "expect hello\n"
        "reply-hello\n"
        "hb 500\n"
        "expect load\n"
        "send {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":5}\n"
        "expect quit\n"
        "exit 7\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    s.handshake();
    s.send(proto::Load{});
    proto::Message m;
    CHECK_MSG(s.waitFor("loaded", m, 5000), "loaded not sent");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && !exit.signaled && exit.code == 7);
}

TEST(fakehost_reply_hello_custom_protocol)
{
    const std::string script = writeScript("reply-hello 1\nexit 0\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    s.send(proto::Hello{});   // 主进程先打招呼也行（fakehost 不在脚本前读 stdin）
    proto::Message m;
    CHECK(s.waitFor("hello", m, 5000));
    auto *h = std::get_if<proto::Hello>(&m);
    CHECK(h && h->protocol == 1 && h->version == "fake");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(fakehost_expect_skips_other_types)
{
    const std::string script = writeScript("expect open\nsend {\"type\":\"busy\",\"jobId\":1,"
                                           "\"kind\":\"final\",\"b16k\":0,\"e16k\":16000}\n"
                                           "expect quit\nexit 0\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    // 本脚本不 reply-hello：直接发命令。open 之前发别的命令会被记日志后忽略，expect 仍在等
    s.send(proto::Append{1, 100});
    s.send(proto::SetLevel{1, 1, 1, 0});
    s.send(proto::Open{});
    proto::Message m;
    CHECK_MSG(s.waitFor("busy", m, 5000), "busy after open");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(fakehost_hb_stream_and_off)
{
    const std::string script = writeScript("hb 200\nsleep 1000\nhb off\nsleep 300\n"
                                           "expect quit\nexit 0\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    int hbCount = 0;
    int64_t lastSeq = -1;
    const int64_t deadline = HostSession::nowMs() + 2500;
    while (HostSession::nowMs() < deadline) {
        std::string line;
        if (!s.proc.readLine(line, 2000))
            break;
        proto::Message m = decodeOrThrow(line);
        if (std::string(proto::typeName(m)) == "hb") {
            ++hbCount;
            const auto &hb = std::get<proto::Heartbeat>(m);
            CHECK(hb.seq > lastSeq);
            CHECK(hb.state == "idle" && hb.rssMB > 0);
            lastSeq = hb.seq;
        }
    }
    CHECK(hbCount >= 3);   // 1 s @200 ms 至少 3-5 条，且 off 之后停发
    // off 之后再等 600 ms：不应再有输出
    std::string extra;
    CHECK(!s.proc.readLine(extra, 600));
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(fakehost_garbage_and_longline)
{
    const std::string script = writeScript("garbage definitely not json\n"
                                           "longline 1048600\n"
                                           "expect quit\nexit 0\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    std::string line;
    CHECK(s.proc.readLine(line, 5000));
    CHECK(line == "definitely not json");
    CHECK(s.proc.readLine(line, 5000));
    CHECK(line.size() == 1048600);   // 字节数精确（不含 '\n'）
    CHECK(line.rfind("{\"type\":\"log\",\"text\":\"", 0) == 0);
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(fakehost_crash)
{
    const std::string script = writeScript("crash\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    auto exit = s.proc.waitForExit(8000);
    CHECK(!exit.timedOut);
    CHECK_MSG(exit.signaled || exit.code != 0, "crash 必须以异常方式退出");
}

TEST(fakehost_eof_exits_zero)
{
    const std::string script = writeScript("expect hello\nreply-hello\nexpect load\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    s.handshake();
    s.proc.closeStdin();   // stdin EOF：expect 中 → 以 0 退出
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && !exit.signaled && exit.code == 0);
}

TEST(fakehost_quit_after_script_end)
{
    // 脚本执行完：继续发 hb、等 quit 或 EOF
    const std::string script = writeScript("reply-hello\nhb 150\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    proto::Message m;
    CHECK(s.waitFor("hello", m, 5000));
    // 有 hb 在流（脚本仍在“结尾等待”阶段）
    CHECK(s.waitFor("hb", m, 3000));
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(fakehost_hang_stops_output)
{
    // sleep 让 hb 先流一会儿，再 hang（脚本步骤是立即执行的，否则 hang 先于第一条 hb）
    const std::string script = writeScript("reply-hello\nhb 100\nsleep 600\nhang\n");
    HostSession s;
    CHECK(s.proc.spawn(kExe, {"--script=" + script}));
    proto::Message m;
    CHECK(s.waitFor("hello", m, 5000));
    CHECK(s.waitFor("hb", m, 3000));   // hang 之前
    // 排干 hang 前已入管道的 hb（100 ms 周期可能积压多条）
    std::string line;
    while (s.proc.readLine(line, 200))
        ;
    CHECK(!s.proc.readLine(line, 1200));   // hang：停止一切输出
    CHECK(s.proc.alive());
}
