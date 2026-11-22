// 进程外假宿主 subrec-asr-fakehost（工作包 C 实现，design-live-v2.md §11.1 第 2 层）。
// 按脚本回放协议消息，供 AsrHostClient / SubtitleJobRunner / HostWatchdog 的测试使用，
// 不依赖模型。脚本格式见 fake_host_script.h（M0 冻结）。
//
// 线程：主线程执行脚本步骤（Expect 会阻塞读 stdin）；hb 线程按脚本设定的周期自动发 hb，
// hang 指令把它停掉。脚本执行完后继续发 hb（若开启），等 quit 或 stdin EOF 后以 0 退出。

#include "fake_host_script.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_error.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

using asrhost::fake::ScriptStep;

std::mutex g_outMutex;

void writeLine(const std::string &line)
{
    std::lock_guard<std::mutex> lock(g_outMutex);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 阻塞读一行（去 \r\n）；EOF 且无残留 → false。
bool readStdinLine(std::string &line)
{
    line.clear();
    char buf[4096];
    for (;;) {
        if (!std::fgets(buf, sizeof buf, stdin))
            return !line.empty();
        line += buf;
        if (!line.empty() && line.back() == '\n')
            break;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return true;
}

struct HbController {
    std::atomic<int64_t> periodMs{0};   // 0 = off
    std::atomic<bool> stop{false};
    std::atomic<int64_t> seq{0};
    std::thread thread;

    void start()
    {
        thread = std::thread([this] {
            int64_t nextAt = nowMs();
            while (!stop.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const int64_t period = periodMs.load(std::memory_order_relaxed);
                const int64_t now = nowMs();
                if (period <= 0) {
                    nextAt = now;   // off 期间保持计时起点，重新开启后立即发一条
                    continue;
                }
                if (now >= nextAt) {
                    subtitle::proto::Heartbeat hb;
                    hb.seq = seq.fetch_add(1) + 1;
                    hb.state = "idle";
                    hb.rssMB = 1;
                    writeLine(subtitle::proto::encode(hb));
                    nextAt = now + period;
                }
            }
        });
    }

    ~HbController()
    {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable())
            thread.join();
    }
};

void sleepMs(int64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// 执行脚本；只有 exit/crash/hang 或函数末尾的等待循环会离开本函数。
void runScript(const std::vector<ScriptStep> &steps)
{
    HbController hb;
    hb.start();
    for (const ScriptStep &st : steps) {
        switch (st.op) {
        case ScriptStep::Op::Expect: {
            for (;;) {
                std::string line;
                if (!readStdinLine(line))
                    std::exit(0);   // stdin EOF → 0 退出
                subtitle::Status err;
                auto msg = subtitle::proto::decode(line, &err);
                if (!msg) {
                    if (err.code == subtitle::ErrorCode::NotSupported) {
                        std::fprintf(stderr, "fakehost: ignore unknown type (expect %s): %s\n",
                                     st.text.c_str(), line.c_str());
                        continue;
                    }
                    std::fprintf(stderr, "fakehost: ignore undecodable line: %s\n", line.c_str());
                    continue;
                }
                const bool matched = std::string(subtitle::proto::typeName(*msg)) == st.text;
                if (matched)
                    break;
                std::fprintf(stderr, "fakehost: skip %s while expecting %s\n",
                             subtitle::proto::typeName(*msg), st.text.c_str());
            }
            break;
        }
        case ScriptStep::Op::Send:
            writeLine(st.text);
            break;
        case ScriptStep::Op::ReplyHello: {
            subtitle::proto::Hello h;
            h.protocol = st.value == 0 ? subtitle::proto::kProtocolVersion
                                       : static_cast<int>(st.value);
            h.version = "fake";
            h.backends = {"cpu"};
            writeLine(subtitle::proto::encode(h));
            break;
        }
        case ScriptStep::Op::Hb:
            hb.periodMs.store(st.value, std::memory_order_relaxed);
            break;
        case ScriptStep::Op::Sleep:
            sleepMs(st.value);
            break;
        case ScriptStep::Op::Garbage:
            writeLine(st.text.empty() ? std::string("this is not json") : st.text);
            break;
        case ScriptStep::Op::LongLine: {
            static const std::string prefix = "{\"type\":\"log\",\"text\":\"";
            static const std::string suffix = "\"}";
            const int64_t padLen = st.value - static_cast<int64_t>(prefix.size() + suffix.size());
            std::string line = prefix;
            if (padLen > 0)
                line.append(static_cast<size_t>(padLen), 'a');
            line += suffix;
            writeLine(line);
            break;
        }
        case ScriptStep::Op::Exit:
            std::exit(static_cast<int>(st.value));
        case ScriptStep::Op::Crash:
            std::abort();
        case ScriptStep::Op::Hang:
            hb.periodMs.store(0, std::memory_order_relaxed);
            for (;;)
                sleepMs(100);
        }
    }
    // 脚本执行完：继续发 hb（若开启）并等 quit 或 stdin EOF。
    for (;;) {
        std::string line;
        if (!readStdinLine(line))
            std::exit(0);
        subtitle::Status err;
        auto msg = subtitle::proto::decode(line, &err);
        if (msg && std::holds_alternative<subtitle::proto::Quit>(*msg))
            std::exit(0);
    }
}

int runMain(int argc, const char *const *argv)
{
    std::string scriptPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--script=", 0) == 0)
            scriptPath = a.substr(9);   // 其余 subrec-asr 参数：接受但忽略
    }
    if (scriptPath.empty()) {
        std::fprintf(stderr, "subrec-asr-fakehost: --script=<path> required\n");
        return static_cast<int>(subtitle::ErrorCode::InvalidArgument);
    }
    std::ifstream in(scriptPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "subrec-asr-fakehost: cannot read script: %s\n", scriptPath.c_str());
        return static_cast<int>(subtitle::ErrorCode::InvalidArgument);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::vector<ScriptStep> steps;
    std::string error;
    if (!asrhost::fake::parseScript(ss.str(), steps, &error)) {
        std::fprintf(stderr, "subrec-asr-fakehost: %s\n", error.c_str());
        return static_cast<int>(subtitle::ErrorCode::InvalidArgument);
    }
    runScript(steps);
    return 0;   // 不可达（runScript 只经 exit/abort 离开）
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::vector<std::string> utf8;
    utf8.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? static_cast<size_t>(len) : 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &s[0], static_cast<int>(s.size()), nullptr,
                            nullptr);
        if (!s.empty() && s.back() == '\0')
            s.pop_back();
        utf8.push_back(std::move(s));
    }
    std::vector<const char *> ptrs;
    ptrs.reserve(utf8.size());
    for (const std::string &a : utf8)
        ptrs.push_back(a.c_str());
    return runMain(argc, ptrs.data());
}
#else
int main(int argc, char **argv)
{
    return runMain(argc, argv);
}
#endif
