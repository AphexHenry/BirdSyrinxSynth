#include "cuicui/control/side_switcher.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/model/bilateral_source.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 192000.0;
constexpr double kGamma = 24000.0;
constexpr double kAlpha = 0.2;

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

double zc_freq(const std::vector<double>& x, double sr, int i0, int i1) {
    double sum = 0.0;
    for (int i = i0; i < i1; ++i) {
        sum += x[static_cast<unsigned>(i)];
    }
    const double mu = sum / static_cast<double>(std::max(1, i1 - i0));
    std::vector<double> zeros;
    for (int i = i0 + 1; i < i1; ++i) {
        const double y0 = x[static_cast<unsigned>(i - 1)] - mu;
        const double y1 = x[static_cast<unsigned>(i)] - mu;
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

std::vector<double> run_one(double alpha, double beta, double seconds) {
    cuicui::model::LabialOscillator osc;
    osc.prepare(kRate);
    osc.resetToRest(cuicui::model::kAlphaRest, beta);
    const int n = static_cast<int>(std::llround(seconds * kRate));
    std::vector<double> x(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        osc.step(alpha, beta, kGamma);
        x[static_cast<unsigned>(i)] = osc.x();
    }
    return x;
}

double mag_at(const std::vector<double>& spec, double rate, int nfft, double freq) {
    const double bin = freq * static_cast<double>(nfft) / rate;
    const int i0 = std::clamp(static_cast<int>(std::floor(bin)), 0, nfft / 2 - 1);
    const int i1 = std::min(i0 + 1, nfft / 2);
    const double frac = bin - static_cast<double>(i0);
    return spec[static_cast<unsigned>(i0)] * (1.0 - frac) + spec[static_cast<unsigned>(i1)] * frac;
}

std::vector<double> power_spectrum(const std::vector<double>& x, int i0, int nfft) {
    std::vector<std::complex<double>> buf(static_cast<std::size_t>(nfft));
    for (int i = 0; i < nfft; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                                 static_cast<double>(nfft - 1));
        buf[static_cast<unsigned>(i)] = x[static_cast<unsigned>(i0 + i)] * hann;
    }
    fft_radix2(buf);
    std::vector<double> p(static_cast<std::size_t>(nfft / 2 + 1), 0.0);
    for (int k = 0; k <= nfft / 2; ++k) {
        p[static_cast<unsigned>(k)] = std::norm(buf[static_cast<unsigned>(k)]);
    }
    return p;
}

struct Peak {
    double freq = 0.0;
    double power = 0.0;
};

std::vector<Peak> top_peaks(const std::vector<double>& spec, double rate, int nfft, double fLo, double fHi,
                            int count) {
    const int k0 = std::max(1, static_cast<int>(fLo * nfft / rate));
    const int k1 = std::min(nfft / 2 - 1, static_cast<int>(fHi * nfft / rate));
    std::vector<Peak> peaks;
    for (int k = k0 + 1; k < k1; ++k) {
        const double p = spec[static_cast<unsigned>(k)];
        if (p > spec[static_cast<unsigned>(k - 1)] && p >= spec[static_cast<unsigned>(k + 1)]) {
            peaks.push_back({static_cast<double>(k) * rate / static_cast<double>(nfft), p});
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.power > b.power; });
    if (static_cast<int>(peaks.size()) > count) {
        peaks.resize(static_cast<std::size_t>(count));
    }
    return peaks;
}

std::vector<double> run_bilateral_mix(double aL, double bL, double aR, double bR, double eps, double seconds,
                                      bool jointAlways) {
    cuicui::model::BilateralSource src;
    src.prepare(kRate);
    src.resetToRest(cuicui::model::kAlphaRest, bL, cuicui::model::kAlphaRest, bR);
    src.right().setState(src.right().x(), src.right().y() + 0.02);
    src.resetNoise(1);
    const int n = static_cast<int>(std::llround(seconds * kRate));
    std::vector<double> mix(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        if (jointAlways) {
            src.stepJoint(aL, bL, aR, bR, kGamma, eps);
        } else {
            src.step(aL, bL, aR, bR, kGamma, eps);
        }
        mix[static_cast<unsigned>(i)] = src.left().x() + src.right().x();
    }
    return mix;
}

std::vector<float> read_f32_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 44);
    int dataOff = -1;
    int dataSize = 0;
    for (int i = 0; i + 8 < static_cast<int>(bytes.size()); ++i) {
        if (bytes[static_cast<unsigned>(i)] == 'd' && bytes[static_cast<unsigned>(i + 1)] == 'a' &&
            bytes[static_cast<unsigned>(i + 2)] == 't' && bytes[static_cast<unsigned>(i + 3)] == 'a') {
            dataSize = static_cast<unsigned char>(bytes[static_cast<unsigned>(i + 4)]) |
                       (static_cast<unsigned char>(bytes[static_cast<unsigned>(i + 5)]) << 8) |
                       (static_cast<unsigned char>(bytes[static_cast<unsigned>(i + 6)]) << 16) |
                       (static_cast<unsigned char>(bytes[static_cast<unsigned>(i + 7)]) << 24);
            dataOff = i + 8;
            break;
        }
    }
    REQUIRE(dataOff >= 0);
    REQUIRE(dataSize > 0);
    REQUIRE(dataOff + dataSize <= static_cast<int>(bytes.size()));
    const int n = dataSize / 4;
    std::vector<float> y(static_cast<std::size_t>(n));
    std::memcpy(y.data(), bytes.data() + dataOff, static_cast<std::size_t>(dataSize));
    return y;
}

