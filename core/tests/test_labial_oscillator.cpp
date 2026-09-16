#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kGamma = 24000.0;
constexpr double kRate = 192000.0;

double mean(const std::vector<double>& x, int i0, int i1) {
    double s = 0.0;
    const int n = std::max(1, i1 - i0);
    for (int i = i0; i < i1; ++i) {
        s += x[static_cast<std::size_t>(i)];
    }
    return s / static_cast<double>(n);
}

double peakToPeak(const std::vector<double>& x, int i0, int i1) {
    double lo = x[static_cast<std::size_t>(i0)];
    double hi = lo;
    for (int i = i0; i < i1; ++i) {
        lo = std::min(lo, x[static_cast<std::size_t>(i)]);
        hi = std::max(hi, x[static_cast<std::size_t>(i)]);
    }
    return hi - lo;
}

double zc_freq(const std::vector<double>& x, double sr, int i0, int i1) {
    const double mu = mean(x, i0, i1);
    std::vector<double> zeros;
    zeros.reserve(static_cast<std::size_t>(i1 - i0) / 8);
    for (int i = i0 + 1; i < i1; ++i) {
        const double y0 = x[static_cast<std::size_t>(i - 1)] - mu;
        const double y1 = x[static_cast<std::size_t>(i)] - mu;
        if (y0 <= 0.0 && y1 > 0.0) {
            const double frac = y0 / (y0 - y1);
            zeros.push_back((static_cast<double>(i - 1) + frac) / sr);
        }
    }
    if (zeros.size() < 2) {
        return 0.0;
    }
    return static_cast<double>(zeros.size() - 1) / (zeros.back() - zeros.front());
}

int rising_zc_count(const std::vector<double>& x, double mu, int i0, int i1) {
    int n = 0;
    for (int i = i0 + 1; i < i1; ++i) {
        const double y0 = x[static_cast<std::size_t>(i - 1)] - mu;
        const double y1 = x[static_cast<std::size_t>(i)] - mu;
        if (y0 <= 0.0 && y1 > 0.0) {
            ++n;
        }
    }
    return n;
}

std::vector<double> run_osc(double alpha, double beta, double gamma, double rate, double tSettle,
                            double tMeas, double restAlpha = cuicui::model::kAlphaRest) {
    cuicui::model::LabialOscillator osc;
    osc.prepare(rate);
    osc.resetToRest(restAlpha, beta);
    const int nSettle = static_cast<int>(std::llround(tSettle * rate));
    const int nMeas = static_cast<int>(std::llround(tMeas * rate));
    std::vector<double> x(static_cast<std::size_t>(nSettle + nMeas), 0.0);
    for (int i = 0; i < nSettle + nMeas; ++i) {
        osc.step(alpha, beta, gamma);
        x[static_cast<std::size_t>(i)] = osc.x();
    }
    return x;
}

double measured_f0(double alpha, double beta, double gamma, double rate) {
    const auto x = run_osc(alpha, beta, gamma, rate, 0.05, 0.20);
    const int nSettle = static_cast<int>(std::llround(0.05 * rate));
    const int n = static_cast<int>(x.size());
    return zc_freq(x, rate, nSettle, n);
}

double cents(double fA, double fB) {
    if (fA <= 0.0 || fB <= 0.0) {
        return 1.0e9;
    }
    return 1200.0 * std::abs(std::log2(fA / fB));
}

} // namespace

TEST_CASE("rest state: root, stable, and stays put") {
    const std::pair<double, double> pts[] = {{-0.1, 0.05}, {-0.05, 0.3}, {-0.1, 0.8}};
    for (const auto& [alpha, beta] : pts) {
        const double x0 = cuicui::model::restDisplacement(alpha, beta);
        REQUIRE(std::abs(cuicui::model::cubicResidual(x0, alpha, beta)) < 1e-9);
        const bool trNeg = x0 * (1.0 + x0) > 0.0;
        const bool detPos = (beta + 3.0 * x0 * x0 - 2.0 * x0) > 0.0;
        REQUIRE(trNeg);
        REQUIRE(detPos);

        cuicui::model::LabialOscillator osc;
        osc.prepare(kRate);
        osc.resetToRest(alpha, beta);
        REQUIRE_THAT(osc.x(), WithinAbs(x0, 1e-12));
        REQUIRE_THAT(osc.y(), WithinAbs(0.0, 1e-15));
        const int n = static_cast<int>(std::llround(0.05 * kRate));
        for (int i = 0; i < n; ++i) {
            osc.step(alpha, beta, kGamma);
        }
        REQUIRE(std::abs(osc.x() - x0) < 1e-6);
        REQUIRE(std::abs(osc.y()) < 1e-6);
    }
}

