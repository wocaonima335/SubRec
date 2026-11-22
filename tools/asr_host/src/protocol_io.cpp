// 协议 IO（工作包 C，design-live-v2.md §7.2）。子进程启动第一件事 install()：_dup(1) 保存协议句柄，
// 再把 fd 1 指向 stderr —— 第三方库 / 假引擎 garbageStdout 注入的任何 printf 都进 stderr，
// 协议通道（dup 出的原 fd）不受污染。写方串行化（互斥），每条消息 encode + '\n' 后立即 flush。

#include "host_runtime.h"

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_error.h"

#include <cstdio>
#include <mutex>
#include <optional>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace asrhost {

subtitle::Status ProtocolIo::install()
{
    if (m_out)
        return {};
    std::fflush(stdout);
#if defined(_WIN32)
    const int protoFd = _dup(1);
    if (protoFd < 0)
        return subtitle::Status::error(subtitle::ErrorCode::HostStartFailed, "dup(1) failed");
    if (_dup2(_fileno(stderr), 1) != 0) {
        _close(protoFd);
        return subtitle::Status::error(subtitle::ErrorCode::HostStartFailed, "dup2(stderr,1) failed");
    }
    m_out = _fdopen(protoFd, "w");
#else
    const int protoFd = dup(1);
    if (protoFd < 0)
        return subtitle::Status::error(subtitle::ErrorCode::HostStartFailed, "dup(1) failed");
    if (dup2(fileno(stderr), 1) < 0) {   // dup2 成功返回 1，失败返回 -1
        close(protoFd);
        return subtitle::Status::error(subtitle::ErrorCode::HostStartFailed, "dup2(stderr,1) failed");
    }
    m_out = fdopen(protoFd, "w");
#endif
    if (!m_out)
        return subtitle::Status::error(subtitle::ErrorCode::HostStartFailed, "fdopen(proto) failed");
    return {};
}

void ProtocolIo::write(const subtitle::proto::Message &m)
{
    const std::string line = subtitle::proto::encode(m);
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_out)
        return;
    std::fwrite(line.data(), 1, line.size(), m_out);
    std::fputc('\n', m_out);
    std::fflush(m_out);
}

std::optional<subtitle::proto::Message> ProtocolIo::readCommand()
{
    m_protocolError = false;
    for (;;) {
        std::string line;
        bool eof = false;
        bool overlong = false;
        char buf[4096];
        for (;;) {
            if (!std::fgets(buf, sizeof buf, stdin)) {
                eof = true;
                break;
            }
            line += buf;
            if (line.size() > subtitle::proto::kMaxLineBytes) {
                overlong = true;
                break;
            }
            if (!line.empty() && line.back() == '\n')
                break;
        }
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty()) {
            if (eof)
                return std::nullopt;
            continue;
        }
        if (overlong || line.size() > subtitle::proto::kMaxLineBytes) {
            write(subtitle::proto::Error{0, static_cast<int>(subtitle::ErrorCode::HostProtocolError),
                                         "stdin line exceeds kMaxLineBytes"});
            m_protocolError = true;
            return std::nullopt;
        }
        subtitle::Status st;
        auto msg = subtitle::proto::decode(line, &st);
        if (msg)
            return msg;
        if (st.code == subtitle::ErrorCode::NotSupported) {
            // 未知 type：记日志后忽略（向前兼容，§1 通则）。
            std::fprintf(stderr, "subrec-asr: ignore unknown message (%s)\n", st.message.c_str());
            if (eof)
                return std::nullopt;
            continue;
        }
        write(subtitle::proto::Error{0, static_cast<int>(st.code), st.message});
        m_protocolError = true;
        return std::nullopt;
    }
}

} // namespace asrhost
