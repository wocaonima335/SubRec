#include "capture/wasapi_reconnect.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
struct DeviceError { int code; };
struct FakeDevice {
    int attempts = 0, waits = 0, cleanups = 0, nativeErrors = 0, formatErrors = 0;
    int succeedsAt = 3;
    bool cancel = false, partialStream = false;
    bool run(int cancelDuringWait = 0, int cancelOnFailure = 0) {
        return capture::wasapi::reconnect<DeviceError>(
            [&] { return cancel; },
            [&] { if (++waits == cancelDuringWait) cancel = true; },
            [&] {
                CHECK(!partialStream); // previous failed Start/open must be cleaned up
                ++attempts; partialStream = true;
                if (attempts == succeedsAt) return;
                if (attempts % 2) throw std::invalid_argument("Unsupported WASAPI mix format");
                throw DeviceError{123}; // original HRESULT recovery path
            },
            [&](int attempt, const auto &error) {
                CHECK(attempt == attempts);
                partialStream = false; ++cleanups;
                if constexpr (std::is_base_of_v<std::exception, std::decay_t<decltype(error)>>) {
                    CHECK(std::string(error.what()) == "Unsupported WASAPI mix format"); ++formatErrors;
                } else { CHECK(error.code == 123); ++nativeErrors; }
                if (attempt == cancelOnFailure) cancel = true;
            });
    }
};
int main() {
    try {
        FakeDevice recovered;
        CHECK(recovered.run());
        CHECK(recovered.attempts == 3 && recovered.waits == 3 && recovered.cleanups == 2);
        CHECK(recovered.formatErrors == 1 && recovered.nativeErrors == 1 && recovered.partialStream);
        FakeDevice exhausted; exhausted.succeedsAt = 11;
        CHECK(!exhausted.run());
        CHECK(exhausted.attempts == 10 && exhausted.waits == 10 && exhausted.cleanups == 10);
        CHECK(exhausted.formatErrors == 5 && exhausted.nativeErrors == 5 && !exhausted.partialStream);
        FakeDevice lastAttempt; lastAttempt.succeedsAt = 10;
        CHECK(lastAttempt.run()); CHECK(lastAttempt.attempts == 10 && lastAttempt.cleanups == 9);
        FakeDevice alreadyCancelled; alreadyCancelled.cancel = true;
        CHECK(!alreadyCancelled.run()); CHECK(alreadyCancelled.attempts == 0 && alreadyCancelled.waits == 0);
        FakeDevice duringWait;
        CHECK(!duringWait.run(1)); CHECK(duringWait.attempts == 0 && duringWait.cleanups == 0);
        FakeDevice afterFailure;
        CHECK(!afterFailure.run(0, 1)); CHECK(afterFailure.attempts == 1 && afterFailure.cleanups == 1);
        for (bool unsupportedFormat : {true, false}) {
            int standardErrors = 0;
            CHECK(!capture::wasapi::reconnect<DeviceError>([] { return false; }, [] {},
                [&] {
                    if (unsupportedFormat) throw std::invalid_argument("Unsupported WASAPI mix format");
                    throw std::runtime_error("converter failed");
                },
                [&](int, const auto &) { ++standardErrors; }));
            CHECK(standardErrors == 10);
        }
        // Non-standard, unrelated exceptions are not hidden by a catch-all.
        bool propagated = false;
        try {
            capture::wasapi::reconnect<DeviceError>([] { return false; }, [] {},
                [] { throw 42; }, [](int, const auto &) {});
        } catch (int value) { propagated = value == 42; }
        CHECK(propagated);
        std::cout << "PASS: unsupported-format/std exceptions and HRESULT retry, cleanup, tenth attempt, exhaustion and cancellation\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
