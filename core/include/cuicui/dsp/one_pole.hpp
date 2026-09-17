#pragma once

#include <algorithm>
#include <cmath>

namespace cuicui::dsp {

/// One-pole low-pass: y[n] = (1−a) x[n] + a y[n−1], a = exp(−2π f_c / f_s).
/// DC gain is 1. `setCutoffHz(0)` bypasses (y = x). Real-time safe after `prepare()`.
class OnePole {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoeff();
        reset();
    }

    void setCutoffHz(double cutoffHz) noexcept {
        cutoff_hz_ = cutoffHz;
        updateCoeff();
    }

    void reset() noexcept { y_ = 0.0; }

    void prime(double ySteady) noexcept { y_ = ySteady; }

    [[nodiscard]] double tick(double x) noexcept {
        y_ = (1.0 - a_) * x + a_ * y_;
        return y_;
    }

    [[nodiscard]] double coefficient() const noexcept { return a_; }
    [[nodiscard]] double value() const noexcept { return y_; }
    void load(double y) noexcept { y_ = y; }

private:
    void updateCoeff() noexcept {
        if (cutoff_hz_ <= 0.0 || rate_ <= 0.0) {
            a_ = 0.0;
            return;
        }
        a_ = std::exp(-2.0 * 3.14159265358979323846 * cutoff_hz_ / rate_);
        a_ = std::min(0.999999, std::max(0.0, a_));
    }

    double rate_ = 48000.0;
    double cutoff_hz_ = 0.0;
    double a_ = 0.0;
    double y_ = 0.0;
};

} // namespace cuicui::dsp
