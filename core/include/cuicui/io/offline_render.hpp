#pragma once

#include "cuicui/io/render_spec.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/params.hpp"

#include <cmath>
#include <initializer_list>
#include <string_view>
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

inline bool spec_has_any(const RenderSpec& spec, std::initializer_list<std::string_view> keys) {
    for (const auto key : keys) {
        const std::string id(key);
        if (spec.params.count(id) != 0 || spec.automation.count(id) != 0) {
            return true;
        }
    }
    return false;
}

inline bool spec_uses_labial(const RenderSpec& spec) {
    return spec_has_any(spec, {"resp.pressure", "src.L.beta", "src.L.alpha_offset", "src.gamma"});
}

inline bool spec_uses_trachea(const RenderSpec& spec) {
    return spec_has_any(spec,
                        {"trachea.length_mm", "trachea.reflection", "trachea.loss", "trachea.input_gain"});
}

inline bool spec_uses_uvt(const RenderSpec& spec) {
    return spec_has_any(spec, {"oec.freq_hz", "oec.damping", "beak.gape", "beak.damping", "out.radiation"});
}

/// Render a spec through BirdVoice (test oscillator or B1 labial source + oversampling).
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
    const auto* pSpec = findParam("resp.pressure");
    const auto* bSpec = findParam("src.L.beta");
    const auto* oSpec = findParam("src.L.alpha_offset");
    const auto* gSpec = findParam("src.gamma");
    const auto* tLen = findParam("trachea.length_mm");
    const auto* tRef = findParam("trachea.reflection");
    const auto* tLoss = findParam("trachea.loss");
    const auto* tGain = findParam("trachea.input_gain");
    const auto* oecF = findParam("oec.freq_hz");
    const auto* oecZ = findParam("oec.damping");
    const auto* gapeS = findParam("beak.gape");
    const auto* beakZ = findParam("beak.damping");
    const auto* radS = findParam("out.radiation");
    const double defFreq = freqSpec ? freqSpec->def : 440.0;
    const double defGain = gainSpec ? gainSpec->def : -12.0;
    const double defOut = outSpec ? outSpec->def : 0.0;
    const double defP = pSpec ? pSpec->def : 0.0;
    const double defB = bSpec ? bSpec->def : 0.3;
    const double defOff = oSpec ? oSpec->def : 0.0;
    const double defGamma = gSpec ? gSpec->def : 24000.0;
    const double defTLen = tLen ? tLen->def : 33.0;
    const double defTRef = tRef ? tRef->def : 0.5;
    const double defTLoss = tLoss ? tLoss->def : 0.3;
    const double defTGain = tGain ? tGain->def : 1.0;
    const double defOecF = oecF ? oecF->def : 3000.0;
    const double defOecZ = oecZ ? oecZ->def : 0.1;
    const double defGape = gapeS ? gapeS->def : 0.5;
    const double defBeakZ = beakZ ? beakZ->def : 0.3;
    const double defRad = radS ? radS->def : 1.0;

    const auto freqA = spec.automationFor("test.freq_hz", defFreq);
    const auto gainA = spec.automationFor("test.gain_db", defGain);
    const auto outA = spec.automationFor("out.gain_db", defOut);
    const auto pA = spec.automationFor("resp.pressure", defP);
    const auto bA = spec.automationFor("src.L.beta", defB);
    const auto oA = spec.automationFor("src.L.alpha_offset", defOff);
    const auto tLenA = spec.automationFor("trachea.length_mm", defTLen);
    const auto tRefA = spec.automationFor("trachea.reflection", defTRef);
    const auto tLossA = spec.automationFor("trachea.loss", defTLoss);
    const auto tGainA = spec.automationFor("trachea.input_gain", defTGain);
    const auto oecFA = spec.automationFor("oec.freq_hz", defOecF);
    const auto oecZA = spec.automationFor("oec.damping", defOecZ);
    const auto gapeA = spec.automationFor("beak.gape", defGape);
    const auto beakZA = spec.automationFor("beak.damping", defBeakZ);
    const auto radA = spec.automationFor("out.radiation", defRad);

    ProcessSpec ps;
    ps.hostSampleRate = static_cast<double>(sr);
    ps.maxBlockSize = 64;
    ps.oversampling = oversampling_from_spec(spec, opt);

    BirdVoice voice;
    voice.prepare(ps);
    const bool labial = spec_uses_labial(spec);
    voice.setSource(labial ? VoiceSource::Labial : VoiceSource::Test);
    if (!labial) {
        TestWaveform wave = TestWaveform::Sine;
        if (!parseTestWaveform(spec.waveform, wave)) {
            wave = TestWaveform::Sine;
        }
        voice.setTestWaveform(wave);
        if (spec_uses_trachea(spec)) {
            voice.setTracheaEnabled(true);
        }
        if (spec_uses_uvt(spec)) {
            voice.setUvtEnabled(true);
        }
    }
    if (!opt.apply_smoothing) {
        voice.setOutGainSmoothingMs(0.0);
    }

    auto clampP = [](const ParamSpec* s, double v) { return s ? clampParam(*s, v) : v; };

    VoiceSettings settings;
    settings.seed = spec.seed;
    settings.out_gain_db = spec.staticParam("out.gain_db", defOut);
    settings.src_gamma = spec.staticParam("src.gamma", defGamma);
    settings.trachea_length_mm = clampP(tLen, tLenA.valueAt(0.0));
    settings.trachea_reflection = clampP(tRef, tRefA.valueAt(0.0));
    settings.trachea_loss = clampP(tLoss, tLossA.valueAt(0.0));
    settings.trachea_input_gain = clampP(tGain, tGainA.valueAt(0.0));
    settings.oec_damping = clampP(oecZ, oecZA.valueAt(0.0));
    settings.beak_damping = clampP(beakZ, beakZA.valueAt(0.0));
    settings.out_radiation = clampP(radS, radA.valueAt(0.0));
    voice.setSettings(settings);
    voice.reset(spec.seed);

    if (labial) {
        const double off0 = clampP(oSpec, oA.valueAt(0.0));
        const double b0 = clampP(bSpec, bA.valueAt(0.0));
        voice.primeLabial(model::kAlphaRest + off0, b0, clampP(oecF, oecFA.valueAt(0.0)),
                          clampP(gapeS, gapeA.valueAt(0.0)));
    }

    const double dt = 1.0 / static_cast<double>(sr);
    constexpr int kBlock = 32;
    for (int i = 0; i < n;) {
        const int ns = std::min(kBlock, n - i);
        const double t0 = static_cast<double>(i) * dt;
        const double t1 = static_cast<double>(i + ns) * dt;
        ControlFrame from, to;
        if (labial) {
            from.resp_pressure = static_cast<float>(clampP(pSpec, pA.valueAt(t0)));
            to.resp_pressure = static_cast<float>(clampP(pSpec, pA.valueAt(t1)));
            from.src_L_beta = static_cast<float>(clampP(bSpec, bA.valueAt(t0)));
            to.src_L_beta = static_cast<float>(clampP(bSpec, bA.valueAt(t1)));
            from.src_L_alpha_offset = static_cast<float>(clampP(oSpec, oA.valueAt(t0)));
            to.src_L_alpha_offset = static_cast<float>(clampP(oSpec, oA.valueAt(t1)));
            from.oec_freq_hz = static_cast<float>(clampP(oecF, oecFA.valueAt(t0)));
            to.oec_freq_hz = static_cast<float>(clampP(oecF, oecFA.valueAt(t1)));
            from.beak_gape = static_cast<float>(clampP(gapeS, gapeA.valueAt(t0)));
            to.beak_gape = static_cast<float>(clampP(gapeS, gapeA.valueAt(t1)));
        } else {
            const float f0 = static_cast<float>(clampP(freqSpec, freqA.valueAt(t0)));
            const float f1 = static_cast<float>(clampP(freqSpec, freqA.valueAt(t1)));
            const float g0 = static_cast<float>(clampP(gainSpec, gainA.valueAt(t0)));
            const float g1 = static_cast<float>(clampP(gainSpec, gainA.valueAt(t1)));
            voice.setTestRamp(f0, f1, g0, g1);
        }
        const float og0 = static_cast<float>(clampP(outSpec, outA.valueAt(t0)));
        const float og1 = static_cast<float>(clampP(outSpec, outA.valueAt(t1)));
        voice.setOutGainRamp(og0, og1);
        settings.out_gain_db = static_cast<double>(og1);
        settings.trachea_length_mm = clampP(tLen, tLenA.valueAt(t1));
        settings.trachea_reflection = clampP(tRef, tRefA.valueAt(t1));
        settings.trachea_loss = clampP(tLoss, tLossA.valueAt(t1));
        settings.trachea_input_gain = clampP(tGain, tGainA.valueAt(t1));
        settings.oec_damping = clampP(oecZ, oecZA.valueAt(t1));
        settings.beak_damping = clampP(beakZ, beakZA.valueAt(t1));
        settings.out_radiation = clampP(radS, radA.valueAt(t1));
        voice.setSettings(settings);
        voice.process(from, to, buf.data() + i, ns);
        i += ns;
    }
    return buf;
}

} // namespace cuicui::io
