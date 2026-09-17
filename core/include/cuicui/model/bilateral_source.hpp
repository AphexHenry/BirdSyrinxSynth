#pragma once

#include "cuicui/dsp/dc_blocker.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/noise_sources.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cuicui::model {

/// Gate closure subtracts this from α (01_research.md §4.4).
inline constexpr double kDeltaAlphaClose = 0.6;
inline constexpr double kGateMixFloor = 1.0e-3;

struct BilateralFrame {
    double alphaBase = 0.0;
    double alphaOffL = 0.0;
    double alphaOffR = 0.0;
    double betaL = 0.3;
    double betaR = 0.3;
    double gateL = 1.0;
    double gateR = 0.0;
    double gamma = 24000.0;
    double coupling = 0.0; // ε
    double sigmaBeta = 0.0;
};

struct BilateralOut {
    double mixTilde = 0.0; // g_L x̃_L + g_R x̃_R
    double xBar = 0.0;
    double xTildeL = 0.0;
    double xTildeR = 0.0;
    double alphaL = 0.0;
    double alphaR = 0.0;
    double betaEffL = 0.0;
    double betaEffR = 0.0;
    double gateL = 1.0;
    double gateR = 0.0;
};

/// Two labial oscillators, per-side gating, optional coupling. 01_research.md §4.4.
class BilateralSource {
public:
    void prepare(double internalRate) noexcept {
        left_.prepare(internalRate);
        right_.prepare(internalRate);
        dcL_.prepare(internalRate, 20.0);
        dcR_.prepare(internalRate, 20.0);
        tensionL_.prepare(internalRate);
        tensionR_.prepare(internalRate);
        h_ = left_.stepSize();
    }

    void setBandwidthHz(double hz) noexcept {
        tensionL_.setBandwidthHz(hz);
        tensionR_.setBandwidthHz(hz);
    }

    void resetNoise(std::uint64_t seed) noexcept {
        tensionL_.reset(dsp::deriveStreamSeed(seed, dsp::kStreamTensionL));
        tensionR_.reset(dsp::deriveStreamSeed(seed, dsp::kStreamTensionR));
    }

    void resetToRest(double alphaL, double betaL, double alphaR, double betaR) noexcept {
        left_.resetToRest(alphaL, betaL);
        right_.resetToRest(alphaR, betaR);
        dcL_.prime(left_.x());
        dcR_.prime(right_.x());
    }

    /// Independent RK4 on each side (ε is ignored).
    void stepIndependent(double alphaL, double betaL, double alphaR, double betaR, double gamma) noexcept {
        left_.step(alphaL, betaL, gamma);
        right_.step(alphaR, betaR, gamma);
    }

    /// One joint RK4 of the 4-state system. Coupling uses the other side's stage values.
    /// When ε = 0 this is bit-identical to `stepIndependent` (coupling add is skipped).
    void stepJoint(double alphaL, double betaL, double alphaR, double betaR, double gamma,
                   double eps) noexcept {
        const double h = h_;
        const double xL0 = left_.x();
        const double yL0 = left_.y();
        const double xR0 = right_.x();
        const double yR0 = right_.y();

        double k1xL = 0.0, k1yL = 0.0, k1xR = 0.0, k1yR = 0.0;
        derivCoupled(xL0, yL0, alphaL, betaL, gamma, xR0, eps, k1xL, k1yL);
        derivCoupled(xR0, yR0, alphaR, betaR, gamma, xL0, eps, k1xR, k1yR);

        double k2xL = 0.0, k2yL = 0.0, k2xR = 0.0, k2yR = 0.0;
        derivCoupled(xL0 + 0.5 * h * k1xL, yL0 + 0.5 * h * k1yL, alphaL, betaL, gamma,
                     xR0 + 0.5 * h * k1xR, eps, k2xL, k2yL);
        derivCoupled(xR0 + 0.5 * h * k1xR, yR0 + 0.5 * h * k1yR, alphaR, betaR, gamma,
                     xL0 + 0.5 * h * k1xL, eps, k2xR, k2yR);

        double k3xL = 0.0, k3yL = 0.0, k3xR = 0.0, k3yR = 0.0;
        derivCoupled(xL0 + 0.5 * h * k2xL, yL0 + 0.5 * h * k2yL, alphaL, betaL, gamma,
                     xR0 + 0.5 * h * k2xR, eps, k3xL, k3yL);
        derivCoupled(xR0 + 0.5 * h * k2xR, yR0 + 0.5 * h * k2yR, alphaR, betaR, gamma,
                     xL0 + 0.5 * h * k2xL, eps, k3xR, k3yR);

        double k4xL = 0.0, k4yL = 0.0, k4xR = 0.0, k4yR = 0.0;
        derivCoupled(xL0 + h * k3xL, yL0 + h * k3yL, alphaL, betaL, gamma, xR0 + h * k3xR, eps, k4xL, k4yL);
        derivCoupled(xR0 + h * k3xR, yR0 + h * k3yR, alphaR, betaR, gamma, xL0 + h * k3xL, eps, k4xR, k4yR);

        left_.setState(xL0 + h * (k1xL + 2.0 * k2xL + 2.0 * k3xL + k4xL) / 6.0,
                       yL0 + h * (k1yL + 2.0 * k2yL + 2.0 * k3yL + k4yL) / 6.0);
        right_.setState(xR0 + h * (k1xR + 2.0 * k2xR + 2.0 * k3xR + k4xR) / 6.0,
                        yR0 + h * (k1yR + 2.0 * k2yR + 2.0 * k3yR + k4yR) / 6.0);
        left_.sanitize(alphaL, betaL);
        right_.sanitize(alphaR, betaR);
    }

