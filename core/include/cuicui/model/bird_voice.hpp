#pragma once

#include "cuicui/dsp/control_ramp.hpp"
#include "cuicui/dsp/dc_blocker.hpp"
#include "cuicui/dsp/oversampling.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/dsp/smoother.hpp"
#include "cuicui/model/bilateral_source.hpp"
#include "cuicui/model/body_scaling.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/noise_sources.hpp"
#include "cuicui/model/respiratory_drive.hpp"
#include "cuicui/model/test_saw.hpp"
#include "cuicui/model/trachea.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"
#include "cuicui/model/voice_state.hpp"
#include "cuicui/params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cuicui {

enum class VoiceSource { Test, Labial };

struct ControlFrame {
    float resp_pressure = 0.0f;
    float src_L_beta = 0.3f, src_R_beta = 0.3f;
    float src_L_gate = 1.0f, src_R_gate = 0.0f;
    float src_L_alpha_offset = 0.0f, src_R_alpha_offset = 0.0f;
    float oec_freq_hz = 3000.0f;
    float beak_gape = 0.5f;
    float noise_beta_sigma = 0.005f;
    float test_freq_hz = 440.0f;
    float test_gain_db = -12.0f;
};

struct VoiceSettings {
    double body_size = 1.0, src_gamma = 24000.0;
    double trachea_length_mm = 33.0, trachea_reflection = 0.5, trachea_loss = 0.3, trachea_input_gain = 1.0;
    double oec_damping = 0.1, beak_damping = 0.3;
    double out_radiation = 1.0, out_gain_db = 0.0;
    double resp_attack_ms = 8.0, resp_release_ms = 15.0;
    double noise_beta_bandwidth_hz = 300.0, noise_aspiration = 0.02, src_coupling = 0.0;
    double test_waveform = 0.0;
    int oversampling = 0;
    std::uint64_t seed = 1;
};

struct VoiceTelemetry {
    float alpha_L = 0.0f, alpha_R = 0.0f, beta_L = 0.0f, beta_R = 0.0f, pressure = 0.0f;
    float f0_estimate_hz = 0.0f, level_db = -120.0f;
    std::uint32_t nonFiniteResets = 0;
};

enum class ParamHome { Control, Settings, None };

[[nodiscard]] inline ParamHome paramHome(ParamId id) noexcept {
    switch (id) {
    case ParamId::RespPressure:
    case ParamId::SrcLBeta:
    case ParamId::SrcRBeta:
    case ParamId::SrcLGate:
    case ParamId::SrcRGate:
    case ParamId::SrcLAlphaOffset:
    case ParamId::SrcRAlphaOffset:
    case ParamId::OecFreqHz:
    case ParamId::BeakGape:
    case ParamId::NoiseBetaSigma:
    case ParamId::TestFreqHz:
    case ParamId::TestGainDb:
        return ParamHome::Control;
    case ParamId::RespAttackMs:
    case ParamId::RespReleaseMs:
    case ParamId::SrcGamma:
    case ParamId::SrcCoupling:
    case ParamId::NoiseBetaBandwidthHz:
    case ParamId::NoiseAspiration:
    case ParamId::TracheaLengthMm:
    case ParamId::TracheaReflection:
    case ParamId::TracheaLoss:
    case ParamId::TracheaInputGain:
    case ParamId::OecDamping:
    case ParamId::BeakDamping:
    case ParamId::OutRadiation:
    case ParamId::OutGainDb:
    case ParamId::BodySize:
    case ParamId::EngineOversampling:
    case ParamId::EngineSeed:
    case ParamId::TestWaveform:
        return ParamHome::Settings;
    default:
        return ParamHome::None;
    }
}

[[nodiscard]] inline const float* controlField(const ControlFrame& f, ParamId id) noexcept {
    switch (id) {
    case ParamId::RespPressure:
        return &f.resp_pressure;
    case ParamId::SrcLBeta:
        return &f.src_L_beta;
    case ParamId::SrcRBeta:
        return &f.src_R_beta;
    case ParamId::SrcLGate:
        return &f.src_L_gate;
    case ParamId::SrcRGate:
        return &f.src_R_gate;
    case ParamId::SrcLAlphaOffset:
        return &f.src_L_alpha_offset;
    case ParamId::SrcRAlphaOffset:
        return &f.src_R_alpha_offset;
    case ParamId::OecFreqHz:
        return &f.oec_freq_hz;
    case ParamId::BeakGape:
        return &f.beak_gape;
    case ParamId::NoiseBetaSigma:
        return &f.noise_beta_sigma;
    case ParamId::TestFreqHz:
        return &f.test_freq_hz;
    case ParamId::TestGainDb:
        return &f.test_gain_db;
    default:
        return nullptr;
    }
}

