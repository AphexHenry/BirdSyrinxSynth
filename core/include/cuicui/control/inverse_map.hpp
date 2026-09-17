#pragma once

#include "cuicui/generated/atlas_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cuicui::control {

struct InverseResult {
    double alpha = 0.2;
    double beta = 0.3;
    double nuUsed = 0.0;
    bool reachable = false;
    bool folded = false;
    bool clamped = false;
};

/// Real-time inverse map ν, tone, breath → (α, β). See 01_research.md §7.4.
/// Lookups are index arithmetic + bilinear interpolation; noexcept; no allocation.
class InverseMap {
public:
    [[nodiscard]] InverseResult solve(double nu, double tone, double alphaBreath, double physics,
                                      bool rangeFold) const noexcept {
        InverseResult r;
        const double t = std::clamp(tone, 0.0, 1.0);
        const double phi = std::clamp(physics, 0.0, 1.0);
        const double nMin = nuMin();
        const double nMax = nuMax(t);
        double nuUsed = nu;
        bool folded = false;
        bool clamped = false;

        auto inRange = [&](double x) noexcept -> bool {
            return x >= nMin && x <= nMax && loHiOk(x);
        };

        if (!inRange(nuUsed)) {
            if (rangeFold && nuUsed > 0.0 && std::isfinite(nuUsed)) {
                double x = nuUsed;
                int guard = 0;
                while (x > nMax && guard < 8) {
                    x *= 0.5;
                    folded = true;
                    ++guard;
                }
                guard = 0;
                while (x < nMin && guard < 8) {
                    x *= 2.0;
                    folded = true;
                    ++guard;
                }
                nuUsed = x;
            }
            if (!inRange(nuUsed)) {
                nuUsed = std::clamp(nuUsed, nMin, nMax);
                clamped = true;
                if (!inRange(nuUsed)) {
                    nuUsed = nMin;
                }
            }
        }

        double lo = 0.0;
        double hi = 0.0;
        if (!sampleLoHi(nuUsed, lo, hi)) {
            r.nuUsed = nuUsed;
            r.folded = folded;
            r.clamped = true;
            r.reachable = false;
            return r;
        }
        const double alphaT = lo + t * (hi - lo);
        double alpha = (1.0 - phi) * alphaT + phi * alphaBreath;
        alpha = std::clamp(alpha, lo, hi);
        const double beta = lookupBeta(alpha, nuUsed);
        r.alpha = alpha;
        r.beta = std::isfinite(beta) ? beta : 0.3;
        r.nuUsed = nuUsed;
        r.folded = folded;
        r.clamped = clamped;
        r.reachable = !clamped && std::isfinite(beta);
        return r;
    }

    [[nodiscard]] double nuMin() const noexcept {
        return static_cast<double>(generated::kAtlasNuMinReachable);
    }

    [[nodiscard]] double nuMax(double tone) const noexcept {
        const double t = std::clamp(tone, 0.0, 1.0);
        const double a = static_cast<double>(generated::kAtlasNuMaxTone0);
        const double b = static_cast<double>(generated::kAtlasNuMaxTone1);
        return a + t * (b - a);
    }

private:
    [[nodiscard]] static double nuToIndex(double nu) noexcept {
        using generated::kAtlasNNu;
        using generated::kAtlasNuMaxGrid;
        using generated::kAtlasNuMinGrid;
        if (!(nu > 0.0)) {
            return 0.0;
        }
        const double nMinG = static_cast<double>(kAtlasNuMinGrid);
        const double nMaxG = static_cast<double>(kAtlasNuMaxGrid);
        const double num = std::log(nu / nMinG);
        const double den = std::log(nMaxG / nMinG);
        const double x = (den == 0.0) ? 0.0 : num / den * static_cast<double>(kAtlasNNu - 1);
        return std::clamp(x, 0.0, static_cast<double>(kAtlasNNu - 1));
    }

