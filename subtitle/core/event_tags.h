#pragma once
// M0 冻结：语音段内事件标签（design-live-v2.md §10.3 FixerRules::eventTags、§14.5、D34）。M2 功能，默认关。
// 接口归属：M0；实现归工作包 K。CMake 目标 subtitle_core。
//
// SenseVoice 的 FinalSegment::event（如 "BGM"/"Applause"/"Laughter"，取值以 sherpa C API 实际输出为准）
// 映射为 [音乐] / [掌声] / [笑声]；不认识或 "Speech" 返回空串。
// 线程：纯函数，任意线程。

#include "subtitle/subtitle_types.h"

#include <string>
#include <string_view>

namespace subtitle {

std::string eventTagFor(std::string_view event);
// 需要加标签时把标签前置到 io.text（"[音乐] ..."），返回是否改动。
bool applyEventTag(FinalSegment &io);

} // namespace subtitle
