#pragma once

#include "cuicui/dsp/control_ramp.hpp"
#include "cuicui/dsp/dc_blocker.hpp"
#include "cuicui/dsp/oversampling.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/dsp/smoother.hpp"
#include "cuicui/model/bilateral_source.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/noise_sources.hpp"
#include "cuicui/model/respiratory_drive.hpp"
#include "cuicui/model/test_saw.hpp"
#include "cuicui/model/trachea.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"
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
};

struct VoiceSettings {
    double body_size = 1.0, src_gamma = 24000.0;
    double trachea_length_mm = 33.0, trachea_reflection = 0.5, trachea_loss = 0.3, trachea_input_gain = 1.0;
    double oec_damping = 0.1, beak_damping = 0.3;
    double out_radiation = 1.0, out_gain_db = 0.0;
    double resp_attack_ms = 8.0, resp_release_ms = 15.0;
    double noise_beta_bandwidth_hz = 300.0, noise_aspiration = 0.02, src_coupling = 0.0;
    std::uint64_t seed = 1;
};

struct VoiceTelemetry {
    float alpha_L = 0.0f, alpha_R = 0.0f, beta_L = 0.0f, beta_R = 0.0f, pressure = 0.0f;
    float f0_estimate_hz = 0.0f, level_db = -120.0f;
    std::uint32_t nonFiniteResets = 0;
};

/// POD snapshot (A2 test oscillator + B1 labial state; delay lines later).
struct VoiceState {
    double sine_phase = 0.0;
    double saw_phase = 0.0;
    double labial_x = 0.0;
    double labial_y = 0.0;
    float out_gain_y = 0.0f;
    std::uint32_t rng0 = 1, rng1 = 0, rng2 = 0, rng3 = 0;
    std::uint32_t nonFiniteResets = 0;
};