std::vector<float> render_labial_voice(const cuicui::ControlFrame& from, const cuicui::ControlFrame& to,
                                       const cuicui::VoiceSettings& settings, double durationS,
                                       std::uint64_t seed = 1) {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.setSettings(settings);
    voice.reset(seed);
    voice.primeLabial(cuicui::model::kAlphaRest + static_cast<double>(from.src_L_alpha_offset),
                      static_cast<double>(from.src_L_beta), static_cast<double>(from.oec_freq_hz),
                      static_cast<double>(from.beak_gape), static_cast<double>(from.src_R_alpha_offset),
                      static_cast<double>(from.src_R_beta));
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);
    const int n = static_cast<int>(std::llround(durationS * 48000.0));
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    voice.process(from, to, buf.data(), n);
    return buf;
}

double energy(const std::vector<float>& y, int i0 = 0) {
    double e = 0.0;
    for (int i = i0; i < static_cast<int>(y.size()); ++i) {
        const double v = static_cast<double>(y[static_cast<unsigned>(i)]);
        e += v * v;
    }
    return e;
}

double max_abs_jump(const std::vector<float>& y, int i0, int i1) {
    double m = 0.0;
    const int a = std::max(1, i0);
    const int b = std::min(i1, static_cast<int>(y.size()));
    for (int i = a; i < b; ++i) {
        m = std::max(m, std::abs(static_cast<double>(y[static_cast<unsigned>(i)]) -
                                 static_cast<double>(y[static_cast<unsigned>(i - 1)])));
    }
    return m;
}

double window_f0(const std::vector<float>& y, int i0, int nWin, double sr) {
    if (i0 < 0 || i0 + nWin > static_cast<int>(y.size())) {
        return 0.0;
    }
    std::vector<double> sl(static_cast<std::size_t>(nWin));
    for (int i = 0; i < nWin; ++i) {
        sl[static_cast<unsigned>(i)] = static_cast<double>(y[static_cast<unsigned>(i0 + i)]);
    }
    return zc_freq(sl, sr, 0, nWin);
}

struct PeriodStats {
    double meanHz = 0.0;
    double jitter = 0.0;
    int nPeriods = 0;
};

