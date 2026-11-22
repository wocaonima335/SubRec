// 段状态机（工作包 C，design-live-v2.md §3.4、§3.5、§8.2）。
//
// 输入：onSpeechStart/onSpeechEnd（VAD）、onSplice（暂停/恢复拼接点）、onFinish（录制结束）、
// setPartialIntervalMs（setLevel 降级）；输出：next() 依次吐出 Partial / Final 动作。
//
// 语义要点
// - splice：在 at16k 强制封口当前段（forcedCut=true），丢弃该段未发出的 partial；空段（at16k ≤ 段起点）
//   直接丢弃不产 final。
// - maxSegment：committed 越过 段起点 + maxSegmentMs×16 时在精确倍数位置强切（forcedCut=true），
//   话音继续则立刻以新 id 在切点重开。
// - partial：段内每 partialIntervalMs 一次（从段被 next() 观察到起武装，首次在 interval 之后）；
//   interval=0 关闭；partialInFlight 时跳过本次不重排计时（§3.4「不排队」）。
// - final 的 forcedCut 只由 maxSegment / splice 置位（subtitle_types.h 语义）；VAD 端点与 finish 的
//   封口是自然端点。

#include "host_runtime.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace asrhost {

void SegmentStateMachine::configure(const subtitle::LiveConfig &live, int64_t resumeFrom16k,
                                    subtitle::SegId firstId)
{
    m_maxSegment16k = live.maxSegmentMs > 0 ? static_cast<int64_t>(live.maxSegmentMs)
                                              * subtitle::kAsrSamplesPerMs
                                            : 0;
    m_partialIntervalMs = live.partialIntervalMs;
    m_pos = resumeFrom16k;
    m_nextId = firstId;
    m_open = false;
    m_segBegin = 0;
    m_segId = -1;
    m_nextPartialAtMs = -1;
    m_pending.clear();
}

void SegmentStateMachine::onSpeechStart(int64_t at16k)
{
    if (m_open)
        return;    // maxSegment 强切后已立即重开；VAD 的旧起点不再触发
    m_open = true;
    m_segBegin = std::max(at16k, m_pos);
    m_segId = m_nextId++;
    m_nextPartialAtMs = -1;
}

void SegmentStateMachine::onSpeechEnd(int64_t at16k)
{
    if (!m_open)
        return;
    const int64_t e = std::max(at16k, m_segBegin + 1);
    // 超长段即使端点已知也按 maxSegment 拆分：整批喂入（committed 一次推进很多）时，
    // 段的开与合会在同一批 feed 内发生，next() 的强切分支没机会介入。
    while (m_maxSegment16k > 0 && e - m_segBegin > m_maxSegment16k) {
        const int64_t hardEnd = m_segBegin + m_maxSegment16k;
        m_pending.push_back({m_segId, m_segBegin, hardEnd, true});
        m_pos = hardEnd;
        m_segBegin = hardEnd;
        m_segId = m_nextId++;
    }
    m_pending.push_back({m_segId, m_segBegin, e, false});
    m_open = false;
    m_pos = e;
    m_nextPartialAtMs = -1;
}

void SegmentStateMachine::onSplice(int64_t at16k)
{
    if (!m_open)
        return;
    if (at16k > m_segBegin) {
        m_pending.push_back({m_segId, m_segBegin, at16k, true});
        m_pos = at16k;
    } else {
        // 空段（拼接点在段起点之前/重合）：丢弃，不产 final。
        m_pos = std::max(m_pos, at16k);
    }
    m_open = false;
    m_nextPartialAtMs = -1;
}

void SegmentStateMachine::onFinish(int64_t total16k)
{
    if (!m_open)
        return;
    const int64_t e = std::max(total16k, m_segBegin + 1);
    while (m_maxSegment16k > 0 && e - m_segBegin > m_maxSegment16k) {
        const int64_t hardEnd = m_segBegin + m_maxSegment16k;
        m_pending.push_back({m_segId, m_segBegin, hardEnd, true});
        m_pos = hardEnd;
        m_segBegin = hardEnd;
        m_segId = m_nextId++;
    }
    m_pending.push_back({m_segId, m_segBegin, e, false});
    m_open = false;
    m_pos = e;
    m_nextPartialAtMs = -1;
}

void SegmentStateMachine::setPartialIntervalMs(int ms)
{
    m_partialIntervalMs = ms;
}

SegmentStateMachine::Action SegmentStateMachine::next(int64_t committed16k, int64_t nowMs,
                                                      bool partialInFlight)
{
    // 1. 已封口的段先出 final。
    if (!m_pending.empty()) {
        const Pending p = m_pending.front();
        m_pending.erase(m_pending.begin());
        Action a;
        a.kind = Action::Final;
        a.id = p.id;
        a.b16k = p.b16k;
        a.e16k = p.e16k;
        a.forcedCut = p.forcedCut;
        return a;
    }

    // 2. maxSegment 强切：committed 到达硬边界即在精确位置切分并立刻重开（话音仍在继续）。
    if (m_open && m_maxSegment16k > 0) {
        const int64_t hardEnd = m_segBegin + m_maxSegment16k;
        if (committed16k >= hardEnd) {
            m_pending.push_back({m_segId, m_segBegin, hardEnd, true});
            m_pos = hardEnd;
            m_segBegin = hardEnd;
            m_segId = m_nextId++;
            m_nextPartialAtMs = -1;
            const Pending p = m_pending.front();
            m_pending.erase(m_pending.begin());
            Action a;
            a.kind = Action::Final;
            a.id = p.id;
            a.b16k = p.b16k;
            a.e16k = p.e16k;
            a.forcedCut = p.forcedCut;
            return a;
        }
    }

    // 3. partial 调度。
    if (m_open && !partialInFlight && m_partialIntervalMs > 0 && committed16k > m_segBegin) {
        if (m_nextPartialAtMs < 0) {
            m_nextPartialAtMs = nowMs + m_partialIntervalMs;   // 只武装，不出 partial
        } else if (nowMs >= m_nextPartialAtMs) {
            m_nextPartialAtMs = nowMs + m_partialIntervalMs;
            Action a;
            a.kind = Action::Partial;
            a.id = m_segId;
            a.b16k = m_segBegin;
            a.e16k = committed16k;
            return a;
        }
    }
    return {};
}

bool SegmentStateMachine::idle() const
{
    return m_pending.empty() && !m_open;
}

} // namespace asrhost
