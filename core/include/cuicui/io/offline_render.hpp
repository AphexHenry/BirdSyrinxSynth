#pragma once

#include "cuicui/io/render_spec.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/params.hpp"

#include <cmath>
#include <vector>

namespace cuicui::io {

struct RenderOptions {
    bool apply_smoothing = true;
    int oversampling_override = -1; // -1 = use spec, 0 = auto, 1/2/4/8 = factor
};

inline int oversampling_from_spec(const RenderSpec& spec, const RenderOptions& opt) {
    if (opt.oversampling_override >= 0) {
        return opt.oversampling_override;
    }
    int os = 0;
    if (!parseOversamplingToken(spec.oversampling, os)) {
        os = 0;
    }
    return os;
}

/// Render a spec through the skeleton BirdVoice (test oscillator + oversampling).
inline std::vector<float> render_offline(const RenderSpec& spec, const RenderOptions& opt = {}) {
    const int sr = spec.sample_rate;
    const int n = static_cast<int>(std::llround(spec.duration_s * sr));
    std::vector<float> buf(n > 0 ? static_cast<std::size_t>(n) : 0, 0.0f);
    if (n <= 0) {
        return buf;
    }

    const auto* freqSpec = findParam("test.freq_hz");
    const auto* gainSpec = findParam("test.gain_db");
    const auto* outSpec = findParam("out.gain_db");
    const double defFreq = freqSpec ? freqSpec->def : 440.0;
    const double defGain = gainSpec ? gainSpec->def : -12.0;
    const double defOut = outSpec ? outSpec->def : 0.0;

    const auto freqA = spec.automationFor("test.freq_hz", defFreq);
    const auto gainA = spec.automationFor("test.gain_db", defGain);
    const auto outA = spec.automationFor("out.gain_db", defOut);

    ProcessSpec ps;
    ps.hostSampleRate = static_cast<double>(sr);
    ps.maxBlockSize = 64;
    ps.oversampling = oversampling_from_spec(spec, opt);

    BirdVoice voice;
    voice.prepare(ps);
    voice.reset(spec.seed);
    TestWaveform wave = TestWaveform::Sine;
    if (!parseTestWaveform(spec.waveform, wave)) {
        wave = TestWaveform::Sine;
    }
    voice.setTestWaveform(wave);
    if (!opt.apply_smoothing) {
        voice.setOutGainSmoothingMs(0.0);
    }

    VoiceSettings settings;
    settings.seed = spec.seed;
    settings.out_gain_db = spec.staticParam("out.gain_db", defOut);
    voice.setSettings(settings);

    const double dt = 1.0 / static_cast<double>(sr);
    constexpr int kBlock = 32;
    for (int i = 0; i < n;) {
        const int ns = std::min(kBlock, n - i);
        const double t0 = static_cast<double>(i) * dt;
        const double t1 = static_cast<double>(i + ns) * dt;
        auto clampP = [](const ParamSpec* s, double v) {
            return s ? clampParam(*s, v) : v;
        };
        const float f0 = static_cast<float>(clampP(freqSpec, freqA.valueAt(t0)));
        const float f1 = static_cast<float>(clampP(freqSpec, freqA.valueAt(t1)));
        const float g0 = static_cast<float>(clampP(gainSpec, gainA.valueAt(t0)));
        const float g1 = static_cast<float>(clampP(gainSpec, gainA.valueAt(t1)));
        const float o0 = static_cast<float>(clampP(outSpec, outA.valueAt(t0)));
        const float o1 = static_cast<float>(clampP(outSpec, outA.valueAt(t1)));
        voice.setTestRamp(f0, f1, g0, g1);
        voice.setOutGainRamp(o0, o1);
        ControlFrame from, to;
        voice.process(from, to, buf.data() + i, ns);
        i += ns;
    }
    return buf;
}

} // namespace cuicui::io
