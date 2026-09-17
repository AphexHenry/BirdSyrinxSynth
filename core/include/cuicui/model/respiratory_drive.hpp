#pragma once

#include "cuicui/model/labial_oscillator.hpp"

#include <algorithm>
#include <cmath>

namespace cuicui::model {

/// Flow gain from normalised air-sac pressure (Bernoulli). 01_research.md §4.2.
[[nodiscard]] inline double flowGain(double pressure) noexcept {
    return std::sqrt(std::max(pressure, 0.0));
}

/// One-pole air-sac pressure dynamics. Command u = `resp.pressure`.
/// Exact update: p ← u + (p − u) exp(−h/τ), τ = attack if u > p else release.
class RespiratoryDrive {
public:
    struct Out {
        double pressure = 0.0;
        double alphaBase = kAlphaRest;
        double G = 0.0;
    };

    void prepare(double internalRate) noexcept {
        const double fs = internalRate > 0.0 ? internalRate : 192000.0;
        h_ = 1.0 / fs;
        reset(0.0);
    }

    void setTimeConstantsMs(double attackMs, double releaseMs) noexcept {
        attack_s_ = std::clamp(attackMs, 1.0, 100.0) * 0.001;
        release_s_ = std::clamp(releaseMs, 1.0, 200.0) * 0.001;
    }

    void reset(double p0 = 0.0) noexcept { p_ = p0; }

    void prime(double pSteady) noexcept { p_ = pSteady; }

    [[nodiscard]] Out process(double u) noexcept {
        const double cmd = std::clamp(u, 0.0, 1.0);
        const double tau = (cmd > p_) ? attack_s_ : release_s_;
        if (tau <= 0.0 || h_ <= 0.0) {
            p_ = cmd;
        } else {
            p_ = cmd + (p_ - cmd) * std::exp(-h_ / tau);
        }
        Out o;
        o.pressure = p_;
        o.alphaBase = alphaFromPressure(p_);
        o.G = flowGain(p_);
        return o;
    }

    [[nodiscard]] double pressure() const noexcept { return p_; }
    void load(double p) noexcept { p_ = p; }

private:
    double h_ = 1.0 / 192000.0;
    double attack_s_ = 0.008;
    double release_s_ = 0.015;
    double p_ = 0.0;
};

} // namespace cuicui::model
