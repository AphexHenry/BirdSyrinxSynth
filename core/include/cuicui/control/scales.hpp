#pragma once

#include "cuicui/params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cuicui::control {

/// Scale degrees in semitones from the root, 01_research.md §7.5 / WP-C3.
inline constexpr int kScaleN[6] = {12, 7, 7, 5, 5, 6};
inline constexpr int kScaleDeg[6][12] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, // chromatic
    {0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0},  // major
    {0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0},  // minor
    {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0},   // pentatonic major
    {0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0},  // pentatonic minor
    {0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0},  // whole tone
};

/// Nearest allowed pitch in cents (MIDI note 0 = 0 cents). Chromatic ignores root.
[[nodiscard]] inline double quantizeCents(double cents, int scale, int root) noexcept {
    const int sc = std::clamp(scale, 0, 5);
    if (sc == static_cast<int>(PerfScale::Chromatic)) {
        return std::round(cents / 100.0) * 100.0;
    }
    const int nDeg = kScaleN[sc];
    const int pcRoot = ((root % 12) + 12) % 12;
    const double note = cents / 100.0;
    const int n0 = static_cast<int>(std::floor(note));
    double best = cents;
    double bestErr = 1.0e9;
    for (int d = 0; d < nDeg; ++d) {
        const int pc = (pcRoot + kScaleDeg[sc][d]) % 12;
        for (int n = n0 - 24; n <= n0 + 24; ++n) {
            int m = n % 12;
            if (m < 0) {
                m += 12;
            }
            if (m != pc) {
                continue;
            }
            const double c = static_cast<double>(n) * 100.0;
            const double err = std::abs(c - cents);
            if (err < bestErr) {
                bestErr = err;
                best = c;
            }
        }
    }
    return best;
}

} // namespace cuicui::control