TEST_CASE("labial frequencies within 0.5 percent") {
    struct Pt {
        double alpha, beta, f0;
    };
    const Pt pts[] = {
        {0.10, 0.50, 3098.0}, {0.20, 0.20, 2658.0}, {0.30, 0.10, 2817.0},
        {0.15, 0.05, 1380.0}, {0.40, 0.35, 3797.0}, {0.25, -0.05, 1740.0},
    };
    for (const auto& p : pts) {
        const double f = measured_f0(p.alpha, p.beta, kGamma, kRate);
        INFO("alpha=" << p.alpha << " beta=" << p.beta << " f=" << f << " expected=" << p.f0);
        REQUIRE_THAT(f, WithinRel(p.f0, 0.005));
    }
}

TEST_CASE("Hopf approximation f0 ≈ gamma sqrt(beta) / 2pi") {
    const double betas[] = {0.3, 0.5, 0.8};
    for (const double beta : betas) {
        const double approx = kGamma * std::sqrt(beta) / (2.0 * std::numbers::pi);
        const double f = measured_f0(0.005, beta, kGamma, kRate);
        INFO("beta=" << beta << " f=" << f << " approx=" << approx);
        REQUIRE_THAT(f, WithinRel(approx, 0.03));
    }
}

TEST_CASE("silence from rest below onset") {
    const std::pair<double, double> pts[] = {{-0.05, 0.3}, {0.05, 0.05}};
    for (const auto& [alpha, beta] : pts) {
        const auto x = run_osc(alpha, beta, kGamma, kRate, 0.05, 0.05);
        const int nSettle = static_cast<int>(std::llround(0.05 * kRate));
        const int n = static_cast<int>(x.size());
        const double p2p = peakToPeak(x, nSettle, n);
        INFO("alpha=" << alpha << " beta=" << beta << " p2p=" << p2p);
        REQUIRE(p2p < 1e-3);
    }
}

TEST_CASE("onset from rest, slow ramp 0.2 /s") {
    struct Exp {
        double beta;
        double aOn;
        bool hopf;
    };
    const Exp cases[] = {
        {-0.10, 0.217, false}, {-0.05, 0.182, false}, {0.05, 0.116, false},
        {0.15, 0.054, false},  {0.20, 0.026, false},  {0.30, 0.0, true},
        {0.50, 0.0, true},
    };
    constexpr double ramp = 0.2; // s^-1
    constexpr double a0 = -0.10;
    constexpr double a1 = 0.40;
    const double duration = (a1 - a0) / ramp;
    const int n = static_cast<int>(std::llround(duration * kRate));
    const int win = static_cast<int>(std::llround(0.010 * kRate));
    const int hop = static_cast<int>(std::llround(0.001 * kRate));

    for (const auto& c : cases) {
        cuicui::model::LabialOscillator osc;
        osc.prepare(kRate);
        osc.resetToRest(a0, c.beta);
        std::vector<double> x(static_cast<std::size_t>(n));
        std::vector<double> alpha(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double a = a0 + ramp * static_cast<double>(i) / kRate;
            alpha[static_cast<std::size_t>(i)] = a;
            osc.step(a, c.beta, kGamma);
            x[static_cast<std::size_t>(i)] = osc.x();
        }
        double onsetA = a1;
        bool found = false;
        bool prev = false;
        for (int i = win; i <= n; i += hop) {
            const int i0 = i - win;
            const double mu = mean(x, i0, i);
            const double p2p = peakToPeak(x, i0, i);
            const int zc = rising_zc_count(x, mu, i0, i);
            const bool ok = zc >= 3 && p2p > 0.05;
            if (ok && prev) {
                // First of two consecutive 10 ms windows — ignore the rest-branch collapse blip.
                onsetA = alpha[static_cast<std::size_t>(i - hop - 1)];
                found = true;
                break;
            }
            prev = ok;
        }
        INFO("beta=" << c.beta << " onsetA=" << onsetA << " found=" << found);
        REQUIRE(found);
        if (c.hopf) {
            REQUIRE(onsetA >= -0.005);
            REQUIRE(onsetA <= 0.04);
        } else {
            REQUIRE_THAT(onsetA, WithinAbs(c.aOn, 0.01));
        }
    }
}