[[nodiscard]] inline float* controlField(ControlFrame& f, ParamId id) noexcept {
    return const_cast<float*>(controlField(static_cast<const ControlFrame&>(f), id));
}

/// Address of the VoiceSettings field for `id`, or nullptr. `engine.seed` is uint64.
[[nodiscard]] inline const void* settingsField(const VoiceSettings& s, ParamId id) noexcept {
    switch (id) {
    case ParamId::BodySize:
        return &s.body_size;
    case ParamId::SrcGamma:
        return &s.src_gamma;
    case ParamId::TracheaLengthMm:
        return &s.trachea_length_mm;
    case ParamId::TracheaReflection:
        return &s.trachea_reflection;
    case ParamId::TracheaLoss:
        return &s.trachea_loss;
    case ParamId::TracheaInputGain:
        return &s.trachea_input_gain;
    case ParamId::OecDamping:
        return &s.oec_damping;
    case ParamId::BeakDamping:
        return &s.beak_damping;
    case ParamId::OutRadiation:
        return &s.out_radiation;
    case ParamId::OutGainDb:
        return &s.out_gain_db;
    case ParamId::RespAttackMs:
        return &s.resp_attack_ms;
    case ParamId::RespReleaseMs:
        return &s.resp_release_ms;
    case ParamId::NoiseBetaBandwidthHz:
        return &s.noise_beta_bandwidth_hz;
    case ParamId::NoiseAspiration:
        return &s.noise_aspiration;
    case ParamId::SrcCoupling:
        return &s.src_coupling;
    case ParamId::EngineSeed:
        return &s.seed;
    case ParamId::EngineOversampling:
        return &s.oversampling;
    case ParamId::TestWaveform:
        return &s.test_waveform;
    default:
        return nullptr;
    }
}

/// Voice: source → trachea → UVT → mix → DC → decimation. 01_research.md §4.1.
class BirdVoice {
public:
    void prepare(const ProcessSpec& spec) {
        spec_ = spec;
        if (spec_.hostSampleRate <= 0.0) {
            spec_.hostSampleRate = 48000.0;
        }
        if (spec_.maxBlockSize <= 0) {
            spec_.maxBlockSize = 512;
        }
        os_ = spec_.resolvedOversampling();
        internal_rate_ = spec_.internalRate();
        f0_window_ = std::max(1, static_cast<int>(std::lround(0.01 * internal_rate_)));
        oversampling_.prepare(spec_);
        internal_.assign(static_cast<std::size_t>(spec_.maxBlockSize * os_), 0.0f);
        saw_.prepare(internal_rate_);
        labia_.prepare(internal_rate_);
        trachea_.prepare(internal_rate_, model::kTracheaMaxLengthMm);
        uvt_.prepare(internal_rate_);
        dc_.prepare(internal_rate_, 20.0);
        resp_.prepare(internal_rate_);
        aspiration_.prepare(internal_rate_);
        asp_sm_.prepare(internal_rate_);
        asp_sm_.setTimeConstantMs(specOf(ParamId::NoiseAspiration).smoothingMs);
        coupling_sm_.prepare(internal_rate_);
        coupling_sm_.setTimeConstantMs(specOf(ParamId::SrcCoupling).smoothingMs);
        size_sm_.prepare(internal_rate_);
        size_sm_.setTimeConstantMs(specOf(ParamId::BodySize).smoothingMs);
        gamma_sm_.prepare(internal_rate_);
        gamma_sm_.setTimeConstantMs(specOf(ParamId::SrcGamma).smoothingMs);
        out_smoother_.prepare(spec_.hostSampleRate);
        out_smoother_.setTimeConstantMs(smoothing_ms_);
        len_sm_.prepare(internal_rate_);
        ref_sm_.prepare(internal_rate_);
        loss_sm_.prepare(internal_rate_);
        gain_sm_.prepare(internal_rate_);
        oec_damp_sm_.prepare(internal_rate_);
        beak_damp_sm_.prepare(internal_rate_);
        rad_sm_.prepare(internal_rate_);
        len_sm_.setTimeConstantMs(specOf(ParamId::TracheaLengthMm).smoothingMs);
        ref_sm_.setTimeConstantMs(specOf(ParamId::TracheaReflection).smoothingMs);
        loss_sm_.setTimeConstantMs(specOf(ParamId::TracheaLoss).smoothingMs);
        gain_sm_.setTimeConstantMs(specOf(ParamId::TracheaInputGain).smoothingMs);
        oec_damp_sm_.setTimeConstantMs(specOf(ParamId::OecDamping).smoothingMs);
        beak_damp_sm_.setTimeConstantMs(specOf(ParamId::BeakDamping).smoothingMs);
        rad_sm_.setTimeConstantMs(specOf(ParamId::OutRadiation).smoothingMs);
        reset(settings_.seed);
    }

