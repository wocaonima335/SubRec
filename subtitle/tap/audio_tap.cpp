// M0 桩：AudioTap。真实实现归工作包 A（SPSC 环、下混、splice 标志、统计）。
// 桩行为：不保存任何数据，pop 永远返回 Empty（finish 后 Eof、abort 后 Aborted）。

#include "subtitle/tap/audio_tap.h"

namespace subtitle {

struct AudioTap::Impl {
    bool finished = false;
    bool aborted = false;
    bool spliceRequested = false;
    Stats stats;
};

AudioTap::AudioTap(int /*capacityMs*/) : m_impl(new Impl) {}

AudioTap::~AudioTap() { delete m_impl; }

void AudioTap::push(const AVFrame & /*frame*/) noexcept {}

void AudioTap::markSplice() noexcept
{
    m_impl->spliceRequested = true;
    ++m_impl->stats.splices;
}

void AudioTap::finish() noexcept { m_impl->finished = true; }

void AudioTap::abort() noexcept { m_impl->aborted = true; }

AudioTap::Pop AudioTap::pop(Block &out) noexcept
{
    out = Block{0, 0, false, nullptr};
    if (m_impl->aborted)
        return Pop::Aborted;
    return m_impl->finished ? Pop::Eof : Pop::Empty;
}

void AudioTap::release() noexcept {}

bool AudioTap::spliceRequested() const noexcept { return m_impl->spliceRequested; }

void AudioTap::consumeSpliceRequest() noexcept { m_impl->spliceRequested = false; }

AudioTap::Stats AudioTap::stats() const noexcept { return m_impl->stats; }

} // namespace subtitle
