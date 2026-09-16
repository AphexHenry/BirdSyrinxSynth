#include "cuicui/dsp/lagrange.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/trachea.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 192000.0;
constexpr double kC = cuicui::model::kSoundSpeed;

double wrap_deg(double d) {
    while (d > 180.0) {
        d -= 360.0;
    }
    while (d < -180.0) {
        d += 360.0;
    }
    return d;
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

std::vector<double> impulse_response(double lengthMm, double r, double loss, double A, double rate, double t) {
    cuicui::model::Trachea tr;
    tr.prepare(rate, cuicui::model::kTracheaMaxLengthMm);
    tr.setParams(lengthMm, r, loss, A);
    tr.reset();
    const int n = static_cast<int>(std::llround(t * rate));
    std::vector<double> y(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        y[static_cast<unsigned>(i)] = tr.process(i == 0 ? 1.0 : 0.0);
    }
    return y;
}

double analytic_mag(double fHz, double A, double r, double tau) {
    const double wr = 2.0 * kPi * fHz * tau;
    const double re = 1.0 + r * std::cos(wr);
    const double im = -r * std::sin(wr);
    return std::abs(A * (1.0 - r)) / std::hypot(re, im);
}

double tau_of(double lengthMm) { return 2.0 * (lengthMm * 0.001) / kC; }

struct Peak {
    double hz = 0.0;
    double mag = 0.0;
};

Peak peak_near(const std::vector<std::complex<double>>& X, double rate, double f0, double windowHz) {
    const int n = static_cast<int>(X.size());
    const int k0 = static_cast<int>(std::llround(f0 * static_cast<double>(n) / rate));
    const int dk = std::max(1, static_cast<int>(std::llround(windowHz * static_cast<double>(n) / rate)));
    int kBest = k0;
    double best = 0.0;
    for (int k = std::max(1, k0 - dk); k <= std::min(n / 2 - 2, k0 + dk); ++k) {
        const double m = std::abs(X[static_cast<unsigned>(k)]);
        if (m > best) {
            best = m;
            kBest = k;
        }
    }
    const double ym1 = std::abs(X[static_cast<unsigned>(kBest - 1)]);
    const double y0 = std::abs(X[static_cast<unsigned>(kBest)]);
    const double yp1 = std::abs(X[static_cast<unsigned>(kBest + 1)]);
    const double den = ym1 - 2.0 * y0 + yp1;
    double delta = 0.0;
    if (std::abs(den) > 1e-18) {
        delta = 0.5 * (ym1 - yp1) / den;
        delta = std::clamp(delta, -0.5, 0.5);
    }
    Peak p;
    p.hz = (static_cast<double>(kBest) + delta) * rate / static_cast<double>(n);
    p.mag = y0;
    return p;
}

std::vector<std::complex<double>> rfft_mag(const std::vector<double>& h, double /*rate*/) {
    const int nfft = next_pow2(static_cast<int>(h.size()));
    std::vector<std::complex<double>> X(static_cast<std::size_t>(nfft), 0.0);
    for (std::size_t i = 0; i < h.size(); ++i) {
        X[i] = h[i];
    }
    fft_radix2(X);
    return X;
}

} // namespace

TEST_CASE("Lagrange delay: 1 kHz phase/amp, 10 kHz amp") {
    constexpr int size = 128;
    std::vector<double> buf(static_cast<std::size_t>(size), 0.0);
    constexpr int n = 9600;
    constexpr int skip = 192; // integer 1 kHz periods in the remaining window

    auto run = [&](double delay, double freq) {
        std::fill(buf.begin(), buf.end(), 0.0);
        int write = 0;
        const double w = 2.0 * kPi * freq / kRate;
        double acc_s = 0.0, acc_c = 0.0;
        int count = 0;
        for (int i = 0; i < n; ++i) {
            const double x = std::sin(w * static_cast<double>(i));
            const double y = cuicui::dsp::lagrange3Read(buf.data(), size, write, delay);
            buf[static_cast<unsigned>(write)] = x;
            write = cuicui::dsp::wrapIndex(write + 1, size);
            if (i < skip) {
                continue;
            }
            const double targ = w * (static_cast<double>(i) - delay);
            acc_s += y * std::sin(targ);
            acc_c += y * std::cos(targ);
            ++count;
        }
        const double nrm = 2.0 / static_cast<double>(count);
        const double a_sin = nrm * acc_s;
        const double a_cos = nrm * acc_c;
        const double amp = std::hypot(a_sin, a_cos);
        const double phase_deg = std::atan2(a_cos, a_sin) * 180.0 / kPi;
        return std::pair<double, double>{amp, phase_deg};
    };

    for (double d = 3.0; d <= 40.0 + 1e-9; d += 0.25) {
        const auto [amp, phase] = run(d, 1000.0);
        const double adb = 20.0 * std::log10(std::max(amp, 1e-18));
        INFO("delay=" << d << " amp_dB=" << adb << " phase_deg=" << phase);
        REQUIRE(std::abs(adb) < 0.01);
        REQUIRE(std::abs(wrap_deg(phase)) < 0.1);
    }
    for (double d = 3.0; d <= 40.0 + 1e-9; d += 0.25) {
        const auto [amp, phase] = run(d, 10000.0);
        const double adb = 20.0 * std::log10(std::max(amp, 1e-18));
        INFO("delay=" << d << " 10k amp_dB=" << adb << " phase=" << phase);
        REQUIRE(std::abs(adb) < 0.5);
    }
}

