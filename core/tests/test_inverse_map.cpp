#include "cuicui/control/inverse_map.hpp"
#include "cuicui/io/audio_features.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/params.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

double gammaOf(double sigma) { return 24000.0 / sigma; }

double nuOfHz(double f0, double gamma) { return 2.0 * std::numbers::pi * f0 / gamma; }

double hzOfNu(double nu, double gamma) { return nu * gamma / (2.0 * std::numbers::pi); }

void alphaToPressure(double alpha, float& pressure, float& offset) {
    using cuicui::model::kAlphaMax;
    using cuicui::model::kAlphaRest;
    if (alpha <= kAlphaMax) {
        pressure = static_cast<float>((alpha - kAlphaRest) / (kAlphaMax - kAlphaRest));
        offset = 0.0f;
    } else {
        pressure = 1.0f;
        offset = static_cast<float>(alpha - kAlphaMax);
    }
}

std::vector<float> renderNote(double alpha, double beta, double sigma, double seconds) {
    cuicui::VoiceSettings st;
    st.body_size = sigma;
    st.src_gamma = 24000.0;
    st.noise_aspiration = 0.0;
    st.resp_attack_ms = 8.0;
    st.resp_release_ms = 15.0;
    float p = 0.0f;
    float off = 0.0f;
    alphaToPressure(alpha, p, off);
    const int n = static_cast<int>(std::lround(seconds * 48000.0));
    std::unordered_map<std::string, std::vector<float>> ctrls;
    ctrls["resp.pressure"] = std::vector<float>(static_cast<std::size_t>(n), p);
    ctrls["src.L.beta"] = std::vector<float>(static_cast<std::size_t>(n), static_cast<float>(beta));
    ctrls["src.R.beta"] = std::vector<float>(static_cast<std::size_t>(n), static_cast<float>(beta));
    ctrls["src.L.gate"] = std::vector<float>(static_cast<std::size_t>(n), 1.0f);
    ctrls["src.R.gate"] = std::vector<float>(static_cast<std::size_t>(n), 0.0f);
    ctrls["src.L.alpha_offset"] = std::vector<float>(static_cast<std::size_t>(n), off);
    ctrls["noise.beta_sigma"] = std::vector<float>(static_cast<std::size_t>(n), 0.0f);
    return cuicui::io::render_controls(st, ctrls, 48000.0, 48000.0, 1, 0);
}

double cents(double fMeas, double fRef) {
    if (!(fMeas > 0.0) || !(fRef > 0.0)) {
        return 1.0e9;
    }
    return 1200.0 * std::log2(fMeas / fRef);
}

void fft_radix2(std::vector<std::complex<double>>& a) {
    const int n = static_cast<int>(a.size());
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[static_cast<unsigned>(i)], a[static_cast<unsigned>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k) {
                auto u = a[static_cast<unsigned>(i + k)];
                auto v = a[static_cast<unsigned>(i + k + len / 2)] * w;
                a[static_cast<unsigned>(i + k)] = u + v;
                a[static_cast<unsigned>(i + k + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }
}

/// Interpolated FFT peak near fRef. Integer-lag AC is ~20 cents coarse at 6 kHz.
double fft_f0_near(const float* x, int n, double sr, int skip, double fRef) {
    if (x == nullptr || n <= skip + 64 || !(fRef > 1.0) || !(sr > 0.0)) {
        return 0.0;
    }
    const int i0 = skip;
    const int m = n - i0;
    int nfft = 1;
    while (nfft < m && nfft < 16384) {
        nfft <<= 1;
    }
    nfft = std::min(nfft, 16384);
    if (nfft < 256) {
        return 0.0;
    }
    std::vector<std::complex<double>> buf(static_cast<std::size_t>(nfft), 0.0);
    const int nWin = std::min(m, nfft);
    double mu = 0.0;
    for (int i = 0; i < nWin; ++i) {
        mu += static_cast<double>(x[i0 + i]);
    }
    mu /= static_cast<double>(nWin);
    for (int i = 0; i < nWin; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                              static_cast<double>(nWin - 1));
        buf[static_cast<std::size_t>(i)] = (static_cast<double>(x[i0 + i]) - mu) * w;
    }
    fft_radix2(buf);
    const int kLo = std::max(1, static_cast<int>(std::floor(fRef * 0.82 * nfft / sr)));
    const int kHi = std::min(nfft / 2 - 1, static_cast<int>(std::ceil(fRef * 1.22 * nfft / sr)));
    if (kHi <= kLo + 1) {
        return 0.0;
    }
    int kBest = kLo;
    double pBest = -1.0;
    for (int k = kLo; k <= kHi; ++k) {
        const double p = std::norm(buf[static_cast<std::size_t>(k)]);
        if (p > pBest) {
            pBest = p;
            kBest = k;
        }
    }
    auto mag = [&](int k) { return std::sqrt(std::norm(buf[static_cast<std::size_t>(k)])); };
    double delta = 0.0;
    if (kBest > 0 && kBest < nfft / 2) {
        const double a = mag(kBest - 1);
        const double b = mag(kBest);
        const double c = mag(kBest + 1);
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-18) {
            delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
    }
    return (static_cast<double>(kBest) + delta) * sr / static_cast<double>(nfft);
}

} // namespace

