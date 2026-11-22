#include "subtitle/live/degrade_controller.h"

#include <iostream>
#include <stdexcept>

using namespace subtitle;
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)

namespace {
using Level = DegradeLevel;
LoadSample sample(int64_t now, double lag = 0, double rtf = 0, double cpu = 0)
{
    LoadSample s;
    s.nowMs = now; s.lagSec = lag; s.rtfEwma = rtf; s.sysCpu = cpu;
    return s;
}
void changed(DegradeController &d, const LoadSample &s, Level level, const char *reason)
{
    const auto result = d.tick(s);
    CHECK(result); CHECK(result->first == level); CHECK(result->second == reason);
    CHECK(d.level() == level); CHECK(d.reason() == reason);
}

// Each degradation trigger has its own named ctest and exact threshold/hold boundary.
void trigger(const std::string &name)
{
    const bool fromL1 = name.find("l1_l2") == 0;
    const bool lag = name.find("lag") != std::string::npos;
    const bool rtf = name.find("rtf") != std::string::npos;
    const double threshold = lag ? (fromL1 ? 20 : 6) : rtf ? (fromL1 ? .9 : .5) : 90;
    const int hold = fromL1 ? (lag ? 15000 : 30000) : 10000;
    const char *reason = lag ? "lag" : rtf ? "rtf" : "sysCpu";
    DegradeController d({});
    if (fromL1) d.forceLevel(Level::L1FinalOnly, "manual");
    auto s = sample(100000);
    auto set = [&](double value) { if (lag) s.lagSec = value; else if (rtf) s.rtfEwma = value; else s.sysCpu = value; };
    set(threshold);
    CHECK(!d.tick(s)); s.nowMs += hold; CHECK(!d.tick(s));
    set(threshold + .01);
    const auto start = ++s.nowMs;
    CHECK(!d.tick(s)); s.nowMs = start + hold - 1; CHECK(!d.tick(s));
    s.nowMs = start + hold;
    changed(d, s, fromL1 ? Level::L2CatchUp : Level::L1FinalOnly, reason);
}

void sustain_reset()
{
    DegradeController d({});
    CHECK(!d.tick(sample(0, 7))); CHECK(!d.tick(sample(9999, 7)));
    CHECK(!d.tick(sample(10000, 6))); // Equality breaks the lag streak.
    CHECK(!d.tick(sample(10001, 7))); CHECK(!d.tick(sample(20000, 7)));
    changed(d, sample(20001, 7), Level::L1FinalOnly, "lag");
    DegradeController separate({});
    CHECK(!separate.tick(sample(0, 7)));
    CHECK(!separate.tick(sample(9000, 0, .6)));
    CHECK(!separate.tick(sample(10000, 0, .6)));
    CHECK(!separate.tick(sample(18999, 0, .6)));
    changed(separate, sample(19000, 0, .6), Level::L1FinalOnly, "rtf");
}

void recovery(bool fromL2)
{
    // Each conjunct at equality must break recovery, as must the hysteresis dead band.
    for (int gate = 0; gate < (fromL2 ? 1 : 3); ++gate) {
        DegradeController d({});
        d.forceLevel(fromL2 ? Level::L2CatchUp : Level::L1FinalOnly, "manual");
        CHECK(!d.tick(sample(0)));
        CHECK(!d.tick(sample(59999)));
        auto boundary = sample(60000);
        if (gate == 0) boundary.lagSec = fromL2 ? 5 : 3;
        if (gate == 1) boundary.rtfEwma = .3;
        if (gate == 2) boundary.sysCpu = 75;
        CHECK(!d.tick(boundary));
        CHECK(!d.tick(sample(60001)));
        CHECK(!d.tick(sample(120000)));
        changed(d, sample(120001), fromL2 ? Level::L1FinalOnly : Level::L0Live, "lag");
    }
    DegradeController d({});
    d.forceLevel(fromL2 ? Level::L2CatchUp : Level::L1FinalOnly, "manual");
    for (int64_t now = 0; now <= 180000; now += 1000)
        CHECK(!d.tick(sample(now, fromL2 ? 10 : 4, .4, 80)));
}

void min_interval()
{
    DegradeThresholds t;
    t.l0l1LagHoldMs = t.l1l2LagHoldMs = t.l2l1HoldMs = t.l1l0HoldMs = 0;
    DegradeController d(t);
    changed(d, sample(50000, 30), Level::L1FinalOnly, "lag");
    CHECK(!d.tick(sample(50000, 30))); CHECK(!d.tick(sample(59999, 30)));
    changed(d, sample(60000, 30), Level::L2CatchUp, "lag");
    CHECK(!d.tick(sample(60000))); CHECK(!d.tick(sample(69999)));
    changed(d, sample(70000), Level::L1FinalOnly, "lag");
    CHECK(!d.tick(sample(79999)));
    changed(d, sample(80000), Level::L0Live, "lag");
    CHECK(!d.tick(sample(89999, 30)));
    changed(d, sample(90000, 30), Level::L1FinalOnly, "lag");
}

void lag_l3()
{
    DegradeController d({});
    d.forceLevel(Level::L2CatchUp, "manual");
    CHECK(!d.tick(sample(0, 120)));
    changed(d, sample(1, 120.001), Level::L3Deferred, "lagL2");
    // A single tick with extreme lag still moves L0 only to L1, then L1 only to L2.
    DegradeController steps({});
    CHECK(!steps.tick(sample(0, 200)));
    changed(steps, sample(10000, 200), Level::L1FinalOnly, "lag");
    CHECK(!steps.tick(sample(10000, 200)));
    CHECK(!steps.tick(sample(24999, 200)));
    changed(steps, sample(25000, 200), Level::L2CatchUp, "lag");
    changed(steps, sample(25001, 200), Level::L3Deferred, "lagL2");
}

LoadSample health(int64_t now, uint64_t frames, uint64_t drops, double cpu)
{
    auto s = sample(now);
    s.health.videoFrames = frames; s.health.videoTickDrops = drops; s.hostCpuCores = cpu;
    return s;
}
void recording_health()
{
    for (auto level : {Level::L0Live, Level::L1FinalOnly, Level::L2CatchUp}) {
        DegradeController d({});
        d.forceLevel(level, "manual");
        CHECK(!d.tick(health(0, 100000, 1000, .3)));
        CHECK(!d.tick(health(9999, 100999, 1003, .3)));
        // Bypass the minimum interval after a just-forced transition.
        d.forceLevel(level, "manual");
        changed(d, health(10000, 101000, 1003, .3), Level::L3Deferred, "rec");
    }
    for (const auto &row : std::vector<std::pair<uint64_t, double>>{{2, .3}, {3, .2}, {0, 1}}) {
        DegradeController d({});
        CHECK(!d.tick(health(0, 0, 0, row.second)));
        CHECK(!d.tick(health(10000, 1000, row.first, row.second)));
    }
    DegradeController empty({});
    CHECK(!empty.tick(health(0, 0, 0, 1)));
    CHECK(!empty.tick(health(10000, 0, 100, 1))); // No denominator.
}

void health_window()
{
    // CPU is averaged over the same window, not taken only from the final sample.
    DegradeController high({});
    CHECK(!high.tick(health(0, 0, 0, .4)));
    CHECK(!high.tick(health(9000, 900, 3, 0)));
    changed(high, health(10000, 1000, 3, 0), Level::L3Deferred, "rec");
    DegradeController low({});
    CHECK(!low.tick(health(0, 0, 0, 0)));
    CHECK(!low.tick(health(9000, 900, 3, 1)));
    CHECK(!low.tick(health(10000, 1000, 3, 1)));
    // Old drop spikes age out; counters are cumulative, not per-tick counts.
    DegradeController rolling({});
    for (int i = 0; i <= 20; ++i)
        CHECK(!rolling.tick(health(i * 1000, i * 100, i ? 3 : 0, i < 10 ? 0 : .3)));
    // Counter rollback establishes a fresh baseline, with no unsigned underflow.
    DegradeController reset({});
    CHECK(!reset.tick(health(0, 1000, 100, .3)));
    CHECK(!reset.tick(health(10000, 0, 0, .3)));
    CHECK(!reset.tick(health(19999, 999, 3, .3)));
    changed(reset, health(20000, 1000, 3, .3), Level::L3Deferred, "rec");
    // An irregular timestamp requires interpolating the window's left boundary.
    DegradeController irregular({});
    CHECK(!irregular.tick(health(0, 0, 0, .3)));
    CHECK(!irregular.tick(health(6000, 600, 0, .3)));
    changed(irregular, health(12000, 1200, 3, .3), Level::L3Deferred, "rec");
}

void l3_latched()
{
    DegradeController d({});
    d.forceLevel(Level::L2CatchUp, "manual");
    changed(d, sample(0, 121), Level::L3Deferred, "lagL2");
    for (int64_t now = 1000; now <= 600000; now += 1000) CHECK(!d.tick(sample(now)));
    CHECK(d.level() == Level::L3Deferred); CHECK(d.reason() == "lagL2");
    d.forceLevel(Level::L2CatchUp, "manual");
    CHECK(d.level() == Level::L2CatchUp); CHECK(!d.tick(sample(600000)));
    changed(d, sample(660000), Level::L1FinalOnly, "lag");
}

void force_level()
{
    DegradeController d({});
    CHECK(!d.tick(sample(0, 7))); CHECK(!d.tick(sample(9999, 7)));
    d.forceLevel(Level::L0Live, "manual"); // Even a same-level force resets sustain.
    CHECK(!d.tick(sample(10000, 7))); CHECK(!d.tick(sample(19999, 7)));
    changed(d, sample(20000, 7), Level::L1FinalOnly, "lag");
    int64_t now = 20000;
    for (const auto *reason : {"spool", "restartExhausted"}) {
        d.forceLevel(Level::L4Failed, reason);
        CHECK(d.level() == Level::L4Failed); CHECK(d.reason() == reason);
        CHECK(!d.tick(health(now += 10000, 0, 0, 1)));
        CHECK(!d.tick(health(now += 10000, 1000, 100, 1))); // Health cannot recover a failed job to L3.
    }
    for (auto level : {Level::L0Live, Level::L1FinalOnly, Level::L2CatchUp, Level::L3Deferred}) {
        d.forceLevel(level, "oom"); CHECK(d.level() == level); CHECK(d.reason() == "oom");
    }
    DegradeThresholds t; t.l1l2LagHoldMs = 0;
    DegradeController beforeTick(t);
    beforeTick.forceLevel(Level::L1FinalOnly, "manual");
    CHECK(!beforeTick.tick(sample(900000, 30)));
    CHECK(!beforeTick.tick(sample(909999, 30)));
    changed(beforeTick, sample(910000, 30), Level::L2CatchUp, "lag");
}
void timestamps()
{
    DegradeController d({});
    CHECK(!d.tick(sample(-20000, 7))); CHECK(!d.tick(sample(-20000, 7)));
    CHECK(!d.tick(sample(-21000))); // Stale sample does not reset a valid streak.
    CHECK(!d.tick(sample(-10001, 7)));
    changed(d, sample(-10000, 7), Level::L1FinalOnly, "lag");
}
} // namespace

int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "l0_l1_lag" || name == "l0_l1_rtf" || name == "l0_l1_cpu" ||
            name == "l1_l2_lag" || name == "l1_l2_rtf") trigger(name);
        else if (name == "sustain_reset") sustain_reset();
        else if (name == "l1_l0_hysteresis") recovery(false);
        else if (name == "l2_l1_hysteresis") recovery(true);
        else if (name == "min_interval") min_interval();
        else if (name == "l2_l3_lag") lag_l3();
        else if (name == "recording_health") recording_health();
        else if (name == "health_window") health_window();
        else if (name == "l3_latched") l3_latched();
        else if (name == "force_level") force_level();
        else if (name == "timestamps") timestamps();
        else CHECK(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
