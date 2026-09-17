#pragma once

#include "cuicui/dsp/biquad.hpp"
#include "cuicui/dsp/one_pole.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/model/body_scaling.hpp"
#include "cuicui/model/voice_state.hpp"

#include <algorithm>
#include <cmath>

namespace cuicui::model {

/// Unit-variance low-passed Gaussian process on β. 01_research.md §4.5.
class TensionNoise {
public:
    void prepare(double internalRate) noexcept {
        lp_.prepare(internalRate);
        setBandwidthHz(bandwidth_hz_);
        rng_.setSeed(1);
        lp_.reset();
    }

    void setBandwidthHz(double freqHz) noexcept {
        bandwidth_hz_ = std::max(1.0, freqHz);
        lp_.setCutoffHz(bandwidth_hz_);
        const double a = lp_.coefficient();
        const double denom = std::max(1.0 - a, 1.0e-12);
        in_scale_ = std::sqrt((1.0 + a) / denom);
    }

    void reset(std::uint64_t seed) noexcept {
        rng_.setSeed(seed);
        lp_.reset();
    }

    /// Coloured unit-variance η. Call once per internal sample.
    [[nodiscard]] double process() noexcept {
        return lp_.tick(in_scale_ * static_cast<double>(rng_.gaussian()));
    }

    [[nodiscard]] double bandwidthHz() const noexcept { return bandwidth_hz_; }

    void save(OnePoleState& lp, RngState& st) const noexcept {
        lp.y = lp_.value();
        std::uint32_t s[4];
        rng_.save(s, st.spare, st.has_spare);
        st.s0 = s[0];
        st.s1 = s[1];
        st.s2 = s[2];
        st.s3 = s[3];
    }

    void load(const OnePoleState& lp, const RngState& st) noexcept {
        lp_.load(lp.y);
        const std::uint32_t s[4] = {st.s0, st.s1, st.s2, st.s3};
        rng_.load(s, st.spare, st.has_spare);
    }

private:
    dsp::OnePole lp_;
    dsp::Rng rng_{1};
    double bandwidth_hz_ = 300.0;
    double in_scale_ = 1.0;
};

/// Flow-modulated aspiration / turbulence. Independent stream. 01_research.md §4.5.
class AspirationNoise {
public:
    void prepare(double internalRate) noexcept {
        bp_.prepare(internalRate);
        setCentreHz(kAspirationCentreHz);
        rng_.setSeed(1);
        bp_.reset();
    }

    void setCentreHz(double freqHz) noexcept { bp_.setBandPass(freqHz, kAspirationQ); }

    void reset(std::uint64_t seed) noexcept {
        rng_.setSeed(seed);
        bp_.reset();
    }

    /// n_asp = a_asp · G · (0.5 + 0.5 tanh(4 x̄)) · BP{w}. Always advances the RNG.
    [[nodiscard]] double process(double G, double xBar, double aAsp) noexcept {
        const double w = static_cast<double>(rng_.gaussian());
        const double bp = bp_.tick(w);
        const double gated = 0.5 + 0.5 * std::tanh(4.0 * xBar);
        return aAsp * G * gated * bp;
    }

    void save(BiquadState& bp, RngState& st) const noexcept {
        bp_.save(bp.b0, bp.b1, bp.b2, bp.a1, bp.a2, bp.z1, bp.z2);
        std::uint32_t s[4];
        rng_.save(s, st.spare, st.has_spare);
        st.s0 = s[0];
        st.s1 = s[1];
        st.s2 = s[2];
        st.s3 = s[3];
    }

    void load(const BiquadState& bp, const RngState& st) noexcept {
        bp_.load(bp.b0, bp.b1, bp.b2, bp.a1, bp.a2, bp.z1, bp.z2);
        const std::uint32_t s[4] = {st.s0, st.s1, st.s2, st.s3};
        rng_.load(s, st.spare, st.has_spare);
    }

private:
    dsp::Biquad bp_;
    dsp::Rng rng_{1};
};

[[nodiscard]] inline double clampBetaEff(double betaEff) noexcept {
    return std::clamp(betaEff, -0.5, 1.5);
}

} // namespace cuicui::model
