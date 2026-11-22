#pragma once
// M0 冻结：IngestWorker（design-live-v2.md §10.3 A、§3.3、§8.2、§9.3）。
// 接口归属：M0；实现归工作包 A。CMake 目标 subtitle_audio（纯 C++17，std::thread，无 Qt）。
//
// 职责（自有 std::thread）：
//   AudioTap::pop → 按 pts48k 补洞（补零，计 gaps/gapSamples）→ MonoResampler16k → PcmSpool::append
//   → 每 100 ms PcmSpool::publish(committed16k)。
//   读到 Block::splice → PcmSpool::markSplice16k(该块对应的 16k 样本号)；
//   AudioTap::spliceRequested() 且本轮无新块 → markSplice16k(committed16k) 并 consumeSpliceRequest()（§8.2 第 4 条）。
//   tap Eof → 重采样器 flush → 最后一次 publish → PcmSpool::finalize → 线程结束。
//   tap Aborted → 不 finalize，直接结束。
//   spool 写失败 → 记录 Status（lastError），继续消费 tap 但不再写（§6.1：不反压录制）。
//
// 线程：构造、start、requestStop、join、stats、lastError 由所属 Job（Runner 线程）调用。
// 时钟：IClock 只用于 100 ms 发布节奏与轮询间隔，测试可注入 ManualClock。

#include "subtitle/subtitle_clock.h"
#include "subtitle/subtitle_error.h"

#include <chrono>
#include <cstdint>
#include <memory>

namespace subtitle {

class AudioTap;
class PcmSpool;

class IngestWorker {
public:
    IngestWorker(std::shared_ptr<AudioTap> tap, std::shared_ptr<PcmSpool> spool, IClock &clock);
    ~IngestWorker();                                  // 未 join 时 requestStop + join
    IngestWorker(const IngestWorker &) = delete;
    IngestWorker &operator=(const IngestWorker &) = delete;

    Status start();
    void requestStop();                               // 不等同于 tap.finish：立即停止，不刷尾
    bool join(std::chrono::milliseconds timeout);     // 超时返回 false

    struct Stats { uint64_t blocks = 0, gaps = 0, gapSamples = 0, splices = 0; };
    Stats stats() const;
    Status lastError() const;                         // spool 写失败等；Ok 表示无错误

private:
    std::shared_ptr<AudioTap> m_tap;
    std::shared_ptr<PcmSpool> m_spool;
    IClock &m_clock;
};

} // namespace subtitle
