// subrec-asr 入口（工作包 C）。
// Windows：wmain + CommandLineToArgvW 语义的宽字符 argv → UTF-8；SetErrorMode 不弹系统对话框
// （§7.6）。捕获 bad_alloc → exit(kExitOutOfMemory)（§7.5）。--version 打印到真实 stdout（install 之前）。

#include "host_options.h"
#include "host_runtime.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_error.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef SUBREC_ASR_VERSION
#define SUBREC_ASR_VERSION "0.0.0-m0"
#endif

namespace {

int runMain(int argc, const char *const *argv)
{
    asrhost::HostOptions options;
    subtitle::Status err;
    if (!asrhost::parseHostOptions(argc, argv, options, &err)) {
        std::fprintf(stderr, "subrec-asr: %s\n", err.message.c_str());
        return static_cast<int>(subtitle::ErrorCode::InvalidArgument);
    }
    if (options.showVersion) {
        std::printf("subrec-asr %s protocol=%d\n", SUBREC_ASR_VERSION,
                    subtitle::proto::kProtocolVersion);
        return subtitle::proto::kExitOk;
    }
    return asrhost::runHost(options);
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
    try {
        return runMain(argc, ptrs.data());
    } catch (const std::bad_alloc &) {
        std::_Exit(subtitle::proto::kExitOutOfMemory);
    } catch (...) {
        std::_Exit(static_cast<int>(subtitle::ErrorCode::InternalError));
    }
}
#else
int main(int argc, char **argv)
{
    try {
        return runMain(argc, argv);
    } catch (const std::bad_alloc &) {
        std::_Exit(subtitle::proto::kExitOutOfMemory);
    } catch (...) {
        std::_Exit(static_cast<int>(subtitle::ErrorCode::InternalError));
    }
}
#endif
