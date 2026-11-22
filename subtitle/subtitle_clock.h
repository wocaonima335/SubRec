#pragma once
// M0 冻结：可注入时钟 IClock（design-live-v2.md §10.3 W 块末尾）。
// 归属：M0。放在顶层而不是 live/，是因为 audio/IngestWorker 与 live/* 都要用，避免 audio → live 的反向依赖。
//
// - 纯 C++17。生产实现 SteadyClock 用 std::chrono::steady_clock；测试用 ManualClock 手动推进。
// - nowMs() 必须单调不减；起点任意（只用差值）。
// - 线程：SteadyClock 任意线程；ManualClock 不加锁，只在单线程测试中使用。

#include <chrono>
#include <cstdint>

namespace subtitle {

struct IClock {
    virtual ~IClock() = default;
    virtual int64_t nowMs() const = 0;
};

class SteadyClock final : public IClock {
public:
    int64_t nowMs() const override
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
};

// 测试用：初值 0，由测试显式推进。
class ManualClock final : public IClock {
public:
    explicit ManualClock(int64_t startMs = 0) noexcept : m_now(startMs) {}
    int64_t nowMs() const override { return m_now; }
    void advance(int64_t ms) noexcept { m_now += ms; }
    void set(int64_t ms) noexcept { m_now = ms; }

private:
    int64_t m_now;
};

} // namespace subtitle
