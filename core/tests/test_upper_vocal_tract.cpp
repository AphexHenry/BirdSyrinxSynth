#include "cuicui/io/offline_render.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 192000.0;

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
        const double ang = -2.0 * kPi / static_cast<double>(len);
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

int next_pow2(int n) {
    int p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

std::complex<double> h_uvt(std::complex<double> s, const cuicui::model::UvtPhysical& p) {
    const auto zc = p.Rh + 1.0 / (s * p.Ch);
    const auto zb = s * p.Lb + p.Rb;
    const auto zp = (zc * zb) / (zc + zb);
    return (zp / (s * p.Lg + zp)) * (p.Rb / (s * p.Lb + p.Rb));
}

void impulse_uvt(double fH, double gape, double zh, double zb, std::vector<double>& flow,
                 std::vector<double>& dflow) {
    cuicui::model::UpperVocalTract uvt;
    uvt.prepare(kRate);
    uvt.setParams(fH, zh, gape, zb, 1.0);
    uvt.reset();
    const int n = static_cast<int>(flow.size());
    for (int i = 0; i < n; ++i) {
        const auto o = uvt.process(i == 0 ? 1.0 : 0.0);
        flow[static_cast<unsigned>(i)] = o.flow;
        dflow[static_cast<unsigned>(i)] = o.dflow;
    }
}

double mag_at(const std::vector<std::complex<double>>& X, double freq, double rate, int nfft) {
    const double bin = freq * static_cast<double>(nfft) / rate;
    const int k = std::clamp(static_cast<int>(std::llround(bin)), 0, nfft / 2);
    return std::abs(X[static_cast<unsigned>(k)]);
}

std::vector<float> render_reference_note(double radiation, double durationS) {
    cuicui::io::RenderSpec spec;
    spec.sample_rate = 48000;
    spec.duration_s = durationS;
    spec.seed = 1;
    spec.oversampling = "auto";
    const double p = (0.2 - cuicui::model::kAlphaRest) /
                     (cuicui::model::kAlphaMax - cuicui::model::kAlphaRest);
    spec.params["resp.pressure"] = p;
    spec.params["src.L.beta"] = 0.2;
    spec.params["src.gamma"] = 24000.0;
    spec.params["trachea.length_mm"] = 33.0;
    spec.params["trachea.reflection"] = 0.5;
    spec.params["trachea.loss"] = 0.3;
    spec.params["trachea.input_gain"] = 1.0;
    spec.params["oec.freq_hz"] = 3000.0;
    spec.params["oec.damping"] = 0.1;
    spec.params["beak.gape"] = 0.5;
    spec.params["beak.damping"] = 0.3;
    spec.params["out.radiation"] = radiation;
    spec.params["out.gain_db"] = 0.0;
    cuicui::io::RenderOptions opt;
    opt.apply_smoothing = false;
    return cuicui::io::render_offline(spec, opt);
}

} // namespace

TEST_CASE("UVT implied volume ~ 0.54 cm3 at 3 kHz gape 0.5") {
    const auto p = cuicui::model::uvtPhysical(3000.0, 0.1, 0.5, 0.3, 1.0);
    REQUIRE_THAT(p.volumeM3 * 1.0e6, WithinAbs(0.54, 0.03));
}

TEST_CASE("discrete flow response matches analytic H_uvt") {
    const double fHs[] = {1000.0, 3000.0, 6000.0};
    const double gapes[] = {0.2, 0.9};
    const int n = static_cast<int>(std::llround(0.4 * kRate));
    const int nfft = next_pow2(n);
    std::vector<double> flow(static_cast<std::size_t>(n), 0.0);
    std::vector<double> dflow(static_cast<std::size_t>(n), 0.0);
    for (double fH : fHs) {
        for (double g : gapes) {
            impulse_uvt(fH, g, 0.1, 0.3, flow, dflow);
            std::vector<std::complex<double>> X(static_cast<std::size_t>(nfft), 0.0);
            for (int i = 0; i < n; ++i) {
                X[static_cast<unsigned>(i)] = flow[static_cast<unsigned>(i)];
            }
            fft_radix2(X);
            const auto phys = cuicui::model::uvtPhysical(fH, 0.1, g, 0.3, 1.0);
            auto check_band = [&](double f0, double f1, double tolDb) {
                for (double f = f0; f <= f1; f *= 1.12) {
                    const double meas = mag_at(X, f, kRate, nfft);
                    const auto ha = h_uvt({0.0, 2.0 * kPi * f}, phys);
                    const double anal = std::abs(ha) / phys.Rb; // H_flow = H_uvt / R_b
                    const double db = 20.0 * std::log10(std::max(meas, 1e-18) / std::max(anal, 1e-18));
                    INFO("fH=" << fH << " g=" << g << " f=" << f << " dB=" << db);
                    REQUIRE(std::abs(db) <= tolDb);
                }
            };
            check_band(50.0, 6000.0, 0.2);
            check_band(6000.0, 12000.0, 0.5);
        }
    }
}