    void step(double alphaL, double betaL, double alphaR, double betaR, double gamma, double eps) noexcept {
        if (eps == 0.0) {
            stepIndependent(alphaL, betaL, alphaR, betaR, gamma);
        } else {
            stepJoint(alphaL, betaL, alphaR, betaR, gamma, eps);
        }
    }

    [[nodiscard]] BilateralOut process(const BilateralFrame& in) noexcept {
        const double gL = std::clamp(in.gateL, 0.0, 1.0);
        const double gR = std::clamp(in.gateR, 0.0, 1.0);
        const double aL = in.alphaBase + in.alphaOffL - (1.0 - gL) * kDeltaAlphaClose;
        const double aR = in.alphaBase + in.alphaOffR - (1.0 - gR) * kDeltaAlphaClose;
        const double etaL = tensionL_.process();
        const double etaR = tensionR_.process();
        const double bL = clampBetaEff(in.betaL + in.sigmaBeta * etaL);
        const double bR = clampBetaEff(in.betaR + in.sigmaBeta * etaR);
        const double gamma = in.gamma > 0.0 ? in.gamma : 24000.0;
        const double eps = std::max(0.0, in.coupling);
        step(aL, bL, aR, bR, gamma, eps);

        BilateralOut o;
        o.xTildeL = dcL_.tick(left_.x());
        o.xTildeR = dcR_.tick(right_.x());
        o.gateL = gL;
        o.gateR = gR;
        o.mixTilde = gL * o.xTildeL + gR * o.xTildeR;
        const double gSum = gL + gR;
        o.xBar = o.mixTilde / std::max(gSum, kGateMixFloor);
        o.alphaL = aL;
        o.alphaR = aR;
        o.betaEffL = bL;
        o.betaEffR = bR;
        return o;
    }

    [[nodiscard]] LabialOscillator& left() noexcept { return left_; }
    [[nodiscard]] LabialOscillator& right() noexcept { return right_; }
    [[nodiscard]] const LabialOscillator& left() const noexcept { return left_; }
    [[nodiscard]] const LabialOscillator& right() const noexcept { return right_; }
    [[nodiscard]] std::uint32_t nonFiniteResets() const noexcept {
        return left_.nonFiniteResets() + right_.nonFiniteResets();
    }

private:
    static void derivCoupled(double x, double y, double alpha, double beta, double gamma, double xOther,
                             double eps, double& dx, double& dy) noexcept {
        LabialOscillator::deriv(x, y, alpha, beta, gamma, dx, dy);
        if (eps != 0.0) {
            dy += eps * gamma * gamma * (xOther - x);
        }
    }

    LabialOscillator left_;
    LabialOscillator right_;
    dsp::DcBlocker dcL_;
    dsp::DcBlocker dcR_;
    TensionNoise tensionL_;
    TensionNoise tensionR_;
    double h_ = 1.0 / 192000.0;
};

} // namespace cuicui::model
