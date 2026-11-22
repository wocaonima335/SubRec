#pragma once
// M0 冻结：字幕写出与 sidecar（design-verified.md §11.3 B；design-live-v2.md §3.6.4、§4.2）。
// 接口归属：M0；实现归工作包 B。CMake 目标 subtitle_core（纯 C++17）。
//
// 格式
// - SRT：UTF-8，bom 时前置 EF BB BF；序号从 1；"HH:MM:SS,mmm --> HH:MM:SS,mmm"；crlf 时所有换行写 "\r\n"。
//   输入应已 resolveOverlaps；不排序、不改时间；文本为空的 cue 跳过（不占序号）。
//   provisional cue 与普通 cue 写法相同（SRT 没有「临时」标记，§3.6.3）。
// - VTT（M3）、ASS（M3）。
// - toJson/fromJson：sidecar name.subrec.json 的段列表（§4.2 第 5 条），fromJson 失败返回 false。
//
// 原子写 writeFileAtomic（§3.6.4）
// - 先写 "<target>.tmp"，flush 到磁盘（FlushFileBuffers / fsync），再覆盖改名为 target
//   （Windows：MoveFileExW(MOVEFILE_REPLACE_EXISTING)）。写到一半失败时 target 原内容完好，尽量删除 .tmp。
// - 只在后台线程（Runner）调用，不得在 GUI 线程或编码线程调用。

#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace subtitle {

struct SrtOptions {
    bool bom = true;
    bool crlf = true;
};

struct AssStyle {                            // M3；字段由 B 在 M3 时追加
    std::string fontName = "Microsoft YaHei";
    int fontSize = 48;
    int marginV = 40;
};

std::string formatSrtTimestamp(int64_t ms);   // 负值按 0
std::string formatSrt(const std::vector<Cue> &, bool bom, bool crlf);
std::string formatSrt(const std::vector<Cue> &, const SrtOptions &);
std::string formatVtt(const std::vector<Cue> &);
std::string formatAss(const std::vector<Cue> &, const AssStyle &);           // M3

std::string toJson(const std::vector<Segment> &);
bool fromJson(std::string_view, std::vector<Segment> &);
// 解析 SRT（resumeFromPart 用）：失败返回 false，out 内容未定义。
bool parseSrt(std::string_view, std::vector<Cue> &out);

Status writeFileAtomic(const std::filesystem::path &target, std::string_view bytes);   // .tmp → rename

// 路径约定：name.mp4 → name.srt / name.srt.part / name.live.srt / name.srt.refine.part
std::filesystem::path srtPathForMp4(const std::filesystem::path &mp4);
std::filesystem::path srtPartPath(const std::filesystem::path &srt);
std::filesystem::path liveSrtPath(const std::filesystem::path &srt);
std::filesystem::path refinePartPath(const std::filesystem::path &srt);

} // namespace subtitle
