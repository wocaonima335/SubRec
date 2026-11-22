#include "subtitle/audio/mono_resampler_16k.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace subtitle {
namespace {
constexpr size_t delay = MonoResampler16k::groupDelay48k();
constexpr size_t taps = 2 * delay + 1;
// 61-tap Blackman-windowed sinc, cutoff 7.2 kHz. Symmetric, unity DC gain.
const std::array<double, taps> &coefficients()
{
    static const auto result = [] {
        std::array<double, taps> h{};
        constexpr double pi = 3.14159265358979323846;
        double sum = 0;
        for (size_t i = 0; i < taps; ++i) {
            const double x = double(i) - delay;
            const double sinc = x == 0 ? 0.3 : std::sin(pi * 0.3 * x) / (pi * x);
            h[i] = sinc * (0.42 - 0.5 * std::cos(2 * pi * i / (taps - 1)) +
                          0.08 * std::cos(4 * pi * i / (taps - 1)));
            sum += h[i];
        }
        for (auto &v : h) v /= sum;
        return h;
    }();
    return result;
}
void emit(std::vector<float> &history, std::vector<float> &out)
{
    const auto &h = coefficients();
    size_t used = 0;
    while (history.size() - used >= taps) {
        double value = 0;
        for (size_t j = 0; j < taps; ++j) value += history[used + j] * h[j];
        out.push_back(float(value));
        used += 3;
    }
    history.erase(history.begin(), history.begin() + used);
}
} // namespace
MonoResampler16k::MonoResampler16k() = default;
void MonoResampler16k::process(const float *input, size_t n, std::vector<float> &out)
{
    if (!n) return;
    if (m_history.empty()) m_history.assign(delay, 0.f);
    // Bound scratch memory even for very large calls. Keep only the FIR window
    // and its 0..2 decimation-phase samples between calls.
    while (n) {
        const size_t count = std::min(n, size_t{4096});
        m_history.insert(m_history.end(), input, input + count);
        emit(m_history, out);
        input += count;
        n -= count;
    }
}
void MonoResampler16k::flush(std::vector<float> &out)
{
    if (!m_history.empty()) {
        m_history.insert(m_history.end(), delay, 0.f);
        emit(m_history, out);
    }
    reset(); // Exactly ceil(inputSamples / 3) outputs, no duration-extending tail.
}
void MonoResampler16k::reset() { m_history.clear(); }
} // namespace subtitle
