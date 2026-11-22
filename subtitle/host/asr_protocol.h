#pragma once
// M0 冻结：主进程 ⇄ subrec-asr 子进程的 JSON-lines 协议（design-live-v2.md §10.3 C、§7.2–§7.5；
// 上版 design-verified.md §11.3 C）。完整字段表、时序与示例见 docs/subtitle/ipc-protocol.md，两者必须一致。
//
// 接口归属：M0。编解码实现 asr_protocol.cpp 归工作包 C（ZCode 终端）；主进程侧使用方是工作包 D。
// 纯 C++17，无 Qt、无 FFmpeg：主进程（MinGW，经 CMake 目标 subtitle_protocol）与 asr/ 子进程（MSVC，
// 直接编译 asr_protocol.cpp）共用同一份源码。只 include subtitle_types.h / subtitle_error.h。
//
// 通则
// - UTF-8 JSON，一行一个对象，以 '\n' 结尾；encode 的结果不含换行，由写方追加。
// - 单行上限 kMaxLineBytes（1 MiB，不含 '\n'），超限 = HostProtocolError → 主进程重启子进程。
// - 每个对象都有字符串字段 "type"。新增字段一律可选：接收方忽略不认识的字段；
//   缺少可选字段时取本文件结构体的默认值。
// - 不认识的 type：decode 返回 nullopt 且 err->code = NotSupported；接收方记日志后忽略，不视为协议错误。
//   必需字段缺失或类型错误：nullopt 且 err->code = HostProtocolError。
// - 整数用 JSON number；jobId 必须 < 2^53（QJsonDocument 以 double 存数）。16k 样本号、毫秒同理。
// - 时间：带 16k 后缀的字段是 spool 16 kHz 样本号；tokens/words 中的 b/e 是成片绝对毫秒。
// - 协议版本 kProtocolVersion = 2；hello 版本不一致 → HostVersionMismatch，不重试。

#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace subtitle::proto {

inline constexpr int kProtocolVersion = 2;
inline constexpr std::size_t kMaxLineBytes = 1024 * 1024;
inline constexpr int kHeartbeatIntervalMs = 1000;

// ---- 子进程退出码（§7.5）。除下列特例外，启动期致命错误以对应 ErrorCode 数值退出 ----
inline constexpr int kExitOk = 0;
inline constexpr int kExitOutOfMemory = 137;                     // 捕获 bad_alloc 后 exit(137) → HostOutOfMemory
inline constexpr uint32_t kStatusIllegalInstruction = 0xC000001Du; // Windows 崩溃码 → HostCpuUnsupported
inline constexpr int kExitRuntimeMissing = static_cast<int>(ErrorCode::EngineRuntimeMissing);   // 301
inline constexpr int kExitEngineLoadFailed = static_cast<int>(ErrorCode::EngineLoadFailed);     // 303
inline constexpr int kExitCpuUnsupported = static_cast<int>(ErrorCode::EngineCpuUnsupported);   // 305
inline constexpr int kExitVersionMismatch = static_cast<int>(ErrorCode::HostVersionMismatch);   // 704

// ---- 命令行参数（主进程启动子进程时传入；fake 宿主同样遵守）----
inline constexpr const char *kArgRole = "--role";           // --role=live | --role=refine（默认 live）
inline constexpr const char *kArgParentPid = "--parent-pid"; // 父进程看门（§7.3）
inline constexpr const char *kArgEngine = "--engine";       // --engine=sherpa-onnx | whisper.cpp | fake（可被 load 覆盖）
inline constexpr const char *kArgFake = "--fake";           // --fake=rtf=0.1,crashAtSeg=3,...（仅测试钩子开启时生效）
inline constexpr const char *kArgLogLevel = "--log-level";  // error | warn | info | debug

inline constexpr const char *kRoleLive = "live";
inline constexpr const char *kRoleRefine = "refine";

// ================= 主进程 → 子进程 =================

