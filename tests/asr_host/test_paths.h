#pragma once
// tests/asr_host：UTF-8 路径的平台辅助。
// 测试源码/协议里的路径一律是 UTF-8 字节串；POSIX 的 fopen/mkdir 直接吃 UTF-8，
// Windows 的窄字符 CRT 走 ANSI 代码页（CI 是 en-US，非 ASCII 会乱码/打不开），必须转宽字符。
// MinGW 的 libstdc++ 没有 wchar_t* 的 fstream 重载，统一用 _wfopen/_wmkdir。

#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // MSVC 的 windows.h 会定义 min/max 宏
#endif
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <cerrno>

namespace testpaths {

inline std::wstring toWide(const std::string &utf8)
{
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(len > 0 ? len : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], static_cast<int>(w.size()));
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return w;
#else
    return std::wstring(utf8.begin(), utf8.end());   // 仅为可编译，POSIX 不用
#endif
}

// 已存在（上一轮测试跑过）视为成功：测试可能重复运行
inline bool makeDirUtf8(const std::string &pathUtf8)
{
#ifdef _WIN32
    if (_wmkdir(toWide(pathUtf8).c_str()) == 0)
        return true;
    return errno == EEXIST;
#else
    if (::mkdir(pathUtf8.c_str(), 0755) == 0)
        return true;
    return errno == EEXIST;
#endif
}

inline std::FILE *fopenUtf8(const std::string &pathUtf8, const char *modeAscii)
{
#ifdef _WIN32
    return _wfopen(toWide(pathUtf8).c_str(), toWide(modeAscii).c_str());
#else
    return std::fopen(pathUtf8.c_str(), modeAscii);
#endif
}

inline bool writeFileUtf8(const std::string &pathUtf8, const std::string &content)
{
    std::FILE *f = fopenUtf8(pathUtf8, "wb");
    if (!f)
        return false;
    const bool ok = content.empty() || std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    return ok;
}

} // namespace testpaths