/// Voice: source → trachea → UVT (B3) → mix → DC → decimation.
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
        snapTrachea();
        snapUvt();
        primeDcFromSource(0.0);
        oversampling_.reset();
        out_smoother_.snap(static_cast<float>(settings_.out_gain_db));
        std::fill(internal_.begin(), internal_.end(), 0.0f);
        telemetry_ = {};
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
        labia_.setBandwidthHz(settings_.noise_beta_bandwidth_hz);
        asp_sm_.setTarget(static_cast<float>(settings_.noise_aspiration));
        coupling_sm_.setTarget(static_cast<float>(settings_.src_coupling));
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
        primeDcFromSource(0.0);
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
        s.sine_phase = sine_phase_;
        s.saw_phase = 0.0;
        s.labial_x = labia_.left().x();
        s.labial_y = labia_.left().y();
        s.out_gain_y = out_smoother_.value();
        s.rng0 = s.rng1 = s.rng2 = s.rng3 = 0;
        s.nonFiniteResets = labia_.nonFiniteResets();
    }

    void loadState(const VoiceState& s) noexcept {
        sine_phase_ = s.sine_phase;
        labia_.left().reset(s.labial_x, s.labial_y);
        out_smoother_.snap(s.out_gain_y);
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

private:
    static float lerp(float a, float b, float u) noexcept { return a + u * (b - a); }

    void seedNoiseStreams(std::uint64_t seed) noexcept {
        labia_.resetNoise(seed);
        aspiration_.reset(dsp::deriveStreamSeed(seed, dsp::kStreamAspiration));
    }

    void snapTrachea() noexcept {
        len_sm_.snap(static_cast<float>(settings_.trachea_length_mm));
        ref_sm_.snap(static_cast<float>(settings_.trachea_reflection));
        loss_sm_.snap(static_cast<float>(settings_.trachea_loss));
        gain_sm_.snap(static_cast<float>(settings_.trachea_input_gain));
        trachea_.setParams(settings_.trachea_length_mm,
                           settings_.trachea_reflection,
                           settings_.trachea_loss,
                           settings_.trachea_input_gain);
        trachea_.reset();
    }

    void snapUvt() noexcept {
        oec_damp_sm_.snap(static_cast<float>(settings_.oec_damping));
        beak_damp_sm_.snap(static_cast<float>(settings_.beak_damping));
        rad_sm_.snap(static_cast<float>(settings_.out_radiation));
        uvt_.setParams(static_cast<double>(from_.oec_freq_hz), settings_.oec_damping,
                       static_cast<double>(from_.beak_gape), settings_.beak_damping, settings_.body_size);
        uvt_.reset();
    }

    void applySmoothedTracheaParams() noexcept {
        trachea_.setParams(static_cast<double>(len_sm_.next()),
                           static_cast<double>(ref_sm_.next()),
                           static_cast<double>(loss_sm_.next()),
                           static_cast<double>(gain_sm_.next()));
    }

    [[nodiscard]] double throughTrachea(double source) noexcept {
        if (!use_trachea_) {
            return source;
        }
        applySmoothedTracheaParams();
        return trachea_.process(source);
    }

    [[nodiscard]] double throughUvt(double pt, double oecHz, double gape) noexcept {
        const float oecZ = oec_damp_sm_.next();
        const float beakZ = beak_damp_sm_.next();
        const float rho = rad_sm_.next();
        if ((uvt_counter_++ & 7) == 0) {
            uvt_.setParams(oecHz, static_cast<double>(oecZ), gape, static_cast<double>(beakZ),
                           settings_.body_size);
        }
        const auto o = uvt_.process(pt);
        return model::uvtMix(o.flow, o.dflow, static_cast<double>(rho));
    }

    void primeDcFromSource(double sourceSteady) noexcept {
        double pt = sourceSteady;
        if (use_trachea_) {
            trachea_.prime(sourceSteady);
            pt = trachea_.lastOutput();
        }
        if (use_uvt_) {
            uvt_.setParams(static_cast<double>(from_.oec_freq_hz), settings_.oec_damping,
                           static_cast<double>(from_.beak_gape), settings_.beak_damping, settings_.body_size);
            uvt_.prime(pt);
            dc_.prime(model::uvtMix(uvt_.last().flow, uvt_.last().dflow, settings_.out_radiation));
            uvt_counter_ = 0;
        } else {
            dc_.prime(pt);
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
            peak = std::max(peak, std::abs(out[i]));
        }
        telemetry_.level_db = peak > 1e-12f ? 20.0f * std::log10(peak) : -120.0f;
        telemetry_.nonFiniteResets = labia_.nonFiniteResets();
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

        const double gamma = settings_.src_gamma > 0.0 ? settings_.src_gamma : 24000.0;
        const auto& betaSpec = specOf(ParamId::SrcLBeta);
        const auto& sigSpec = specOf(ParamId::NoiseBetaSigma);
        const auto& gateSpec = specOf(ParamId::SrcLGate);
        float lastP = from_.resp_pressure;
        float lastBL = from_.src_L_beta;
        float lastBR = from_.src_R_beta;
        float lastAL = 0.0f;
        float lastAR = 0.0f;
        for (int i = 0; i < nInt; ++i) {
            lastP = pRamp.next();
            lastBL = bLRamp.next();
            lastBR = bRRamp.next();
            const float offL = oLRamp.next();
            const float offR = oRRamp.next();
            const float gL = gLRamp.next();
            const float gR = gRRamp.next();
            const float oec = oecRamp.next();
            const float gape = gapeRamp.next();
            const float sigma = static_cast<float>(clampParam(sigSpec, static_cast<double>(sigRamp.next())));
            const auto drive = resp_.process(static_cast<double>(lastP));
            model::BilateralFrame fr;
            fr.alphaBase = drive.alphaBase;
            fr.alphaOffL = static_cast<double>(offL);
            fr.alphaOffR = static_cast<double>(offR);
            fr.betaL = clampParam(betaSpec, static_cast<double>(lastBL));
            fr.betaR = clampParam(specOf(ParamId::SrcRBeta), static_cast<double>(lastBR));
            fr.gateL = clampParam(gateSpec, static_cast<double>(gL));
            fr.gateR = clampParam(specOf(ParamId::SrcRGate), static_cast<double>(gR));
            fr.gamma = gamma;
            fr.coupling = static_cast<double>(coupling_sm_.next());
            fr.sigmaBeta = static_cast<double>(sigma);
            const auto src = labia_.process(fr);
            lastAL = static_cast<float>(src.alphaL);
            lastAR = static_cast<float>(src.alphaR);
            const double aAsp = static_cast<double>(asp_sm_.next());
            const double nAsp = aspiration_.process(drive.G, src.xBar, aAsp);
            const double source = drive.G * src.mixTilde + nAsp;
            double y = throughTrachea(source);
            if (use_uvt_) {
                y = dc_.tick(throughUvt(y, static_cast<double>(oec), static_cast<double>(gape)));
            } else {
                y = 0.5 * dc_.tick(y);
            }
            internal_[static_cast<std::size_t>(i)] = static_cast<float>(y);
            lastP = static_cast<float>(drive.pressure);
            lastBL = static_cast<float>(src.betaEffL);
            lastBR = static_cast<float>(src.betaEffR);
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
    dsp::Rng rng_{1};
    model::TestSaw saw_;
    model::BilateralSource labia_;
    model::RespiratoryDrive resp_;
    model::AspirationNoise aspiration_;
    model::Trachea trachea_;
    model::UpperVocalTract uvt_;
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
    int os_ = 4;
    bool prepared_ = false;
};

} // namespace cuicui
