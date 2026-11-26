#pragma once
// tests/asr_host：驱动 subrec-asr / subrec-asr-fakehost 子进程的公共辅助。

#include "child_process.h"
#include "test_framework.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_types.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef SUBREC_ASR_EXE
#define SUBREC_ASR_EXE "subrec-asr"
#endif
#ifndef SUBREC_ASR_FAKEHOST_EXE
#define SUBREC_ASR_FAKEHOST_EXE "subrec-asr-fakehost"
#endif

inline std::string asrExe()
{
#ifdef _WIN32
    const wchar_t *value = _wgetenv(L"SUBREC_ASR_EXE");
    if (value && *value)
        return std::filesystem::path(value).u8string();
#else
    const char *value = std::getenv("SUBREC_ASR_EXE");
    if (value && *value)
        return value;
#endif
    return SUBREC_ASR_EXE;
}

inline std::string asrFakeHostExe()
{
#ifdef _WIN32
    const wchar_t *value = _wgetenv(L"SUBREC_ASR_FAKEHOST_EXE");
    if (value && *value)
        return std::filesystem::path(value).u8string();
#else
    const char *value = std::getenv("SUBREC_ASR_FAKEHOST_EXE");
    if (value && *value)
        return value;
#endif
    return SUBREC_ASR_FAKEHOST_EXE;
}

// 一次与 subrec-asr 的会话：启动（hooks=1）→ 握手 → load fake。测试在其上追加 open/append/…
struct HostSession {
    ChildProcess proc;
    subtitle::JobId job = 1;

    static int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void start(const std::string &fakeArgs = "", const std::string &role = "live",
               const std::vector<std::string> &unsetEnv = {})
    {
        std::vector<std::string> args;
        if (!role.empty())
            args.push_back("--role=" + role);
        args.push_back("--engine=fake");
        if (!fakeArgs.empty())
            args.push_back("--fake=" + fakeArgs);
        CHECK(proc.spawn(asrExe(), args, {{"SUBREC_ASR_TEST_HOOKS", "1"}}, unsetEnv));
        handshake(role);
    }

    void handshake(const std::string &role = "live")
    {
        subtitle::proto::Hello h;
        h.protocol = subtitle::proto::kProtocolVersion;
        h.role = role;
        send(h);
        subtitle::proto::Message m;
        CHECK_MSG(waitFor("hello", m, 5000), "no hello reply");
    }

    void sendText(const std::string &line) { proc.writeLine(line); }

    template <typename T>
    void send(const T &msg)
    {
        sendText(subtitle::proto::encode(subtitle::proto::Message{msg}));
    }

    // 读一条消息（decode；未知类型也返回，标记为 unknown）。超时 / EOF / 非 JSON → false。
    bool readMsg(subtitle::proto::Message &out, int timeoutMs)
    {
        std::string line;
        if (!proc.readLine(line, timeoutMs))
            return false;
        subtitle::Status st;
        auto m = subtitle::proto::decode(line, &st);
        if (!m) {
            std::printf("  [asr out, undecodable] %s\n", line.c_str());
            return false;
        }
        out = std::move(*m);
        return true;
    }

    // 等待指定 type（跳过 hb / log）。期间收到的其它消息压入 sawOthers（可空）。超时 → false。
    bool waitFor(const char *type, subtitle::proto::Message &out, int timeoutMs,
                 std::vector<subtitle::proto::Message> *sawOthers = nullptr)
    {
        const int64_t deadline = nowMs() + timeoutMs;
        for (;;) {
            const int remain = static_cast<int>(std::max<int64_t>(1, deadline - nowMs()));
            subtitle::proto::Message m;
            if (!readMsg(m, remain))
                return false;
            const std::string t = subtitle::proto::typeName(m);
            if (t == type) {
                out = std::move(m);
                return true;
            }
            if (t != "hb" && t != "log" && sawOthers)
                sawOthers->push_back(std::move(m));
        }
    }

    // load fake 引擎并等待 loaded；收到 error 时返回 false 并（若 err 非空）带回该消息。
    bool loadFake(int partialIntervalMs, int maxSegmentMs, const std::string &fakeOpts = {},
                  subtitle::proto::Message *err = nullptr)
    {
        subtitle::proto::Load ld;
        ld.request.engineId = "fake";
        ld.request.threads = 1;
        ld.live.partialIntervalMs = partialIntervalMs;
        ld.live.maxSegmentMs = maxSegmentMs;
        ld.fakeOptions = fakeOpts;
        send(ld);
        const int64_t deadline = nowMs() + 8000;
        for (;;) {
            subtitle::proto::Message m;
            const int remain = static_cast<int>(std::max<int64_t>(1, deadline - nowMs()));
            if (!readMsg(m, remain))
                return false;
            const std::string t = subtitle::proto::typeName(m);
            if (t == "loaded")
                return true;
            if (t == "error") {
                if (err)
                    *err = std::move(m);
                return false;
            }
        }
    }

    bool readError(subtitle::proto::Message &out, int timeoutMs)
    {
        return waitFor("error", out, timeoutMs);
    }

    void openJob(const std::string &spoolPath, int64_t resumeFrom16k, int64_t committed16k,
                 subtitle::JobId jobId = 1, bool live = true)
    {
        job = jobId;
        subtitle::proto::Open op;
        op.jobId = jobId;
        op.spoolPath = spoolPath;
        op.resumeFrom16k = resumeFrom16k;
        op.committed16k = committed16k;
        op.live = live;
        send(op);
    }

    void appendCommitted(int64_t committed16k)
    {
        send(subtitle::proto::Append{job, committed16k});
    }

    void spliceAt(int64_t at16k) { send(subtitle::proto::Splice{job, at16k}); }

    void setPartialLevel(int partialIntervalMs, int level = 1)
    {
        send(subtitle::proto::SetLevel{job, level, 1, partialIntervalMs});
    }

    void finishJob(int64_t total16k) { send(subtitle::proto::Finish{job, total16k}); }

    void quit() { send(subtitle::proto::Quit{}); }
};
