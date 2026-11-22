#include "subtitle/live/host_watchdog.h"
#include "subtitle/host/asr_protocol.h"

#include <iostream>
#include <stdexcept>

using namespace subtitle;
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)

namespace {
using Verdict = HostWatchdog::Verdict;
struct Rig {
    ManualClock clock{100000};
    HostWatchdog w;
    explicit Rig(WatchdogConfig config = {}) : w(config, clock) { w.onProcessStarted(); }
    void at(int64_t elapsed, bool heartbeat = true)
    {
        clock.set(100000 + elapsed);
        if (heartbeat) w.onHeartbeat(elapsed);
    }
    Verdict poll() const { return w.poll(clock.nowMs()); }
    HostWatchdog::RestartPlan crash(int64_t begin = 100, int64_t end = 16100)
    {
        w.onBusy(begin, end, end - begin);
        return w.onExit(1, true, 50);
    }
};

void heartbeat()
{
    Rig r;
    r.at(4999, false); CHECK(r.poll() == Verdict::Ok);
    r.at(5000, false); CHECK(r.poll() == Verdict::KillHung);
    CHECK(r.poll() == Verdict::KillHung); // poll is observational.
    r.w.onHeartbeat(1); CHECK(r.poll() == Verdict::Ok);
    r.at(9999, false); CHECK(r.poll() == Verdict::Ok);
    r.at(10000, false); CHECK(r.poll() == Verdict::KillHung);
    r.w.onHeartbeat(1); CHECK(r.poll() == Verdict::Ok); // Receipt, not seq, is liveness.
    r.w.onExit(1, true, 0);
    r.at(100000, false); CHECK(r.poll() == Verdict::Ok); // No live process to kill.
}

void decode_rtf()
{
    struct Row { int64_t samples; int deadline; };
    for (const auto &row : std::vector<Row>{{16000, 10000}, {160000, 30000}, {64001, 12001}}) {
        Rig r;
        r.w.onBusy(5000, 5000 + row.samples, row.samples);
        r.at(row.deadline - 1); CHECK(r.poll() == Verdict::Ok);
        r.at(row.deadline); CHECK(r.poll() == Verdict::KillDecodeTimeout);
        CHECK(r.poll() == Verdict::KillDecodeTimeout);
        r.w.onIdle(); CHECK(r.poll() == Verdict::Ok);
        r.at(row.deadline + 100000); CHECK(r.poll() == Verdict::Ok);
    }
    Rig r;
    r.w.onBusy(0, 16000, 16000);
    r.at(9000); r.w.onBusy(16000, 32000, 16000);
    r.at(10000); CHECK(r.poll() == Verdict::Ok); // New busy re-arms the timer.
    r.at(18999); CHECK(r.poll() == Verdict::Ok);
    r.at(19000); CHECK(r.poll() == Verdict::KillDecodeTimeout);
}

void load_timeout()
{
    Rig r;
    r.at(2000); r.w.onLoadStarted();
    r.at(61999); CHECK(r.poll() == Verdict::Ok);
    r.at(62000); CHECK(r.poll() == Verdict::KillLoadTimeout);
    CHECK(r.poll() == Verdict::KillLoadTimeout);
    const auto plan = r.w.onExit(1, true, 123);
    CHECK(!plan.allowed); CHECK(plan.delayMs == 0); CHECK(!plan.poison); CHECK(plan.skipTo16k == 123);
    r.w.onProcessStarted(); r.w.onLoadStarted(); r.w.onLoaded();
    r.at(200000); CHECK(r.poll() == Verdict::Ok);
    CHECK(r.w.onExit(1, true, 0).delayMs == 1000); // Load rejection did not consume a retry.
    Rig heartbeatDuringLoad;
    heartbeatDuringLoad.w.onLoadStarted();
    heartbeatDuringLoad.at(5000, false);
    CHECK(heartbeatDuringLoad.poll() == Verdict::KillHung);
    CHECK(heartbeatDuringLoad.w.onExit(1, true, 0).allowed); // Before load deadline: hb recovery.
}

void backoff()
{
    WatchdogConfig config; config.maxRestarts = 100;
    Rig r(config);
    for (const int delay : {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000}) {
        const auto plan = r.w.onExit(1, true, 0);
        CHECK(plan.allowed); CHECK(plan.delayMs == delay);
        r.clock.advance(delay); r.w.onProcessStarted();
    }
    // Saturation also works for large values without overflowing a signed int.
    config.backoffBaseMs = 1500000000; config.backoffMaxMs = 2000000001;
    Rig large(config);
    CHECK(large.w.onExit(1, true, 0).delayMs == 1500000000);
    CHECK(large.w.onExit(1, true, 0).delayMs == 2000000001);
    CHECK(large.w.onExit(1, true, 0).delayMs == 2000000001);
}

void restart_window()
{
    Rig r;
    int64_t time = 0;
    for (int delay : {1000, 2000, 4000, 8000, 16000}) {
        r.at(time, false);
        const auto plan = r.w.onExit(1, true, 0);
        CHECK(plan.allowed); CHECK(plan.delayMs == delay);
        time += delay; r.w.onProcessStarted();
    }
    r.at(31000, false); CHECK(!r.w.onExit(1, true, 0).allowed);
    r.at(599999, false); CHECK(!r.w.onExit(1, true, 0).allowed);
    r.at(600000, false);
    const auto next = r.w.onExit(1, true, 0);
    CHECK(next.allowed); CHECK(next.delayMs == 30000);
    CHECK(!r.w.onExit(1, true, 0).allowed); // Only the oldest entry expired.
    r.at(601000, false); CHECK(r.w.onExit(1, true, 0).allowed);
}

void poison()
{
    Rig r;
    auto first = r.crash(); CHECK(first.allowed); CHECK(!first.poison); CHECK(first.skipTo16k == 50);
    r.w.onProcessStarted();
    const auto second = r.crash(); CHECK(second.allowed); CHECK(second.poison); CHECK(second.skipTo16k == 16100);

    Rig different;
    different.crash(); different.w.onProcessStarted();
    CHECK(!different.crash(101, 16100).poison); // Both endpoints matter.
    different.w.onProcessStarted(); CHECK(!different.crash(101, 16101).poison);
    different.w.onProcessStarted(); CHECK(different.crash(101, 16101).poison);

    // Killed decodes use exactly the same poison rule as crashes, with live heartbeats.
    Rig hung;
    for (int attempt = 0; attempt < 2; ++attempt) {
        hung.w.onProcessStarted(); hung.w.onBusy(16000, 32000, 16000);
        hung.clock.advance(10000); hung.w.onHeartbeat(attempt);
        CHECK(hung.poll() == Verdict::KillDecodeTimeout);
        const auto plan = hung.w.onExit(1, true, 8000);
        CHECK(plan.poison == (attempt == 1)); CHECK(plan.skipTo16k == (attempt ? 32000 : 8000));
    }
}

void poison_idle()
{
    Rig r;
    r.crash(); r.w.onProcessStarted();
    r.w.onBusy(100, 16100, 16000); r.w.onIdle();
    auto idle = r.w.onExit(1, true, 777);
    CHECK(!idle.poison); CHECK(idle.skipTo16k == 777);
    r.w.onProcessStarted(); CHECK(!r.crash().poison); // Idle exit breaks consecutive failures.
    r.w.onProcessStarted(); r.w.onBusy(100, 16100, 16000);
    auto clean = r.w.onExit(0, false, 999);
    CHECK(!clean.allowed); CHECK(!clean.poison); CHECK(clean.skipTo16k == 999);
    r.w.onProcessStarted(); CHECK(!r.crash().poison);
}

void gpu_cpu()
{
    for (auto backend : {Backend::Auto, Backend::Cpu, Backend::Vulkan, Backend::Cuda}) {
        const bool gpu = backend == Backend::Vulkan || backend == Backend::Cuda;
        Rig r; r.w.setBackend(backend);
        const auto plan = r.w.onExit(1, true, 0);
        CHECK(plan.allowed); CHECK(plan.forceCpu == gpu);
        r.w.onProcessStarted(); r.w.setBackend(Backend::Cpu);
        CHECK(r.w.onExit(1, true, 0).forceCpu == gpu); // Fallback sticks for the session.
        r.w.resetForJob(); r.w.onProcessStarted();
        CHECK(r.w.onExit(1, true, 0).forceCpu == gpu);
    }
    Rig clean; clean.w.setBackend(Backend::Vulkan);
    CHECK(!clean.w.onExit(0, false, 0).forceCpu);
    Rig deviceLost; deviceLost.w.setBackend(Backend::Vulkan);
    CHECK(deviceLost.w.onExit(static_cast<int>(ErrorCode::GpuDeviceLost), false, 0).forceCpu);
}

void nonretry()
{
    for (int code : {proto::kExitCpuUnsupported, static_cast<int>(proto::kStatusIllegalInstruction),
                     proto::kExitRuntimeMissing, proto::kExitEngineLoadFailed, proto::kExitVersionMismatch}) {
        for (bool crashed : {false, true}) {
            Rig r;
            const auto plan = r.w.onExit(code, crashed, 888);
            CHECK(!plan.allowed); CHECK(plan.delayMs == 0); CHECK(plan.skipTo16k == 888);
            r.w.onProcessStarted(); CHECK(r.w.onExit(1, true, 0).delayMs == 1000);
        }
    }
    Rig r;
    CHECK(!r.w.onExit(proto::kExitOk, false, 0).allowed);
    r.w.onProcessStarted(); CHECK(r.w.onExit(proto::kExitOk, true, 0).allowed);
    r.w.onProcessStarted(); CHECK(r.w.onExit(42, false, 0).allowed);
}

void oom()
{
    Rig r;
    const auto first = r.w.onExit(proto::kExitOutOfMemory, false, 0);
    CHECK(first.allowed); CHECK(first.delayMs == 1000);
    r.w.onProcessStarted();
    const auto second = r.w.onExit(proto::kExitOutOfMemory, false, 0);
    CHECK(second.allowed); CHECK(second.delayMs == 2000); // Level policy belongs to Runner.
}

void process_started()
{
    ManualClock clock(-100000); HostWatchdog w({}, clock);
    CHECK(w.poll(clock.nowMs() + 1000000) == Verdict::Ok); // Not started.
    w.onProcessStarted();
    clock.advance(4999); CHECK(w.poll(clock.nowMs()) == Verdict::Ok);
    clock.advance(1); CHECK(w.poll(clock.nowMs()) == Verdict::KillHung);
    w.onLoadStarted(); w.onBusy(0, 16000, 16000); w.setBackend(Backend::Vulkan);
    clock.advance(100000); w.onProcessStarted();
    CHECK(w.poll(clock.nowMs()) == Verdict::Ok); // Clears old operation deadlines.
    auto plan = w.onExit(1, true, 99);
    CHECK(plan.allowed); CHECK(!plan.poison); CHECK(!plan.forceCpu); // Old backend was cleared.
    clock.advance(1000); w.onProcessStarted();
    w.onBusy(0, 16000, 16000); plan = w.onExit(1, true, 99);
    CHECK(plan.delayMs == 2000); CHECK(!plan.poison);
    w.onProcessStarted(); w.onBusy(0, 16000, 16000); plan = w.onExit(1, true, 99);
    CHECK(plan.delayMs == 4000); CHECK(plan.poison); // Restart history survives.
}

void set_backend()
{
    Rig r;
    r.w.setBackend(Backend::Vulkan); r.w.setBackend(Backend::Cpu);
    CHECK(!r.w.onExit(1, true, 0).forceCpu); // loaded reports actual CPU fallback.
    r.w.onProcessStarted(); r.w.setBackend(Backend::Cuda);
    CHECK(r.w.onExit(1, true, 0).forceCpu);
}

void reset_for_job()
{
    Rig r;
    for (int i = 0; i < 5; ++i) { CHECK(r.crash().allowed); r.w.onProcessStarted(); }
    CHECK(!r.crash().allowed);
    r.w.resetForJob(); r.w.onProcessStarted();
    auto plan = r.crash(); CHECK(plan.allowed); CHECK(plan.delayMs == 1000); CHECK(!plan.poison);
    r.w.onProcessStarted(); plan = r.crash(); CHECK(plan.delayMs == 2000); CHECK(plan.poison);
    // Job changes may reuse a process: do not hide its current heartbeat/load/decode deadlines.
    Rig live;
    live.w.onLoadStarted(); live.at(59999); live.w.resetForJob();
    live.at(60000); CHECK(live.poll() == Verdict::KillLoadTimeout);
    Rig busy;
    busy.w.onBusy(0, 16000, 16000); busy.at(9999); busy.w.resetForJob();
    busy.at(10000); CHECK(busy.poll() == Verdict::KillDecodeTimeout);
    Rig hb;
    hb.at(4999, false); hb.w.resetForJob(); hb.at(5000, false);
    CHECK(hb.poll() == Verdict::KillHung);
}
} // namespace

int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "heartbeat") heartbeat();
        else if (name == "decode_rtf") decode_rtf();
        else if (name == "load_timeout") load_timeout();
        else if (name == "backoff") backoff();
        else if (name == "restart_window") restart_window();
        else if (name == "poison") poison();
        else if (name == "poison_idle") poison_idle();
        else if (name == "gpu_cpu") gpu_cpu();
        else if (name == "nonretry") nonretry();
        else if (name == "oom") oom();
        else if (name == "process_started") process_started();
        else if (name == "set_backend") set_backend();
        else if (name == "reset_for_job") reset_for_job();
        else CHECK(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