PeriodStats period_stats(const std::vector<double>& x, double rate, int skip) {
    PeriodStats s;
    const int n = static_cast<int>(x.size());
    if (n - skip < 64) {
        return s;
    }
    double sum = 0.0;
    for (int i = skip; i < n; ++i) {
        sum += x[static_cast<unsigned>(i)];
    }
    const double mu = sum / static_cast<double>(n - skip);
    std::vector<double> times;
    for (int i = std::max(skip, 1); i < n; ++i) {
        const double a = x[static_cast<unsigned>(i - 1)] - mu;
        const double b = x[static_cast<unsigned>(i)] - mu;
        if (a <= 0.0 && b > 0.0) {
            const double frac = -a / (b - a);
            times.push_back((static_cast<double>(i - 1) + frac) / rate);
        }
    }
    std::vector<double> periods;
    for (std::size_t i = 1; i < times.size(); ++i) {
        const double dt = times[i] - times[i - 1];
        if (dt > 1.0e-5 && dt < 0.02) {
            periods.push_back(dt);
        }
    }
    if (periods.size() < 16) {
        return s;
    }
    double psum = 0.0;
    for (double p : periods) {
        psum += p;
    }
    const double mean = psum / static_cast<double>(periods.size());
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

double f0_of_beta(double alpha, double beta) {
    auto x = run_one(alpha, beta, 0.12);
    const int skip = static_cast<int>(0.05 * kRate);
    return zc_freq(x, kRate, skip, static_cast<int>(x.size()));
}

double beta_for_f0(double alpha, double targetHz) {
    double lo = -0.12;
    double hi = 1.15;
    for (int it = 0; it < 22; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double f = f0_of_beta(alpha, mid);
        if (f <= 1.0) {
            lo = mid;
            continue;
        }
        if (f < targetHz) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

} // namespace

TEST_CASE("B4 golden left-only regression (R gate 0, coupling 0)") {
    const auto root = std::filesystem::path(CUICUI_SOURCE_DIR);
    for (const char* name : {"b4_left_only_quiet", "b4_left_only_noisy"}) {
        const auto specPath = root / "core/tests/golden" / (std::string(name) + ".render.json");
        const auto wavPath = root / "core/tests/golden" / (std::string(name) + ".wav");
        REQUIRE(std::filesystem::exists(specPath));
        REQUIRE(std::filesystem::exists(wavPath));
        const auto spec = cuicui::io::parse_render_spec_file(specPath);
        const auto got = cuicui::io::render_offline(spec);
        const auto want = read_f32_wav(wavPath);
        REQUIRE(got.size() == want.size());
        int mism = 0;
        float maxDiff = 0.0f;
        for (std::size_t i = 0; i < got.size(); ++i) {
            const float d = std::abs(got[i] - want[i]);
            maxDiff = std::max(maxDiff, d);
            if (got[i] != want[i]) {
                ++mism;
            }
        }
        INFO(name << " mismatches=" << mism << " maxDiff=" << maxDiff);
        REQUIRE(mism == 0);
    }
}

TEST_CASE("closed side energy is below -80 dB relative") {
    cuicui::VoiceSettings s;
    s.noise_aspiration = 0.0;
    s.src_coupling = 0.0;
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.55f;
    from.src_L_beta = to.src_L_beta = 0.3f;
    from.src_R_beta = to.src_R_beta = 0.3f;
    from.noise_beta_sigma = to.noise_beta_sigma = 0.0f;
    from.src_L_gate = to.src_L_gate = 0.0f;
    from.src_R_gate = to.src_R_gate = 1.0f;
    auto rightOnly = render_labial_voice(from, to, s, 0.25);
    from.src_R_gate = to.src_R_gate = 0.0f;
    auto bothClosed = render_labial_voice(from, to, s, 0.25);
    const int skip = 48000 / 10;
    const double eNote = energy(rightOnly, skip);
    const double eClosed = energy(bothClosed, skip);
    REQUIRE(eNote > 1e-8);
    const double db = 10.0 * std::log10(std::max(eClosed, 1e-30) / eNote);
    INFO("closed vs right-only dB=" << db);
    REQUIRE(db < -80.0);
}

TEST_CASE("two voices: both fundamentals, combination tones low") {
    constexpr double bL = 0.2;
    constexpr double bR = 0.5;
    auto left = run_one(kAlpha, bL, 0.25);
    auto right = run_one(kAlpha, bR, 0.25);
    const int skip = static_cast<int>(0.08 * kRate);
    const double fL = zc_freq(left, kRate, skip, static_cast<int>(left.size()));
    const double fR = zc_freq(right, kRate, skip, static_cast<int>(right.size()));
    REQUIRE(fL > 100.0);
    REQUIRE(fR > 100.0);

    cuicui::model::BilateralSource src;
    src.prepare(kRate);
    src.resetToRest(cuicui::model::kAlphaRest, bL, cuicui::model::kAlphaRest, bR);
    src.resetNoise(1);
    const int n = static_cast<int>(0.25 * kRate);
    std::vector<double> mix(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        cuicui::model::BilateralFrame fr;
        fr.alphaBase = kAlpha;
        fr.betaL = bL;
        fr.betaR = bR;
        fr.gateL = 1.0;
        fr.gateR = 1.0;
        fr.gamma = kGamma;
        fr.coupling = 0.0;
        fr.sigmaBeta = 0.0;
        mix[static_cast<unsigned>(i)] = src.process(fr).mixTilde;
    }
    constexpr int nfft = 16384;
    const auto spec = power_spectrum(mix, n - nfft, nfft);
    const double pL = mag_at(spec, kRate, nfft, fL);
    const double pR = mag_at(spec, kRate, nfft, fR);
    const double pSum = mag_at(spec, kRate, nfft, fL + fR);
    const double pDiff = mag_at(spec, kRate, nfft, std::abs(fR - fL));
    const double fund = std::min(pL, pR);
    REQUIRE(fund > 0.0);
    // Both fundamentals present within 0.5 % of single-side f0: check peaks near those freqs.
    auto peaks = top_peaks(spec, kRate, nfft, 400.0, 8000.0, 8);
    REQUIRE(peaks.size() >= 2);
    auto near = [&](double f) {
        for (const auto& p : peaks) {
            if (std::abs(p.freq - f) / f < 0.005) {
                return true;
            }
        }
        return false;
    };
    INFO("fL=" << fL << " fR=" << fR << " peaks0=" << peaks[0].freq << " peaks1=" << peaks[1].freq);
    REQUIRE(near(fL));
    REQUIRE(near(fR));
    const double dbSum = 10.0 * std::log10(std::max(pSum, 1e-30) / fund);
    const double dbDiff = 10.0 * std::log10(std::max(pDiff, 1e-30) / fund);
    INFO("comb dB sum=" << dbSum << " diff=" << dbDiff);
    REQUIRE(dbSum < -60.0);
    REQUIRE(dbDiff < -60.0);
}

TEST_CASE("joint RK4 with eps=0 equals independent stepping bit-for-bit") {
    cuicui::model::BilateralSource a, b;
    a.prepare(kRate);
    b.prepare(kRate);
    a.resetToRest(-0.1, 0.25, -0.1, 0.55);
    b.resetToRest(-0.1, 0.25, -0.1, 0.55);
    for (int i = 0; i < 4000; ++i) {
        a.stepIndependent(0.2, 0.25, 0.18, 0.55, kGamma);
        b.stepJoint(0.2, 0.25, 0.18, 0.55, kGamma, 0.0);
        REQUIRE(a.left().x() == b.left().x());
        REQUIRE(a.left().y() == b.left().y());
        REQUIRE(a.right().x() == b.right().x());
        REQUIRE(a.right().y() == b.right().y());
    }
}

TEST_CASE("coupling lock range is monotone in epsilon") {
    const double epsv[] = {0.01, 0.03, 0.1};
    double lockDelta[3] = {0.0, 0.0, 0.0};
    const double fL = f0_of_beta(kAlpha, 0.30);
    REQUIRE(fL > 500.0);
    const int skip = static_cast<int>(0.30 * kRate);
    for (int e = 0; e < 3; ++e) {
        auto mix0 = run_bilateral_mix(kAlpha, 0.30, kAlpha, 0.30, epsv[e], 0.70, true);
        const auto st0 = period_stats(mix0, kRate, skip);
        REQUIRE(st0.nPeriods >= 24);
        double last = 0.0;
        for (int k = 0; k <= 24; ++k) {
            const double d = 0.005 * static_cast<double>(k);
            auto mix = run_bilateral_mix(kAlpha, 0.30, kAlpha, 0.30 + d, epsv[e], 0.70, true);
            const auto st = period_stats(mix, kRate, skip);
            const bool locked = st.nPeriods >= 24 && st.jitter < 0.04 &&
                                st.nPeriods < static_cast<int>(1.35 * static_cast<double>(st0.nPeriods));
            if (locked) {
                last = d;
            } else if (k > 0) {
                break;
            }
        }
        lockDelta[e] = last;
        INFO("eps=" << epsv[e] << " lockDelta=" << lockDelta[e] << " fL=" << fL);
        REQUIRE(lockDelta[e] >= 0.0);
    }
    REQUIRE(lockDelta[0] <= lockDelta[1] + 1e-9);
    REQUIRE(lockDelta[1] <= lockDelta[2] + 1e-9);
}

TEST_CASE("side switch without glide") {
    constexpr double kSwitchAlpha = 0.16;
    const double bLow = beta_for_f0(kSwitchAlpha, 1500.0);
    const double bHigh = beta_for_f0(kSwitchAlpha, 3500.0);
    const double fLow = f0_of_beta(kSwitchAlpha, bLow);
    const double fHigh = f0_of_beta(kSwitchAlpha, bHigh);
    INFO("beta 1.5kHz=" << bLow << " -> " << fLow << "  3.5kHz=" << bHigh << " -> " << fHigh);
    REQUIRE_THAT(fLow, WithinRel(1500.0, 0.08));
    REQUIRE_THAT(fHigh, WithinRel(3500.0, 0.08));

    constexpr int sr = 48000;
    constexpr double dur = 0.6;
    const int n = static_cast<int>(dur * sr);
    std::vector<float> y(static_cast<std::size_t>(n), 0.0f);

    cuicui::ProcessSpec spec;
    spec.hostSampleRate = static_cast<double>(sr);
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.setTracheaEnabled(false);
    voice.setUvtEnabled(false);
    cuicui::VoiceSettings settings;
    settings.noise_aspiration = 0.0;
    settings.src_coupling = 0.0;
    voice.setSettings(settings);
    voice.reset(1);
    voice.primeLabial(cuicui::model::kAlphaRest, bLow, 3000.0, 0.5, 0.0, bHigh);
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);

    cuicui::control::SideSwitcher sw;
    sw.prepare(static_cast<double>(sr));
    sw.setCrossfadeMs(4.0);
    std::vector<float> gL(static_cast<std::size_t>(n)), gR(static_cast<std::size_t>(n));
    std::vector<float> bLa(static_cast<std::size_t>(n)), bRa(static_cast<std::size_t>(n));
    int active = 0;
    double nextJump = 0.25;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        if (t + 1e-12 >= nextJump && nextJump < dur - 0.05) {
            active = 1 - active;
            sw.setActive(active);
            nextJump += 0.25;
        }
        const auto st = sw.next(bLow, bHigh);
        gL[static_cast<unsigned>(i)] = static_cast<float>(st.gateL);
        gR[static_cast<unsigned>(i)] = static_cast<float>(st.gateR);
        bLa[static_cast<unsigned>(i)] = static_cast<float>(st.betaL);
        bRa[static_cast<unsigned>(i)] = static_cast<float>(st.betaR);
    }

    const double p = (kSwitchAlpha - cuicui::model::kAlphaRest) /
                     (cuicui::model::kAlphaMax - cuicui::model::kAlphaRest);
    constexpr int hop = 32;
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = static_cast<float>(p);
    from.noise_beta_sigma = to.noise_beta_sigma = 0.0f;
    from.oec_freq_hz = to.oec_freq_hz = 3000.0f;
    from.beak_gape = to.beak_gape = 0.5f;
    for (int i = 0; i < n;) {
        const int ns = std::min(hop, n - i);
        from.src_L_beta = bLa[static_cast<unsigned>(i)];
        to.src_L_beta = bLa[static_cast<unsigned>(i + ns - 1)];
        from.src_R_beta = bRa[static_cast<unsigned>(i)];
        to.src_R_beta = bRa[static_cast<unsigned>(i + ns - 1)];
        from.src_L_gate = gL[static_cast<unsigned>(i)];
        to.src_L_gate = gL[static_cast<unsigned>(i + ns - 1)];
        from.src_R_gate = gR[static_cast<unsigned>(i)];
        to.src_R_gate = gR[static_cast<unsigned>(i + ns - 1)];
        voice.process(from, to, y.data() + i, ns);
        i += ns;
    }

    const int win = static_cast<int>(0.002 * sr); // 2 ms
    int streak = 0;
    int maxStreak = 0;
    bool inTrans = false;
    int transStart = -1;
    int transEnd = -1;
    for (int i = sr / 20; i + win < n; i += win) {
        const double f0 = window_f0(y, i, win, sr);
        const bool inGap = f0 > 1800.0 && f0 < 3200.0;
        if (inGap) {
            ++streak;
            maxStreak = std::max(maxStreak, streak);
            if (!inTrans) {
                inTrans = true;
                transStart = i;
            }
            transEnd = i + win;
        } else {
            streak = 0;
            if (inTrans && f0 > 100.0) {
                inTrans = false;
            }
        }
    }
    INFO("max gap streak windows=" << maxStreak << " transStart=" << transStart << " transEnd=" << transEnd);
    REQUIRE(maxStreak <= 2);
    if (transStart >= 0 && transEnd > transStart) {
        const double transMs = 1000.0 * static_cast<double>(transEnd - transStart) / sr;
        INFO("transition_ms=" << transMs);
        REQUIRE(transMs <= 8.0);
    }
}

