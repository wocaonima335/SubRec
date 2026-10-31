#include "capture/test_video_start_delay.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
using namespace std::chrono_literals;
namespace {
void environment(const char *name, const char *value) {
#ifdef _WIN32
    // Keep the CRT environment used by hookEnvironment in sync, then set the
    // OS value explicitly: _putenv_s(name, "") alone would DELETE the variable.
    const int size = value ? MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0) : 0;
    std::wstring wideName;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
        wideName.push_back(static_cast<wchar_t>(*p));
    std::wstring wideValue;
    if (value) {
        wideValue.resize(static_cast<size_t>(size));
        MultiByteToWideChar(CP_UTF8, 0, value, -1, wideValue.data(), size);
        if (!wideValue.empty() && wideValue.back() == L'\0') wideValue.pop_back();
    }
    // Keep the CRT environment in sync using the wide API, so Unicode values
    // do not depend on the ANSI code page. Removing a variable that is absent
    // from the CRT environment fails on MSVCRT; that is not an error for unset
    // or empty values, which the OS-level call below sets explicitly.
    const int putenvResult = _wputenv_s(wideName.c_str(), wideValue.c_str());
    if (value && *value) CHECK(putenvResult == 0);
    if (!SetEnvironmentVariableW(wideName.c_str(), value ? wideValue.c_str() : nullptr))
        throw std::runtime_error("SetEnvironmentVariableW failed");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
bool environmentIsSet(const char *name, bool *empty = nullptr) {
#ifdef _WIN32
    std::wstring wideName;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
        wideName.push_back(static_cast<wchar_t>(*p));
    SetLastError(ERROR_SUCCESS);
    wchar_t probe[2] = {};
    const DWORD length = GetEnvironmentVariableW(wideName.c_str(), probe, 2);
    const bool present = length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    if (empty) *empty = present && length == 0;
    return present;
#else
    const char *value = std::getenv(name);
    if (empty) *empty = value && *value == '\0';
    return value != nullptr;
#endif
}
void checkHooks(bool expected) {
    CHECK(capture::test::hooksEnabled() == expected);
    CHECK(capture::test::muxFailAtPacket() == (expected ? 17 : 0));
    CHECK(capture::test::videoOpenTimeoutRequested() == expected);
    capture::test::VideoStartDelay delay;
    int waits = 0, logs = 0;
    auto wait = [&](auto duration) { CHECK(duration == 2s); ++waits; return false; };
    auto log = [&](auto ms) { CHECK(ms == 2000); ++logs; };
    CHECK(delay.beforeFirstPacket(wait, log));
    CHECK(delay.beforeFirstPacket(wait, log));
    CHECK(waits == (expected ? 1 : 0) && logs == waits);
}
void checkScenario(bool scenarioExpected, bool hookExpected) {
    CHECK(capture::test::autoTestScenarioActive() == scenarioExpected);
    checkHooks(hookExpected);
}
}
int main() {
    try {
#if defined(SUBREC_ENABLE_TEST_HOOKS) && SUBREC_ENABLE_TEST_HOOKS
        constexpr bool testBuild = true;
#else
        constexpr bool testBuild = false;
#endif
        environment("SUBREC_TEST_MUX_FAIL_AT_PACKET", "17");
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", "2000");
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "1");
        environment("SUBREC_AUTOTEST_SCENARIO", nullptr);
        checkScenario(false, testBuild); // Hook variables alone must not affect GUI recording.
        environment("SUBREC_AUTOTEST_SCENARIO", "");
        bool empty = false;
        CHECK(environmentIsSet("SUBREC_AUTOTEST_SCENARIO", &empty) && empty);
        checkScenario(false, testBuild);
        for (const char *scenario : {" \t\r\n", "\xc2\xa0", "\xe3\x80\x80", "\xe2\x80\xa8\xe2\x80\xa9", " \xc2\xa0\xe3\x80\x80\t"}) {
            environment("SUBREC_AUTOTEST_SCENARIO", scenario);
            checkScenario(false, testBuild); // Unicode whitespace matches QString::trimmed().
        }
        environment("SUBREC_AUTOTEST_SCENARIO", " \xc2\xa0record_seconds\xe3\x80\x80");
        checkScenario(true, true);
        environment("SUBREC_AUTOTEST_SCENARIO", " record_seconds \t");
        checkHooks(true);
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "0");
        CHECK(!capture::test::videoOpenTimeoutRequested());
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", "invalid");
        capture::test::VideoStartDelay invalid;
        CHECK(invalid.beforeFirstPacket([](auto) { throw std::runtime_error("unexpected wait"); return false; },
                                       [](auto) { throw std::runtime_error("unexpected log"); }));
        environment("SUBREC_TEST_VIDEO_START_DELAY_MS", nullptr);
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", nullptr);
        CHECK(!capture::test::videoOpenTimeoutRequested());
        capture::test::VideoStartDelay unset;
        CHECK(unset.beforeFirstPacket([](auto) { throw std::runtime_error("unexpected wait"); return false; },
                                     [](auto) { throw std::runtime_error("unexpected log"); }));
        for (const char *invalid : {"", "0", "-1", "bogus", "17x", "18446744073709551616"}) {
            environment("SUBREC_TEST_MUX_FAIL_AT_PACKET", invalid);
            CHECK(capture::test::muxFailAtPacket() == 0);
        }
        std::printf("PASS hook gate: test_build=%d, normal/empty/whitespace/AutoTest/unset/invalid/one-shot\n", testBuild);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
