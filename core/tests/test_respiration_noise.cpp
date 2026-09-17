#include "cuicui/dsp/rng.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/noise_sources.hpp"
#include "cuicui/model/respiratory_drive.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kIntRate = 192000.0;

double crossing_time(double t0, double y0, double t1, double y1, double target) {
    const double dy = y1 - y0;
    if (std::abs(dy) < 1e-18) {
        return t1;
    }
    const double u = (target - y0) / dy;
    return t0 + u * (t1 - t0);
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

double minus3db_hz(const std::vector<double>& x, double rate) {
    constexpr int nfft = 8192;
    constexpr int hop = 4096;
    const int n = static_cast<int>(x.size());
    std::vector<double> acc(static_cast<std::size_t>(nfft / 2 + 1), 0.0);
    int frames = 0;
    std::vector<std::complex<double>> buf(static_cast<std::size_t>(nfft));
    for (int start = 0; start + nfft <= n; start += hop) {
        for (int i = 0; i < nfft; ++i) {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                                     static_cast<double>(nfft - 1));
            buf[static_cast<unsigned>(i)] = x[static_cast<unsigned>(start + i)] * hann;
        }
        fft_radix2(buf);
        for (int k = 0; k <= nfft / 2; ++k) {
            acc[static_cast<unsigned>(k)] += std::norm(buf[static_cast<unsigned>(k)]);
        }
        ++frames;
    }
    REQUIRE(frames > 4);
    const double p0 = acc[0] / static_cast<double>(frames);
    const double thr = 0.5 * p0;
    for (int k = 1; k <= nfft / 2; ++k) {
        const double p = acc[static_cast<unsigned>(k)] / static_cast<double>(frames);
        const double prev = acc[static_cast<unsigned>(k - 1)] / static_cast<double>(frames);
        if (prev >= thr && p < thr) {
            const double f0 = static_cast<double>(k - 1) * rate / static_cast<double>(nfft);
            const double f1 = static_cast<double>(k) * rate / static_cast<double>(nfft);
            return crossing_time(f0, prev, f1, p, thr);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

struct PeriodStats {
    double meanHz = 0.0;
    double jitter = 0.0;
    int nPeriods = 0;
};

PeriodStats period_stats(const std::vector<double>& x, double rate, int skip) {
    PeriodStats s;
    std::vector<double> periods;
    std::vector<double> times;
    const int n = static_cast<int>(x.size());
    for (int i = std::max(skip, 1); i < n; ++i) {
        const double a = x[static_cast<unsigned>(i - 1)];
        const double b = x[static_cast<unsigned>(i)];
        if (a <= 0.0 && b > 0.0) {
            const double frac = -a / (b - a);
            times.push_back((static_cast<double>(i - 1) + frac) / rate);
        }
    }
    for (std::size_t i = 1; i < times.size(); ++i) {
        const double dt = times[i] - times[i - 1];
        if (dt > 1.0e-5 && dt < 0.02) {
            periods.push_back(dt);
        }
    }
    if (periods.size() < 16) {
        return s;
    }
    double sum = 0.0;
    for (double p : periods) {
        sum += p;
    }
    const double mean = sum / static_cast<double>(periods.size());
    double var = 0.0;
    for (double p : periods) {
        const double d = p - mean;
        var += d * d;
    }
    var /= static_cast<double>(periods.size());
    s.meanHz = 1.0 / mean;
    s.jitter = std::sqrt(var) / mean;
    s.nPeriods = static_cast<int>(periods.size());
    return s;
}

std::vector<double> run_osc_with_noise(double alpha, double beta, double sigma, std::uint64_t seed,
                                       double seconds) {
    cuicui::model::LabialOscillator osc;
    osc.prepare(kIntRate);
    osc.resetToRest(alpha, beta);
    cuicui::model::TensionNoise tn;
    tn.prepare(kIntRate);
    tn.setBandwidthHz(300.0);
    tn.reset(cuicui::dsp::deriveStreamSeed(seed, cuicui::dsp::kStreamTensionL));
    const int n = static_cast<int>(std::llround(seconds * kIntRate));
    std::vector<double> x(static_cast<std::size_t>(n), 0.0);
    constexpr double gamma = 24000.0;
    for (int i = 0; i < n; ++i) {
        const double eta = tn.process();
        osc.step(alpha, cuicui::model::clampBetaEff(beta + sigma * eta), gamma);
        x[static_cast<unsigned>(i)] = osc.x();
    }
    return x;
}

std::vector<float> render_voice(std::uint64_t seed, float pressure, float beta, float sigma, double asp,
                                double attackMs, double durationS, int sr = 48000) {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = static_cast<double>(sr);
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings s;
    s.src_gamma = 24000.0;
    s.out_gain_db = 0.0;
    s.resp_attack_ms = attackMs;
    s.resp_release_ms = 15.0;
    s.noise_aspiration = asp;
    s.noise_beta_bandwidth_hz = 300.0;
    s.seed = seed;
    voice.setSettings(s);
    voice.reset(seed);
    voice.primeLabial(cuicui::model::kAlphaRest, static_cast<double>(beta));
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);
    const int n = static_cast<int>(std::llround(durationS * sr));
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = pressure;
    from.src_L_beta = to.src_L_beta = beta;
    from.noise_beta_sigma = to.noise_beta_sigma = sigma;
    voice.process(from, to, buf.data(), n);
    return buf;
}

} // namespace

TEST_CASE("respiratory step 0->1 and 1->0 hits one time-constant") {
    const double taus[] = {2.0, 8.0, 50.0};
    for (double tauMs : taus) {
        cuicui::model::RespiratoryDrive d;
        d.prepare(kIntRate);
        d.setTimeConstantsMs(tauMs, tauMs);
        d.reset(0.0);
        const double targetUp = 1.0 - std::exp(-1.0);
        double tUp = -1.0;
        double prev = 0.0;
        const int n = static_cast<int>(std::llround(0.3 * kIntRate));
        for (int i = 0; i < n; ++i) {
            const double p = d.process(1.0).pressure;
            const double t = static_cast<double>(i + 1) / kIntRate;
            if (tUp < 0.0 && p >= targetUp) {
                tUp = crossing_time((t - 1.0 / kIntRate), prev, t, p, targetUp);
            }
            prev = p;
        }
        REQUIRE(tUp > 0.0);
        REQUIRE_THAT(tUp, WithinRel(tauMs * 0.001, 0.02));

        d.reset(1.0);
        d.setTimeConstantsMs(tauMs, tauMs);
        const double targetDn = std::exp(-1.0);
        double tDn = -1.0;
        prev = 1.0;
        for (int i = 0; i < n; ++i) {
            const double p = d.process(0.0).pressure;
            const double t = static_cast<double>(i + 1) / kIntRate;
            if (tDn < 0.0 && p <= targetDn) {
                tDn = crossing_time((t - 1.0 / kIntRate), prev, t, p, targetDn);
            }
            prev = p;
        }
        REQUIRE(tDn > 0.0);
        REQUIRE_THAT(tDn, WithinRel(tauMs * 0.001, 0.02));
        INFO("tauMs=" << tauMs << " tUp=" << tUp << " tDn=" << tDn);
    }
}

TEST_CASE("tension noise unit variance and bandwidth") {
    const double fEtas[] = {100.0, 300.0, 1000.0};
    constexpr double seconds = 10.0;
    const int skip = static_cast<int>(0.2 * kIntRate);
    for (double fEta : fEtas) {
        cuicui::model::TensionNoise tn;
        tn.prepare(kIntRate);
        tn.setBandwidthHz(fEta);
        tn.reset(cuicui::dsp::deriveStreamSeed(1, cuicui::dsp::kStreamTensionL));
        const int n = static_cast<int>(std::llround(seconds * kIntRate));
        std::vector<double> x(static_cast<std::size_t>(n - skip));
        double sum = 0.0;
        double sumsq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double y = tn.process();
            if (i >= skip) {
                x[static_cast<unsigned>(i - skip)] = y;
                sum += y;
                sumsq += y * y;
            }
        }
        const double m = static_cast<double>(x.size());
        const double mean = sum / m;
        const double var = sumsq / m - mean * mean;
        const double f3 = minus3db_hz(x, kIntRate);
        INFO("fEta=" << fEta << " var=" << var << " f3=" << f3);
        REQUIRE_THAT(var, WithinAbs(1.0, 0.05));
        REQUIRE_THAT(f3, WithinRel(fEta, 0.10));
    }
}

