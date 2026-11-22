#pragma once
// subrec-asr 运行时骨架：协议 IO、spool 尾随读取、段状态机、父进程看门（design-live-v2.md §3.3、§3.4、§7.2、§7.3、§8.2、§9.1）。
// 归属：M0 给出初版，之后归工作包 C（ZCode 终端）独占，可自由调整（不影响跨进程协议）。
//
// 线程（§9.3）
// - IO 线程：ProtocolIo 读 stdin 命令、每 1 s 发 hb、串行化全部输出；stdin EOF → 退出。
// - 解码线程：SpoolTailReader → IVad → SegmentStateMachine → IAsrEngine；发 busy / partial / final / progress。
// - 父进程看门线程：ParentWatch 等父进程句柄，返回即 TerminateProcess(GetCurrentProcess())。

#include "subtitle/host/asr_protocol.h"
#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace asrhost {

// ---- 协议 IO（§7.2）----
// install()：保存协议句柄（_dup(1)），再把 fd 1 指向 stderr，第三方库的 printf 都进 stderr。
// write() 线程安全（内部互斥），每条消息 encode + '\n' 后立即 flush。
class ProtocolIo {
public:
    subtitle::Status install();
    void write(const subtitle::proto::Message &);
    // 阻塞读一行；EOF 返回 nullopt；超长（> kMaxLineBytes）或非 JSON → 发 error{HostProtocolError} 后返回 nullopt。
    std::optional<subtitle::proto::Message> readCommand();

private:
    std::mutex m_writeMutex;
    std::FILE *m_out = nullptr;
};

// ---- spool 尾随读取（只读共享打开；以主进程 append 告知的 committed16k 为准，不信任 RIFF 长度）----
class SpoolTailReader {
public:
    subtitle::Status open(const std::string &spoolPathUtf8);
    void setCommitted(int64_t committed16k);         // IO 线程写，解码线程读（原子）
    int64_t committed() const;
    // 读 [from16k, to16k) 的 float 样本；to16k 不得超过 committed()，否则 SpoolRangeNotCommitted。
    subtitle::Status read(int64_t from16k, int64_t to16k, std::vector<float> &out);
    void close();
};

// ---- 段状态机（§3.4、§3.5、§8.2）----
// 输入：VAD 封口事件、splice、maxSegmentMs 强切、finish；输出：何时出 partial（每 partialIntervalMs、
// 上一次未完成则跳过）、何时出 final；splice 时强制封口并丢弃该段未发出的 partial。
class SegmentStateMachine {
public:
    struct Action {
        enum Kind { None, Partial, Final } kind = None;
        subtitle::SegId id = -1;
        int64_t b16k = 0, e16k = 0;
        bool forcedCut = false;
    };
    void configure(const subtitle::LiveConfig &, int64_t resumeFrom16k, subtitle::SegId firstId);
    void onSpeechStart(int64_t at16k);
    void onSpeechEnd(int64_t at16k);                 // VAD 端点
    void onSplice(int64_t at16k);
    void onFinish(int64_t total16k);
    void setPartialIntervalMs(int ms);               // setLevel：0 = 关 partial
    Action next(int64_t committed16k, int64_t nowMs, bool partialInFlight);
};

// ---- 父进程看门（§7.3）----
class ParentWatch {
public:
    // parentPid = 0 时不启动。onParentGone 在看门线程上调用，默认实现直接终止本进程。
    subtitle::Status start(int64_t parentPid, std::function<void()> onParentGone = {});
};

// 主循环：hello 握手 → 命令分发 → 解码线程管理。返回进程退出码（kExitOk 或 ErrorCode 数值）。
struct HostOptions;
int runHost(const HostOptions &);

} // namespace asrhost
