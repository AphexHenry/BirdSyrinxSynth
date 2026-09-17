#pragma once

#include "cuicui/dsp/lagrange.hpp"
#include "cuicui/dsp/one_pole.hpp"
#include "cuicui/model/voice_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cuicui::model {

/// Speed of sound in the trachea (m/s). 01_research.md §4.6.
inline constexpr double kSoundSpeed = 350.0;
/// Registry max length × body.size max, so B6 can scale up to 4 without realloc.
inline constexpr double kTracheaMaxLengthMm = 120.0 * 4.0;
inline constexpr double kMinDelaySamples = 3.0;

/// Loss ℓ ∈ [0, 1] → one-pole cutoff. ℓ = 0 is lossless (bypass); otherwise
/// f_c = 22000 · (3000/22000)^ℓ. Bypass at 0 because a 22 kHz pole at 192 kHz
/// is not transparent enough for the analytic-magnitude tests (0.3 dB).
[[nodiscard]] inline double tracheaLossCutoffHz(double loss) noexcept {
    loss = std::clamp(loss, 0.0, 1.0);
    if (loss <= 0.0) {
        return 0.0;
    }
    return 22000.0 * std::pow(3000.0 / 22000.0, loss);
}

[[nodiscard]] inline int tracheaBufferSize(double internalRate, double maxLengthMm) noexcept {
    const double rate = internalRate > 0.0 ? internalRate : 192000.0;
    const double lMax = std::max(maxLengthMm, 10.0) * 0.001;
    const double maxD = 2.0 * lMax / kSoundSpeed * rate;
    const int n = static_cast<int>(std::ceil(maxD)) + 4;
    return std::max(n, 8);
}

/// Delayed-feedback tracheal waveguide. 01_research.md §4.6 / WP-B2.
/// p_i[n] = A s[n] − r LP{p_i}[n − D],  p_t[n] = (1 − r) p_i[n − D/2].
/// D = 2L/c · f_int (fractional). No allocation in `process()`.
class Trachea {
public:
    void prepare(double internalRate, double maxLengthMm = kTracheaMaxLengthMm) {
        rate_ = internalRate > 0.0 ? internalRate : 192000.0;
        max_length_mm_ = maxLengthMm > 0.0 ? maxLengthMm : kTracheaMaxLengthMm;
        const int n = tracheaBufferSize(rate_, max_length_mm_);
        buf_.assign(static_cast<std::size_t>(n), 0.0);
        size_ = n;
        lp_.prepare(rate_);
        setParams(length_mm_, reflection_, loss_, input_gain_);
        reset();
    }

    void reset() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0);
        write_ = 0;
        lp_.reset();
        last_out_ = 0.0;
    }

    /// Fill the delay with the DC steady state of a constant source (no startup step).
    void prime(double sourceSteady) noexcept {
        setParams(length_mm_, reflection_, loss_, input_gain_);
        const double den = 1.0 + reflection_;
        const double pi = (std::abs(den) > 1e-9) ? (input_gain_ * sourceSteady / den) : 0.0;
        std::fill(buf_.begin(), buf_.end(), pi);
        lp_.prime(pi);
        write_ = 0;
        last_out_ = (1.0 - reflection_) * pi;
    }

    /// Immediate (no extra smoothing — the voice already smooths). Clamps to registry-ish ranges.
    void setParams(double lengthMm, double reflection, double loss, double inputGain) noexcept {
        length_mm_ = std::clamp(lengthMm, 1.0, max_length_mm_);
        reflection_ = std::clamp(reflection, -0.95, 0.95);
        loss_ = std::clamp(loss, 0.0, 1.0);
        input_gain_ = std::clamp(inputGain, 0.0, 4.0);
        const double L = length_mm_ * 0.001;
        delay_ = 2.0 * L / kSoundSpeed * rate_;
        if (delay_ < kMinDelaySamples) {
            delay_ = kMinDelaySamples;
        }
        const double maxD = static_cast<double>(size_ - 4);
        if (delay_ > maxD) {
            delay_ = maxD;
        }
        lp_.setCutoffHz(tracheaLossCutoffHz(loss_));
    }

    [[nodiscard]] double process(double source) noexcept {
        const double pi_d = dsp::lagrange3Read(buf_.data(), size_, write_, delay_);
        const double pi_d2 = dsp::lagrange3Read(buf_.data(), size_, write_, 0.5 * delay_);
        const double fb = lp_.tick(pi_d);
        const double pi = input_gain_ * source - reflection_ * fb;
        buf_[static_cast<unsigned>(write_)] = pi;
        write_ = dsp::wrapIndex(write_ + 1, size_);
        last_out_ = (1.0 - reflection_) * pi_d2;
        return last_out_;
    }

    [[nodiscard]] double lastOutput() const noexcept { return last_out_; }
    [[nodiscard]] double delaySamples() const noexcept { return delay_; }
    [[nodiscard]] int bufferSize() const noexcept { return size_; }
    [[nodiscard]] double lengthMm() const noexcept { return length_mm_; }
    [[nodiscard]] double reflection() const noexcept { return reflection_; }

    void saveDelay(double* dst, int cap, std::int32_t& write, std::int32_t& size,
                   OnePoleState& lp, double& lastOut) const noexcept {
        size = size_;
        write = write_;
        lastOut = last_out_;
        lp.y = lp_.value();
        const int n = std::min(size_, cap);
        if (dst != nullptr && n > 0) {
            std::copy(buf_.begin(), buf_.begin() + n, dst);
        }
    }

    void loadDelay(const double* src, int cap, std::int32_t write, std::int32_t /*size*/,
                   const OnePoleState& lp, double lastOut) noexcept {
        const int n = std::min(size_, cap);
        if (src != nullptr && n > 0) {
            std::copy(src, src + n, buf_.begin());
        }
        write_ = wrapWrite(write);
        lp_.load(lp.y);
        last_out_ = lastOut;
    }

private:
    [[nodiscard]] int wrapWrite(int w) const noexcept {
        if (size_ <= 0) {
            return 0;
        }
        w %= size_;
        if (w < 0) {
            w += size_;
        }
        return w;
    }
    std::vector<double> buf_{};
    dsp::OnePole lp_{};
    double rate_ = 192000.0;
    double max_length_mm_ = kTracheaMaxLengthMm;
    double length_mm_ = 33.0;
    double reflection_ = 0.5;
    double loss_ = 0.3;
    double input_gain_ = 1.0;
    double delay_ = kMinDelaySamples;
    double last_out_ = 0.0;
    int size_ = 0;
    int write_ = 0;
};

} // namespace cuicui::model