    void reset(std::uint64_t seed) {
        settings_.seed = seed;
        rng_.setSeed(seed);
        sine_phase_ = 0.0;
        saw_.reset();
        labia_.setBandwidthHz(settings_.noise_beta_bandwidth_hz);
        labia_.resetNoise(seed);
        labia_.resetToRest(model::kAlphaRest, specOf(ParamId::SrcLBeta).def,
                            model::kAlphaRest - model::kDeltaAlphaClose, specOf(ParamId::SrcRBeta).def);
        seedNoiseStreams(seed);
        resp_.setTimeConstantsMs(settings_.resp_attack_ms, settings_.resp_release_ms);
        resp_.reset(0.0);
        asp_sm_.snap(static_cast<float>(settings_.noise_aspiration));
        coupling_sm_.snap(static_cast<float>(settings_.src_coupling));
        size_sm_.snap(static_cast<float>(settings_.body_size));
        gamma_sm_.snap(static_cast<float>(settings_.src_gamma));
        snapTrachea();
        snapUvt();
        applyDerivedImmediate();
        primeDcFromSource(0.0);
        oversampling_.reset();
        out_smoother_.snap(static_cast<float>(settings_.out_gain_db));
        std::fill(internal_.begin(), internal_.end(), 0.0f);
        telemetry_ = {};
        f0_zc_prev_ = 0.0;
        f0_zc_count_ = 0;
        f0_zc_samples_ = 0;
        prepared_ = true;
    }

    void setSettings(const VoiceSettings& s) noexcept {
        settings_ = s;
        out_smoother_.setTarget(static_cast<float>(settings_.out_gain_db));
        len_sm_.setTarget(static_cast<float>(settings_.trachea_length_mm));
        ref_sm_.setTarget(static_cast<float>(settings_.trachea_reflection));
        loss_sm_.setTarget(static_cast<float>(settings_.trachea_loss));
        gain_sm_.setTarget(static_cast<float>(settings_.trachea_input_gain));
        oec_damp_sm_.setTarget(static_cast<float>(settings_.oec_damping));
        beak_damp_sm_.setTarget(static_cast<float>(settings_.beak_damping));
        rad_sm_.setTarget(static_cast<float>(settings_.out_radiation));
        resp_.setTimeConstantsMs(settings_.resp_attack_ms, settings_.resp_release_ms);
        asp_sm_.setTarget(static_cast<float>(settings_.noise_aspiration));
        coupling_sm_.setTarget(static_cast<float>(settings_.src_coupling));
        size_sm_.setTarget(static_cast<float>(settings_.body_size));
        gamma_sm_.setTarget(static_cast<float>(settings_.src_gamma));
    }

    void setSource(VoiceSource src) noexcept {
        source_ = src;
        use_trachea_ = (src == VoiceSource::Labial);
        use_uvt_ = (src == VoiceSource::Labial);
    }

    void setTracheaEnabled(bool on) noexcept { use_trachea_ = on; }
    void setUvtEnabled(bool on) noexcept { use_uvt_ = on; }

    void setTestWaveform(TestWaveform w) noexcept { waveform_ = w; }

    void setTestRamp(float freqFrom, float freqTo, float gainDbFrom, float gainDbTo) noexcept {
        freq_from_ = freqFrom;
        freq_to_ = freqTo;
        gain_from_ = gainDbFrom;
        gain_to_ = gainDbTo;
    }

    void setOutGainRamp(float fromDb, float toDb) noexcept {
        out_from_ = fromDb;
        out_to_ = toDb;
    }

    /// Place the oscillators on the silent rest branch. Call after reset.
    /// Right side is primed closed (α − Δα_close) unless the caller passes a different offset.
    void primeLabial(double alpha, double beta, double oecHz = 3000.0, double gape = 0.5,
                     double alphaOffR = 0.0, double betaR = 0.3) noexcept {
        from_.oec_freq_hz = static_cast<float>(oecHz);
        from_.beak_gape = static_cast<float>(gape);
        from_.src_R_alpha_offset = static_cast<float>(alphaOffR);
        from_.src_R_beta = static_cast<float>(betaR);
        const double aR = alpha + alphaOffR - model::kDeltaAlphaClose;
        labia_.resetToRest(alpha, beta, aR, betaR);
        resp_.reset(0.0);
        snapTrachea();
        snapUvt();
        applyDerivedImmediate();
        primeDcFromSource(0.0);
        f0_zc_prev_ = 0.0;
        f0_zc_count_ = 0;
        f0_zc_samples_ = 0;
    }