TEST_CASE("perf.range_fold is in the registry") {
    const auto* p = cuicui::findParam("perf.range_fold");
    REQUIRE(p != nullptr);
    REQUIRE(p->layer == cuicui::ParamLayer::Performance);
    REQUIRE(p->def == Catch::Approx(1.0));
    REQUIRE(p->min == Catch::Approx(0.0));
    REQUIRE(p->max == Catch::Approx(1.0));
}

TEST_CASE("inverse table: lo <= hi and beta in range") {
    using namespace cuicui::generated;
    int nOk = 0;
    int nBad = 0;
    for (int j = 0; j < kAtlasNNu; ++j) {
        const float lo = kAtlasAlphaLo[static_cast<std::size_t>(j)];
        const float hi = kAtlasAlphaHi[static_cast<std::size_t>(j)];
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            continue;
        }
        REQUIRE(lo <= hi + 1e-5f);
        ++nOk;
    }
    REQUIRE(nOk > 50);
    for (int i = 0; i < kAtlasNAlpha; ++i) {
        for (int j = 0; j < kAtlasNNu; ++j) {
            const float b =
                kAtlasBetaOfAlphaNu[static_cast<std::size_t>(i) * static_cast<std::size_t>(kAtlasNNu) +
                                    static_cast<std::size_t>(j)];
            if (!std::isfinite(b)) {
                continue;
            }
            if (b < kAtlasBeta0 - 1e-3f || b > 1.2f + 1e-3f) {
                ++nBad;
            }
        }
    }
    REQUIRE(nBad == 0);
}

TEST_CASE("folding: one octave above max returns folded and half nu") {
    cuicui::control::InverseMap map;
    const double tone = 0.6;
    const double nMax = map.nuMax(tone);
    REQUIRE(nMax > 0.1);
    const auto r = map.solve(2.0 * nMax, tone, 0.2, 0.0, true);
    REQUIRE(r.folded);
    REQUIRE_FALSE(r.clamped);
    REQUIRE_THAT(r.nuUsed, WithinRel(nMax, 1e-9));
}

TEST_CASE("solve is O(1) under 100 ns") {
    cuicui::control::InverseMap map;
    (void)map.solve(0.5, 0.6, 0.2, 0.0, true);
    constexpr int n = 400000;
    volatile double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        const double nu = 0.35 + 0.002 * static_cast<double>(i % 50);
        const auto r = map.solve(nu, 0.6, 0.2, 0.0, true);
        sink += r.beta + r.alpha;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(n);
    INFO("ns_per_call=" << ns << " sink=" << sink);
    REQUIRE(ns < 100.0);
}

TEST_CASE("closed-loop tuning sigma=1 full semitone grid") {
    cuicui::control::InverseMap map;
    const double sigma = 1.0;
    const double gamma = gammaOf(sigma);
    struct ToneTol {
        double tone;
        double maxCents;
        double medCents;
    };
    const ToneTol cases[] = {{0.2, 5.0, 2.0}, {0.6, 5.0, 2.0}, {1.0, 5.0, 2.0}, {0.05, 15.0, 15.0}};
    for (const auto& c : cases) {
        const double nMin = map.nuMin();
        const double nMax = map.nuMax(c.tone);
        const double fLo = hzOfNu(nMin, gamma);
        const double fHi = hzOfNu(nMax, gamma);
        const double midi0 = std::ceil(69.0 + 12.0 * std::log2(fLo / 440.0) - 1e-9);
        const double midi1 = std::floor(69.0 + 12.0 * std::log2(fHi / 440.0) + 1e-9);
        std::vector<double> absC;
        for (double m = midi0; m <= midi1 + 0.1; m += 1.0) {
            const double fRef = 440.0 * std::pow(2.0, (m - 69.0) / 12.0);
            const double nu = nuOfHz(fRef, gamma);
            const auto sol = map.solve(nu, c.tone, 0.2, 0.0, false);
            if (sol.clamped || !sol.reachable) {
                continue;
            }
            const auto y = renderNote(sol.alpha, sol.beta, sigma, 0.28);
            const int n = static_cast<int>(y.size());
            const int skip = static_cast<int>(std::lround(0.12 * 48000.0));
            const double fMeas = fft_f0_near(y.data(), n, 48000.0, skip, fRef);
            const double err = cents(fMeas, fRef);
            INFO("tone=" << c.tone << " midi=" << m << " fRef=" << fRef << " fMeas=" << fMeas
                         << " cents=" << err << " a=" << sol.alpha << " b=" << sol.beta);
            REQUIRE(std::abs(err) <= c.maxCents + 0.05);
            absC.push_back(std::abs(err));
        }
        REQUIRE(absC.size() >= 8);
        std::sort(absC.begin(), absC.end());
        const double med = absC[absC.size() / 2];
        INFO("tone=" << c.tone << " n=" << absC.size() << " median_abs_cents=" << med
                     << " max=" << absC.back());
        REQUIRE(med <= c.medCents + 0.05);
    }
}