TEST_CASE("tension noise f0 mean and cycle jitter") {
    constexpr double alpha = 0.2;
    constexpr double beta = 0.5;
    auto quiet = run_osc_with_noise(alpha, beta, 0.0, 1, 2.0);
    auto noisy = run_osc_with_noise(alpha, beta, 0.02, 1, 2.0);
    const int skip = static_cast<int>(0.2 * kIntRate);
    const auto q = period_stats(quiet, kIntRate, skip);
    const auto n = period_stats(noisy, kIntRate, skip);
    INFO("quiet f0=" << q.meanHz << " jitter=" << q.jitter << " n=" << q.nPeriods);
    INFO("noisy f0=" << n.meanHz << " jitter=" << n.jitter << " n=" << n.nPeriods);
    REQUIRE(q.nPeriods > 50);
    REQUIRE(n.nPeriods > 50);
    REQUIRE_THAT(n.meanHz, WithinRel(q.meanHz, 0.01));
    REQUIRE(n.jitter > 5.0 * std::max(q.jitter, 1.0e-12));
}

TEST_CASE("aspiration RMS follows sqrt(p) and is zero at p=0") {
    cuicui::model::AspirationNoise asp;
    asp.prepare(kIntRate);
    constexpr int n = static_cast<int>(kIntRate); // 1 s
    auto rms_at = [&](double G, std::uint64_t seed) {
        asp.reset(seed);
        double e = 0.0;
        for (int i = 0; i < n; ++i) {
            const double y = asp.process(G, 0.0, 1.0);
            e += y * y;
        }
        return std::sqrt(e / static_cast<double>(n));
    };
    const double r1 = rms_at(1.0, 11);
    const double r25 = rms_at(std::sqrt(0.25), 11);
    REQUIRE(r1 > 1e-6);
    REQUIRE_THAT(r25 / r1, WithinAbs(0.5, 0.05)); // ±10% of 0.5 → 0.05 abs
    asp.reset(11);
    for (int i = 0; i < 4096; ++i) {
        REQUIRE(asp.process(0.0, 0.0, 1.0) == 0.0);
    }
}

