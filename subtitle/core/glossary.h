#pragma once
// M0 冻结：热词词表类型（design-live-v2.md §10.3 FixerRules、§4.3.3、D32）。M2 功能。
// 接口归属：M0（只冻结 GlossaryEntry 结构与函数签名）；实现归工作包 K。CMake 目标 subtitle_core。
//
// - 词表替换：在规整后的 final 文本中，用拼音近似匹配把误识别词替换为 term（误替换风险见 R30）。
// - Whisper prompt：sendToPrompt 的词按最近使用顺序拼成 initial_prompt，总长不超过 maxTokens（约 224，未核实）。
// 线程：纯函数，任意线程。

#include <string>
#include <string_view>
#include <vector>

namespace subtitle {

struct GlossaryEntry {
    std::string term;              // 正确词形（UTF-8）
    std::string pinyinKey;         // 拼音键（无声调、空格分隔）；空 = 由实现按 term 生成
    bool sendToPrompt = true;      // 是否送 Whisper initial_prompt
};

// 返回替换次数；text 原地改写。
int applyGlossary(std::string &text, const std::vector<GlossaryEntry> &glossary);
// 热词在 pass-1 中缺失（RefineRouting::glossaryMissTriggers 用）：词表某词的拼音在 text 中近似出现但词形不同。
bool glossaryMiss(std::string_view text, const std::vector<GlossaryEntry> &glossary);
std::string buildWhisperPrompt(const std::vector<GlossaryEntry> &glossary, int maxTokens = 224);

} // namespace subtitle
