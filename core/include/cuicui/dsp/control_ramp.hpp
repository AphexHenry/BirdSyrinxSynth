#pragma once

namespace cuicui::dsp {

/// Linear interpolation of one scalar across `numInternalSamples` (no allocation).
/// Sample 0 is `from`; sample `n` would be `to` (the last emitted sample is just before `to`).
class ControlRamp {
public:
    void set(float from, float to, int numInternalSamples) noexcept {
        from_ = from;
        to_ = to;
        n_ = numInternalSamples > 0 ? numInternalSamples : 1;
        i_ = 0;
        inv_n_ = 1.0f / static_cast<float>(n_);
    }

    [[nodiscard]] float next() noexcept {
        const float u = static_cast<float>(i_) * inv_n_;
        ++i_;
        return from_ + u * (to_ - from_);
    }

    [[nodiscard]] float at(int i) const noexcept {
        const float u = static_cast<float>(i) * inv_n_;
        return from_ + u * (to_ - from_);
    }

    [[nodiscard]] int numSamples() const noexcept { return n_; }

private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    float inv_n_ = 1.0f;
    int n_ = 1;
    int i_ = 0;
};

} // namespace cuicui::dsp
