#pragma once

#include <cmath>

namespace cuicui::dsp {

/// One-pole DC blocker. y[n] = x[n] − x[n−1] + R y[n−1], R = exp(−2π f_c / f_s).
/// Real-time safe after `prepare()`.
class DcBlocker {
public:
    void prepare(double sampleRate, double cutoffHz = 20.0) noexcept {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double fc = cutoffHz > 0.0 ? cutoffHz : 20.0;
        r_ = std::exp(-2.0 * 3.14159265358979323846 * fc / fs);
        reset();
    }

    void reset() noexcept {
        x1_ = 0.0;
        y1_ = 0.0;
    }

    /// Pretend `xSteady` has been the input forever (no startup step).
    void prime(double xSteady) noexcept {
        x1_ = xSteady;
        y1_ = 0.0;
    }

    [[nodiscard]] double tick(double x) noexcept {
        const double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

    [[nodiscard]] double x1() const noexcept { return x1_; }
    [[nodiscard]] double y1() const noexcept { return y1_; }
    void load(double x1, double y1) noexcept {
        x1_ = x1;
        y1_ = y1;
    }

    [[nodiscard]] double coefficient() const noexcept { return r_; }

private:
    double r_ = 0.0;
    double x1_ = 0.0;
    double y1_ = 0.0;
};

} // namespace cuicui::dsp