TEST_CASE("noise streams: same seed identical, L independent of R") {
    auto a = render_voice(7, 0.55f, 0.4f, 0.02f, 0.1, 8.0, 0.3);
    auto b = render_voice(7, 0.55f, 0.4f, 0.02f, 0.1, 8.0, 0.3);
    auto c = render_voice(9, 0.55f, 0.4f, 0.02f, 0.1, 8.0, 0.3);
    REQUIRE(a.size() == b.size());
    REQUIRE(std::equal(a.begin(), a.end(), b.begin()));
    bool differ = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != c[i]) {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);

    cuicui::model::TensionNoise l1, l2, r;
    const auto seedL = cuicui::dsp::deriveStreamSeed(3, cuicui::dsp::kStreamTensionL);
    const auto seedR = cuicui::dsp::deriveStreamSeed(3, cuicui::dsp::kStreamTensionR);
    l1.prepare(kIntRate);
    l2.prepare(kIntRate);
    r.prepare(kIntRate);
    l1.setBandwidthHz(300.0);
    l2.setBandwidthHz(300.0);
    r.setBandwidthHz(300.0);
    l1.reset(seedL);
    l2.reset(seedL);
    r.reset(seedR);
    for (int i = 0; i < 20000; ++i) {
        const double aL = l1.process();
        (void)r.process();
        (void)r.process();
        const double bL = l2.process();
        REQUIRE(aL == bL);
    }
}

TEST_CASE("onset latency pressure step 0.6 tau_att 8 ms (report)") {
    auto measure = [](float beta) {
        auto y = render_voice(1, 0.6f, beta, 0.0f, 0.0, 8.0, 0.4);
        const int sr = 48000;
        const int win = std::max(1, sr / 500); // 2 ms
        const int n = static_cast<int>(y.size());
        const int steady0 = n - sr / 5;
        double eSteady = 0.0;
        int nS = 0;
        for (int i = std::max(0, steady0); i < n; ++i) {
            eSteady += static_cast<double>(y[static_cast<unsigned>(i)]) *
                       static_cast<double>(y[static_cast<unsigned>(i)]);
            ++nS;
        }
        const double rmsSteady = std::sqrt(eSteady / std::max(nS, 1));
        const double thr = 0.1 * rmsSteady; // −20 dB
        for (int i = 0; i + win <= n; ++i) {
            double e = 0.0;
            for (int k = 0; k < win; ++k) {
                const double v = static_cast<double>(y[static_cast<unsigned>(i + k)]);
                e += v * v;
            }
            const double rms = std::sqrt(e / static_cast<double>(win));
            if (rms >= thr) {
                return 1000.0 * static_cast<double>(i) / static_cast<double>(sr);
            }
        }
        return -1.0;
    };
    const double tLow = measure(0.05f);
    const double tHigh = measure(0.5f);
    INFO("onset_ms beta=0.05 " << tLow << " beta=0.5 " << tHigh);
    REQUIRE(tLow >= 0.0);
    REQUIRE(tHigh >= 0.0);
}

