#pragma once

#include "cuicui/control/gesture_source.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cuicui::control {

/// Gesture curves on u ∈ [0, 1]. See 01_research.md §7.7 and WP-C4 §4.1.
struct GestureCurves {
    static constexpr int kN = 64;
    std::array<double, static_cast<std::size_t>(kN)> pitchCents{};
    std::array<double, static_cast<std::size_t>(kN)> ampDb{};
    std::array<double, static_cast<std::size_t>(kN)> tone{};
    std::array<double, static_cast<std::size_t>(kN)> side{};
};

struct GestureHints {
    double oecRatio = std::numeric_limits<double>::quiet_NaN();
    double gape = std::numeric_limits<double>::quiet_NaN();
    double secondVoiceCents = std::numeric_limits<double>::quiet_NaN();
};

struct GestureInstance {
    double tStart = 0.0;       // seconds
    double durationMs = 100.0; // milliseconds
    double anchorCents = 0.0;  // absolute cents (0 = MIDI note 0)
    GestureCurves curves;
    GestureHints hints;
};

/// Uniform-grid Fritsch–Carlson (PCHIP) slopes. `m[i]` is dy/du. Real-time safe.
inline void pchipSlopes(const double* y, double* m, int n) noexcept {
    if (n <= 0) {
        return;
    }
    if (n == 1) {
        m[0] = 0.0;
        return;
    }
    const double h = 1.0 / static_cast<double>(n - 1);
    const int nm1 = n - 1;
    double dlt[GestureCurves::kN]; // N is 64; callers never pass more.
    const int nd = std::min(nm1, GestureCurves::kN);
    for (int i = 0; i < nd; ++i) {
        dlt[i] = (y[i + 1] - y[i]) / h;
    }
    m[0] = dlt[0];
    m[nm1] = dlt[nm1 - 1];
    for (int i = 1; i < nm1; ++i) {
        if (dlt[i - 1] * dlt[i] <= 0.0) {
            m[i] = 0.0;
        } else {
            m[i] = 2.0 / (1.0 / dlt[i - 1] + 1.0 / dlt[i]);
        }
    }
    auto limitEnd = [](double& d, double delta) noexcept {
        if (delta == 0.0 || d * delta < 0.0) {
            d = 0.0;
        } else if (std::abs(d) > 3.0 * std::abs(delta)) {
            d = 3.0 * delta;
        }
    };
    limitEnd(m[0], dlt[0]);
    limitEnd(m[nm1], dlt[nm1 - 1]);
}

[[nodiscard]] inline double hermiteEval(const double* y, const double* m, int n, double u) noexcept {
    if (n <= 0) {
        return 0.0;
    }
    if (n == 1) {
        return y[0];
    }
    u = std::clamp(u, 0.0, 1.0);
    const double last = static_cast<double>(n - 1);
    const double idx = u * last;
    int i0 = static_cast<int>(idx);
    if (i0 >= n - 1) {
        return y[n - 1];
    }
    const double t = idx - static_cast<double>(i0);
    const double h = 1.0 / last;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    return h00 * y[i0] + h10 * h * m[i0] + h01 * y[i0 + 1] + h11 * h * m[i0 + 1];
}

[[nodiscard]] inline double linearEval(const double* y, int n, double u) noexcept {
    if (n <= 0) {
        return 0.0;
    }
    if (n == 1) {
        return y[0];
    }
    u = std::clamp(u, 0.0, 1.0);
    const double last = static_cast<double>(n - 1);
    const double idx = u * last;
    int i0 = static_cast<int>(idx);
    if (i0 >= n - 1) {
        return y[n - 1];
    }
    const double t = idx - static_cast<double>(i0);
    return y[i0] + t * (y[i0 + 1] - y[i0]);
}

struct EvaluatedCurves {
    double pitchCents = 0.0;
    double ampDb = 0.0;
    double tone = 0.5;
    double side = -1.0;
};

/// Cubic Hermite (monotone) on pitch/amp/tone; linear on side (keeps steps sharp).
[[nodiscard]] inline EvaluatedCurves evaluateCurves(const GestureCurves& c, double u) noexcept {
    constexpr int n = GestureCurves::kN;
    double mP[n], mA[n], mT[n];
    pchipSlopes(c.pitchCents.data(), mP, n);
    pchipSlopes(c.ampDb.data(), mA, n);
    pchipSlopes(c.tone.data(), mT, n);
    EvaluatedCurves e;
    e.pitchCents = hermiteEval(c.pitchCents.data(), mP, n, u);
    e.ampDb = hermiteEval(c.ampDb.data(), mA, n, u);
    e.tone = std::clamp(hermiteEval(c.tone.data(), mT, n, u), 0.0, 1.0);
    e.side = std::clamp(linearEval(c.side.data(), n, u), -1.0, 1.0);
    return e;
}

[[nodiscard]] inline double gestureU(const GestureInstance& g, double tSeconds) noexcept {
    const double dur = g.durationMs * 0.001;
    if (!(dur > 0.0)) {
        return 0.0;
    }
    return (tSeconds - g.tStart) / dur;
}

[[nodiscard]] inline bool gestureCovers(const GestureInstance& g, double tSeconds) noexcept {
    const double dur = g.durationMs * 0.001;
    if (!(dur > 0.0)) {
        return false;
    }
    return tSeconds >= g.tStart && tSeconds < g.tStart + dur;
}

/// Absolute pitch (anchor + relative cents) and the four channels at time t.
[[nodiscard]] inline GestureSample evaluateInstance(const GestureInstance& g, double tSeconds) noexcept {
    GestureSample s;
    if (!gestureCovers(g, tSeconds)) {
        return s;
    }
    const auto e = evaluateCurves(g.curves, gestureU(g, tSeconds));
    s.active = true;
    s.pitchCents = g.anchorCents + e.pitchCents;
    s.ampDb = e.ampDb;
    s.tone = e.tone;
    s.side = e.side;
    s.oecRatio = g.hints.oecRatio;
    s.gape = g.hints.gape;
    s.secondVoiceCents = g.hints.secondVoiceCents;
    return s;
}

[[nodiscard]] inline double hzFromCents(double cents) noexcept {
    return 440.0 * std::pow(2.0, (cents - 6900.0) / 1200.0);
}

[[nodiscard]] inline double centsFromHz(double hz) noexcept {
    if (!(hz > 0.0)) {
        return 0.0;
    }
    return 6900.0 + 1200.0 * std::log2(hz / 440.0);
}

/// RMS of dP/du (cents per unit u). Used by C5 to rank “movement”.
[[nodiscard]] inline double movementScore(const GestureCurves& c) noexcept {
    constexpr int n = GestureCurves::kN;
    const double last = static_cast<double>(n - 1);
    double acc = 0.0;
    for (int i = 0; i < n - 1; ++i) {
        const double d = (c.pitchCents[static_cast<std::size_t>(i + 1)] -
                          c.pitchCents[static_cast<std::size_t>(i)]) *
                         last;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(n - 1));
}

} // namespace cuicui::control
