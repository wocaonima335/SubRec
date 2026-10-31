#pragma once

#include <cstdlib>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#ifdef _WIN32
// Keep this Qt-free header safe after `using namespace std`: windows.h pulls
// in RPC's global `byte`, which conflicts with std::byte in that include order.
// Declare only the Win32 API we need, with the SDK's DWORD/WINAPI ABI. Reading
// the OS environment (rather than the CRT snapshot) preserves empty/Unicode
// values set by SetEnvironmentVariableW, including the Windows gate tests.
extern "C" __declspec(dllimport) unsigned long __stdcall
GetEnvironmentVariableW(const wchar_t *name, wchar_t *buffer, unsigned long size);
#endif

#if defined(SUBREC_STRIP_TEST_HOOKS) && SUBREC_STRIP_TEST_HOOKS \
    && defined(SUBREC_ENABLE_TEST_HOOKS) && SUBREC_ENABLE_TEST_HOOKS
#error "SUBREC_STRIP_TEST_HOOKS and SUBREC_ENABLE_TEST_HOOKS are mutually exclusive"
#endif

namespace capture::test {

// Both main and the gate use this helper without a Qt dependency: Windows
// reads the Unicode environment with GetEnvironmentVariableW (UTF-16), while
// other platforms interpret getenv() as UTF-8. The decoders below
// applies the Unicode whitespace set used by QChar::isSpace(), including NBSP,
// ideographic space, and the Unicode line separators.
inline bool qtSpaceCodePoint(char32_t codePoint)
{
    return (codePoint >= 0x0009 && codePoint <= 0x000d)
        || codePoint == 0x0020 || codePoint == 0x0085 || codePoint == 0x00a0
        || codePoint == 0x1680 || codePoint == 0x2028 || codePoint == 0x2029
        || codePoint == 0x202f || codePoint == 0x205f || codePoint == 0x3000
        || (codePoint >= 0x2000 && codePoint <= 0x200a);
}

inline bool utf8ScenarioHasNonSpace(const char *value)
{
    if (!value) return false;
    const auto *bytes = reinterpret_cast<const unsigned char *>(value);
    while (*bytes) {
        char32_t codePoint = *bytes++;
        unsigned continuation = 0;
        char32_t minimum = 0;
        if (codePoint >= 0xc2 && codePoint <= 0xdf) {
            continuation = 1; minimum = 0x80; codePoint &= 0x1f;
        } else if (codePoint >= 0xe0 && codePoint <= 0xef) {
            continuation = 2; minimum = 0x800; codePoint &= 0x0f;
        } else if (codePoint >= 0xf0 && codePoint <= 0xf4) {
            continuation = 3; minimum = 0x10000; codePoint &= 0x07;
        } else if (codePoint < 0x80) {
            continuation = 0;
        } else {
            return true; // Invalid UTF-8 is not whitespace.
        }
        for (unsigned i = 0; i < continuation; ++i) {
            const unsigned char next = *bytes;
            if ((next & 0xc0) != 0x80) return true;
            codePoint = (codePoint << 6) | (next & 0x3f);
            ++bytes;
        }
        if ((continuation && codePoint < minimum) || codePoint > 0x10ffff
            || (codePoint >= 0xd800 && codePoint <= 0xdfff)) return true;
        if (!qtSpaceCodePoint(codePoint)) return true;
    }
    return false;
}

inline bool autoTestScenarioActive()
{
#ifdef _WIN32
    const auto required = GetEnvironmentVariableW(L"SUBREC_AUTOTEST_SCENARIO", nullptr, 0);
    if (!required) return false;
    std::wstring value(static_cast<size_t>(required), L'\0');
    const auto length = GetEnvironmentVariableW(L"SUBREC_AUTOTEST_SCENARIO", value.data(),
                                                 static_cast<unsigned long>(value.size()));
    if (!length || length >= value.size()) return false; // Environment changed during the read.
    for (size_t i = 0; i < length;) {
        char32_t codePoint = value[i++];
        if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
            if (i >= length || value[i] < 0xdc00 || value[i] > 0xdfff) return true;
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (value[i++] - 0xdc00);
        } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
            return true;
        }
        if (!qtSpaceCodePoint(codePoint)) return true;
    }
    return false;
#else
    return utf8ScenarioHasNonSpace(std::getenv("SUBREC_AUTOTEST_SCENARIO"));
#endif
}

#if defined(SUBREC_STRIP_TEST_HOOKS) && SUBREC_STRIP_TEST_HOOKS
inline constexpr bool hooksEnabled() { return false; }
inline const char *hookEnvironment(const char *) { return nullptr; }
inline constexpr bool videoOpenTimeoutRequested() { return false; }
inline constexpr uint64_t muxFailAtPacket() { return 0; }
#else
// Standalone test targets may opt in at compile time. Production modules retain
// the runtime AutoTest gate even when the top-level test option is ON.
inline bool hooksEnabled()
{
#if defined(SUBREC_ENABLE_TEST_HOOKS) && SUBREC_ENABLE_TEST_HOOKS
    return true;
#else
    return autoTestScenarioActive();
#endif
}
inline const char *hookEnvironment(const char *name)
{
    return hooksEnabled() ? std::getenv(name) : nullptr;
}
inline uint64_t muxFailAtPacket()
{
    const char *value = hookEnvironment("SUBREC_TEST_MUX_FAIL_AT_PACKET");
    if (!value || *value < '1' || *value > '9') return 0;
    char *end = nullptr;
    errno = 0;
    const auto target = std::strtoull(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' ? target : 0;
}
inline bool videoOpenTimeoutRequested()
{
    const char *value = hookEnvironment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT");
    return value && std::strcmp(value, "1") == 0;
}
#endif
}
