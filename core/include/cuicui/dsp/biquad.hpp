#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cuicui::dsp {

/// Transposed direct-form II biquad. RBJ cookbook setters. Real-time safe after `prepare()`.
class Biquad {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    /// Constant-skirt-gain / 0 dB peak-gain band-pass (RBJ). See 01_research.md §4.5.
    void setBandPass(double freqHz, double q) noexcept {
        const double fs = rate_ > 0.0 ? rate_ : 48000.0;
        const double f = std::clamp(freqHz, 1.0, 0.45 * fs);
        const double Q = std::max(0.05, q);
        const double w0 = 2.0 * std::numbers::pi * f / fs;
        const double cw = std::cos(w0);
        const double sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        b0_ = alpha / a0;
        b1_ = 0.0;
        b2_ = -alpha / a0;
        a1_ = (-2.0 * cw) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    [[nodiscard]] double tick(double x) noexcept {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    double rate_ = 48000.0;
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace cuicui::dsp
