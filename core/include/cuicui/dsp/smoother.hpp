#pragma once

#include <algorithm>
#include <cmath>

namespace cuicui::dsp {

/// One-pole parameter smoother: y[n] = y[n-1] + (1 - e^{-1/(τ f)}) (x - y[n-1]).
/// `next()` must be called at a constant rate `f` (usually the host sample rate).
/// τ = 0 snaps immediately. Real-time safe after `prepare()`.
class Smoother {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoeff();
    }

    void setTimeConstantMs(double ms) noexcept {
        tau_s_ = std::max(0.0, ms) * 0.001;
        updateCoeff();
    }

    void setTarget(float x) noexcept { target_ = x; }

    void snap(float x) noexcept { y_ = target_ = x; }

    void snap() noexcept { y_ = target_; }

    [[nodiscard]] float next() noexcept {
        y_ += coeff_ * (target_ - y_);
        return y_;
    }

    [[nodiscard]] float value() const noexcept { return y_; }

    [[nodiscard]] double timeConstantS() const noexcept { return tau_s_; }

private:
    void updateCoeff() noexcept {
        if (tau_s_ <= 0.0) {
            coeff_ = 1.0f;
            return;
        }
        coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (tau_s_ * rate_)));
    }

    double rate_ = 48000.0;
    double tau_s_ = 0.0;
    float coeff_ = 1.0f;
    float y_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace cuicui::dsp
