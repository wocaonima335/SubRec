#include "child_process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#ifdef _WIN32
std::wstring toWide(const std::string &utf8)
{
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(len > 0 ? len : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], static_cast<int>(w.size()));
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return w;
}

std::string toMultiByte(const std::wstring &w)
{
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len > 0 ? len : 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], static_cast<int>(s.size()), nullptr,
                        nullptr);
    if (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}
#endif

} // namespace

ChildProcess::~ChildProcess()
{
    kill();
}

bool ChildProcess::drainPending(std::string &line)
{
    const size_t nl = m_buf.find('\n');
    if (nl == std::string::npos)
        return false;
    line = m_buf.substr(0, nl);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    m_buf.erase(0, nl + 1);
    return true;
}

#ifdef _WIN32

bool ChildProcess::spawn(const std::string &exe, const std::vector<std::string> &args,
                         const std::map<std::string, std::string> &setEnv,
                         const std::vector<std::string> &unsetEnv)
{
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0))
        return false;

    std::string cmdline = "\"" + exe + "\"";
    for (const std::string &a : args)
        cmdline += " " + a;
    std::wstring wcmd = toWide(cmdline);

    // 环境块：当前环境 + 覆盖/删除
    std::map<std::string, std::string> env;
    {
        LPWCH block = GetEnvironmentStringsW();
        for (LPWCH p = block; *p; p += wcslen(p) + 1) {
            std::wstring w(p);
            const size_t eq = w.find(L'=');
            if (eq != std::wstring::npos)
                env[toMultiByte(w.substr(0, eq))] = toMultiByte(w.substr(eq + 1));
        }
        FreeEnvironmentStringsW(block);
    }
    for (const std::string &key : unsetEnv)
        env.erase(key);
    for (const auto &kv : setEnv)
        env[kv.first] = kv.second;
    std::wstring envBlock;
    for (const auto &kv : env)
        envBlock += toWide(kv.first) + L"=" + toWide(kv.second) + L'\0';
    envBlock += L'\0';

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
                                   envBlock.empty() ? nullptr : &envBlock[0], nullptr, &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;
    m_inWrite = inWrite;
    m_outRead = outRead;
    return true;
}

