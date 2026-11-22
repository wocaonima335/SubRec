#pragma once
// M0 冻结：录制生命周期钩子 RecordingAudioHook（design-verified.md §11.3 A）。
// 接口归属：M0；实现方 = SubtitleService（工作包 D）；调用方 = CaptureScheduler（工作包 G，I3 挂接）。
//
// 约定
// - 只在 GUI 线程、CaptureScheduler 的 m_mutex 之外调用。
// - 每次 beginRecording 之后恰好一次 endRecording（成功或失败路径都要配对）。
// - beginRecording 返回 nullptr 表示本次不做字幕（关闭、不可用、出错），录制照常进行。
// - endRecording(true) = MP4 已正常收尾：只投递消息并立即返回，定稿在 Idle 之后异步完成（§7.7 第 3 条）。
//   endRecording(false) = 录制失败：不写 .srt，保留 .srt.part 与 spool（§4.2 第 3 条）。
// - 两个函数都不得阻塞、不得抛异常。

#include <filesystem>
#include <memory>

namespace subtitle {

class AudioTap;

class RecordingAudioHook {
public:
    virtual ~RecordingAudioHook() = default;
    virtual std::shared_ptr<AudioTap> beginRecording(const std::filesystem::path &mp4) noexcept = 0;
    virtual void endRecording(bool mp4Finalized) noexcept = 0;
};

} // namespace subtitle
