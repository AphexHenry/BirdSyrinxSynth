#pragma once

#include "cuicui/params.hpp"

#include <algorithm>
#include <cmath>

namespace cuicui::model {

/// Musician size σ = 1 ≈ zebra finch. 01_research.md §4.9 / WP-B6.
inline constexpr double kBodySizeMin = 0.25;
inline constexpr double kBodySizeMax = 4.0;
/// Aspiration band-pass centre at σ = 1 (01_research.md §4.5).
inline constexpr double kAspirationCentreHz = 4000.0;
inline constexpr double kAspirationQ = 0.7;

[[nodiscard]] inline double clampBodySize(double sigma) noexcept {
    const auto& spec = specOf(ParamId::BodySize);
    return clampParam(spec, sigma);
}

/// Derived anatomy after applying σ. Lengths × σ, frequencies × 1/σ, respiratory τ × √σ.
struct ScaledAnatomy {
    double sigma = 1.0;
    double gamma = 24000.0;
    double tracheaLengthMm = 33.0;
    double sizeScale = 1.0;
    double tensionBandwidthHz = 300.0;
    double aspirationCentreHz = kAspirationCentreHz;
    double respAttackMs = 8.0;
    double respReleaseMs = 15.0;
};

[[nodiscard]] inline ScaledAnatomy scaleAnatomy(double sigma, double gamma0, double tracheaLengthMm,
                                               double tensionBwHz, double respAttackMs,
                                               double respReleaseMs) noexcept {
    const double s = clampBodySize(sigma);
    const double root = std::sqrt(s);
    ScaledAnatomy a;
    a.sigma = s;
    a.gamma = (gamma0 > 0.0 ? gamma0 : 24000.0) / s;
    a.tracheaLengthMm = tracheaLengthMm * s;
    a.sizeScale = s;
    a.tensionBandwidthHz = tensionBwHz / s;
    a.aspirationCentreHz = kAspirationCentreHz / s;
    a.respAttackMs = respAttackMs * root;
    a.respReleaseMs = respReleaseMs * root;
    return a;
}

} // namespace cuicui::model