TEST_CASE("frequency scales linearly with gamma") {
    const double f24 = measured_f0(0.2, 0.2, 24000.0, kRate);
    const double f12 = measured_f0(0.2, 0.2, 12000.0, kRate);
    REQUIRE(f24 > 0.0);
    REQUIRE_THAT(f12, WithinRel(0.5 * f24, 0.002));
}

TEST_CASE("pitch at 176.4 kHz vs 8x differs by less than 1 cent") {
    struct Pt {
        double alpha, beta;
    };
    const Pt pts[] = {
        {0.10, 0.50}, {0.20, 0.20}, {0.30, 0.10}, {0.15, 0.05}, {0.40, 0.35}, {0.25, -0.05},
    };
    constexpr double rLo = 176400.0;
    constexpr double rHi = 1411200.0;
    for (const auto& p : pts) {
        const double fLo = measured_f0(p.alpha, p.beta, kGamma, rLo);
        const double fHi = measured_f0(p.alpha, p.beta, kGamma, rHi);
        const double c = cents(fLo, fHi);
        INFO("alpha=" << p.alpha << " beta=" << p.beta << " fLo=" << fLo << " fHi=" << fHi
                      << " cents=" << c);
        REQUIRE(c < 1.0);
    }
}

TEST_CASE("stability fuzz: 10 s smooth trajectories, 0 non-finite resets") {
    cuicui::model::LabialOscillator osc;
    osc.prepare(kRate);
    osc.resetToRest(cuicui::model::kAlphaRest, 0.3);

    std::mt19937 rng(1);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    double p = 0.0, beta = 0.3, gamma = kGamma, off = 0.0;
    double pT = 0.2, bT = 0.3, gT = kGamma, oT = 0.0;
    const double coeff = 1.0 - std::exp(-1.0 / (0.05 * kRate)); // 50 ms
    const int n = static_cast<int>(std::llround(10.0 * kRate));
    int nanCount = 0;
    for (int i = 0; i < n; ++i) {
        if (i % 64 == 0) {
            pT = std::clamp(0.5 + 0.5 * uni(rng), 0.0, 1.0);
            bT = std::clamp(-0.3 + 1.5 * (0.5 + 0.5 * uni(rng)), -0.3, 1.2);
            gT = std::clamp(23000.0 + 2000.0 * uni(rng), 20000.0, 26000.0);
            oT = std::clamp(0.3 * uni(rng), -0.3, 0.3);
        }
        p += coeff * (pT - p);
        beta += coeff * (bT - beta);
        gamma += coeff * (gT - gamma);
        off += coeff * (oT - off);
        const double alpha = cuicui::model::alphaFromPressure(p, off);
        osc.step(alpha, beta, gamma);
        if (!std::isfinite(osc.x()) || !std::isfinite(osc.y())) {
            ++nanCount;
        }
    }
    REQUIRE(osc.nonFiniteResets() == 0);
    REQUIRE(nanCount == 0);
}

TEST_CASE("labial rest starts silent: no DC-blocker click") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.reset(1);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.primeLabial(cuicui::model::kAlphaRest, 0.5);
    cuicui::VoiceSettings settings;
    settings.src_gamma = kGamma;
    settings.out_gain_db = 0.0;
    voice.setSettings(settings);
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);

    constexpr int sr = 48000;
    constexpr int n = sr / 10; // 100 ms
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = 0.0f;
    to.resp_pressure = 0.0f;
    from.src_L_beta = 0.5f;
    to.src_L_beta = 0.5f;
    voice.process(from, to, buf.data(), n);

    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        peak = std::max(peak, std::abs(buf[static_cast<std::size_t>(i)]));
    }
    INFO("rest peak=" << peak);
    REQUIRE(peak < 1e-4f);
}
