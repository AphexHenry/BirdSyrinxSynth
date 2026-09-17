#pragma once

#include "cuicui/io/render_spec.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/params.hpp"

#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cuicui::io {

struct RenderOptions {
    bool apply_smoothing = true;
    int oversampling_override = -1; // -1 = use spec, 0 = auto, 1/2/4/8 = factor
    int max_block_size = 64;        // BirdVoice prepare only; CLI still ramps 1 host sample
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
    return spec_has_any(spec, {"resp.pressure", "src.L.beta", "src.L.alpha_offset", "src.gamma",
                               "src.R.beta", "src.R.alpha_offset", "src.L.gate", "src.R.gate",
                               "src.coupling", "resp.attack_ms", "noise.beta_sigma", "noise.aspiration",
                               "body.size"});
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
    const auto* attS = findParam("resp.attack_ms");
    const auto* relS = findParam("resp.release_ms");
    const auto* sigS = findParam("noise.beta_sigma");
    const auto* bwS = findParam("noise.beta_bandwidth_hz");
    const auto* aspS = findParam("noise.aspiration");
    const auto* bRS = findParam("src.R.beta");
    const auto* oRS = findParam("src.R.alpha_offset");
    const auto* gLS = findParam("src.L.gate");
    const auto* gRS = findParam("src.R.gate");
    const auto* cplS = findParam("src.coupling");
    const auto* sizeS = findParam("body.size");
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
    const double defAtt = attS ? attS->def : 8.0;
    const double defRel = relS ? relS->def : 15.0;
    const double defSig = sigS ? sigS->def : 0.005;
    const double defBw = bwS ? bwS->def : 300.0;
    const double defAsp = aspS ? aspS->def : 0.02;
    const double defBR = bRS ? bRS->def : 0.3;
    const double defOffR = oRS ? oRS->def : 0.0;
    const double defGL = gLS ? gLS->def : 1.0;
    const double defGR = gRS ? gRS->def : 0.0;
    const double defCpl = cplS ? cplS->def : 0.0;
    const double defSize = sizeS ? sizeS->def : 1.0;

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
    const auto attA = spec.automationFor("resp.attack_ms", defAtt);
    const auto relA = spec.automationFor("resp.release_ms", defRel);
    const auto sigA = spec.automationFor("noise.beta_sigma", defSig);
    const auto bwA = spec.automationFor("noise.beta_bandwidth_hz", defBw);
    const auto aspA = spec.automationFor("noise.aspiration", defAsp);
    const auto bRA = spec.automationFor("src.R.beta", defBR);
    const auto oRA = spec.automationFor("src.R.alpha_offset", defOffR);
    const auto gLA = spec.automationFor("src.L.gate", defGL);
    const auto gRA = spec.automationFor("src.R.gate", defGR);
    const auto cplA = spec.automationFor("src.coupling", defCpl);
    const auto sizeA = spec.automationFor("body.size", defSize);

    ProcessSpec ps;
    ps.hostSampleRate = static_cast<double>(sr);
    ps.maxBlockSize = opt.max_block_size > 0 ? opt.max_block_size : 64;
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
    settings.resp_attack_ms = clampP(attS, attA.valueAt(0.0));
    settings.resp_release_ms = clampP(relS, relA.valueAt(0.0));
    settings.noise_beta_bandwidth_hz = clampP(bwS, bwA.valueAt(0.0));
    settings.noise_aspiration = clampP(aspS, aspA.valueAt(0.0));
    settings.src_coupling = clampP(cplS, cplA.valueAt(0.0));
    settings.body_size = clampP(sizeS, sizeA.valueAt(0.0));
    settings.oversampling = ps.oversampling;
    settings.seed = spec.seed;
    voice.setSettings(settings);
    voice.reset(spec.seed);

    if (labial) {
        const double off0 = clampP(oSpec, oA.valueAt(0.0));
        const double b0 = clampP(bSpec, bA.valueAt(0.0));
        voice.primeLabial(model::kAlphaRest + off0, b0, clampP(oecF, oecFA.valueAt(0.0)),
                          clampP(gapeS, gapeA.valueAt(0.0)), clampP(oRS, oRA.valueAt(0.0)),
                          clampP(bRS, bRA.valueAt(0.0)));
    }

