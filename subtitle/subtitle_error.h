#pragma once
// M0 冻结：错误码与 Status（design-live-v2.md §10.2；上版 design-verified.md §11.2）。
// 归属：M0。冻结后修改需 Codex 终端与 ZCode 终端双方同意，并同步修订设计文档 §10.2。
//
// 约定
// - 所有可能失败的跨模块接口都返回 Status（或带 Status 的结果结构），异常不得跨模块边界。
//   模块内部若使用会抛异常的库（std::filesystem 等），必须在模块出口处捕获并转换为 Status。
//   例外：非 noexcept 的构造函数、返回 std::vector/std::string 的函数可能抛 std::bad_alloc，
//   由调用方在 Job 创建、线程入口处统一捕获（按 OutOfMemory 处理）。
// - ErrorCode 按百位分段：1xx 通用、2xx 音频链、3xx 引擎/VAD/GPU、4xx 模型管理、5xx 输出、
//   6xx 任务/服务、7xx 识别宿主子进程。数值只追加、不改、不复用；删除的码保留注释占位。
// - 1xx–6xx 的数值沿用旧 m0_headers（2026-10-30 定稿草案）；7xx、320–322、610–615 为实时方案追加。
// - 同一数值同时用于 IPC 协议的 error{code} 字段与子进程退出码（见 docs/subtitle/ipc-protocol.md）。
// - Status::message 是给日志用的诊断细节（UTF-8）。UI 中文文案由 SubtitleService 按 code 映射，
//   不直接显示 message。
// - 本头文件纯 C++17，无 Qt、无 FFmpeg；主进程与 asr/ 子进程都 include。

#include <cstdint>
#include <string>
#include <utility>

namespace subtitle {

enum class ErrorCode : int32_t {
    Ok = 0,

    // ---- 1xx 通用 ----
    Unknown = 100,
    InvalidArgument = 101,
    Cancelled = 102,        // 被调用方主动取消
    Timeout = 103,
    NotSupported = 104,
    OutOfMemory = 105,
    InternalError = 106,    // 不变量被破坏（实现 bug），记日志后按失败处理
    NotReady = 107,         // 前置条件未满足（未 load、未 open 等）

    // ---- 2xx 音频链（tap / resampler / spool / ingest）----
    TapFormatRejected = 200,      // 非 48 kHz、非 float、声道数或块长不受支持
    TapOverflow = 201,            // 环满丢块（只计数，不作为调用返回值）
    ResamplerInitFailed = 210,
    ResamplerProcessFailed = 211,
    SpoolOpenFailed = 220,
    SpoolWriteFailed = 221,
    SpoolDiskFull = 222,
    SpoolRangeNotCommitted = 223, // 读取了尚未提交的区间
    SpoolReadFailed = 224,
    SpoolFinalizeFailed = 225,
    SpoolCorrupt = 226,           // 打开已有 spool 时 WAV 头不合法

    // ---- 3xx 识别引擎 / VAD / GPU ----
    EngineUnknown = 300,          // engineId 不认识
    EngineRuntimeMissing = 301,   // 运行时 DLL 缺失或无法加载
    EngineSymbolMissing = 302,    // DLL 已加载但缺少所需 C API 符号
    EngineLoadFailed = 303,       // 模型加载失败（文件损坏、格式不符等）
    EngineNotLoaded = 304,
    EngineCpuUnsupported = 305,   // CPU 指令集不满足
    EngineRecognizeFailed = 306,
    EngineInputTooLong = 307,
    VadLoadFailed = 310,
    VadUnavailable = 311,
    GpuBackendUnavailable = 320,  // 可自动降级到 CPU，只记日志
    GpuOutOfMemory = 321,         // 可自动降级到 CPU，只记日志
    GpuDeviceLost = 322,          // Vulkan 设备丢失（M2 精修）→ 回退 CPU

    // ---- 4xx 模型管理 ----
    ModelUnknown = 400,
    ModelNotInstalled = 401,
    ModelManifestInvalid = 402,
    ModelDownloadFailed = 403,    // 所有镜像均失败
    ModelHashMismatch = 404,
    ModelSizeMismatch = 405,
    ModelDiskFull = 406,
    ModelPermissionDenied = 407,
    ModelInstalledStale = 408,
    ModelLicenseNotAccepted = 409,
    ModelInsecureUrl = 410,
    ModelBusy = 411,

