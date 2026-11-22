#pragma once
// M0 冻结：PCM spool（design-verified.md §11.3 A + design-live-v2.md §10.3 A、§7.1）。
// 接口归属：M0；实现归工作包 A。CMake 目标 subtitle_audio（纯 C++17）。
//
// 文件格式：16 kHz、单声道、s16le 的 WAV，追加写的平面文件（不是环形）。RIFF 长度在 finalize 时回填；
// 录制中读者（subrec-asr 子进程，只读共享打开）以 committed16k 为准，不信任 RIFF 头里的长度。
//
// 线程模型
// - 写者：唯一一个，IngestWorker 线程：create / append / publish / markSplice16k / finalize。
// - committedSamples()、takeSplice()：Runner 线程读（原子量）。takeSplice 只允许一个读者。
// - 读者可在另一进程，只依赖「已发布的 committed16k 之前的字节都已 WriteFile 完成」这一点。
//
// 发布语义（§7.1、§9.3）
// - append 只写文件，不改变已发布的提交点；publish(committed16k) 每 100 ms 由 IngestWorker 调用，
//   把提交点原子地前移（单调不减，且不超过实际已写样本数）。
// - 不做 FlushFileBuffers（子进程通过同一文件缓存读得到，§7.1 最后一条）。
//
// 失败：磁盘满返回 SpoolDiskFull，其余写失败 SpoolWriteFailed；Job 据此进入 L4（§6.2）。

#include "subtitle/subtitle_error.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace subtitle {

class PcmSpool {
public:
    PcmSpool();
    ~PcmSpool();
    PcmSpool(const PcmSpool &) = delete;
    PcmSpool &operator=(const PcmSpool &) = delete;

    // ---- 写者（IngestWorker）----
    Status create(const std::filesystem::path &wav);
    Status append(const float *pcm16k, size_t n);     // s16 截幅写入；不发布
    void publish(int64_t committed16k) noexcept;      // 每 100 ms；单调、≤ 已写样本数
    void markSplice16k(int64_t at16k) noexcept;       // IngestWorker 读到 splice 块或暂停无新块时记录
    Status finalize();                                // 回填 RIFF 长度；之后只读
    int64_t writtenSamples() const noexcept;          // 已写（未必已发布）样本数，仅写者线程
    std::filesystem::path path() const;

    // ---- Runner 线程 ----
    int64_t committedSamples() const noexcept;        // 原子读
    int64_t takeSplice() noexcept;                    // 返回并清除最近的 splice16k（无则 -1）

private:
    std::filesystem::path m_path;
    std::atomic<int64_t> m_committed{0};
    std::atomic<int64_t> m_splice{-1};
    int64_t m_written = 0;
    void *m_file = nullptr;                           // 实现阶段由 A 决定（HANDLE / FILE*）
};

} // namespace subtitle
