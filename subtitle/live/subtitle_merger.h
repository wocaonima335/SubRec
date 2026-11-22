#pragma once
// M0 冻结：SubtitleDocument 与 SubtitleMerger（design-live-v2.md §10.3 H、§3.6.3–3.6.5、§4.2、§4.3.3、§7.4）。
// 接口归属：M0；实现归工作包 H。CMake 目标 subtitle_live（纯 C++17，可注入 IClock）。
//
// SubtitleDocument：单个 Job 的权威 cue 列表与句子状态。
// - acceptFinal 幂等：(job, begin16k, end16k, pass) 唯一；end16k <= resumePoint16k() 的重复 final 返回 false；
//   接受时删除同 id 的临时 cue（provisional）。pass-1 final 使句子进入 Pass1Final（经 SentenceFixer 后 Fixed）。
// - 硬边界（splice）以 16k 样本号记录，CueBuilder 用时换算为毫秒。
// - markPoison：插入空洞标记（不进 SRT，只进 sidecar 与日志，§7.4），句子状态 Poisoned。
// - revisableTail(n)：返回最后 n 条非临时 cue 的指针（录完时 n 取 cues().size() 即全篇）；指针在下一次修改 Document 前有效。
// - acceptRefined：逐句替换（D30）；只对 Pass1Final / Fixed 状态的句子生效，UserEdited 等返回 false。
// - revision() 每次变更 +1，用于节流写盘。
//
// SubtitleMerger：Runner 线程内把 partial/final/refined/降级事件汇入 Document 并负责写盘。
// - onPartial：喂给 LocalAgreement，更新 current()（L2+ 时忽略）；LiveConfig::writeConfirmedToPart 时按 §3.6.3 写临时 cue。
// - onFinal：LocalAgreement::recordFinal → reset；SentenceFixer::apply(recording=true) → buildCues(硬边界) → acceptFinal。
// - onRefined：SentenceFixer::apply(recording=false) → acceptRefined。
// - onLevelChanged：L1 → LocalAgreement::freeze()，tentative 清空；L2+ → current() 返回空；L1→L0 从下一新段恢复。
// - flushPart：整文件重写 name.srt.part（writeFileAtomic：.tmp + 改名，§3.6.4）。
// - finalize：resolveOverlaps → 原子写 name.srt（初稿定稿）。
// - finalizeRefined：userEditedFile=false 时把初稿另存 name.live.srt 再原子替换；true 时不覆盖，只写 name.srt.refine.part。
// - resumeFromPart：解析 .srt.part 填充空 Document（JobResumeFromPart，§4.2 第 4 条）。
// 线程：两者都只在 SubtitleJobRunner 线程使用；非线程安全。

#include "subtitle/core/subtitle_writers.h"
#include "subtitle/subtitle_clock.h"
#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace subtitle {

class LocalAgreement;
class SentenceFixer;

class SubtitleDocument {                            // 单 Job 的权威 cue 列表
public:
    SubtitleDocument();
    bool acceptFinal(const FinalSegment &);         // 幂等；end16k <= resume 的重复返回 false；删除同 id 临时 cue
    void addHardBoundary(int64_t at16k);
    void markPoison(int64_t b16k, int64_t e16k);
    int64_t resumePoint16k() const;                 // 最后一个 final 的 end16k
    const std::vector<Cue> &cues() const;
    std::vector<Segment> segmentsForSidecar() const;
    uint64_t revision() const;                      // 每次变更 +1，用于节流写盘
    // ---- 2026-11-21 新增 ----
    void setProvisional(SegId, const ConfirmedPrefix &);    // 仅 writeConfirmedToPart 时调用
    void dropProvisional(SegId);
    std::vector<Cue *> revisableTail(int n);        // 可修订窗口（录制中 n = revisableCues，录完后全篇）
    bool acceptRefined(const FinalSegment &pass2);  // 逐句替换；UserEdited / 非 Pass1Final|Fixed 状态返回 false
    SentenceState state(SegId) const;
    void markUserEdited(SegId);                     // M3 编辑器调用
    double refineProgress() const;                  // 已处理 / 路由命中句数

    // ---- M0 补齐（实现 §10.3 上述语义所必需）----
    const std::vector<int64_t> &hardBoundaries16k() const;   // 升序
    void setSentenceState(SegId, SentenceState);    // Merger / RefineScheduler 推进状态机（§4.3.4）
    void setRefineTotal(int routedCount);           // refineProgress 的分母

private:                                            // 私有成员归实现方 H，可自由调整
    std::vector<Cue> m_cues;
    std::vector<int64_t> m_hardBoundaries16k;
    std::map<SegId, SentenceState> m_states;
    int64_t m_resume16k = 0;
    uint64_t m_revision = 0;
    int m_refineTotal = 0;
    int m_refineDone = 0;
};

class SubtitleMerger {
public:
    SubtitleMerger(SubtitleDocument &, CueConfig, SentenceFixer &, LocalAgreement &, IClock &);
    void onPartial(const PartialSegment &);         // 喂给 LocalAgreement，更新 currentConfirmed/currentTentative
    void onFinal(const FinalSegment &);             // pass-1：SentenceFixer → CueBuilder → Document
    void onRefined(const FinalSegment &);           // pass-2：SentenceFixer（录完模式）→ acceptRefined
    void onLevelChanged(DegradeLevel);              // L1 冻结确认、L2+ 清除显示
    std::optional<ConfirmedPrefix> current() const; // 显示用，替代 v1 的 currentLine()
    bool shouldFlushPart(int64_t nowMs) const;      // 新 final 或距上次 ≥ partFlushIntervalMs
    Status flushPart(const std::filesystem::path &srtPart, const SrtOptions &);   // tmp + 改名
    Status finalize(const std::filesystem::path &srt, const SrtOptions &);   // 初稿定稿：resolveOverlaps + 原子改名
    Status finalizeRefined(const std::filesystem::path &srt, const SrtOptions &, bool userEditedFile);  // 精修稿
    static Status resumeFromPart(const std::filesystem::path &srtPart, SubtitleDocument &);

    // ---- M0 补齐：构造函数没有 LiveConfig 参数，partFlushIntervalMs / writeConfirmedToPart 等由此注入 ----
    void setLiveConfig(const LiveConfig &);
    void onSplice(int64_t at16k);                   // = doc.addHardBoundary + LocalAgreement::reset + 清除当前句显示
    void onHostRestarted();                         // LocalAgreement::reset + 清除当前句显示

private:                                            // 私有成员归实现方 H，可自由调整
    SubtitleDocument &m_doc;
    CueConfig m_cueConfig;
    SentenceFixer &m_fixer;
    LocalAgreement &m_agreement;
    IClock &m_clock;
    LiveConfig m_live;
    DegradeLevel m_level = DegradeLevel::L0Live;
    uint64_t m_flushedRevision = 0;
    int64_t m_lastFlushMs = 0;
};

} // namespace subtitle
