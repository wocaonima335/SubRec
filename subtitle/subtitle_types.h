#pragma once
// M0 冻结：AI 字幕共享类型（design-live-v2.md §10.1；上版 design-verified.md §11.2）。
// 归属：M0。冻结后修改需 Codex 终端与 ZCode 终端双方同意，并同步修订设计文档 §10.1。
//
// - 纯 C++17，无 Qt、无 FFmpeg；主进程（MinGW）与 asr/ 子进程（MSVC）共用。
// - 时间轴：字幕毫秒 = tap 48 kHz 样本号 / 48 = spool 16 kHz 样本号 / 16（§8.1）。
//   带 16k 后缀的字段一律是 spool 的 16 kHz 样本号；区间一律半开 [begin, end)。
// - 文本一律 UTF-8。
// - 本文件只追加字段/枚举值，不改已有名字与语义；新增字段必须带默认值。
//   相对设计文档的唯一增量：Cue::provisional（§3.6.3 所述「临时 cue」标志），以及给
//   §10.1 中未写初值的成员补了零初值（不改变语义）。

#include "subtitle/subtitle_error.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace subtitle {

// ---- 采样率与时间换算（上版 §11.2）----
inline constexpr int kTapSampleRate = 48000;
inline constexpr int kAsrSampleRate = 16000;
inline constexpr int kAsrSamplesPerMs = 16;

constexpr int64_t tapToAsrSamples(int64_t s48) noexcept { return s48 / 3; }
constexpr int64_t asrSamplesToMs(int64_t s16) noexcept { return s16 / kAsrSamplesPerMs; }

// 识别模式：关 / 边录边识别 / 录完再识别（after 也是 L3 的补识别路径）。
enum class Mode { Off, Live, After };

// token / 词级时间戳；beginMs/endMs 为成片绝对毫秒。prob < 0 表示引擎未给置信度。
struct Word {
    int64_t beginMs = 0, endMs = 0;
    std::string text;
    float prob = -1.f;
};

// 上版的段结构；v2 中仍用于 sidecar JSON（SubtitleDocument::segmentsForSidecar）。
struct Segment {
    int64_t id = -1;
    int64_t begin16k = 0, end16k = 0;
    std::string text;
    std::vector<Word> words;
    float avgLogProb = NAN, noSpeechProb = NAN;
    std::string language;
};

// 一条字幕。segmentId 关联产生它的 VAD 段（SegId）；edited = 用户在编辑器改过（M3）。
struct Cue {
    int64_t beginMs = 0, endMs = 0;
    std::string text;
    int64_t segmentId = -1;
    bool edited = false;
    bool provisional = false;   // 临时 cue（§3.6.3，仅 LiveConfig::writeConfirmedToPart 时出现）；final 到达即被替换
};

struct CueConfig {
    int maxCharsPerLine = 18, maxLines = 1, minDurationMs = 800, maxDurationMs = 7000;
};

// VAD 参数（§3.5 实时值）。实时模式下 maxSegmentMs 以 LiveConfig::maxSegmentMs 为准。
struct VadConfig {
    float threshold = 0.5f;
    int minSpeechMs = 250, minSilenceMs = 500, speechPadMs = 150, maxSegmentMs = 15000;
};

enum class Backend { Auto, Cpu, Vulkan, Cuda };

struct EngineRequest {
    std::string engineId;        // "sherpa-onnx" | "whisper.cpp" | "fake"
    std::string modelId;
    std::filesystem::path modelDir;
    std::string language = "zh";
    int threads = 1;
    Backend backend = Backend::Auto;
    VadConfig vad;
    std::string initialPrompt;
};

// ---- v2 新增（§10.1）----

using JobId = uint64_t;
using SegId = int64_t;                      // 每个 Job 内单调递增；重启后从 resume 处继续编号