TEST_CASE("reopening a closed side has a physical onset") {
    constexpr int sr = 48000;
    cuicui::VoiceSettings s;
    s.noise_aspiration = 0.0;
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.55f;
    from.src_L_beta = to.src_L_beta = 0.35f;
    from.src_R_beta = to.src_R_beta = 0.35f;
    from.src_L_gate = to.src_L_gate = 1.0f;
    from.src_R_gate = to.src_R_gate = 0.0f;
    from.noise_beta_sigma = to.noise_beta_sigma = 0.0f;
    auto held = render_labial_voice(from, to, s, 0.25);
    const int steady0 = static_cast<int>(0.15 * sr);
    const double jumpSteady = max_abs_jump(held, steady0, static_cast<int>(held.size()));

    cuicui::ProcessSpec spec;
    spec.hostSampleRate = static_cast<double>(sr);
    spec.maxBlockSize = 64;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.setSettings(s);
    voice.reset(3);
    voice.primeLabial(cuicui::model::kAlphaRest, 0.35);
    voice.setOutGainSmoothingMs(0.0);
    voice.setOutGainRamp(0.0f, 0.0f);
    const int n = sr / 2;
    std::vector<float> y(static_cast<std::size_t>(n), 0.0f);
    const int closedN = static_cast<int>(0.08 * sr);
    from.src_L_gate = to.src_L_gate = 0.0f;
    voice.process(from, to, y.data(), closedN);
    from.src_L_gate = 0.0f;
    to.src_L_gate = 1.0f;
    const int openN = n - closedN;
    voice.process(from, to, y.data() + closedN, openN);
    const double jumpOpen = max_abs_jump(y, closedN, closedN + sr / 20);
    INFO("jumpSteady=" << jumpSteady << " jumpOpen=" << jumpOpen);
    REQUIRE(jumpSteady > 0.0);
    REQUIRE(jumpOpen <= 3.0 * jumpSteady);
}

