#pragma once
// subrec-asr 内部引擎接口（design-verified.md §11.3 C 的 IAsrEngine / IVad，按 design-live-v2.md §10.1 的
// FinalSegment 字段扩展）。只在子进程内使用，不跨进程；跨进程契约只有 subtitle/host/asr_protocol.h。
//
// 接口归属：M0 给出初版；之后归工作包 C（ZCode 终端）独占，C 可自行调整本文件而不需要 Codex 同意，
// 前提是不改变 docs/subtitle/ipc-protocol.md 的外部行为。
//
// 线程：引擎与 VAD 只在解码线程使用（唯一使用 sherpa 识别器的线程，§9.3）。requestCancel 可从 IO 线程调用。

#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace asrhost {

enum class RecognizeKind { Partial, Final, Refine };

class IAsrEngine {
public:
    virtual ~IAsrEngine() = default;
    // 加载模型；backendUsed 返回实际后端（"cpu" / "vulkan"），GPU 失败自动回退时也在这里体现。
    virtual subtitle::Status load(const subtitle::EngineRequest &, std::string *backendUsed) = 0;
    // 识别 [io.begin16k, io.end16k) 的样本（pcm16k 长度 n = end16k − begin16k）。
    // 引擎填写 text / language / emotion / event / words（绝对毫秒）/ decodeMs / modelId；
    // 调用方负责 job / id / begin16k / end16k / forcedCut / pass。
    virtual subtitle::Status recognize(const float *pcm16k, size_t n, RecognizeKind kind,
                                       subtitle::FinalSegment &io) = 0;
    virtual void requestCancel() noexcept = 0;
    virtual std::string engineId() const = 0;
};

class IVad {
public:
    virtual ~IVad() = default;
    // 喂入从 first16k 开始的连续样本；新封口的段追加到 closed（半开区间，已含 speechPad 外扩）。
    virtual void feed(const float *pcm16k, size_t n, int64_t first16k,
                      std::vector<std::pair<int64_t, int64_t>> &closed) = 0;
    virtual void forceCut(int64_t at16k) = 0;       // splice / maxSegment：在 at16k 强制封口
    virtual void finish(std::vector<std::pair<int64_t, int64_t>> &closed) = 0;
    virtual void reset(int64_t from16k) = 0;        // 断点续识别：状态从静音初始化
    virtual bool inSpeech() const = 0;              // 当前是否在段内（partial 调度用）
    virtual int64_t openSegmentBegin16k() const = 0;   // 段内时的段起点；否则 −1
};

// 工厂：engineId = "sherpa-onnx" | "whisper.cpp" | "fake"。不认识 → nullptr + EngineUnknown。
std::unique_ptr<IAsrEngine> createEngine(const std::string &engineId, const std::string &fakeOptions,
                                         subtitle::Status *err);
// VAD 工厂：M1 = Silero v6（经 sherpa-onnx）；fake 引擎配套的能量阈值 VAD 用于测试。
std::unique_ptr<IVad> createVad(const subtitle::EngineRequest &, subtitle::Status *err);

} // namespace asrhost