    [[nodiscard]] static bool sampleLoHi(double nu, double& lo, double& hi) noexcept {
        using generated::kAtlasAlphaHi;
        using generated::kAtlasAlphaLo;
        using generated::kAtlasNNu;
        const double x = nuToIndex(nu);
        const int j0 = static_cast<int>(x);
        const int j1 = std::min(j0 + 1, kAtlasNNu - 1);
        const double u = x - static_cast<double>(j0);
        const float l0 = kAtlasAlphaLo[static_cast<std::size_t>(j0)];
        const float l1 = kAtlasAlphaLo[static_cast<std::size_t>(j1)];
        const float h0 = kAtlasAlphaHi[static_cast<std::size_t>(j0)];
        const float h1 = kAtlasAlphaHi[static_cast<std::size_t>(j1)];
        const bool ok0 = std::isfinite(l0) && std::isfinite(h0);
        const bool ok1 = std::isfinite(l1) && std::isfinite(h1);
        if (!ok0 && !ok1) {
            return false;
        }
        if (!ok0) {
            lo = static_cast<double>(l1);
            hi = static_cast<double>(h1);
            return lo <= hi;
        }
        if (!ok1) {
            lo = static_cast<double>(l0);
            hi = static_cast<double>(h0);
            return lo <= hi;
        }
        lo = (1.0 - u) * static_cast<double>(l0) + u * static_cast<double>(l1);
        hi = (1.0 - u) * static_cast<double>(h0) + u * static_cast<double>(h1);
        return std::isfinite(lo) && std::isfinite(hi) && lo <= hi;
    }

    [[nodiscard]] static bool loHiOk(double nu) noexcept {
        double lo = 0.0;
        double hi = 0.0;
        return sampleLoHi(nu, lo, hi);
    }

    [[nodiscard]] static double betaAtRow(int ia, double nIndex) noexcept {
        using generated::kAtlasBetaOfAlphaNu;
        using generated::kAtlasNAlpha;
        using generated::kAtlasNNu;
        if (ia < 0 || ia >= kAtlasNAlpha) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const int j0 = static_cast<int>(nIndex);
        const int j1 = std::min(j0 + 1, kAtlasNNu - 1);
        const double u = nIndex - static_cast<double>(j0);
        const std::size_t base = static_cast<std::size_t>(ia) * static_cast<std::size_t>(kAtlasNNu);
        const float b0 = kAtlasBetaOfAlphaNu[base + static_cast<std::size_t>(j0)];
        const float b1 = kAtlasBetaOfAlphaNu[base + static_cast<std::size_t>(j1)];
        if (std::isfinite(b0) && std::isfinite(b1)) {
            return (1.0 - u) * static_cast<double>(b0) + u * static_cast<double>(b1);
        }
        if (std::isfinite(b0)) {
            return static_cast<double>(b0);
        }
        if (std::isfinite(b1)) {
            return static_cast<double>(b1);
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    [[nodiscard]] static double lookupBeta(double alpha, double nu) noexcept {
        using generated::kAtlasAlpha0;
        using generated::kAtlasAlphaStep;
        using generated::kAtlasNAlpha;
        const double nIndex = nuToIndex(nu);
        const double a0 = static_cast<double>(kAtlasAlpha0);
        const double aStep = static_cast<double>(kAtlasAlphaStep);
        double aIndex = (alpha - a0) / aStep;
        aIndex = std::clamp(aIndex, 0.0, static_cast<double>(kAtlasNAlpha - 1));
        const int i0 = static_cast<int>(aIndex);
        const int i1 = std::min(i0 + 1, kAtlasNAlpha - 1);
        const double t = aIndex - static_cast<double>(i0);
        const double b0 = betaAtRow(i0, nIndex);
        const double b1 = betaAtRow(i1, nIndex);
        if (std::isfinite(b0) && std::isfinite(b1)) {
            return (1.0 - t) * b0 + t * b1;
        }
        if (std::isfinite(b0)) {
            return b0;
        }
        if (std::isfinite(b1)) {
            return b1;
        }
        for (int d = 1; d <= 4; ++d) {
            const double bm = betaAtRow(i0 - d, nIndex);
            if (std::isfinite(bm)) {
                return bm;
            }
            const double bp = betaAtRow(i1 + d, nIndex);
            if (std::isfinite(bp)) {
                return bp;
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
};

} // namespace cuicui::control