    const double dt = 1.0 / static_cast<double>(sr);
    // Per host sample (ramps of length 1) so results are independent of maxBlockSize. WP-B6 §4.3.
    for (int i = 0; i < n; ++i) {
        const double t0 = static_cast<double>(i) * dt;
        const double t1 = static_cast<double>(i + 1) * dt;
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
            from.noise_beta_sigma = static_cast<float>(clampP(sigS, sigA.valueAt(t0)));
            to.noise_beta_sigma = static_cast<float>(clampP(sigS, sigA.valueAt(t1)));
            from.src_R_beta = static_cast<float>(clampP(bRS, bRA.valueAt(t0)));
            to.src_R_beta = static_cast<float>(clampP(bRS, bRA.valueAt(t1)));
            from.src_R_alpha_offset = static_cast<float>(clampP(oRS, oRA.valueAt(t0)));
            to.src_R_alpha_offset = static_cast<float>(clampP(oRS, oRA.valueAt(t1)));
            from.src_L_gate = static_cast<float>(clampP(gLS, gLA.valueAt(t0)));
            to.src_L_gate = static_cast<float>(clampP(gLS, gLA.valueAt(t1)));
            from.src_R_gate = static_cast<float>(clampP(gRS, gRA.valueAt(t0)));
            to.src_R_gate = static_cast<float>(clampP(gRS, gRA.valueAt(t1)));
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
        settings.resp_attack_ms = clampP(attS, attA.valueAt(t1));
        settings.resp_release_ms = clampP(relS, relA.valueAt(t1));
        settings.noise_beta_bandwidth_hz = clampP(bwS, bwA.valueAt(t1));
        settings.noise_aspiration = clampP(aspS, aspA.valueAt(t1));
        settings.src_coupling = clampP(cplS, cplA.valueAt(t1));
        settings.body_size = clampP(sizeS, sizeA.valueAt(t1));
        voice.setSettings(settings);
        voice.process(from, to, buf.data() + i, 1);
    }
    return buf;
}

/// Interpolate a control array at host time `t` (seconds). `rate` is the control sample rate.
inline float interp_control(const std::vector<float>& x, double rate, double t) noexcept {
    if (x.empty()) {
        return 0.0f;
    }
    if (x.size() == 1 || rate <= 0.0) {
        return x[0];
    }
    const double idx = t * rate;
    if (idx <= 0.0) {
        return x[0];
    }
    const double last = static_cast<double>(x.size() - 1);
    if (idx >= last) {
        return x.back();
    }
    const int i0 = static_cast<int>(idx);
    const float u = static_cast<float>(idx - static_cast<double>(i0));
    return x[static_cast<std::size_t>(i0)] * (1.0f - u) + x[static_cast<std::size_t>(i0 + 1)] * u;
}

inline void apply_control_at(ControlFrame& fr, VoiceSettings& settings, const std::string& id, float v) {
    if (const auto* p = findParam(id)) {
        const double c = clampParam(*p, static_cast<double>(v));
        if (float* f = controlField(fr, p->pid)) {
            *f = static_cast<float>(c);
            return;
        }
        switch (p->pid) {
        case ParamId::BodySize:
            settings.body_size = c;
            break;
        case ParamId::SrcGamma:
            settings.src_gamma = c;
            break;
        case ParamId::TracheaLengthMm:
            settings.trachea_length_mm = c;
            break;
        case ParamId::TracheaReflection:
            settings.trachea_reflection = c;
            break;
        case ParamId::TracheaLoss:
            settings.trachea_loss = c;
            break;
        case ParamId::TracheaInputGain:
            settings.trachea_input_gain = c;
            break;
        case ParamId::OecDamping:
            settings.oec_damping = c;
            break;
        case ParamId::BeakDamping:
            settings.beak_damping = c;
            break;
        case ParamId::OutRadiation:
            settings.out_radiation = c;
            break;
        case ParamId::OutGainDb:
            settings.out_gain_db = c;
            break;
        case ParamId::RespAttackMs:
            settings.resp_attack_ms = c;
            break;
        case ParamId::RespReleaseMs:
            settings.resp_release_ms = c;
            break;
        case ParamId::NoiseBetaBandwidthHz:
            settings.noise_beta_bandwidth_hz = c;
            break;
        case ParamId::NoiseAspiration:
            settings.noise_aspiration = c;
            break;
        case ParamId::SrcCoupling:
            settings.src_coupling = c;
            break;
        default:
            break;
        }
    }
}