TEST_CASE("trachea resonances r = ±0.5, L = 33 mm") {
    constexpr double L = 33.0;
    auto check = [](double r, std::initializer_list<double> expected, bool low_freq_max) {
        const auto h = impulse_response(L, r, 0.0, 1.0, kRate, 1.0);
        const auto X = rfft_mag(h, kRate);
        for (double f0 : expected) {
            const Peak p = peak_near(X, kRate, f0, 0.02 * f0);
            const double rel = std::abs(p.hz - f0) / f0;
            INFO("r=" << r << " expected=" << f0 << " got=" << p.hz << " rel=" << rel);
            REQUIRE(rel <= 0.005);
        }
        if (low_freq_max) {
            const Peak low = peak_near(X, kRate, 80.0, 200.0);
            REQUIRE(low.mag > 0.5 * std::abs(X[static_cast<unsigned>(
                                        std::llround(2000.0 * static_cast<double>(X.size()) / kRate))]));
        }
    };
    check(0.5, {2651.0, 7955.0, 13258.0}, false);
    check(-0.5, {5303.0, 10606.0}, true);
}

TEST_CASE("trachea measured vs analytic magnitude within 0.3 dB") {
    const double rs[] = {-0.9, -0.5, 0.5, 0.9};
    const double Ls[] = {20.0, 33.0, 80.0};
    const double freqs[] = {50.0,  80.0,  120.0, 200.0,  400.0,  800.0,  1500.0,
                            2000.0, 2651.0, 4000.0, 5303.0, 8000.0, 10000.0, 12000.0};
    for (double r : rs) {
        for (double L : Ls) {
            const auto h = impulse_response(L, r, 0.0, 1.0, kRate, 1.0);
            const auto X = rfft_mag(h, kRate);
            const double tau = tau_of(L);
            const int nfft = static_cast<int>(X.size());
            double worst = 0.0;
            for (double f : freqs) {
                const double Ha = analytic_mag(f, 1.0, r, tau);
                if (Ha < 1e-3) {
                    continue; // skip deep notches
                }
                const double k = f * static_cast<double>(nfft) / kRate;
                const int k0 = std::clamp(static_cast<int>(std::floor(k)), 1, nfft / 2 - 2);
                const double frac = k - static_cast<double>(k0);
                const double m0 = std::abs(X[static_cast<unsigned>(k0)]);
                const double m1 = std::abs(X[static_cast<unsigned>(k0 + 1)]);
                const double Hm = m0 + frac * (m1 - m0);
                const double err = std::abs(20.0 * std::log10(std::max(Hm, 1e-18) / Ha));
                worst = std::max(worst, err);
                INFO("r=" << r << " L=" << L << " f=" << f << " Hm=" << Hm << " Ha=" << Ha
                          << " err_dB=" << err);
                REQUIRE(err <= 0.3);
            }
            REQUIRE(worst <= 0.3);
        }
    }
}

TEST_CASE("trachea loss drops the ~8 kHz peak by at least 6 dB") {
    constexpr double L = 33.0;
    constexpr double r = 0.5;
    const auto h0 = impulse_response(L, r, 0.0, 1.0, kRate, 1.0);
    const auto h1 = impulse_response(L, r, 1.0, 1.0, kRate, 1.0);
    auto windowed_fft = [](const std::vector<double>& h) {
        std::vector<double> w = h;
        const int n = static_cast<int>(w.size());
        for (int i = 0; i < n; ++i) {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                                     static_cast<double>(n - 1));
            w[static_cast<unsigned>(i)] *= hann;
        }
        return rfft_mag(w, kRate);
    };
    const auto X0 = windowed_fft(h0);
    const auto X1 = windowed_fft(h1);
    const int nfft = static_cast<int>(X0.size());
    auto mag_at = [&](const std::vector<std::complex<double>>& X, double f) {
        const double k = f * static_cast<double>(nfft) / kRate;
        const int k0 = std::clamp(static_cast<int>(std::floor(k)), 1, nfft / 2 - 2);
        const double frac = k - static_cast<double>(k0);
        const double m0 = std::abs(X[static_cast<unsigned>(k0)]);
        const double m1 = std::abs(X[static_cast<unsigned>(k0 + 1)]);
        return m0 + frac * (m1 - m0);
    };
    const double m0 = mag_at(X0, 7955.0);
    const double m1 = mag_at(X1, 7955.0);
    const double drop = 20.0 * std::log10(std::max(m0, 1e-18) / std::max(m1, 1e-18));
    INFO("mag0=" << m0 << " mag1=" << m1 << " drop_dB=" << drop);
    REQUIRE(drop >= 6.0);
}

