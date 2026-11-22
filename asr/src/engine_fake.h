#pragma once
// M0 冻结：进程内假 ASR 引擎（design-live-v2.md §11.1 第 1 层）。
// 接口归属：M0（参数名与语义是 Codex/ZCode 的交汇点：主进程测试按这些名字注入故障）；实现归工作包 C。
//
// 启用：subrec-asr --engine=fake [--fake=<k=v,...>]，或 load{engineId:"fake", fakeOptions:"<k=v,...>"}。
// 只在测试钩子打开时可选（沿用 capture/test_hook_gate.h 的 hooksEnabled() 门控语义；子进程侧用环境变量
// SUBREC_ASR_TEST_HOOKS=1 判定，主进程只在自身 hooksEnabled() 时设置该变量）。未开启时 engineId=fake → EngineUnknown。
//
// 输出：final 文本 = "第<id>段"（id 为段号）；按段长均匀生成 token 时间戳（每个码点一个 token）；
// partial 文本为 final 文本按已识别时长截取的前缀。language 由 lang 参数决定。
//
// 参数（逗号分隔 k=v；未知键 → InvalidArgument；同一键重复以后者为准）：
//   rtf=<x>             用 sleep 模拟解码耗时 = 段长 × x（默认 0）
//   crashAtSeg=<n>      识别第 n 段时 *(volatile int*)0 = 0
//   hangAtSeg=<n>       第 n 段解码线程无限 sleep，IO 线程继续 hb
//   hangAll=<n>         第 n 段起 hb 也停止
//   oomAtSeg=<n>        第 n 段以 kExitOutOfMemory(137) 退出
//   illegalInstr        启动即以 STATUS_ILLEGAL_INSTRUCTION 退出（非 Windows：以 kExitCpuUnsupported 退出）
//   garbageStdout       启动后向 fd 1 打印非 JSON（验证 stdout 重定向，协议通道不应受影响）
//   loadFail=<code>     load 返回 error{code}
//   partialNoise=<p>    每次 partial 以概率 p 改写尾部 1–3 个 token（模拟改口）
//   partialRegress=<k>  第 k 次 partial 改掉已出现的前缀（测「只增不减」）
//   finalDiffers=<p>    final 以概率 p 与最后一次 partial 不同
//   lang=<zh|en|mix>    final 的 language 与 ASCII 占比（mix = language "zh" 且 ASCII ≥ 30 %）
//   refineRtf=<x>       role=refine 时的模拟耗时
//   refineEmpty=<p>     role=refine 时以概率 p 返回空文本
//   seed=<u32>          随机数种子（默认 1），保证测试可复现
// 段号 n 从 0 开始计（与 SegId 一致）。

#include "asr_engine.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace asrhost {

struct FakeEngineOptions {
    double rtf = 0.0;
    int64_t crashAtSeg = -1;
    int64_t hangAtSeg = -1;
    int64_t hangAll = -1;
    int64_t oomAtSeg = -1;
    bool illegalInstr = false;
    bool garbageStdout = false;
    int loadFail = 0;                 // 0 = 不注入
    double partialNoise = 0.0;
    int64_t partialRegress = -1;
    double finalDiffers = 0.0;
    std::string lang = "zh";          // zh | en | mix
    double refineRtf = 0.0;
    double refineEmpty = 0.0;
    uint32_t seed = 1;
};

// 解析 "k=v,k=v"；空串 → 默认值。失败时 err 给出 InvalidArgument 与出错的键。
bool parseFakeOptions(std::string_view text, FakeEngineOptions &out, subtitle::Status *err);
std::string formatFakeOptions(const FakeEngineOptions &);   // 反向（测试与日志）

// 测试钩子是否开启（读 SUBREC_ASR_TEST_HOOKS）。
bool fakeEngineAllowed() noexcept;

class FakeAsrEngine final : public IAsrEngine {
public:
    explicit FakeAsrEngine(FakeEngineOptions options);
    subtitle::Status load(const subtitle::EngineRequest &, std::string *backendUsed) override;
    subtitle::Status recognize(const float *pcm16k, size_t n, RecognizeKind kind,
                               subtitle::FinalSegment &io) override;
    void requestCancel() noexcept override;
    std::string engineId() const override { return "fake"; }
    const FakeEngineOptions &options() const noexcept { return m_options; }

private:
    FakeEngineOptions m_options;
    uint32_t m_partialCount = 0;
};

} // namespace asrhost