    // ---- 5xx 输出 ----
    SrtWriteFailed = 500,
    SrtRenameFailed = 501,
    SrtTargetNotWritable = 502,

    // ---- 6xx 任务 / 服务 ----
    JobCancelled = 600,           // endRecording(false)：不写 .srt
    JobTooManyFailures = 601,
    ShutdownIncomplete = 602,     // 退出时超时，只写出了已完成部分
    JobAborted = 603,
    ServiceShutDown = 604,
    SubtitleOff = 605,
    JobDeferred = 610,            // 进入 L3（不是错误，用于日志与状态）
    JobPoisonSegment = 611,       // 跳过毒段，定稿后提示
    JobResumeFromPart = 612,      // 从 .srt.part 恢复
    RefineSkippedEdited = 613,    // 句子已被用户编辑，精修跳过（仅日志）
    RefineInterrupted = 614,      // 精修被新录制/退出中断，记断点稍后续跑
    RefineKeptPass1 = 615,        // Whisper 结果为空或判为幻觉，保留初稿（仅日志）

    // ---- 7xx 识别宿主子进程（subrec-asr）----
    HostMissing = 700,            // asr/subrec-asr.exe 不存在
    HostStartFailed = 701,
    HostCrashed = 702,
    HostProtocolError = 703,      // JSON 解析失败、行超长（> kMaxLineBytes）、未知必需字段
    HostVersionMismatch = 704,    // hello 协议版本不一致
    HostTimeout = 705,
    HostHeartbeatLost = 706,      // 5 s 无 hb → 重启
    HostDecodeTimeout = 707,      // 段级超时 → 重启；同区间第 2 次 → 毒段
    HostRestartExhausted = 708,   // 10 min 内重启 5 次 → L3
    HostOutOfMemory = 709,        // 子进程以 OOM 退出码退出 → L1 → L3
    HostCpuUnsupported = 710,     // STATUS_ILLEGAL_INSTRUCTION → 禁用本机字幕
    HostBlocked = 711,            // 启动失败（被安全软件拦截等）→ unavailable
    RefineHostFailed = 712,       // 精修宿主启动/加载失败或重启耗尽 → 精修放弃，初稿可用
};

// 错误码所在的百位分段，例如 SpoolDiskFull → 2。
constexpr int errorCategory(ErrorCode code) noexcept
{
    return static_cast<int>(code) / 100;
}

// 稳定的英文标识，用于日志（如 "[Subtitle] failed code=SpoolDiskFull"）、IPC 调试与测试断言。
// 未列出的值返回 "Unrecognized"。
inline const char *errorCodeName(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::Cancelled: return "Cancelled";
    case ErrorCode::Timeout: return "Timeout";
    case ErrorCode::NotSupported: return "NotSupported";
    case ErrorCode::OutOfMemory: return "OutOfMemory";
    case ErrorCode::InternalError: return "InternalError";
    case ErrorCode::NotReady: return "NotReady";
    case ErrorCode::TapFormatRejected: return "TapFormatRejected";
    case ErrorCode::TapOverflow: return "TapOverflow";
    case ErrorCode::ResamplerInitFailed: return "ResamplerInitFailed";
    case ErrorCode::ResamplerProcessFailed: return "ResamplerProcessFailed";
    case ErrorCode::SpoolOpenFailed: return "SpoolOpenFailed";
    case ErrorCode::SpoolWriteFailed: return "SpoolWriteFailed";
    case ErrorCode::SpoolDiskFull: return "SpoolDiskFull";
    case ErrorCode::SpoolRangeNotCommitted: return "SpoolRangeNotCommitted";
    case ErrorCode::SpoolReadFailed: return "SpoolReadFailed";
    case ErrorCode::SpoolFinalizeFailed: return "SpoolFinalizeFailed";
    case ErrorCode::SpoolCorrupt: return "SpoolCorrupt";
    case ErrorCode::EngineUnknown: return "EngineUnknown";
    case ErrorCode::EngineRuntimeMissing: return "EngineRuntimeMissing";
    case ErrorCode::EngineSymbolMissing: return "EngineSymbolMissing";
    case ErrorCode::EngineLoadFailed: return "EngineLoadFailed";
    case ErrorCode::EngineNotLoaded: return "EngineNotLoaded";
    case ErrorCode::EngineCpuUnsupported: return "EngineCpuUnsupported";
    case ErrorCode::EngineRecognizeFailed: return "EngineRecognizeFailed";
    case ErrorCode::EngineInputTooLong: return "EngineInputTooLong";
    case ErrorCode::VadLoadFailed: return "VadLoadFailed";
    case ErrorCode::VadUnavailable: return "VadUnavailable";
    case ErrorCode::GpuBackendUnavailable: return "GpuBackendUnavailable";
    case ErrorCode::GpuOutOfMemory: return "GpuOutOfMemory";
    case ErrorCode::GpuDeviceLost: return "GpuDeviceLost";
    case ErrorCode::ModelUnknown: return "ModelUnknown";
    case ErrorCode::ModelNotInstalled: return "ModelNotInstalled";
    case ErrorCode::ModelManifestInvalid: return "ModelManifestInvalid";
    case ErrorCode::ModelDownloadFailed: return "ModelDownloadFailed";
    case ErrorCode::ModelHashMismatch: return "ModelHashMismatch";
    case ErrorCode::ModelSizeMismatch: return "ModelSizeMismatch";
    case ErrorCode::ModelDiskFull: return "ModelDiskFull";
    case ErrorCode::ModelPermissionDenied: return "ModelPermissionDenied";
    case ErrorCode::ModelInstalledStale: return "ModelInstalledStale";
    case ErrorCode::ModelLicenseNotAccepted: return "ModelLicenseNotAccepted";
    case ErrorCode::ModelInsecureUrl: return "ModelInsecureUrl";
    case ErrorCode::ModelBusy: return "ModelBusy";
    case ErrorCode::SrtWriteFailed: return "SrtWriteFailed";
    case ErrorCode::SrtRenameFailed: return "SrtRenameFailed";
    case ErrorCode::SrtTargetNotWritable: return "SrtTargetNotWritable";
    case ErrorCode::JobCancelled: return "JobCancelled";
    case ErrorCode::JobTooManyFailures: return "JobTooManyFailures";
    case ErrorCode::ShutdownIncomplete: return "ShutdownIncomplete";
    case ErrorCode::JobAborted: return "JobAborted";
    case ErrorCode::ServiceShutDown: return "ServiceShutDown";
    case ErrorCode::SubtitleOff: return "SubtitleOff";
    case ErrorCode::JobDeferred: return "JobDeferred";
    case ErrorCode::JobPoisonSegment: return "JobPoisonSegment";
    case ErrorCode::JobResumeFromPart: return "JobResumeFromPart";
    case ErrorCode::RefineSkippedEdited: return "RefineSkippedEdited";
    case ErrorCode::RefineInterrupted: return "RefineInterrupted";
    case ErrorCode::RefineKeptPass1: return "RefineKeptPass1";
    case ErrorCode::HostMissing: return "HostMissing";
    case ErrorCode::HostStartFailed: return "HostStartFailed";
    case ErrorCode::HostCrashed: return "HostCrashed";
    case ErrorCode::HostProtocolError: return "HostProtocolError";
    case ErrorCode::HostVersionMismatch: return "HostVersionMismatch";
    case ErrorCode::HostTimeout: return "HostTimeout";
    case ErrorCode::HostHeartbeatLost: return "HostHeartbeatLost";
    case ErrorCode::HostDecodeTimeout: return "HostDecodeTimeout";
    case ErrorCode::HostRestartExhausted: return "HostRestartExhausted";
    case ErrorCode::HostOutOfMemory: return "HostOutOfMemory";
    case ErrorCode::HostCpuUnsupported: return "HostCpuUnsupported";
    case ErrorCode::HostBlocked: return "HostBlocked";
    case ErrorCode::RefineHostFailed: return "RefineHostFailed";
    }
    return "Unrecognized";
}

// 值类型；可自由拷贝、跨线程传递（不含共享状态）。
struct Status {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    bool ok() const noexcept { return code == ErrorCode::Ok; }

    static Status success() { return {}; }
    static Status error(ErrorCode c, std::string msg = {})
    {
        Status s;
        s.code = c;
        s.message = std::move(msg);
        return s;
    }
};

} // namespace subtitle
