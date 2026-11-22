#pragma once
// M0 冻结：音频分接环 AudioTap（design-verified.md §11.3 A，design-live-v2.md §10.3 A、§7.7、§8.2）。
// 接口归属：M0；实现 audio_tap.cpp 归工作包 A。CMake 目标 subtitle_tap（实现阶段只依赖 avutil，无 Qt）。
//
// 线程模型
// - 生产者：唯一一个，FFAEncoderThread（I1 挂接）。push / markSplice / finish 只在该线程调用，
//   wait-free、noexcept、不加锁、不分配、不写日志、不做系统调用（maxPushNs 计时除外）。
// - 消费者：唯一一个，IngestWorker。pop / release 只在该线程调用；Empty 时由调用方自行 sleep。
// - 任意线程：abort、stats。
// - 环满：丢弃**最新**块并计数（droppedBlocks/droppedSamples），绝不阻塞生产者；
//   下一个被接收块的 pts 自然体现空洞，由 IngestWorker 补零。
//
// splice（§8.2）
// - 暂停开始与恢复两个方向都调用 markSplice()（只设原子标志）。
// - 下一个被接收的块带 Block::splice = true；暂停开始后若一直没有新块，IngestWorker 通过
//   spliceRequested() 发现「已置位且无新块」，自行按 committed16k 上报 splice（§8.2 第 4 条）。
//
// 状态：Running → finish() → Finished（读完剩余数据后 pop 得到 Eof）；abort() → Aborted
// （丢弃未读数据，此后 pop 只得到 Aborted，优先于 Eof）。finish/abort 幂等；之后的 push 被忽略。
//
// 所有权：由 SubtitleService::beginRecording 创建并以 shared_ptr 交给编码线程与 IngestWorker，
// 任何一方先释放都不会悬空。push 只读 AVFrame 数据，不 ref、不保留指针。

#include <cstdint>

struct AVFrame;   // libavutil/frame.h；头文件不 include FFmpeg，实现文件再 include

namespace subtitle {

class AudioTap {
public:
    // 预分配 capacityMs（48 kHz 单声道）的环；之后不再分配。
    explicit AudioTap(int capacityMs = 30000);
    ~AudioTap();
    AudioTap(const AudioTap &) = delete;
    AudioTap &operator=(const AudioTap &) = delete;

    // ---- 生产者（FFAEncoderThread）----
    // 48 kHz、AV_SAMPLE_FMT_FLT/FLTP、1 或 2 声道（2 声道下混 (L+R)/2）、pts 有效；否则计 formatRejects。
    void push(const AVFrame &frame) noexcept;
    // 暂停开始与恢复都调用：下一块带 splice 标志（§8.2）。
    void markSplice() noexcept;
    // 编码线程每个退出点调用（建议 RAII）；幂等。
    void finish() noexcept;

    // ---- 任意线程 ----
    void abort() noexcept;

    // ---- 消费者（IngestWorker）----
    enum class Pop { Data, Empty, Eof, Aborted };
    struct Block {
        int64_t pts48k;        // 首样本的 48 kHz 样本号（readyFrame->pts）
        int samples;           // 有效样本数
        bool splice;           // 本块之前有暂停开始/恢复
        const float *mono;     // 指向环内数据，release() 前有效
    };
    // 单消费者；返回 Data 后必须调用 release() 才能取下一块。
    Pop pop(Block &out) noexcept;
    void release() noexcept;
    // markSplice() 已调用但尚未随任何块带出（暂停开始后无新块时用）。读取不清除；
    // 清除发生在下一个被接收块带出 splice、或消费者调用 consumeSpliceRequest() 时。
    bool spliceRequested() const noexcept;
    void consumeSpliceRequest() noexcept;

    struct Stats {
        uint64_t pushed = 0, droppedBlocks = 0, droppedSamples = 0, formatRejects = 0, maxPushNs = 0;
        uint64_t splices = 0;   // markSplice 次数
    };
    Stats stats() const noexcept;

private:
    struct Impl;
    Impl *m_impl = nullptr;     // 实现阶段由 A 决定布局；M0 桩为空
};

} // namespace subtitle
