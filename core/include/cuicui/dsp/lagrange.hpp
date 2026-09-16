#pragma once

#include <cmath>

namespace cuicui::dsp {

/// Wrap `i` into [0, n). C++ `%` truncates toward zero for negatives.
[[nodiscard]] inline int wrapIndex(int i, int n) noexcept {
    i %= n;
    if (i < 0) {
        i += n;
    }
    return i;
}

/// Sample at integer delay `d` samples behind the next-write index (`write`).
/// Delay 1 = most recently written sample. Delay 0 is the slot about to be written.
[[nodiscard]] inline double delayRead(const double* buf, int size, int write, int delay) noexcept {
    return buf[static_cast<unsigned>(wrapIndex(write - delay, size))];
}

/// 3rd-order Lagrange fractional delay. `delay` in samples, measured from `write`
/// (same convention as `delayRead`). Integer delays are exact.
///
/// For delay ≥ 2, uses the centered 4-point window (taps at n−1…n+2).
/// For 1 ≤ delay < 2 (output tap D/2 when D is just above 3), uses the causal
/// window (taps at n…n+3) so we never read delay 0. See 01_research.md §4.6.
[[nodiscard]] inline double lagrange3Read(const double* buf, int size, int write, double delay) noexcept {
    if (delay < 1.0) {
        delay = 1.0;
    }
    const int n = static_cast<int>(std::floor(delay));
    const double f = delay - static_cast<double>(n);
    if (delay >= 2.0) {
        const double ym1 = delayRead(buf, size, write, n - 1);
        const double y0 = delayRead(buf, size, write, n);
        const double y1 = delayRead(buf, size, write, n + 1);
        const double y2 = delayRead(buf, size, write, n + 2);
        const double hm1 = -f * (f - 1.0) * (f - 2.0) / 6.0;
        const double h0 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        const double h1 = -f * (f + 1.0) * (f - 2.0) / 2.0;
        const double h2 = f * (f + 1.0) * (f - 1.0) / 6.0;
        return hm1 * ym1 + h0 * y0 + h1 * y1 + h2 * y2;
    }
    const double y0 = delayRead(buf, size, write, n);
    const double y1 = delayRead(buf, size, write, n + 1);
    const double y2 = delayRead(buf, size, write, n + 2);
    const double y3 = delayRead(buf, size, write, n + 3);
    const double h0 = -(f - 1.0) * (f - 2.0) * (f - 3.0) / 6.0;
    const double h1 = f * (f - 2.0) * (f - 3.0) / 2.0;
    const double h2 = -f * (f - 1.0) * (f - 3.0) / 2.0;
    const double h3 = f * (f - 1.0) * (f - 2.0) / 6.0;
    return h0 * y0 + h1 * y1 + h2 * y2 + h3 * y3;
}

} // namespace cuicui::dsp
