#pragma once

#include <cmath>

namespace cuicui::model {

/// Naive (non-band-limited) sawtooth at the internal rate — aliasing tests only.
class TestSaw {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = sampleRate > 0.0 ? sampleRate : 192000.0;
        reset();
    }

    void reset() noexcept { phase_ = 0.0; }

    [[nodiscard]] float tick(double freqHz) noexcept {
        const float y = static_cast<float>(2.0 * phase_ - 1.0);
        phase_ += freqHz / rate_;
        phase_ -= std::floor(phase_);
        return y;
    }

private:
    double rate_ = 192000.0;
    double phase_ = 0.0;
};

} // namespace cuicui::model