TEST_CASE("trachea stability: 10 s white noise, random smooth params") {
    cuicui::model::Trachea tr;
    tr.prepare(kRate, cuicui::model::kTracheaMaxLengthMm);
    tr.reset();
    std::mt19937 rng(2);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double r = 0.0, loss = 0.3, L = 33.0;
    double rT = 0.0, lossT = 0.3, LT = 33.0;
    const double coeff = 1.0 - std::exp(-1.0 / (0.05 * kRate));
    const int n = static_cast<int>(std::llround(10.0 * kRate));
    double peakIn = 0.0, peakOut = 0.0;
    bool finite = true;
    for (int i = 0; i < n; ++i) {
        if (i % 64 == 0) {
            rT = -0.95 + 1.9 * uni(rng);
            lossT = uni(rng);
            LT = 10.0 + 110.0 * uni(rng);
        }
        r += coeff * (rT - r);
        loss += coeff * (lossT - loss);
        L += coeff * (LT - L);
        tr.setParams(L, r, loss, 1.0);
        const double x = 2.0 * uni(rng) - 1.0;
        const double y = tr.process(x);
        peakIn = std::max(peakIn, std::abs(x));
        peakOut = std::max(peakOut, std::abs(y));
        if (!std::isfinite(y)) {
            finite = false;
            break;
        }
    }
    INFO("peakIn=" << peakIn << " peakOut=" << peakOut << " ratio=" << peakOut / std::max(peakIn, 1e-12));
    REQUIRE(finite);
    REQUIRE(peakOut < 40.0 * peakIn);
}

TEST_CASE("trachea length modulation: HF energy 60 dB below 500 Hz") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 32;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Test);
    voice.setTracheaEnabled(true);
    voice.setTestWaveform(cuicui::TestWaveform::Sine);
    voice.setTestRamp(500.0f, 500.0f, 0.0f, 0.0f);
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);
    cuicui::VoiceSettings s;
    s.trachea_length_mm = 20.0;
    s.trachea_reflection = 0.5;
    s.trachea_loss = 0.0;
    s.trachea_input_gain = 1.0;
    s.out_gain_db = 0.0;
    voice.setSettings(s);
    voice.reset(1);

    constexpr int sr = 48000;
    constexpr int n = sr; // 1 s
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    constexpr int kBlock = 32;
    cuicui::ControlFrame from, to;
    for (int i = 0; i < n;) {
        const int ns = std::min(kBlock, n - i);
        const double t1 = static_cast<double>(i + ns) / static_cast<double>(sr);
        s.trachea_length_mm = 20.0 + 40.0 * t1;
        voice.setSettings(s);
        voice.setTestRamp(500.0f, 500.0f, 0.0f, 0.0f);
        voice.setOutGainRamp(0.0f, 0.0f);
        voice.process(from, to, buf.data() + i, ns);
        i += ns;
    }

    constexpr int skip = 4800; // 100 ms
    const int nfft = next_pow2(n - skip);
    std::vector<std::complex<double>> X(static_cast<std::size_t>(nfft), 0.0);
    for (int i = 0; i < n - skip; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                                 static_cast<double>(n - skip - 1));
        X[static_cast<unsigned>(i)] = static_cast<double>(buf[static_cast<unsigned>(skip + i)]) * hann;
    }
    fft_radix2(X);
    const int k500 = static_cast<int>(std::llround(500.0 * static_cast<double>(nfft) / sr));
    double e500 = 0.0;
    for (int k = k500 - 2; k <= k500 + 2; ++k) {
        const double m = std::abs(X[static_cast<unsigned>(k)]);
        e500 += m * m;
    }
    double eHi = 0.0;
    const int k15 = static_cast<int>(std::llround(15000.0 * static_cast<double>(nfft) / sr));
    for (int k = k15; k < nfft / 2; ++k) {
        const double m = std::abs(X[static_cast<unsigned>(k)]);
        eHi += m * m;
    }
    const double db = 10.0 * std::log10(std::max(e500, 1e-30) / std::max(eHi, 1e-30));
    INFO("e500=" << e500 << " eHi=" << eHi << " ratio_dB=" << db);
    REQUIRE(db > 60.0);
}