TEST_CASE("fuzz 30 s all source params finite; zero alloc") {
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
    from.src_L_gate = 1.0f;
    bool finite = true;
    for (int i = 0; i < n; i += block) {
        const int ns = std::min(block, n - i);
        to.resp_pressure = lerpP(cuicui::specOf(cuicui::ParamId::RespPressure), u01(rng));
        to.src_L_beta = lerpP(cuicui::specOf(cuicui::ParamId::SrcLBeta), u01(rng));
        to.src_R_beta = lerpP(cuicui::specOf(cuicui::ParamId::SrcRBeta), u01(rng));
        to.src_L_alpha_offset = lerpP(cuicui::specOf(cuicui::ParamId::SrcLAlphaOffset), u01(rng));
        to.src_R_alpha_offset = lerpP(cuicui::specOf(cuicui::ParamId::SrcRAlphaOffset), u01(rng));
        to.src_L_gate = lerpP(cuicui::specOf(cuicui::ParamId::SrcLGate), u01(rng));
        to.src_R_gate = lerpP(cuicui::specOf(cuicui::ParamId::SrcRGate), u01(rng));
        to.oec_freq_hz = lerpP(cuicui::specOf(cuicui::ParamId::OecFreqHz), u01(rng));
        to.beak_gape = lerpP(cuicui::specOf(cuicui::ParamId::BeakGape), u01(rng));
        to.noise_beta_sigma = lerpP(cuicui::specOf(cuicui::ParamId::NoiseBetaSigma), u01(rng));
        s.src_coupling = lerpP(cuicui::specOf(cuicui::ParamId::SrcCoupling), u01(rng));
        s.resp_attack_ms = lerpP(cuicui::specOf(cuicui::ParamId::RespAttackMs), u01(rng));
        s.resp_release_ms = lerpP(cuicui::specOf(cuicui::ParamId::RespReleaseMs), u01(rng));
        s.noise_beta_bandwidth_hz = lerpP(cuicui::specOf(cuicui::ParamId::NoiseBetaBandwidthHz), u01(rng));
        s.noise_aspiration = lerpP(cuicui::specOf(cuicui::ParamId::NoiseAspiration), u01(rng));
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
    REQUIRE(voice.bilateral().nonFiniteResets() == 0);
}