TEST_CASE("fuzz 30 s random modulation: finite") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings s;
    voice.setSettings(s);
    voice.reset(42);
    voice.primeLabial(-0.1, 0.3);
    voice.setOutGainSmoothingMs(0.0);
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    auto lerpP = [&](const cuicui::ParamSpec& p, float u) {
        return static_cast<float>(p.min + (p.max - p.min) * static_cast<double>(u));
    };
    constexpr int sr = 48000;
    constexpr int n = sr * 30;
    constexpr int block = 64;
    std::vector<float> buf(static_cast<std::size_t>(block), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = 0.4f;
    from.src_L_beta = 0.3f;
    bool finite = true;
    for (int i = 0; i < n; i += block) {
        const int ns = std::min(block, n - i);
        to.resp_pressure = lerpP(cuicui::specOf(cuicui::ParamId::RespPressure), u01(rng));
        to.src_L_beta = lerpP(cuicui::specOf(cuicui::ParamId::SrcLBeta), u01(rng));
        to.src_L_alpha_offset = lerpP(cuicui::specOf(cuicui::ParamId::SrcLAlphaOffset), u01(rng));
        to.oec_freq_hz = lerpP(cuicui::specOf(cuicui::ParamId::OecFreqHz), u01(rng));
        to.beak_gape = lerpP(cuicui::specOf(cuicui::ParamId::BeakGape), u01(rng));
        to.noise_beta_sigma = lerpP(cuicui::specOf(cuicui::ParamId::NoiseBetaSigma), u01(rng));
        s.resp_attack_ms = lerpP(cuicui::specOf(cuicui::ParamId::RespAttackMs), u01(rng));
        s.resp_release_ms = lerpP(cuicui::specOf(cuicui::ParamId::RespReleaseMs), u01(rng));
        s.noise_beta_bandwidth_hz = lerpP(cuicui::specOf(cuicui::ParamId::NoiseBetaBandwidthHz), u01(rng));
        s.noise_aspiration = lerpP(cuicui::specOf(cuicui::ParamId::NoiseAspiration), u01(rng));
        s.trachea_length_mm = lerpP(cuicui::specOf(cuicui::ParamId::TracheaLengthMm), u01(rng));
        s.trachea_reflection = lerpP(cuicui::specOf(cuicui::ParamId::TracheaReflection), u01(rng));
        s.trachea_loss = lerpP(cuicui::specOf(cuicui::ParamId::TracheaLoss), u01(rng));
        s.oec_damping = lerpP(cuicui::specOf(cuicui::ParamId::OecDamping), u01(rng));
        s.beak_damping = lerpP(cuicui::specOf(cuicui::ParamId::BeakDamping), u01(rng));
        s.out_radiation = lerpP(cuicui::specOf(cuicui::ParamId::OutRadiation), u01(rng));
        voice.setSettings(s);
        voice.setOutGainRamp(0.0f, 0.0f);
        voice.process(from, to, buf.data(), ns);
        for (int k = 0; k < ns; ++k) {
            if (!std::isfinite(buf[static_cast<unsigned>(k)])) {
                finite = false;
            }
        }
        from = to;
    }
    REQUIRE(finite);
    REQUIRE(voice.oscillator().nonFiniteResets() == 0);
}

TEST_CASE("splitmix stream seeds are stable when R is reserved") {
    const auto master = 12345ULL;
    const auto l = cuicui::dsp::deriveStreamSeed(master, cuicui::dsp::kStreamTensionL);
    const auto r = cuicui::dsp::deriveStreamSeed(master, cuicui::dsp::kStreamTensionR);
    const auto a = cuicui::dsp::deriveStreamSeed(master, cuicui::dsp::kStreamAspiration);
    REQUIRE(l != r);
    REQUIRE(l != a);
    REQUIRE(r != a);
    REQUIRE(cuicui::dsp::deriveStreamSeed(master, 0) == l);
    REQUIRE(cuicui::dsp::deriveStreamSeed(master, 2) == a);
}