    /// Override registry smoothing for `out.gain_db` (0 = zipper / unsmoothed). Call after prepare.
    void setOutGainSmoothingMs(double ms) noexcept {
        smoothing_ms_ = std::max(0.0, ms);
        out_smoother_.setTimeConstantMs(smoothing_ms_);
    }

    void process(const ControlFrame& from, const ControlFrame& to, float* out, int numSamples) noexcept {
        from_ = from;
        to_ = to;
        if (!prepared_ || out == nullptr || numSamples <= 0) {
            return;
        }
        int done = 0;
        while (done < numSamples) {
            const int n = std::min(numSamples - done, spec_.maxBlockSize);
            processBlock(out + done, n, done, numSamples);
            done += n;
        }
    }

    void saveState(VoiceState& s) const noexcept {
        s = VoiceState{};
        s.version = 1;
        s.sine_phase = sine_phase_;
        s.saw_phase = saw_.phase();
        labia_.save(s.labial_x_l, s.labial_y_l, s.labial_x_r, s.labial_y_r, s.labial_resets_l,
                    s.labial_resets_r, s.dc_l, s.dc_r, s.tension_lp_l, s.rng_tension_l, s.tension_lp_r,
                    s.rng_tension_r);
        s.dc_out.x1 = dc_.x1();
        s.dc_out.y1 = dc_.y1();
        s.resp_p = resp_.pressure();
        aspiration_.save(s.aspiration_bp, s.rng_aspiration);
        std::uint32_t rs[4];
        rng_.save(rs, s.rng_voice.spare, s.rng_voice.has_spare);
        s.rng_voice.s0 = rs[0];
        s.rng_voice.s1 = rs[1];
        s.rng_voice.s2 = rs[2];
        s.rng_voice.s3 = rs[3];
        trachea_.saveDelay(s.trachea_buf, kVoiceDelayCapacity, s.trachea_write, s.trachea_size,
                           s.trachea_lp, s.trachea_last_out);
        uvt_.save(s.uvt_q0, s.uvt_q1, s.uvt_q2, s.uvt_last_u, s.uvt_flow, s.uvt_dflow,
                  s.uvt_since_update, s.uvt_dirty);
        s.uvt_counter = uvt_counter_;
        saveSm(s.out_gain, out_smoother_);
        saveSm(s.len, len_sm_);
        saveSm(s.ref, ref_sm_);
        saveSm(s.loss, loss_sm_);
        saveSm(s.tgain, gain_sm_);
        saveSm(s.oec_damp, oec_damp_sm_);
        saveSm(s.beak_damp, beak_damp_sm_);
        saveSm(s.rad, rad_sm_);
        saveSm(s.asp, asp_sm_);
        saveSm(s.coupling, coupling_sm_);
        saveSm(s.size, size_sm_);
        saveSm(s.gamma, gamma_sm_);
        oversampling_.save(s.os_stages, s.hb, kMaxHalfbandStages);
        s.f0_zc_prev = f0_zc_prev_;
        s.f0_zc_count = f0_zc_count_;
        s.f0_zc_samples = f0_zc_samples_;
        s.f0_hz = telemetry_.f0_estimate_hz;
        s.nonFiniteResets = labia_.nonFiniteResets();
    }

    void loadState(const VoiceState& s) noexcept {
        sine_phase_ = s.sine_phase;
        saw_.setPhase(s.saw_phase);
        labia_.load(s.labial_x_l, s.labial_y_l, s.labial_x_r, s.labial_y_r, s.labial_resets_l,
                    s.labial_resets_r, s.dc_l, s.dc_r, s.tension_lp_l, s.rng_tension_l, s.tension_lp_r,
                    s.rng_tension_r);
        dc_.load(s.dc_out.x1, s.dc_out.y1);
        resp_.load(s.resp_p);
        aspiration_.load(s.aspiration_bp, s.rng_aspiration);
        const std::uint32_t rs[4] = {s.rng_voice.s0, s.rng_voice.s1, s.rng_voice.s2, s.rng_voice.s3};
        rng_.load(rs, s.rng_voice.spare, s.rng_voice.has_spare);
        loadSm(out_smoother_, s.out_gain);
        loadSm(len_sm_, s.len);
        loadSm(ref_sm_, s.ref);
        loadSm(loss_sm_, s.loss);
        loadSm(gain_sm_, s.tgain);
        loadSm(oec_damp_sm_, s.oec_damp);
        loadSm(beak_damp_sm_, s.beak_damp);
        loadSm(rad_sm_, s.rad);
        loadSm(asp_sm_, s.asp);
        loadSm(coupling_sm_, s.coupling);
        loadSm(size_sm_, s.size);
        loadSm(gamma_sm_, s.gamma);
        applyDerivedImmediate();
        trachea_.loadDelay(s.trachea_buf, kVoiceDelayCapacity, s.trachea_write, s.trachea_size,
                           s.trachea_lp, s.trachea_last_out);
        uvt_.load(s.uvt_q0, s.uvt_q1, s.uvt_q2, s.uvt_last_u, s.uvt_flow, s.uvt_dflow,
                  s.uvt_since_update, s.uvt_dirty);
        uvt_counter_ = s.uvt_counter;
        oversampling_.load(s.os_stages, s.hb, kMaxHalfbandStages);
        f0_zc_prev_ = s.f0_zc_prev;
        f0_zc_count_ = s.f0_zc_count;
        f0_zc_samples_ = s.f0_zc_samples;
        telemetry_.f0_estimate_hz = s.f0_hz;
        telemetry_.nonFiniteResets = s.nonFiniteResets;
    }