/// Host-rate render from per-parameter control arrays. Returns audio; writes final state if `stateOut`.
inline std::vector<float> render_controls(VoiceSettings settings,
                                          const std::unordered_map<std::string, std::vector<float>>& controls,
                                          double controlRateHz, double sampleRate, std::uint64_t seed,
                                          int oversampling = 0, const VoiceState* stateIn = nullptr,
                                          VoiceState* stateOut = nullptr, int maxBlockSize = 64) {
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double cr = controlRateHz > 0.0 ? controlRateHz : sr;
    int nCtrl = 0;
    for (const auto& kv : controls) {
        nCtrl = std::max(nCtrl, static_cast<int>(kv.second.size()));
    }
    const int n = nCtrl > 0 ? static_cast<int>(std::llround(static_cast<double>(nCtrl) * sr / cr)) : 0;
    std::vector<float> buf(n > 0 ? static_cast<std::size_t>(n) : 0, 0.0f);
    if (n <= 0) {
        return buf;
    }

    ProcessSpec ps;
    ps.hostSampleRate = sr;
    ps.maxBlockSize = maxBlockSize > 0 ? maxBlockSize : 64;
    ps.oversampling = oversampling;
    BirdVoice voice;
    voice.prepare(ps);
    voice.setSource(VoiceSource::Labial);
    settings.seed = seed;
    settings.oversampling = oversampling;
    voice.setSettings(settings);
    voice.reset(seed);
    const float b0 = controls.count("src.L.beta") ? interp_control(controls.at("src.L.beta"), cr, 0.0) : 0.3f;
    const float off0 =
        controls.count("src.L.alpha_offset") ? interp_control(controls.at("src.L.alpha_offset"), cr, 0.0) : 0.0f;
    const float oec0 = controls.count("oec.freq_hz") ? interp_control(controls.at("oec.freq_hz"), cr, 0.0) : 3000.0f;
    const float gape0 = controls.count("beak.gape") ? interp_control(controls.at("beak.gape"), cr, 0.0) : 0.5f;
    const float offR0 =
        controls.count("src.R.alpha_offset") ? interp_control(controls.at("src.R.alpha_offset"), cr, 0.0) : 0.0f;
    const float bR0 = controls.count("src.R.beta") ? interp_control(controls.at("src.R.beta"), cr, 0.0) : 0.3f;
    voice.primeLabial(model::kAlphaRest + static_cast<double>(off0), static_cast<double>(b0),
                      static_cast<double>(oec0), static_cast<double>(gape0), static_cast<double>(offR0),
                      static_cast<double>(bR0));
    if (stateIn != nullptr) {
        voice.loadState(*stateIn);
    }

    auto fill = [&](ControlFrame& fr, VoiceSettings& st, double t) {
        fr = ControlFrame{};
        for (const auto& kv : controls) {
            apply_control_at(fr, st, kv.first, interp_control(kv.second, cr, t));
        }
    };

    const double dt = 1.0 / sr;
    for (int i = 0; i < n; ++i) {
        const double t0 = static_cast<double>(i) * dt;
        const double t1 = static_cast<double>(i + 1) * dt;
        ControlFrame from, to;
        VoiceSettings s0 = settings;
        VoiceSettings s1 = settings;
        fill(from, s0, t0);
        fill(to, s1, t1);
        voice.setOutGainRamp(static_cast<float>(s0.out_gain_db), static_cast<float>(s1.out_gain_db));
        voice.setSettings(s1);
        voice.process(from, to, buf.data() + i, 1);
    }
    if (stateOut != nullptr) {
        voice.saveState(*stateOut);
    }
    return buf;
}

} // namespace cuicui::io