struct LiveConfig {                         // 实时参数（§3.5、§6.3）
    int partialIntervalMs = 1000;           // 0 = 关闭 partial
    int maxSegmentMs = 10000;
    int endpointSilenceMs = 500;
    int appendThrottleMs = 200;
    int partFlushIntervalMs = 10000;        // .srt.part 最长重写间隔
    int threadsWhileRecording = 1;
    // ---- 2026-11-21 新增（§3.6、§4.2、§4.3）----
    int agreementN = 2;                     // LocalAgreement-n，允许 2..3
    int confirmGuardMs = 300;               // 尾部时间护栏
    bool writeConfirmedToPart = false;      // 确认文本是否作为临时 cue 写入 .srt.part（D25，默认关）
    int provisionalMinSegmentMs = 3000;     // 开关打开时，句子至少持续这么久才写临时 cue
    int revisableCues = 2;                  // 录制中句末规则可改写的最近 cue 数
};

struct PartialSegment {                     // 只用于显示，不进 SRT（临时 cue 除外，见 D25）
    JobId job = 0;
    SegId id = -1;
    int64_t begin16k = 0, upto16k = 0;      // 已识别到的位置（半开）
    std::string text;                       // UTF-8
    std::vector<Word> tokens;               // 带 token 时间戳；旧宿主可能为空（兼容路径）
    uint32_t seq = 0;                       // 本句内第几次 partial，用于检测漏收
};

struct ConfirmedPrefix {                    // LocalAgreement 输出
    JobId job = 0;
    SegId id = -1;
    std::vector<Word> confirmed;            // 只增不减
    std::string confirmedText, tentativeText;   // 显示用：实色 / 浅色
    int64_t confirmedUpto16k = 0;           // 最后一个确认 token 的结束样本
    bool frozen = false;                    // L1 冻结
};

struct FinalSegment {                       // 幂等：(job, begin16k, end16k, pass) 唯一
    JobId job = 0;
    SegId id = -1;
    int64_t begin16k = 0, end16k = 0;
    std::string text, language, emotion, event;   // sherpa C API 字段
    std::vector<Word> words;                // token 时间戳，已换算为绝对 ms
    bool forcedCut = false;                 // 由 maxSegment / splice 强制封口
    float decodeMs = 0.f;                   // 用于 RTF 统计
    int pass = 1;                           // 1 = 实时 final，2 = 句级模型精修
    std::string modelId;                    // 产生该结果的模型
};

struct RefineRouting {                      // §4.3.3，D28
    bool skipMandarin = true;               // language=="zh" 且 ASCII 占比低于阈值时跳过
    double asciiRatioThreshold = 0.30;
    bool refineAll = false;                 // 用户选「全部句子都精修」
    bool glossaryMissTriggers = true;       // 热词表命中但 pass-1 未输出（M2）
};

enum class SentenceState { InProgress, PartlyConfirmed, Pass1Final, Fixed, Refining, Refined,
                           Dropped, Poisoned, UserEdited };   // §4.3.4

enum class DegradeLevel : int { L0Live = 0, L1FinalOnly = 1, L2CatchUp = 2, L3Deferred = 3, L4Failed = 4 };

struct RecorderHealth {                     // I9：录制侧只读快照（累计值，增量由使用方计算）
    uint64_t videoTickDrops = 0;            // slotCollisions + laggedTicks
    uint64_t videoFrames = 0;
    uint64_t wasapiDroppedSamples = 0;
};

struct LoadSample {                         // DegradeController 的输入（每 1 s 一份）
    int64_t nowMs = 0;
    double lagSec = 0;
    double rtfEwma = 0;
    double sysCpu = 0;                      // 系统总 CPU 百分比 0..100（与 §6.3 阈值、[ASR] level 日志同单位）
    double hostCpuCores = 0;                // 子进程 CPU，单位：核
    RecorderHealth health;
};

// 事件总线（Runner 线程内部，及跨线程信号的载荷）
enum class SubtitleEventKind { Partial, Final, Progress, LevelChanged, HostRestarting, HostRecovered,
                               PoisonSkipped, JobDone, JobFailed,
                               Confirmed, Fixed, Refined, RefineProgress, RefineInterrupted };

} // namespace subtitle
