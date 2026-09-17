#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cuicui::control {

struct SideTargets {
    double betaL = 0.3;
    double betaR = 0.3;
    double gateL = 1.0;
    double gateR = 0.0;
};

/// Equal-power gate crossfade between syrinx sides. No audio; C3 will drive it.
/// Inactive-side β always follows the value the caller provides (pre-tuning).
class SideSwitcher {
public:
    void prepare(double controlRate) noexcept {
        rate_ = controlRate > 0.0 ? controlRate : 1000.0;
        updateDu();
        reset();
    }

    void reset() noexcept {
        active_ = 0;
        from_ = 0;
        to_ = 0;
        u_ = 1.0;
        fading_ = false;
    }

    void setCrossfadeMs(double ms) noexcept {
        ms_ = std::max(0.0, ms);
        updateDu();
    }

    /// 0 = L, 1 = R. Starts (or reverses) an equal-power crossfade.
    void setActive(int side) noexcept {
        const int s = side != 0 ? 1 : 0;
        if (!fading_ && s == active_) {
            return;
        }
        if (fading_ && s == to_) {
            return;
        }
        if (fading_) {
            u_ = 1.0 - u_;
            from_ = to_;
            to_ = s;
            fading_ = true;
            return;
        }
        from_ = active_;
        to_ = s;
        u_ = 0.0;
        fading_ = true;
    }

    [[nodiscard]] SideTargets next(double betaForPitchL, double betaForPitchR) noexcept {
        SideTargets t;
        t.betaL = betaForPitchL;
        t.betaR = betaForPitchR;
        if (!fading_) {
            t.gateL = active_ == 0 ? 1.0 : 0.0;
            t.gateR = active_ == 1 ? 1.0 : 0.0;
            return t;
        }
        u_ += du_;
        if (u_ >= 1.0) {
            u_ = 1.0;
            fading_ = false;
            active_ = to_;
        }
        const double gNew = std::sin(0.5 * std::numbers::pi * u_);
        const double gOld = std::cos(0.5 * std::numbers::pi * u_);
        if (to_ == 1) {
            t.gateR = gNew;
            t.gateL = gOld;
        } else {
            t.gateL = gNew;
            t.gateR = gOld;
        }
        return t;
    }

    [[nodiscard]] int activeSide() const noexcept { return fading_ ? to_ : active_; }
    [[nodiscard]] bool fading() const noexcept { return fading_; }
    [[nodiscard]] double crossfadeMs() const noexcept { return ms_; }

private:
    void updateDu() noexcept {
        const double n = std::max(1.0, std::round(ms_ * 0.001 * rate_));
        du_ = 1.0 / n;
    }

    double rate_ = 1000.0;
    double ms_ = 4.0;
    double du_ = 0.25;
    double u_ = 1.0;
    int active_ = 0;
    int from_ = 0;
    int to_ = 0;
    bool fading_ = false;
};

} // namespace cuicui::control