void ChildProcess::writeLine(const std::string &line)
{
    const std::string data = line + "\n";
    DWORD written = 0;
    WriteFile(m_inWrite, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
}

void ChildProcess::closeStdin()
{
    if (m_inWrite) {
        CloseHandle(m_inWrite);
        m_inWrite = nullptr;
    }
}

bool ChildProcess::readLine(std::string &line, int timeoutMs)
{
    const int64_t deadline = nowMs() + timeoutMs;
    for (;;) {
        if (drainPending(line))
            return true;
        DWORD avail = 0;
        if (!PeekNamedPipe(m_outRead, nullptr, 0, nullptr, &avail, nullptr))
            return false;   // 管道关闭 = EOF
        if (avail == 0) {
            if (nowMs() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        char chunk[4096];
        DWORD got = 0;
        if (!ReadFile(m_outRead, chunk, static_cast<DWORD>(std::min<DWORD>(avail, sizeof chunk)), &got,
                      nullptr)
            || got == 0)
            return false;
        m_buf.append(chunk, chunk + got);
    }
}

ChildProcess::Exit ChildProcess::waitForExit(int timeoutMs)
{
    Exit res;
    if (m_reaped) {
        res.code = m_final.code;
        res.signaled = m_final.signaled;
        return res;
    }
    const DWORD wait = WaitForSingleObject(m_process, timeoutMs < 0 ? INFINITE : timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        res.timedOut = true;
        return res;
    }
    DWORD code = 0;
    GetExitCodeProcess(m_process, &code);
    m_reaped = true;
    m_final.code = static_cast<int>(code);
    m_final.signaled = code >= 0x80000000;   // NTSTATUS 区间近似为「崩溃」
    res.code = m_final.code;
    res.signaled = m_final.signaled;
    return res;
}

bool ChildProcess::alive()
{
    if (m_reaped)
        return false;
    return WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
}

void ChildProcess::kill()
{
    if (m_process && !m_reaped) {
        TerminateProcess(m_process, 1);
        WaitForSingleObject(m_process, 3000);
    }
    closeStdin();
    if (m_outRead) {
        CloseHandle(m_outRead);
        m_outRead = nullptr;
    }
    if (m_process) {
        CloseHandle(m_process);
        m_process = nullptr;
    }
}

#else // ---- POSIX ----

extern char **environ;

bool ChildProcess::spawn(const std::string &exe, const std::vector<std::string> &args,
                         const std::map<std::string, std::string> &setEnv,
                         const std::vector<std::string> &unsetEnv)
{
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
        return false;

    std::vector<std::string> envStore;
    for (char **e = environ; e && *e; ++e)
        envStore.emplace_back(*e);
    for (const std::string &key : unsetEnv)
        envStore.erase(std::remove_if(envStore.begin(), envStore.end(),
                                      [&](const std::string &entry) {
                                          return entry.rfind(key + "=", 0) == 0;
                                      }),
                       envStore.end());
    for (const auto &kv : setEnv) {
        envStore.erase(std::remove_if(envStore.begin(), envStore.end(),
                                      [&](const std::string &entry) {
                                          return entry.rfind(kv.first + "=", 0) == 0;
                                      }),
                       envStore.end());
        envStore.push_back(kv.first + "=" + kv.second);
    }
    std::vector<char *> envp;
    envp.reserve(envStore.size() + 1);
    for (const std::string &e : envStore)
        envp.push_back(const_cast<char *>(e.c_str()));
    envp.push_back(nullptr);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char *>(exe.c_str()));
    for (const std::string &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    const long pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        execve(exe.c_str(), argv.data(), envp.data());
        std::_Exit(127);
    }
    close(inPipe[0]);
    close(outPipe[1]);
    m_pid = pid;
    m_in = fdopen(inPipe[1], "w");
    m_outFd = outPipe[0];
    m_eof = false;
    m_reaped = false;
    return true;
}

void ChildProcess::writeLine(const std::string &line)
{
    if (!m_in)
        return;
    std::fputs((line + "\n").c_str(), m_in);
    std::fflush(m_in);
}

void ChildProcess::closeStdin()
{
    if (m_in) {
        std::fclose(m_in);
        m_in = nullptr;
    }
}

bool ChildProcess::readLine(std::string &line, int timeoutMs)
{
    const int64_t deadline = nowMs() + timeoutMs;
    for (;;) {
        if (drainPending(line))
            return true;
        if (m_eof)
            return false;
        pollfd p{m_outFd, POLLIN, 0};
        const int remain = static_cast<int>(std::max<int64_t>(0, deadline - nowMs()));
        const int r = poll(&p, 1, static_cast<int>(remain));
        if (r == 0)
            return false;   // 超时
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        char chunk[4096];
        const ssize_t got = ::read(m_outFd, chunk, sizeof chunk);
        if (got <= 0) {
            m_eof = true;
            continue;
        }
        m_buf.append(chunk, chunk + got);
    }
}

ChildProcess::Exit ChildProcess::waitForExit(int timeoutMs)
{
    Exit res;
    const int64_t deadline = nowMs() + timeoutMs;
    for (;;) {
        int status = 0;
        const pid_t r = waitpid(static_cast<pid_t>(m_pid), &status, WNOHANG);
        if (r == static_cast<pid_t>(m_pid)) {
            m_reaped = true;
            m_status = status;
            if (WIFSIGNALED(status)) {
                res.signaled = true;
                res.code = WTERMSIG(status);
            } else {
                res.code = WEXITSTATUS(status);
            }
            return res;
        }
        if (r < 0 && errno == ECHILD) {
            m_reaped = true;
            res.code = 0;
            return res;
        }
        if (nowMs() >= deadline) {
            res.timedOut = true;
            return res;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

bool ChildProcess::alive()
{
    if (m_reaped)
        return false;
    return ::kill(static_cast<pid_t>(m_pid), 0) == 0;
}

void ChildProcess::kill()
{
    if (m_pid > 0 && !m_reaped) {
        ::kill(static_cast<pid_t>(m_pid), SIGKILL);
        int status = 0;
        waitpid(static_cast<pid_t>(m_pid), &status, 0);
        m_reaped = true;
    }
    closeStdin();
    if (m_outFd >= 0) {
        ::close(m_outFd);
        m_outFd = -1;
    }
    m_pid = -1;
}

#endif
