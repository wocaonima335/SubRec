// P3a: QPC -> session clock mapping and presentation-time stamping of screen frames.
#include "capture/present_stamp.h"
#include "capture/qpc_session_clock.h"
#include "queue/video_tick_scheduler.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)

namespace {
void ticksConversion()
{
    CHECK(QpcSessionClock::ticksTo100ns(10000000, 10000000) == 10000000);
    // 3.579545 MHz (ACPI PM timer) and a 10-hour value without overflow.
    const int64_t f = 3579545;
    const int64_t ticks = f * 36000 + 1234567;
    const int64_t expected = 36000LL * 10000000 + 1234567LL * 10000000 / f;
    CHECK(QpcSessionClock::ticksTo100ns(ticks, f) == expected);
    CHECK(QpcSessionClock::ticksTo100ns(5, 0) == AV_NOPTS_VALUE);
}

void mappingTracksSkewAndOffset()
{
    QpcSessionClock clock;
    CHECK(clock.toSessionUs(123) == AV_NOPTS_VALUE);
    // Session clock = QPC * (1 + 80 ppm) + 7 s offset, anchors every 10 s with
    // +/-20 us bracketing jitter, over 30 minutes.
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> jitter(-20, 20);
    const double skew = 80e-6;
    auto session = [&](double qpcUs) { return int64_t(std::llround(7e6 + qpcUs * (1 + skew))); };
    double worst = 0;
    for (int i = 0; i <= 180; ++i) {
        const double qpcUs = 1e9 + i * 10e6;
        clock.addAnchor({int64_t(qpcUs * 10), session(qpcUs) + jitter(rng), 20});
        // With a single anchor the slope is unknown (80 ppm -> 0.8 ms after 10 s).
        if (i == 0) continue;
        // Extrapolate up to one refresh interval past the newest anchor.
        for (double d : {2.5e6, 5e6, 9.9e6}) {
            const double q = qpcUs + d;
            worst = std::max(worst, std::abs(double(clock.toSessionUs(int64_t(q * 10)) - session(q))));
        }
    }
    const auto s = clock.stats();
    std::cout << "qpc fit: slope_ppm=" << s.slopePpm << " residual_us=" << s.maxResidualUs
              << " drift_us=" << s.driftUs << " worst_mapping_error_us=" << worst << '\n';
    CHECK(s.anchors == 181);
    CHECK(std::abs(s.slopePpm - 80) < 5);
    CHECK(s.maxResidualUs <= 40);
    CHECK(std::abs(s.driftUs - 144000) < 100);   // 80 ppm over 1800 s
    CHECK(worst < 40);
}

void steppedSessionClockIsVisible()
{
    // A wall-clock based session clock (1 ms granularity, H3 in the P3 plan)
    // shows up as residual instead of being hidden by a one-shot offset.
    QpcSessionClock clock;
    for (int i = 0; i < 16; ++i) {
        const int64_t qpcUs = 5000000 + i * 10000000LL + (i % 3) * 437;
        clock.addAnchor({qpcUs * 10, (qpcUs / 1000) * 1000, 1});
    }
    CHECK(clock.stats().maxResidualUs > 200);
    clock.reset();
    CHECK(clock.stats().anchors == 0 && clock.toSessionUs(1) == AV_NOPTS_VALUE);
}

void stamperRules()
{
    PresentStamper st;   // 30 fps, max age 250 ms
    // New image: present time is used.
    CHECK(st.stamp({true, 1000000, true, 1012000}) == 1000000);
    // Two duplicates: +1 and +2 frames, capped by receive time.
    CHECK(st.stamp({true, 1000000, false, 1045000}) == 1033333);
    CHECK(st.stamp({true, 1000000, false, 1050000}) == 1050000);
    // No metadata (unpatched FFmpeg): receive time, counted.
    CHECK(st.stamp({false, 0, false, 1080000}) == 1080000);
    // Present time in the future or too old: rejected, receive time.
    CHECK(st.stamp({true, 1200000, true, 1110000}) == 1110000);
    CHECK(st.stamp({true, 800000, true, 1140000}) == 1140000);
    // Present time older than the previous stamp: clamped to stay increasing.
    CHECK(st.stamp({true, 1139000, true, 1150000}) == 1140001);
    const auto &c = st.counters();
    CHECK(c.present == 2 && c.synthesized == 2 && c.fallback == 3);
    CHECK(c.rejectedFuture == 1 && c.rejectedStale == 1 && c.clamped == 1);
    CHECK(c.maxLatencyUs == 12000);
    st.reset();
    CHECK(st.counters().present == 0 && st.stamp({true, 5, true, 10}) == 5);
}

// #30 lock-screen hold / capture recovery: held copies of an old picture are
// stamped with their queue time and never let the next presentation time run
// backwards into them.
void heldFramesAcrossLock()
{
    PresentStamper st;
    CHECK(st.stamp({true, 1000000, true, 1010000}) == 1000000);
    CHECK(st.stamp({true, 1000000, false, 1043000}) == 1033333);    // duplicate on the grid
    // Session locked: the pre-lock snapshot is re-queued at ~30 fps.
    int64_t last = 1033333;
    for (int i = 1; i <= 90; ++i) {
        const int64_t held = st.stampHeld(1043000 + i * 33333);
        CHECK(held > last);
        last = held;
    }
    // A duplicate right after the hold must not be synthesized from the pre-lock image.
    const int64_t dup = st.stamp({true, 1000000, false, last + 20000});
    CHECK(dup == last + 20000);
    last = dup;
    st.stampHeld(last + 33333);
    last += 33333;
    // Unlock: the first new image was presented while the hold was still running.
    const int64_t first = st.stamp({true, last - 15000, true, last + 9000});
    CHECK(first == last + 9000);
    // Later new images use their presentation time again.
    CHECK(st.stamp({true, last + 40000, true, last + 52000}) == last + 40000);
    const auto &c = st.counters();
    CHECK(c.held == 91 && c.afterHold == 1 && c.present == 2 && c.synthesized == 1);
    CHECK(c.fallback == 2);   // the post-hold duplicate and the after-hold image
}

// Content presents at 60 Hz; ddagrab acquires at its 30 fps tick and SubRec
// receives the frame after copy/graph/thread latency (P1 field logs: ~13-33 ms).
// VideoTickScheduler::decidePaced removes per-frame jitter once paced, so the
// timestamp source decides the session's constant phase. Presentation stamps
// keep "tick time - shown time" within one 60 Hz vsync in every session; receive
// stamps add the session's random latency to every frame.
void flashTickError()
{
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> latency(13000, 33000), phase(0, 1e6 / 30);
    const double frameUs = 1e6 / 30, vsyncUs = 1e6 / 60;
    double worstPresent = 0, worstReceive = 0;
    uint64_t collisions = 0, receiveDrops = 0;
    for (int session = 0; session < 200; ++session) {
        const int64_t t0 = 10000000;
        const double captureOffset = phase(rng);   // ddagrab tick phase vs. session origin
        VideoTickScheduler presentTicks, receiveTicks;
        CHECK(presentTicks.configure(AVRational{30, 1}) && receiveTicks.configure(AVRational{30, 1}));
        PresentStamper stamper;
        double lastPresent = -1e9, sumPresent = 0, sumReceive = 0;
        int frames = 0;
        for (int n = 1; n <= 300; ++n) {
            const double acquire = captureOffset + n * frameUs;
            const double present = std::floor(acquire / vsyncUs) * vsyncUs;   // newest vsync
            const bool isNew = present > lastPresent;
            lastPresent = present;
            const double receive = acquire + latency(rng);
            const auto p = presentTicks.decidePaced(
                stamper.stamp({true, t0 + int64_t(present), isNew, t0 + int64_t(receive)}) - t0);
            const auto r = receiveTicks.decidePaced(int64_t(receive));
            if (!p.accepted()) {
                std::cerr << "present decision kind=" << int(p.kind) << " session=" << session << " n=" << n << '\n';
                CHECK(false);
            }
            sumPresent += p.tick * frameUs - present;
            if (r.accepted()) sumReceive += r.tick * frameUs - present;   // receive may collide
            else ++receiveDrops;
            ++frames;
        }
        collisions += presentTicks.counters().slotCollisions;
        worstPresent = std::max(worstPresent, std::abs(sumPresent / frames));
        worstReceive = std::max(worstReceive, std::abs(sumReceive / frames));
    }
    std::cout << "phase error over 200 sessions: present max_us=" << worstPresent
              << " receive max_us=" << worstReceive << " present collisions=" << collisions
              << " receive drops=" << receiveDrops << '\n';
    CHECK(collisions == 0);
    CHECK(worstPresent <= frameUs / 2 + vsyncUs);
    CHECK(worstReceive > worstPresent + 10000);
}
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "all";
    try {
        if (mode == "qpc" || mode == "all") {
            ticksConversion();
            mappingTracksSkewAndOffset();
            steppedSessionClockIsVisible();
        }
        if (mode == "present" || mode == "all") {
            stamperRules();
            heldFramesAcrossLock();
            flashTickError();
        }
        std::cout << "PASS: qpc/present " << mode << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