    [[nodiscard]] VoiceTelemetry telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] int latencySamples() const noexcept { return oversampling_.latencySamples(); }
    [[nodiscard]] int oversamplingFactor() const noexcept { return os_; }
    [[nodiscard]] double internalRate() const noexcept { return internal_rate_; }
    [[nodiscard]] const dsp::Oversampling& oversampling() const noexcept { return oversampling_; }
    [[nodiscard]] const model::LabialOscillator& oscillator() const noexcept { return labia_.left(); }
    [[nodiscard]] const model::BilateralSource& bilateral() const noexcept { return labia_; }
    [[nodiscard]] const model::Trachea& trachea() const noexcept { return trachea_; }
    [[nodiscard]] const model::UpperVocalTract& uvt() const noexcept { return uvt_; }
    [[nodiscard]] const model::RespiratoryDrive& respiratory() const noexcept { return resp_; }
    [[nodiscard]] bool tracheaEnabled() const noexcept { return use_trachea_; }
    [[nodiscard]] bool uvtEnabled() const noexcept { return use_uvt_; }
    [[nodiscard]] const VoiceSettings& settings() const noexcept { return settings_; }

private:
    static float lerp(float a, float b, float u) noexcept { return a + u * (b - a); }

    static void saveSm(SmootherState& d, const dsp::Smoother& sm) noexcept {
        d.y = sm.value();
        d.target = sm.target();
    }

    static void loadSm(dsp::Smoother& sm, const SmootherState& d) noexcept { sm.load(d.y, d.target); }

    void seedNoiseStreams(std::uint64_t seed) noexcept {
        labia_.resetNoise(seed);
        aspiration_.reset(dsp::deriveStreamSeed(seed, dsp::kStreamAspiration));
    }

    void snapTrachea() noexcept {
        len_sm_.snap(static_cast<float>(settings_.trachea_length_mm));
        ref_sm_.snap(static_cast<float>(settings_.trachea_reflection));
        loss_sm_.snap(static_cast<float>(settings_.trachea_loss));
        gain_sm_.snap(static_cast<float>(settings_.trachea_input_gain));
        trachea_.reset();
    }

    void snapUvt() noexcept {
        oec_damp_sm_.snap(static_cast<float>(settings_.oec_damping));
        beak_damp_sm_.snap(static_cast<float>(settings_.beak_damping));
        rad_sm_.snap(static_cast<float>(settings_.out_radiation));
        uvt_.reset();
    }

    void applyDerivedImmediate() noexcept {
        last_scaled_ = model::scaleAnatomy(static_cast<double>(size_sm_.value()),
                                           static_cast<double>(gamma_sm_.value()),
                                           static_cast<double>(len_sm_.value()),
                                           settings_.noise_beta_bandwidth_hz, settings_.resp_attack_ms,
                                           settings_.resp_release_ms);
        trachea_.setParams(last_scaled_.tracheaLengthMm, static_cast<double>(ref_sm_.value()),
                           static_cast<double>(loss_sm_.value()), static_cast<double>(gain_sm_.value()));
        labia_.setBandwidthHz(last_scaled_.tensionBandwidthHz);
        aspiration_.setCentreHz(last_scaled_.aspirationCentreHz);
        resp_.setTimeConstantsMs(last_scaled_.respAttackMs, last_scaled_.respReleaseMs);
        uvt_.setParams(static_cast<double>(from_.oec_freq_hz), static_cast<double>(oec_damp_sm_.value()),
                       static_cast<double>(from_.beak_gape), static_cast<double>(beak_damp_sm_.value()),
                       last_scaled_.sizeScale);
        oec_z_ = oec_damp_sm_.value();
        beak_z_ = beak_damp_sm_.value();
        rad_ = rad_sm_.value();
        asp_ = asp_sm_.value();
        coupling_ = coupling_sm_.value();
    }

    void tickDerived() noexcept {
        const double sigma = static_cast<double>(size_sm_.next());
        const double g0 = static_cast<double>(gamma_sm_.next());
        const double len = static_cast<double>(len_sm_.next());
        const double ref = static_cast<double>(ref_sm_.next());
        const double loss = static_cast<double>(loss_sm_.next());
        const double gain = static_cast<double>(gain_sm_.next());
        last_scaled_ = model::scaleAnatomy(sigma, g0, len, settings_.noise_beta_bandwidth_hz,
                                           settings_.resp_attack_ms, settings_.resp_release_ms);
        trachea_.setParams(last_scaled_.tracheaLengthMm, ref, loss, gain);
        oec_z_ = oec_damp_sm_.next();
        beak_z_ = beak_damp_sm_.next();
        rad_ = rad_sm_.next();
        asp_ = asp_sm_.next();
        coupling_ = coupling_sm_.next();
        if ((uvt_counter_ & 7) == 0) {
            labia_.setBandwidthHz(last_scaled_.tensionBandwidthHz);
            aspiration_.setCentreHz(last_scaled_.aspirationCentreHz);
            resp_.setTimeConstantsMs(last_scaled_.respAttackMs, last_scaled_.respReleaseMs);
        }
    }

    [[nodiscard]] double throughTrachea(double source) noexcept {
        if (!use_trachea_) {
            return source;
        }
        return trachea_.process(source);
    }

    [[nodiscard]] double throughUvt(double pt, double oecHz, double gape) noexcept {
        if ((uvt_counter_ & 7) == 0) {
            uvt_.setParams(oecHz, static_cast<double>(oec_z_), gape, static_cast<double>(beak_z_),
                           last_scaled_.sizeScale);
        }
        const auto o = uvt_.process(pt);
        return model::uvtMix(o.flow, o.dflow, static_cast<double>(rad_));
    }

    void primeDcFromSource(double sourceSteady) noexcept {
        double pt = sourceSteady;
        if (use_trachea_) {
            trachea_.prime(sourceSteady);
            pt = trachea_.lastOutput();
        }
        if (use_uvt_) {
            uvt_.setParams(static_cast<double>(from_.oec_freq_hz), settings_.oec_damping,
                           static_cast<double>(from_.beak_gape), settings_.beak_damping,
                           last_scaled_.sizeScale);
            uvt_.prime(pt);
            dc_.prime(model::uvtMix(uvt_.last().flow, uvt_.last().dflow, settings_.out_radiation));
            uvt_counter_ = 0;
        } else {
            dc_.prime(pt);
        }
    }

    void noteZeroCrossing(double x) noexcept {
        if (f0_zc_prev_ <= 0.0 && x > 0.0) {
            ++f0_zc_count_;
        }
        f0_zc_prev_ = x;
        ++f0_zc_samples_;
        if (f0_zc_samples_ >= f0_window_) {
            const double t = static_cast<double>(f0_zc_samples_) / internal_rate_;
            telemetry_.f0_estimate_hz = (t > 0.0) ? static_cast<float>(static_cast<double>(f0_zc_count_) / t)
                                                  : 0.0f;
            f0_zc_count_ = 0;
            f0_zc_samples_ = 0;
        }
    }

    void processBlock(float* out, int n, int offsetInCall, int callLen) noexcept {
        const int nInt = n * os_;
        const float u0 = static_cast<float>(offsetInCall) / static_cast<float>(callLen);
        const float u1 = static_cast<float>(offsetInCall + n) / static_cast<float>(callLen);

        if (source_ == VoiceSource::Labial) {
            processLabial(nInt, u0, u1);
        } else {
            processTest(nInt, u0, u1);
        }
        oversampling_.decimate(internal_.data(), out, n);

        const float og0 = out_from_ + u0 * (out_to_ - out_from_);
        const float og1 = out_from_ + u1 * (out_to_ - out_from_);
        dsp::ControlRamp outRamp;
        outRamp.set(og0, og1, n);
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            out_smoother_.setTarget(outRamp.next());
            const float g = std::pow(10.0f, out_smoother_.next() / 20.0f);
            out[i] *= g;
            if (!std::isfinite(out[i])) {
                out[i] = 0.0f;
                ++telemetry_.nonFiniteResets;
            }
            peak = std::max(peak, std::abs(out[i]));
        }
        telemetry_.level_db = peak > 1e-12f ? 20.0f * std::log10(peak) : -120.0f;
        telemetry_.nonFiniteResets += labia_.nonFiniteResets();
        if (source_ != VoiceSource::Labial) {
            const float f1 = freq_from_ + u1 * (freq_to_ - freq_from_);
            telemetry_.f0_estimate_hz = f1;
            telemetry_.pressure = 0.0f;
            telemetry_.beta_L = 0.0f;
        }
    }

    void processTest(int nInt, float u0, float u1) noexcept {
        dsp::ControlRamp freqRamp, gainRamp;
        const float f0 = freq_from_ + u0 * (freq_to_ - freq_from_);
        const float f1 = freq_from_ + u1 * (freq_to_ - freq_from_);
        const float g0 = gain_from_ + u0 * (gain_to_ - gain_from_);
        const float g1 = gain_from_ + u1 * (gain_to_ - gain_from_);
        freqRamp.set(f0, f1, nInt);
        gainRamp.set(g0, g1, nInt);

        constexpr double two_pi = 2.0 * std::numbers::pi;
        const double dt = 1.0 / internal_rate_;
        for (int i = 0; i < nInt; ++i) {
            tickDerived();
            const double freq = std::max(0.0, static_cast<double>(freqRamp.next()));
            const double gdb = static_cast<double>(gainRamp.next());
            const double amp = std::pow(10.0, gdb / 20.0);
            float s = 0.0f;
            switch (waveform_) {
            case TestWaveform::Saw:
                s = saw_.tick(freq);
                break;
            case TestWaveform::Noise:
                s = static_cast<float>(2.0 * static_cast<double>(rng_.uniform()) - 1.0);
                break;
            case TestWaveform::Sine:
            default:
                s = static_cast<float>(std::sin(sine_phase_));
                sine_phase_ += two_pi * freq * dt;
                if (sine_phase_ > two_pi) {
                    sine_phase_ -= two_pi * std::floor(sine_phase_ / two_pi);
                }
                break;
            }
            double y = amp * static_cast<double>(s);
            if (use_trachea_) {
                y = throughTrachea(y);
            }
            if (use_uvt_) {
                y = throughUvt(y, static_cast<double>(from_.oec_freq_hz),
                               static_cast<double>(from_.beak_gape));
            }
            if (use_trachea_ || use_uvt_) {
                y = dc_.tick(y);
            }
            internal_[static_cast<std::size_t>(i)] = static_cast<float>(y);
            ++uvt_counter_;
        }
    }

    void processLabial(int nInt, float u0, float u1) noexcept {
        dsp::ControlRamp pRamp, bLRamp, bRRamp, oLRamp, oRRamp, gLRamp, gRRamp, oecRamp, gapeRamp, sigRamp;
        pRamp.set(lerp(from_.resp_pressure, to_.resp_pressure, u0),
                  lerp(from_.resp_pressure, to_.resp_pressure, u1), nInt);
        bLRamp.set(lerp(from_.src_L_beta, to_.src_L_beta, u0), lerp(from_.src_L_beta, to_.src_L_beta, u1),
                   nInt);
        bRRamp.set(lerp(from_.src_R_beta, to_.src_R_beta, u0), lerp(from_.src_R_beta, to_.src_R_beta, u1),
                   nInt);
        oLRamp.set(lerp(from_.src_L_alpha_offset, to_.src_L_alpha_offset, u0),
                   lerp(from_.src_L_alpha_offset, to_.src_L_alpha_offset, u1), nInt);
        oRRamp.set(lerp(from_.src_R_alpha_offset, to_.src_R_alpha_offset, u0),
                   lerp(from_.src_R_alpha_offset, to_.src_R_alpha_offset, u1), nInt);
        gLRamp.set(lerp(from_.src_L_gate, to_.src_L_gate, u0), lerp(from_.src_L_gate, to_.src_L_gate, u1),
                   nInt);
        gRRamp.set(lerp(from_.src_R_gate, to_.src_R_gate, u0), lerp(from_.src_R_gate, to_.src_R_gate, u1),
                   nInt);
        oecRamp.set(lerp(from_.oec_freq_hz, to_.oec_freq_hz, u0), lerp(from_.oec_freq_hz, to_.oec_freq_hz, u1),
                    nInt);
        gapeRamp.set(lerp(from_.beak_gape, to_.beak_gape, u0), lerp(from_.beak_gape, to_.beak_gape, u1), nInt);
        sigRamp.set(lerp(from_.noise_beta_sigma, to_.noise_beta_sigma, u0),
                    lerp(from_.noise_beta_sigma, to_.noise_beta_sigma, u1), nInt);

        const auto& betaSpec = specOf(ParamId::SrcLBeta);
        const auto& sigSpec = specOf(ParamId::NoiseBetaSigma);
        const auto& gateSpec = specOf(ParamId::SrcLGate);
        float lastP = from_.resp_pressure;
        float lastBL = from_.src_L_beta;
        float lastBR = from_.src_R_beta;
        float lastAL = 0.0f;
        float lastAR = 0.0f;
        for (int i = 0; i < nInt; ++i) {
            tickDerived();
            lastP = pRamp.next();
            lastBL = bLRamp.next();
            lastBR = bRRamp.next();
            const float offL = oLRamp.next();
            const float offR = oRRamp.next();
            const float gL = gLRamp.next();
            const float gR = gRRamp.next();
            const float oec = oecRamp.next();
            const float gape = gapeRamp.next();
            const float sigmaN = static_cast<float>(clampParam(sigSpec, static_cast<double>(sigRamp.next())));
            const auto drive = resp_.process(static_cast<double>(lastP));
            model::BilateralFrame fr;
            fr.alphaBase = drive.alphaBase;
            fr.alphaOffL = static_cast<double>(offL);
            fr.alphaOffR = static_cast<double>(offR);
            fr.betaL = clampParam(betaSpec, static_cast<double>(lastBL));
            fr.betaR = clampParam(specOf(ParamId::SrcRBeta), static_cast<double>(lastBR));
            fr.gateL = clampParam(gateSpec, static_cast<double>(gL));
            fr.gateR = clampParam(specOf(ParamId::SrcRGate), static_cast<double>(gR));
            fr.gamma = last_scaled_.gamma;
            fr.coupling = static_cast<double>(coupling_);
            fr.sigmaBeta = static_cast<double>(sigmaN);
            const auto src = labia_.process(fr);
            lastAL = static_cast<float>(src.alphaL);
            lastAR = static_cast<float>(src.alphaR);
            const double nAsp = aspiration_.process(drive.G, src.xBar, static_cast<double>(asp_));
            const double source = drive.G * src.mixTilde + nAsp;
            double y = throughTrachea(source);
            if (use_uvt_) {
                y = dc_.tick(throughUvt(y, static_cast<double>(oec), static_cast<double>(gape)));
            } else {
                y = 0.5 * dc_.tick(y);
            }
            if (!std::isfinite(y)) {
                y = 0.0;
                ++telemetry_.nonFiniteResets;
            }
            internal_[static_cast<std::size_t>(i)] = static_cast<float>(y);
            lastP = static_cast<float>(drive.pressure);
            lastBL = static_cast<float>(src.betaEffL);
            lastBR = static_cast<float>(src.betaEffR);
            const double xDom = (fr.gateR > fr.gateL) ? src.xTildeR : src.xTildeL;
            noteZeroCrossing(xDom);
            ++uvt_counter_;
        }
        telemetry_.pressure = lastP;
        telemetry_.beta_L = lastBL;
        telemetry_.beta_R = lastBR;
        telemetry_.alpha_L = lastAL;
        telemetry_.alpha_R = lastAR;
    }

    ProcessSpec spec_{};
    VoiceSettings settings_{};
    VoiceTelemetry telemetry_{};
    ControlFrame from_{};
    ControlFrame to_{};
    dsp::Oversampling oversampling_;
    dsp::Smoother out_smoother_;
    dsp::DcBlocker dc_;
    dsp::Smoother len_sm_, ref_sm_, loss_sm_, gain_sm_;
    dsp::Smoother oec_damp_sm_, beak_damp_sm_, rad_sm_, asp_sm_, coupling_sm_;
    dsp::Smoother size_sm_, gamma_sm_;
    dsp::Rng rng_{1};
    model::TestSaw saw_;
    model::BilateralSource labia_;
    model::RespiratoryDrive resp_;
    model::AspirationNoise aspiration_;
    model::Trachea trachea_;
    model::UpperVocalTract uvt_;
    model::ScaledAnatomy last_scaled_{};
    std::vector<float> internal_;
    VoiceSource source_ = VoiceSource::Test;
    bool use_trachea_ = false;
    bool use_uvt_ = false;
    unsigned uvt_counter_ = 0;
    TestWaveform waveform_ = TestWaveform::Sine;
    double internal_rate_ = 192000.0;
    double sine_phase_ = 0.0;
    double smoothing_ms_ = specOf(ParamId::OutGainDb).smoothingMs;
    float freq_from_ = 440.0f, freq_to_ = 440.0f;
    float gain_from_ = -12.0f, gain_to_ = -12.0f;
    float out_from_ = 0.0f, out_to_ = 0.0f;
    float oec_z_ = 0.1f, beak_z_ = 0.3f, rad_ = 1.0f, asp_ = 0.02f, coupling_ = 0.0f;
    double f0_zc_prev_ = 0.0;
    int f0_zc_count_ = 0;
    int f0_zc_samples_ = 0;
    int f0_window_ = 1920;
    int os_ = 4;
    bool prepared_ = false;
};

} // namespace cuicui