TEST_CASE("closed-loop tuning sigma 0.5 and 2 every 3rd semitone") {
    cuicui::control::InverseMap map;
    for (double sigma : {0.5, 2.0}) {
        const double gamma = gammaOf(sigma);
        for (double tone : {0.2, 0.6, 1.0}) {
            const double nMin = map.nuMin();
            const double nMax = map.nuMax(tone);
            const double fLo = hzOfNu(nMin, gamma);
            const double fHi = hzOfNu(nMax, gamma);
            const double midi0 = std::ceil(69.0 + 12.0 * std::log2(fLo / 440.0) - 1e-9);
            const double midi1 = std::floor(69.0 + 12.0 * std::log2(fHi / 440.0) + 1e-9);
            std::vector<double> absC;
            int k = 0;
            for (double m = midi0; m <= midi1 + 0.1; m += 1.0, ++k) {
                if (k % 3 != 0) {
                    continue;
                }
                const double fRef = 440.0 * std::pow(2.0, (m - 69.0) / 12.0);
                const double nu = nuOfHz(fRef, gamma);
                const auto sol = map.solve(nu, tone, 0.2, 0.0, false);
                if (sol.clamped || !sol.reachable) {
                    continue;
                }
                const auto y = renderNote(sol.alpha, sol.beta, sigma, 0.28);
                const int n = static_cast<int>(y.size());
                const int skip = static_cast<int>(std::lround(0.12 * 48000.0));
                const double fMeas = fft_f0_near(y.data(), n, 48000.0, skip, fRef);
                const double err = cents(fMeas, fRef);
                INFO("sigma=" << sigma << " tone=" << tone << " midi=" << m << " cents=" << err);
                REQUIRE(std::abs(err) <= 5.05);
                absC.push_back(std::abs(err));
            }
            REQUIRE_FALSE(absC.empty());
            std::sort(absC.begin(), absC.end());
            REQUIRE(absC[absC.size() / 2] <= 2.05);
        }
    }
}

TEST_CASE("physics=1 breath sweep keeps pitch, changes SCI") {
    cuicui::control::InverseMap map;
    const double sigma = 1.0;
    const double gamma = gammaOf(sigma);
    const double fRef = 2000.0;
    const double nu = nuOfHz(fRef, gamma);
    const auto mid = map.solve(nu, 0.6, 0.2, 0.0, false);
    REQUIRE(mid.reachable);
    std::vector<double> f0s;
    std::vector<double> scis;
    for (double breath : {mid.alpha - 0.15, mid.alpha, mid.alpha + 0.15, -0.05, 0.55}) {
        const auto sol = map.solve(nu, 0.6, breath, 1.0, false);
        REQUIRE(sol.reachable);
        const auto y = renderNote(sol.alpha, sol.beta, sigma, 0.32);
        const int n = static_cast<int>(y.size());
        const int skip = static_cast<int>(std::lround(0.12 * 48000.0));
        const double fMeas = fft_f0_near(y.data(), n, 48000.0, skip, fRef);
        const auto feat = cuicui::io::measure_features(y.data() + skip, n - skip, 48000.0);
        f0s.push_back(fMeas);
        scis.push_back(feat.spectral_centroid_hz / std::max(1.0, fMeas));
        INFO("breath=" << breath << " alpha=" << sol.alpha << " f0=" << fMeas << " sci=" << scis.back());
        REQUIRE(std::abs(cents(fMeas, fRef)) <= 5.05);
    }
    const double sciSpan = *std::max_element(scis.begin(), scis.end()) - *std::min_element(scis.begin(), scis.end());
    REQUIRE(sciSpan > 0.02);
}

TEST_CASE("atlas header is under 5 MB") {
    // The generated file is compiled in; size is also checked on disk in the Python test.
    REQUIRE(sizeof(cuicui::generated::kAtlasBetaOfAlphaNu) < 5 * 1024 * 1024);
}
