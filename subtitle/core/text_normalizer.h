#pragma once
// M0 冻结：文本规整（design-verified.md §11.3 B；design-live-v2.md §4.3.2 第 1 步）。
// 接口归属：M0；实现归工作包 B。CMake 目标 subtitle_core（纯 C++17，纯函数）。
//
// 规则：去 "<|...|>" 引擎标签残留；合并连续空白、去首尾空白（中英之间不强加空格）；
// 规整后只剩标点/空白或只由语气词组成 → 返回空串（调用方丢弃该句）；非法 UTF-8 按 U+FFFD 处理，不崩溃。
// 不做繁简转换（Whisper 精修的 OpenCC 繁转简在宿主或 R 包内完成）。
// 线程：无共享状态，任意线程。

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace subtitle {

struct NormalizeOptions {
    bool stripEngineTags = true;
    bool collapseWhitespace = true;
    bool dropFillerOnly = true;
    std::vector<std::string> fillerWords = {"嗯", "啊", "呃", "哦", "唔", "额", "噢"};   // 初值，B 定稿
};

std::string normalizeText(std::string_view raw, const NormalizeOptions &);

// Unicode 码点数（不是字节数），非法字节各计 1。
std::size_t countCodePoints(std::string_view utf8) noexcept;
// 有效字符数：CJK 表意/假名/谚文各计 1，连续拉丁字母/数字串计 1，标点与空白计 0（CueBuilder 用）。
std::size_t effectiveCharCount(std::string_view utf8) noexcept;
// ASCII 字母占全部非空白、非标点码点的比例（RefineScheduler::routes 用，§4.3.3）。
double asciiLetterRatio(std::string_view utf8) noexcept;

} // namespace subtitle