struct Hello {                     // 双向同名：主→子带 protocol/role；子→主回 protocol/version/backends/gpus
    int protocol = kProtocolVersion;
    std::string role = kRoleLive;  // 主→子
    std::string version;           // 子→主：subrec-asr 版本
    std::vector<std::string> backends;   // 子→主：如 {"cpu"}、{"cpu","vulkan"}
    struct Gpu { std::string name; int vramMB = 0; std::string api; };
    std::vector<Gpu> gpus;         // 子→主（M2）
};

struct Load {
    EngineRequest request;
    LiveConfig live;               // v2 新增
    std::string fakeOptions;       // 仅 engineId == "fake" 且测试钩子开启时生效，格式同 --fake
};

struct Open {
    JobId jobId = 0;
    std::string spoolPath;         // UTF-8
    int64_t resumeFrom16k = 0;     // 断点续识别（§7.4）
    int64_t committed16k = 0;
    bool live = true;              // false = after / L3 补识别
};

struct Append { JobId jobId = 0; int64_t committed16k = 0; };          // 节流 200 ms，后发覆盖先发
struct Splice { JobId jobId = 0; int64_t at16k = 0; };                 // 暂停开始/恢复都发
struct SetLevel {                                                       // 降级指令（§6.2）
    JobId jobId = 0;
    int level = 0;                 // DegradeLevel 0..4；L3 由主进程改发 pause
    int threads = 1;
    int partialIntervalMs = 1000;  // 0 = 关 partial（L1 起）
};
struct Pause { JobId jobId = 0; };                                      // L3 暂停解码
struct Resume { JobId jobId = 0; };                                     // 录完恢复
struct Finish { JobId jobId = 0; int64_t total16k = 0; };               // 录制结束，刷完回 done
struct Cancel { JobId jobId = 0; };                                     // 回 cancelled
struct Quit {};

struct RefineSegment { SegId id = -1; int64_t b16k = 0, e16k = 0; };
struct Refine {                    // M2，仅 role=refine 接受
    JobId jobId = 0;
    std::string spoolPath;
    std::vector<RefineSegment> segments;
    std::string prompt;            // Whisper initial_prompt
};

// ================= 子进程 → 主进程 =================

struct Loaded { std::string backend; int64_t loadMs = 0; std::string fallbackReason; };
struct Heartbeat { int64_t seq = 0; std::string state; int rssMB = 0; };   // state: idle|loading|decoding|paused
struct Busy { JobId jobId = 0; std::string kind; int64_t b16k = 0, e16k = 0; };  // kind: partial|final|refine
struct PartialMsg { PartialSegment seg; };                                // 字段见 ipc-protocol.md
struct FinalMsg { FinalSegment seg; };                                    // pass=1 实时 / pass=2 精修
struct Progress { JobId jobId = 0; int64_t done16k = 0; double decodeMsTotal = 0; };
struct Done { JobId jobId = 0; };
struct Cancelled { JobId jobId = 0; };
struct Error { JobId jobId = 0; int code = 0; std::string message; };     // jobId = 0：宿主级错误
struct Log { std::string level; std::string text; };

using Message = std::variant<Hello, Load, Open, Append, Splice, SetLevel, Pause, Resume, Finish, Cancel, Quit,
                             Refine, Loaded, Heartbeat, Busy, PartialMsg, FinalMsg, Progress, Done, Cancelled,
                             Error, Log>;

// JSON 中 "type" 字段的取值（与 Message 的备选类型一一对应）。
const char *typeName(const Message &) noexcept;

std::string encode(const Message &);                 // 一行 JSON，无换行
std::optional<Message> decode(std::string_view line, Status *err);

// role=refine 的实例只接受 hello / load / refine / cancel / quit（§10.3 C 表）。
bool acceptedByRole(const Message &, std::string_view role) noexcept;

const char *backendName(Backend) noexcept;           // "auto" | "cpu" | "vulkan" | "cuda"
std::optional<Backend> parseBackend(std::string_view) noexcept;

} // namespace subtitle::proto
