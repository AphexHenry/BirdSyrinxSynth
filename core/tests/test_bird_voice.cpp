#include "cuicui/model/bird_voice.hpp"
#include "cuicui/params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

std::vector<float> render_saw(int os, double freq, double duration, int sr = 48000) {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = static_cast<double>(sr);
    spec.maxBlockSize = 256;
    spec.oversampling = os;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.reset(1);
    voice.setTestWaveform(cuicui::TestWaveform::Saw);
    const float f = static_cast<float>(freq);
    voice.setTestRamp(f, f, -6.0f, -6.0f);
    voice.setOutGainRamp(0.0f, 0.0f);
    voice.setOutGainSmoothingMs(0.0);
    const int n = static_cast<int>(std::lround(duration * sr));
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    cuicui::ControlFrame a, b;
    voice.process(a, b, buf.data(), n);
    return buf;
}

double goertzel_power(const std::vector<float>& x, double freq, double sr, int skip) {
    const int n = static_cast<int>(x.size());
    if (skip >= n - 32) {
        return 0.0;
    }
    const int nWin = n - skip;
    const double w = 2.0 * 3.14159265358979323846 * freq / sr;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < nWin; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) /
                                                 static_cast<double>(nWin - 1));
        const double v = static_cast<double>(x[static_cast<std::size_t>(skip + i)]) * hann;
        s0 = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return p / static_cast<double>(nWin * nWin);
}

} // namespace

TEST_CASE("naive saw 3 kHz: OS 4 reduces inharmonic energy by >= 40 dB vs OS 1") {
    // Exact 3 kHz aliases onto harmonics at 48 kHz. 3171 Hz puts the first wrap at 2736 Hz.
    constexpr double f0 = 3171.0;
    constexpr double alias_hz = 16.0 * f0 - 48000.0; // 2736 Hz
    const auto os1 = render_saw(1, f0, 0.4);
    const auto os4 = render_saw(4, f0, 0.4);
    const int skip = 2048;
    const double p1 = goertzel_power(os1, alias_hz, 48000.0, skip);
    const double p4 = goertzel_power(os4, alias_hz, 48000.0, skip);
    const double db = 10.0 * std::log10(std::max(p1, 1e-30) / std::max(p4, 1e-30));
    INFO("alias=" << alias_hz << " P_os1=" << p1 << " P_os4=" << p4 << " reduction_dB=" << db);
    REQUIRE(db >= 40.0);
}

TEST_CASE("BirdVoice OS auto at 48 kHz is 4") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.oversampling = 0;
    cuicui::BirdVoice v;
    v.prepare(spec);
    REQUIRE(v.oversamplingFactor() == 4);
}
