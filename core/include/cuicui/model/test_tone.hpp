#pragma once

#include "cuicui/io/render_spec.hpp"

#include <cmath>
#include <numbers>

namespace cuicui::model {

/// Offline test-tone generator: phase-continuous sine with per-sample frequency and gain.
/// Used in A1 to prove the render pipeline. Not part of the bird model.
class TestTone {
public:
    void prepare(double sampleRate) {
        sample_rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept { phase_ = 0.0; }

    /// Fill `out[0..n)` starting at time `t0` seconds. Frequency (Hz) and gain (dB) from automation.
    void process(float* out,
                 int n,
                 const io::Automation& freq,
                 const io::Automation& gainDb,
                 double t0) noexcept {
        if (out == nullptr || n <= 0) {
            return;
        }
        const double dt = 1.0 / sample_rate_;
        constexpr double two_pi = 2.0 * std::numbers::pi;
        for (int i = 0; i < n; ++i) {
            const double t = t0 + static_cast<double>(i) * dt;
            const double f = std::max(0.0, freq.valueAt(t));
            const double gdb = gainDb.valueAt(t);
            const double amp = std::pow(10.0, gdb / 20.0);
            out[i] = static_cast<float>(amp * std::sin(phase_));
            phase_ += two_pi * f * dt;
            if (phase_ > two_pi) {
                phase_ -= two_pi * std::floor(phase_ / two_pi);
            }
        }
    }

private:
    double sample_rate_ = 48000.0;
    double phase_ = 0.0;
};

} // namespace cuicui::model
