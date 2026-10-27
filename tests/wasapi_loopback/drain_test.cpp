#include "capture/wasapi_drain.h"
#include "capture/wasapi_reconnect.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
using namespace capture::wasapi;
struct DeviceError { bool removed; };
struct FakeDevice {
    std::vector<std::string> events;
    int queued = 3, read = 0, released = 0, lossReports = 0;
    bool stopped = false, abort = false, stopRequested = false, changed = false;
    bool lostAtStop = false, lostAtNext = false, lostAtGet = false, emptyAtGet = false;
    bool otherStopError = false, failConsumer = false, abortAfterRead = false;
    bool changeAfterRead = false, stopAfterRead = false;
    int next() {
        events.push_back("next");
        if (lostAtStop || (lostAtNext && read == 1)) throw DeviceError{true};
        return queued;
    }
    bool packet() {
        events.push_back("get");
        if (lostAtGet && read == 1) throw DeviceError{true};
        if (emptyAtGet) return false;
        struct Release {
            FakeDevice &d;
            ~Release() { ++d.released; --d.queued; d.events.push_back("release"); }
        } release{*this};
        ++read;
        if (failConsumer) throw std::runtime_error("sink closed");
        events.push_back("deliver");
        if (abortAfterRead) abort = true;
        if (changeAfterRead) changed = true;
        if (stopAfterRead) stopRequested = true;
        return true;
    }
    DrainResult drain(bool final) {
        return drainPackets([&] { return abort || (!final && (stopRequested || changed)); },
                            [&] { return next(); }, [&] { return packet(); });
    }
    DrainResult retire() {
        return stopAndDrain<DeviceError>(
            [&] {
                events.push_back("stop"); stopped = true;
                if (lostAtStop || otherStopError) throw DeviceError{lostAtStop};
            },
            [&] { CHECK(stopped); return drain(true); },
            [&] { return abort; },
            [](const DeviceError &e) { return e.removed; },
            [&](const DeviceError &) { ++lossReports; events.push_back("lost"); });
    }
};
void switching() {
    FakeDevice old; old.changed = true;
    CHECK(!old.drain(false).empty && old.read == 0); // normal poll yields to transition
    const auto result = old.retire();
    CHECK(result.empty && result.receivedPacket && old.read == 3 && old.released == 3);
    old.events.push_back("destroy");
    CHECK(reconnect<DeviceError>([] { return false; }, [] {}, [&] {
        CHECK(old.queued == 0 && old.read == old.released);
        old.events.push_back("reopen");
    }, [](int, const auto &) {}));
    CHECK(old.events == std::vector<std::string>({"stop", "next", "get", "deliver", "release",
        "next", "get", "deliver", "release", "next", "get", "deliver", "release", "next", "destroy", "reopen"}));
}
void removal() {
    for (int when = 0; when < 3; ++when) {
        FakeDevice d;
        d.lostAtStop = when == 0; d.lostAtNext = when == 1; d.lostAtGet = when == 2;
        const auto result = d.retire();
        CHECK(!result.empty && d.lossReports > 0); // unavailable != silent
        CHECK(d.read == (when == 0 ? 0 : 1) && d.read == d.released);
        bool reopened = false;
        CHECK(reconnect<DeviceError>([] { return false; }, [] {}, [&] { reopened = true; }, [](int, const auto &) {}));
        CHECK(reopened);
    }
    FakeDevice unexpected; unexpected.otherStopError = true;
    bool propagated = false;
    try { unexpected.retire(); } catch (const DeviceError &e) { propagated = !e.removed; }
    CHECK(propagated && unexpected.read == 0);
}
void stopping() {
    for (bool switching : {false, true}) {
        FakeDevice midPoll;
        midPoll.changeAfterRead = switching; midPoll.stopAfterRead = !switching;
        const auto partial = midPoll.drain(false);
        CHECK(partial.receivedPacket && !partial.empty && midPoll.read == 1);
        CHECK(midPoll.retire().empty && midPoll.read == 3 && midPoll.released == 3);
    }
    FakeDevice d; d.stopRequested = true;
    CHECK(!d.drain(false).empty && d.read == 0);
    CHECK(d.retire().empty && d.queued == 0 && d.read == 3 && d.released == 3);
    // Finalization happens only after all old packets have been released.
    d.events.push_back("flush"); d.events.push_back("EOF");
    CHECK(d.events[d.events.size()-3] == "next");
    FakeDevice aborted; aborted.abort = true;
    CHECK(!aborted.retire().empty && aborted.stopped && aborted.read == 0);
    FakeDevice midAbort; midAbort.abortAfterRead = true;
    CHECK(!midAbort.retire().empty && midAbort.read == 1 && midAbort.released == 1);
    FakeDevice consumerError; consumerError.failConsumer = true;
    bool propagated = false;
    try { consumerError.retire(); } catch (const std::runtime_error &) { propagated = true; }
    CHECK(propagated && consumerError.read == 1 && consumerError.released == 1);
    FakeDevice racedEmpty; racedEmpty.emptyAtGet = true;
    CHECK(racedEmpty.retire().empty && racedEmpty.read == 0 && racedEmpty.released == 0);
}
int main() {
    try {
        switching(); removal(); stopping();
        std::cout << "PASS: old buffer drained before reopen, removal at Stop/Next/Get, drain-stop, abort and packet release\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
