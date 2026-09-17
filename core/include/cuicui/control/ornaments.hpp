#pragma once

#include "cuicui/dsp/rng.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cuicui::control {

/// Scoop, FM/trill, and release fall. Applied in cents before InverseMap::solve.
/// See 01_research.md §7.6 and WP-C3 §4.4. Per-note jitter ±10 % of rates/depths.
class Ornaments {
public:
    void reset() noexcept {
        t_ = 0.0;
        tFall_ = 0.0;
        falling_ = false;
        scooping_ = false;
        phase_ = 0.0;
        sqSm_ = 0.0;
        aScoop_ = 0.0;
        tauScoop_ = 0.02;
        delayFm_ = 0.08;
        rateFm_ = 15.0;
        depthFm_ = 0.0;
        aFall_ = 0.0;
        releaseS_ = 0.015;
        morph_ = 0.0;
    }

    /// Non-legato note start. `seed` is derived from engine.seed + note index.
    void noteOn(double motion, double range, double speed, double releaseMs, std::uint64_t seed,
                bool scoop) noexcept {
        dsp::Rng rng(seed == 0 ? 1ULL : seed);
        auto jitter = [&](double x) noexcept {
            return x * (0.9 + 0.2 * static_cast<double>(rng.uniform()));
        };
        const double mot = std::clamp(motion, 0.0, 1.0);
        const double rngAmt = std::max(0.0, range);
        const double spd = std::max(1.0e-3, speed);
        t_ = 0.0;
        tFall_ = 0.0;
        falling_ = false;
        scooping_ = scoop;
        phase_ = 0.0;
        sqSm_ = 0.0;
        aScoop_ = scoop ? jitter(300.0 * rngAmt * mot) : 0.0;
        tauScoop_ = jitter((0.015 + 0.025 * (1.0 - mot)) / spd);
        tauScoop_ = std::max(0.001, tauScoop_);
        delayFm_ = jitter(0.080 / spd);
        rateFm_ = jitter((15.0 + 20.0 * mot) * spd);
        depthFm_ = jitter(150.0 * mot * mot * rngAmt);
        aFall_ = jitter(200.0 * rngAmt * mot);
        releaseS_ = std::max(0.001, releaseMs * 0.001);
        morph_ = std::clamp(2.0 * (mot - 0.5), 0.0, 1.0);
    }

    void noteOff() noexcept {
        falling_ = true;
        tFall_ = 0.0;
    }

    /// Sum of scoop + FM + fall, in cents.
    [[nodiscard]] double next(double dt) noexcept {
        dt = std::max(dt, 0.0);
        t_ += dt;
        double o = 0.0;
        if (scooping_ && aScoop_ != 0.0 && tauScoop_ > 0.0) {
            o += -aScoop_ * std::exp(-t_ / tauScoop_);
        }
        if (depthFm_ > 0.0 && t_ > delayFm_) {
            const double tFade = t_ - delayFm_;
            const double fade = std::clamp(tFade / 0.050, 0.0, 1.0);
            phase_ += 2.0 * std::numbers::pi * rateFm_ * dt;
            if (phase_ > 2.0 * std::numbers::pi) {
                phase_ -= 2.0 * std::numbers::pi * std::floor(phase_ / (2.0 * std::numbers::pi));
            }
            const double sine = std::sin(phase_);
            const double sq = sine >= 0.0 ? 1.0 : -1.0;
            const double a = (dt <= 0.0) ? 1.0 : std::exp(-dt / 0.001);
            sqSm_ = sq + (sqSm_ - sq) * a;
            const double wave = (1.0 - morph_) * sine + morph_ * sqSm_;
            o += fade * depthFm_ * wave;
        }
        if (falling_) {
            tFall_ += dt;
            const double u = std::clamp(tFall_ / releaseS_, 0.0, 1.0);
            o += -aFall_ * u;
        }
        return o;
    }

private:
    double t_ = 0.0;
    double tFall_ = 0.0;
    bool falling_ = false;
    bool scooping_ = false;
    double phase_ = 0.0;
    double sqSm_ = 0.0;
    double aScoop_ = 0.0;
    double tauScoop_ = 0.02;
    double delayFm_ = 0.08;
    double rateFm_ = 15.0;
    double depthFm_ = 0.0;
    double aFall_ = 0.0;
    double releaseS_ = 0.015;
    double morph_ = 0.0;
};

} // namespace cuicui::control
