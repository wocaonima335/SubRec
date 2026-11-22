// 子进程封装：spawn（管道 stdin/stdout、stderr 继承）、带超时的按行读、带超时的等待退出、kill。
// POSIX 完整实现；Windows 路径（CreatePipe + CreateProcessW + PeekNamedPipe）保证可编译，
// 供 Windows CI 使用。崩溃检测：POSIX 用 WIFSIGNALED；Windows 用 NTSTATUS 区间的退出码近似。

#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <vector>

class ChildProcess {
public:
    struct Exit {
        int code = -1;
        bool signaled = false;    // POSIX：被信号杀死；Windows：NTSTATUS 崩溃码区间
        bool timedOut = false;
    };

    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    ChildProcess(ChildProcess &&other) { stealFrom(other); }
    ChildProcess &operator=(ChildProcess &&other)
    {
        if (this != &other) {
            kill();
            stealFrom(other);
        }
        return *this;
    }

    bool spawn(const std::string &exe, const std::vector<std::string> &args,
               const std::map<std::string, std::string> &setEnv = {},
               const std::vector<std::string> &unsetEnv = {});
    void writeLine(const std::string &line);   // 追加 '\n' 并 flush
    void closeStdin();
    // 带截止时间的按行读。超时 → false（未 EOF）；EOF → false 且 isEof() 置位（缓冲中剩余行先吐完）。
    bool readLine(std::string &line, int timeoutMs);
    bool isEof() const { return m_eof; }
    Exit waitForExit(int timeoutMs);
    bool alive();
    void kill();

private:
    bool drainPending(std::string &line);
    std::string m_buf;
    bool m_eof = false;
    void stealFrom(ChildProcess &other)
    {
#ifdef _WIN32
        m_process = other.m_process;
        m_inWrite = other.m_inWrite;
        m_outRead = other.m_outRead;
        m_buf = std::move(other.m_buf);
        m_final = other.m_final;
        m_reaped = other.m_reaped;
        other.m_process = other.m_inWrite = other.m_outRead = nullptr;
#else
        m_pid = other.m_pid;
        m_in = other.m_in;
        m_outFd = other.m_outFd;
        m_buf = std::move(other.m_buf);
        m_eof = other.m_eof;
        m_reaped = other.m_reaped;
        m_status = other.m_status;
        other.m_pid = -1;
        other.m_in = nullptr;
        other.m_outFd = -1;
#endif
        other.m_reaped = true;
    }

#ifdef _WIN32
    void *m_process = nullptr;   // HANDLE
    void *m_inWrite = nullptr;
    void *m_outRead = nullptr;
    Exit m_final;
    bool m_reaped = false;
#else
    long m_pid = -1;
    std::FILE *m_in = nullptr;      // 写子进程 stdin
    int m_outFd = -1;               // 读子进程 stdout
    bool m_reaped = false;
    int m_status = 0;
#endif
};

// POSIX 上 exit(code) 只保留低 8 位（703 → 191）；Windows 退出码是 32 位，原样。
inline int normalizedExitCode(int code)
{
#ifndef _WIN32
    return code & 0xFF;
#else
    return code;
#endif
}
