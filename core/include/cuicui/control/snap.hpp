#pragma once

#include "cuicui/control/scales.hpp"

#include <algorithm>
#include <cmath>

namespace cuicui::control {

inline constexpr double kSnapV0 = 2000.0; // cents/s, 01_research.md §7.5

/// Pull stable pitch toward the scale. Sweeps (high |dP/dt|) stay free.
[[nodiscard]] inline double applySnap(double pitchCents, double snap, double dPdt, int scale,
                                      int root) noexcept {
    const double s = std::clamp(snap, 0.0, 1.0);
    if (s <= 0.0) {
        return pitchCents;
    }
    const double w = std::exp(-std::abs(dPdt) / kSnapV0);
    const double q = quantizeCents(pitchCents, scale, root);
    return pitchCents + s * w * (q - pitchCents);
}

} // namespace cuicui::control