TEST_CASE("radiated peak within 5 percent of oec.freq_hz") {
    const double fHs[] = {1000.0, 2000.0, 3000.0, 4500.0, 6000.0, 8000.0};
    const double gapes[] = {0.2, 0.5, 0.9};
    const int n = static_cast<int>(std::llround(0.5 * kRate));
    const int nfft = next_pow2(n);
    std::vector<double> flow(static_cast<std::size_t>(n), 0.0);
    std::vector<double> dflow(static_cast<std::size_t>(n), 0.0);
    for (double fH : fHs) {
        for (double g : gapes) {
            impulse_uvt(fH, g, 0.1, 0.3, flow, dflow);
            std::vector<std::complex<double>> X(static_cast<std::size_t>(nfft), 0.0);
            for (int i = 0; i < n; ++i) {
                X[static_cast<unsigned>(i)] = dflow[static_cast<unsigned>(i)];
            }
            fft_radix2(X);
            double bestF = 0.0;
            double bestM = -1.0;
            const int kMin = std::max(1, static_cast<int>(50.0 * nfft / kRate));
            const int kMax = static_cast<int>(12000.0 * nfft / kRate);
            for (int k = kMin; k <= kMax; ++k) {
                const double m = std::abs(X[static_cast<unsigned>(k)]);
                if (m > bestM) {
                    bestM = m;
                    bestF = static_cast<double>(k) * kRate / static_cast<double>(nfft);
                }
            }
            const double err = std::abs(bestF - fH) / fH;
            INFO("fH=" << fH << " g=" << g << " peak=" << bestF << " rel=" << err);
            REQUIRE(err <= 0.05);
        }
    }
}

TEST_CASE("UVT stability: 10 s noise, LFO f_H and gape") {
    cuicui::model::UpperVocalTract uvt;
    uvt.prepare(kRate);
    uvt.setParams(3000.0, 0.1, 0.5, 0.3, 1.0);
    uvt.reset();
    const int n = static_cast<int>(std::llround(10.0 * kRate));
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> damp(0.01, 1.0);
    const double zh = damp(rng);
    const double zb = damp(rng);
    double peakStatic = 0.0;
    {
        cuicui::model::UpperVocalTract u0;
        u0.prepare(kRate);
        u0.setParams(3000.0, zh, 0.5, zb, 1.0);
        u0.reset();
        std::mt19937 r2(99);
        std::normal_distribution<double> nrm(0.0, 1.0);
        for (int i = 0; i < static_cast<int>(kRate); ++i) {
            const auto o = u0.process(nrm(r2));
            peakStatic = std::max(peakStatic, std::max(std::abs(o.flow), std::abs(o.dflow)));
        }
    }
    std::normal_distribution<double> nrm(0.0, 1.0);
    double peak = 0.0;
    bool finite = true;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double fH = 1000.0 + 4500.0 * (1.0 + std::sin(2.0 * kPi * 20.0 * t));
        const double gape = 0.5 + 0.5 * std::sin(2.0 * kPi * 7.0 * t);
        uvt.setParams(std::clamp(fH, 1000.0, 10000.0), zh, std::clamp(gape, 0.0, 1.0), zb, 1.0);
        const auto o = uvt.process(nrm(rng));
        if (!std::isfinite(o.flow) || !std::isfinite(o.dflow)) {
            finite = false;
            break;
        }
        peak = std::max(peak, std::max(std::abs(o.flow), std::abs(o.dflow)));
    }
    INFO("peak=" << peak << " static=" << peakStatic);
    REQUIRE(finite);
    REQUIRE(peak < 100.0 * std::max(peakStatic, 1e-18));
}

TEST_CASE("calibration: reference note -6 dBFS radiation and flow RMS") {
    const auto y1 = render_reference_note(1.0, 1.0);
    const auto y0 = render_reference_note(0.0, 1.0);
    const int skip = 48000 / 5;
    double peak1 = 0.0;
    double e0 = 0.0, e1 = 0.0;
    int n = 0;
    for (int i = skip; i < static_cast<int>(y1.size()); ++i) {
        peak1 = std::max(peak1, static_cast<double>(std::abs(y1[static_cast<unsigned>(i)])));
        e1 += static_cast<double>(y1[static_cast<unsigned>(i)]) * y1[static_cast<unsigned>(i)];
        e0 += static_cast<double>(y0[static_cast<unsigned>(i)]) * y0[static_cast<unsigned>(i)];
        ++n;
    }
    const double peakDb = 20.0 * std::log10(std::max(peak1, 1e-18));
    const double rms0 = std::sqrt(e0 / std::max(n, 1));
    const double rms1 = std::sqrt(e1 / std::max(n, 1));
    const double rmsDb = 20.0 * std::log10(std::max(rms0, 1e-18) / std::max(rms1, 1e-18));
    INFO("peak dBFS=" << peakDb << " RMS rho0/rho1 dB=" << rmsDb);
    REQUIRE_THAT(peakDb, WithinAbs(-6.0, 1.0));
    REQUIRE(std::abs(rmsDb) <= 3.0);
}

TEST_CASE("full voice CPU: 10 s at 48 kHz under 0.2 s") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings s;
    s.src_gamma = 24000.0;
    s.out_gain_db = 0.0;
    voice.setSettings(s);
    voice.reset(1);
    voice.primeLabial(0.2, 0.2);
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);
    constexpr int sr = 48000;
    constexpr int n = sr * 10;
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.545454f;
    from.src_L_beta = to.src_L_beta = 0.2f;
    from.oec_freq_hz = to.oec_freq_hz = 3000.0f;
    from.beak_gape = to.beak_gape = 0.5f;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kBlock = 64;
    for (int i = 0; i < n; i += kBlock) {
        const int ns = std::min(kBlock, n - i);
        voice.process(from, to, buf.data() + i, ns);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    INFO("render 10s took " << sec << " s");
    REQUIRE(sec < 0.2);
}
