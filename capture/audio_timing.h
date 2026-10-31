#pragma once
#include <cstdint>

namespace capture::audio {
// Mixed PCM sample zero is session time zero, but cannot be emitted until this
// much capture history is available. Share the latency with video clock pacing.
inline constexpr int64_t mixHoldbackUs = 200000;
}
