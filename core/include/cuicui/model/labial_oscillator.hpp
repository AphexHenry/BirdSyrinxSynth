#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cuicui::model {

/// Rest unfolding parameter (silent air-sac pressure). See 01_research.md §4.2–§4.3.
inline constexpr double kAlphaRest = -0.10;
/// Unfolding parameter at p = 1 (before per-side offset).
inline constexpr double kAlphaMax = 0.45;

[[nodiscard]] inline double alphaFromPressure(double pressure, double alphaOffset = 0.0) noexcept {
    return kAlphaRest + pressure * (kAlphaMax - kAlphaRest) + alphaOffset;
}

[[nodiscard]] inline double cubicResidual(double x, double alpha, double beta) noexcept {
    return ((x - 1.0) * x + beta) * x + alpha; // x^3 − x^2 + βx + α
}

/// Stable rest displacement: largest real root of x³ − x² + βx + α = 0 that is
/// attracting (tr J < 0 and det J > 0). If none, x = 0. See 01_research.md §4.3.1.
[[nodiscard]] inline double restDisplacement(double alpha, double beta) noexcept {
    const double A = -1.0;
    const double B = beta;
    const double C = alpha;
    constexpr double third = 1.0 / 3.0;
    const double Aoff = A * third;
    const double p = B - A * Aoff;
    const double q = C + (2.0 * A * A * A - 9.0 * A * B) / 27.0;
    const double p3 = p * third;
    const double halfq = 0.5 * q;
    const double disc = halfq * halfq + p3 * p3 * p3;

    double roots[3] = {0.0, 0.0, 0.0};
    int n = 0;
    auto push = [&](double z) {
        const double x = z - Aoff;
        if (!std::isfinite(x)) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            if (std::abs(roots[i] - x) < 1e-10) {
                return;
            }
        }
        if (n < 3) {
            roots[n++] = x;
        }
    };

    constexpr double eps = 1e-16;
    if (disc > eps) {
        const double sq = std::sqrt(disc);
        push(std::cbrt(-halfq + sq) + std::cbrt(-halfq - sq));
    } else if (p < 0.0) {
        const double r = std::sqrt(-p3);
        const double denom = r * r * r;
        const double arg = (denom > 1e-18) ? std::clamp(-halfq / denom, -1.0, 1.0) : 0.0;
        const double phi = std::acos(arg);
        constexpr double two_pi = 2.0 * std::numbers::pi;
        for (int k = 0; k < 3; ++k) {
            push(2.0 * r * std::cos((phi - two_pi * static_cast<double>(k)) / 3.0));
        }
    } else {
        const double r = std::cbrt(-halfq);
        push(2.0 * r);
        push(-r);
    }

    double best = 0.0;
    bool found = false;
    for (int i = 0; i < n; ++i) {
        double x = roots[i];
        for (int it = 0; it < 16; ++it) {
            const double f = cubicResidual(x, alpha, beta);
            const double fp = 3.0 * x * x - 2.0 * x + beta;
            if (std::abs(fp) < 1e-18) {
                break;
            }
            const double nx = x - f / fp;
            if (!std::isfinite(nx)) {
                break;
            }
            if (std::abs(nx - x) < 1e-15) {
                x = nx;
                break;
            }
            x = nx;
        }
        if (!std::isfinite(x) || std::abs(cubicResidual(x, alpha, beta)) > 1e-8) {
            continue;
        }
        // tr J = −γ x(1+x); γ > 0 ⇒ tr < 0 iff x(1+x) > 0.
        const bool trNeg = x * (1.0 + x) > 0.0;
        const bool detPos = (beta + 3.0 * x * x - 2.0 * x) > 0.0;
        if (trNeg && detPos && (!found || x > best)) {
            best = x;
            found = true;
        }
    }
    return found ? best : 0.0;
}

/// Takens–Bogdanov labial oscillator, this project's sign convention
/// (sound for α > 0, pitch rises with β). See 01_research.md §4.3.
/// RK4 at the internal rate; parameters sample-and-hold within a step.
class LabialOscillator {
public:
    void prepare(double internalRate) noexcept {
        const double fs = internalRate > 0.0 ? internalRate : 192000.0;
        h_ = 1.0 / fs;
        resetToRest(kAlphaRest, 0.3);
        resets_ = 0;
    }

    void reset(double x0, double y0) noexcept {
        x_ = x0;
        y_ = y0;
    }

    void resetToRest(double alpha, double beta) noexcept {
        x_ = restDisplacement(alpha, beta);
        y_ = 0.0;
    }

    void step(double alpha, double beta, double gamma) noexcept {
        const double h = h_;
        double k1x = 0.0, k1y = 0.0;
        double k2x = 0.0, k2y = 0.0;
        double k3x = 0.0, k3y = 0.0;
        double k4x = 0.0, k4y = 0.0;
        deriv(x_, y_, alpha, beta, gamma, k1x, k1y);
        deriv(x_ + 0.5 * h * k1x, y_ + 0.5 * h * k1y, alpha, beta, gamma, k2x, k2y);
        deriv(x_ + 0.5 * h * k2x, y_ + 0.5 * h * k2y, alpha, beta, gamma, k3x, k3y);
        deriv(x_ + h * k3x, y_ + h * k3y, alpha, beta, gamma, k4x, k4y);
        x_ += h * (k1x + 2.0 * k2x + 2.0 * k3x + k4x) / 6.0;
        y_ += h * (k1y + 2.0 * k2y + 2.0 * k3y + k4y) / 6.0;
        sanitize(alpha, beta);
    }

    void setState(double x, double y) noexcept {
        x_ = x;
        y_ = y;
    }

    void sanitize(double alpha, double beta) noexcept {
        if (!std::isfinite(x_) || !std::isfinite(y_) || std::abs(x_) > 10.0) {
            resetToRest(alpha, beta);
            ++resets_;
        }
    }

    /// ẋ = y; ẏ as in 01_research.md §4.3. Used by BilateralSource joint RK4.
    static void deriv(double x, double y, double alpha, double beta, double gamma, double& dx,
                      double& dy) noexcept {
        const double g = gamma;
        const double g2 = g * g;
        const double x2 = x * x;
        dx = y;
        dy = -alpha * g2 - beta * g2 * x - g2 * x2 * x - g * x2 * y + g2 * x2 - g * x * y;
    }

    [[nodiscard]] double x() const noexcept { return x_; }
    [[nodiscard]] double y() const noexcept { return y_; }
    [[nodiscard]] std::uint32_t nonFiniteResets() const noexcept { return resets_; }
    void setNonFiniteResets(std::uint32_t n) noexcept { resets_ = n; }
    [[nodiscard]] double stepSize() const noexcept { return h_; }

private:

    double h_ = 1.0 / 192000.0;
    double x_ = 0.0;
    double y_ = 0.0;
    std::uint32_t resets_ = 0;
};

} // namespace cuicui::model
