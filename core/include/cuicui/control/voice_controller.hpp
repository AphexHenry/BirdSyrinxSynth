#pragma once

#include "cuicui/control/gesture_source.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/ornaments.hpp"
#include "cuicui/control/side_switcher.hpp"
#include "cuicui/control/snap.hpp"
#include "cuicui/dsp/rng.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/labial_oscillator.hpp"
#include "cuicui/model/respiratory_drive.hpp"
#include "cuicui/params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace cuicui::control {

/// MIDI + host macros at one control tick. See WP-C3 §4.1.
struct PerformInputs {
    bool gate = false;
    double noteCents = 6900.0;
    double bendCents = 0.0;
    double velocity01 = 0.8;
    double pressure01 = 0.0; // aftertouch / MPE pressure 0–1
    double breath = 0.7;
    double tone = 0.6;
    double physics = 0.5;
    double motion = 0.3;
    double range = 1.0;
    double speed = 1.0;
    double snap = 0.0;
    double glideMs = 30.0;
    double size = 1.0;
    int scale = 0;
    int root = 0;
    double oecTracking = 0.7;
    double gapeTracking = 0.15;
    double sideSwitchCents = 700.0;
    bool rangeFold = true;
    double leash = 0.0;
    std::uint64_t seed = 1;
    /// Phrase seed for GestureEngine (0 = caller already randomised). WP-C5.
    std::uint32_t phraseSeed = 0;
    double variation = 1.0;
    double density = 0.5;
    bool freeze = false;
    bool tempoSync = false;
    int syncDivision = 2;
    bool tempoValid = false;
    bool hostPlaying = false;
    double bpm = 120.0;
    double ppq = 0.0;
    /// Set true on the tick a new MIDI note-on is processed (including legato).
    bool noteOnEvent = false;
    bool noteOffEvent = false;
};

/// Pitch-led voice controller: MIDI intention → physiological ControlFrame.
///
/// Pressure vs α (WP-C3 §4.3, 01_research.md §7.3–§7.4):
/// B-phase physiology is unchanged: inside BirdVoice, α = α_base(p) + α^off with
/// α_base(p) = −0.10 + 0.55 p and p following resp.pressure through the respiratory
/// one-pole. This controller:
///  1. separates player breath u_pl (breath × velocity × aftertouch) from the
///     command u = u_pl · 10^(E_gest/20) written as resp.pressure;
///  2. solves a timbre target α_T = InverseMap::solve(..., α_breath = α_base(u_pl));
///  3. sets α^off = α_T − α_base(u_pl) while the gate is on, **frozen on release**
///     so α falls below threshold as p decays and the note stops;
///  4. mirrors the respiratory one-pole at control rate to predict p̂, then looks
///     up β = B(α_base(p̂)+α^off, ν) so pitch stays in tune during swells, gesture
///     amplitude dips, and attacks.
/// Quiet moments therefore sit closer to threshold (richer), which is bird-like.
class VoiceController {
public:
    struct Debug {
        double targetCents = 0.0;
        double f0Target = 0.0;
        double nu = 0.0;
        double alpha = 0.0;
        double beta = 0.0;
        double tone = 0.0;
        double side = 0.0;
        bool folded = false;
        double oecHz = 3000.0;
        double uPl = 0.0;
        double pHat = 0.0;
        double ornamentCents = 0.0;
        int activeSide = 0;
    };

    void prepare(double controlRate, const InverseMap& map) noexcept {
        rate_ = controlRate > 1.0 ? controlRate : 1500.0;
        dt_ = 1.0 / rate_;
        map_ = &map;
        switcher_.prepare(rate_);
        switcher_.setCrossfadeMs(4.0);
        resp_.prepare(rate_);
        reset();
    }

    void reset() noexcept {
        pNote_ = 6900.0;
        pPrev_ = 6900.0;
        pHistFill_ = 0;
        pHistIdx_ = 0;
        for (double& x : pHist_) {
            x = 6900.0;
        }
        wasGate_ = false;
        alphaOff_ = 0.0;
        pHat_ = 0.0;
        betaHoldL_ = 0.3;
        betaHoldR_ = 0.3;
        holdOutgoing_ = false;
        outgoingSide_ = 0;
        noteIndex_ = 0;
        debug_ = {};
        ornaments_.reset();
        switcher_.reset();
        resp_.reset(0.0);
        fadeBetaL_ = 0.3;
        fadeBetaR_ = 0.3;
        pUnsnapped_ = 6900.0;
        lambda_ = 0.0;
    }

    void setGestureSource(GestureSource* src) noexcept { src_ = src; }

    /// `out` must already contain the host ControlFrame (oec.freq_hz, beak.gape,
    /// noise.beta_sigma are used as bases). Driven fields are overwritten.
    void tick(const PerformInputs& in, const VoiceSettings& base, ControlFrame& out) noexcept {
        const InverseMap& map = map_ != nullptr ? *map_ : localMap_;
        const double dt = dt_;
        const double sigma = std::clamp(in.size, 0.25, 4.0);
        const double gamma0 = base.src_gamma > 0.0 ? base.src_gamma : 24000.0;
        const double gamma = gamma0 / sigma;
        const double rootS = std::sqrt(sigma);
        const double attMs = std::max(1.0, base.resp_attack_ms) * rootS;
        const double relMs = std::max(1.0, base.resp_release_ms) * rootS;
        resp_.setTimeConstantsMs(attMs, relMs);

        const bool gate = in.gate;
        const bool fromSilence = gate && !wasGate_;
        const bool asNew = fromSilence && (pHat_ < 0.20); // new note, not release-legato
        const std::uint64_t noteSeed = dsp::deriveStreamSeed(
            in.seed == 0 ? 1ULL : in.seed, dsp::kStreamOrnament + static_cast<int>(noteIndex_));
        const int vel127 = static_cast<int>(std::lround(std::clamp(in.velocity01, 0.0, 1.0) * 127.0));
        const std::uint32_t gSeed =
            in.phraseSeed != 0 ? in.phraseSeed : static_cast<std::uint32_t>(noteSeed == 0 ? 1ULL : noteSeed);
        const double anchor = in.noteCents + in.bendCents;

        if (asNew) {
            ornaments_.noteOn(in.motion, in.range, in.speed, relMs, noteSeed, true);
            if (src_ != nullptr) {
                src_->noteOn(anchor, vel127, gSeed);
            }
            ++noteIndex_;
            pNote_ = in.noteCents;
            pUnsnapped_ = in.noteCents;
            pPrev_ = in.noteCents;
            for (double& x : pHist_) {
                x = in.noteCents;
            }
            pHistFill_ = kHistMax;
            pHistIdx_ = 0;
        } else if (fromSilence || (in.noteOnEvent && wasGate_)) {
            if (src_ != nullptr) {
                src_->noteOn(anchor, vel127, gSeed);
            }
        }
        if (!gate && wasGate_) {
            ornaments_.noteOff();
            if (src_ != nullptr) {
                src_->noteOff();
            }
        }

        GestureSample g{};
        if (src_ != nullptr) {
            g = src_->next(dt);
        }

        // Glide: one-pole in cents, legato only; jump after silence. WP-C3 §4.2.
        if (gate) {
            if (!wasGate_ && pHat_ < 0.20) {
                pNote_ = in.noteCents;
            } else {
                pNote_ = onePole(pNote_, in.noteCents, in.glideMs * 0.001, dt);
            }
        }

        const double ornament = ornaments_.next(dt);
        const double D = pNote_ + in.bendCents + ornament;
        const double leashT = std::clamp(in.leash, 0.0, 1.0);
        if (asNew) {
            lambda_ = leashT;
        } else {
            lambda_ = onePole(lambda_, leashT, 0.010, dt);
        }
        const double lambda = lambda_;
        const double G = (g.active && src_ != nullptr) ? g.pitchCents : D;
        double P = (1.0 - lambda) * D + lambda * G;

        const double dPdt = dt > 0.0 ? (P - pUnsnapped_) / dt : 0.0;
        pUnsnapped_ = P;
        P = applySnap(P, in.snap, dPdt, in.scale, in.root);

        const double f0 = hzFromCents(P);
        const double nu = (gamma > 0.0) ? (2.0 * std::numbers::pi * f0 / gamma) : 0.0;

        double tone = std::clamp(in.tone, 0.0, 1.0);
        if (lambda > 0.0 && g.active) {
            tone = (1.0 - lambda) * tone + lambda * std::clamp(g.tone, 0.0, 1.0);
        }

        const double uPl = std::clamp(in.breath * (0.6 + 0.4 * std::clamp(in.velocity01, 0.0, 1.0)) *
                                          (1.0 + std::clamp(in.pressure01, 0.0, 1.0) * 0.5),
                                      0.0, 1.0);
        const double eGest = g.active ? g.ampDb : 0.0;
        const double gestAmp = g.active ? std::pow(10.0, eGest / 20.0) : 0.0;
        // WP-C5 §4.2: gaps become silences as λ → 1; λ = 0 is the C3 player breath.
        const double art = (1.0 - lambda) + lambda * gestAmp;
        const double u = gate ? std::clamp(uPl * art, 0.0, 1.0) : 0.0;

        const double alphaBreath = model::alphaFromPressure(uPl, 0.0);
        const auto solved = map.solve(nu, tone, alphaBreath, in.physics, in.rangeFold);
        if (gate) {
            alphaOff_ = solved.alpha - alphaBreath;
        }

        const auto respOut = resp_.process(u);
        pHat_ = respOut.pressure;
        double alphaHat = model::alphaFromPressure(pHat_, alphaOff_);
        double lo = solved.alpha;
        double hi = solved.alpha;
        if (!map.alphaBand(solved.nuUsed, lo, hi)) {
            lo = -0.10;
            hi = 0.45;
        }
        const double alphaLookup = std::clamp(alphaHat, lo, hi);
        double beta = map.betaOf(alphaLookup, solved.nuUsed);
        if (!std::isfinite(beta)) {
            beta = solved.beta;
        }

        // OEC / gape tracking. 01_research.md §7.3.
        const double fRef = 1000.0 / sigma;
        double fH = static_cast<double>(out.oec_freq_hz);
        if (lambda > 0.5 && hintSet(g.oecRatio) && g.oecRatio > 0.0) {
            fH = g.oecRatio * f0;
        } else {
            const double kappa = std::clamp(in.oecTracking, 0.0, 1.0);
            const double ratio = (fRef > 1.0e-9 && f0 > 0.0) ? (f0 / fRef) : 1.0;
            fH = static_cast<double>(out.oec_freq_hz) * std::pow(std::max(ratio, 1.0e-9), kappa);
        }
        fH = clampParam(specOf(ParamId::OecFreqHz), fH);

        double gape = static_cast<double>(out.beak_gape);
        if (lambda > 0.5 && hintSet(g.gape)) {
            gape = g.gape;
        } else {
            const double kg = std::clamp(in.gapeTracking, 0.0, 0.5);
            const double oct = (fRef > 1.0e-9 && f0 > 0.0) ? std::log2(f0 / fRef) : 0.0;
            gape = static_cast<double>(out.beak_gape) + kg * oct;
        }
        gape = clampParam(specOf(ParamId::BeakGape), gape);

        const double buzz = 0.03 * std::max(0.0, 0.15 - tone) / 0.15;
        const double sigmaBeta =
            clampParam(specOf(ParamId::NoiseBetaSigma), static_cast<double>(out.noise_beta_sigma) + buzz);

        // Sides. WP-C3 §4.3.
        double betaL = beta;
        double betaR = beta;
        double gateL = 1.0;
        double gateR = 0.0;
        int active = switcher_.activeSide();

        if (lambda > 0.5 && hintSet(g.secondVoiceCents)) {
            const double nu2 = (gamma > 0.0)
                                   ? (2.0 * std::numbers::pi * hzFromCents(P + g.secondVoiceCents) / gamma)
                                   : nu;
            const auto s2 = map.solve(nu2, tone, alphaBreath, in.physics, in.rangeFold);
            double a2 = model::alphaFromPressure(pHat_, alphaOff_);
            a2 = std::clamp(a2, lo, hi);
            double b2 = map.betaOf(a2, s2.nuUsed);
            if (!std::isfinite(b2)) {
                b2 = s2.beta;
            }
            betaL = beta;
            betaR = b2;
            gateL = 1.0;
            gateR = 1.0;
            holdOutgoing_ = false;
        } else if (lambda > 0.5 && g.active && hintSet(g.side)) {
            const double s = std::clamp(g.side, -1.0, 1.0);
            betaL = beta;
            betaR = beta;
            gateL = std::sqrt(std::max(0.0, 0.5 * (1.0 - s)));
            gateR = std::sqrt(std::max(0.0, 0.5 * (1.0 + s)));
            holdOutgoing_ = false;
            if (s < -0.33) {
                switcher_.setActive(0);
            } else if (s > 0.33) {
                switcher_.setActive(1);
            }
            active = switcher_.activeSide();
        } else {
            const double split = playableSplitCents(map, gamma, tone);
            const int jumpSide = detectJumpSide(P, in.sideSwitchCents, active);
            int desired = active;
            if (P > split + 100.0) {
                desired = 1;
            } else if (P < split - 100.0) {
                desired = 0;
            } else if (jumpSide >= 0) {
                desired = jumpSide;
            }
            if (desired != active && !switcher_.fading()) {
                outgoingSide_ = active;
                holdOutgoing_ = true;
                if (outgoingSide_ == 0) {
                    fadeBetaL_ = betaHoldL_;
                    fadeBetaR_ = beta;
                } else {
                    fadeBetaR_ = betaHoldR_;
                    fadeBetaL_ = beta;
                }
            }
            switcher_.setActive(desired);
            if (holdOutgoing_ && switcher_.fading()) {
                if (outgoingSide_ == 0) {
                    betaL = fadeBetaL_;
                    betaR = beta;
                } else {
                    betaR = fadeBetaR_;
                    betaL = beta;
                }
            } else {
                holdOutgoing_ = false;
                betaL = beta;
                betaR = beta;
            }
            const auto st = switcher_.next(betaL, betaR);
            betaL = st.betaL;
            betaR = st.betaR;
            gateL = st.gateL;
            gateR = st.gateR;
            if (!switcher_.fading()) {
                holdOutgoing_ = false;
            }
            active = switcher_.activeSide();
        }

        betaHoldL_ = betaL;
        betaHoldR_ = betaR;
        pushHist(P);
        pPrev_ = P;
        wasGate_ = gate;

        out.resp_pressure = static_cast<float>(u);
        out.src_L_beta = static_cast<float>(clampParam(specOf(ParamId::SrcLBeta), betaL));
        out.src_R_beta = static_cast<float>(clampParam(specOf(ParamId::SrcRBeta), betaR));
        out.src_L_gate = static_cast<float>(clampParam(specOf(ParamId::SrcLGate), gateL));
        out.src_R_gate = static_cast<float>(clampParam(specOf(ParamId::SrcRGate), gateR));
        out.src_L_alpha_offset = static_cast<float>(clampParam(specOf(ParamId::SrcLAlphaOffset), alphaOff_));
        out.src_R_alpha_offset = static_cast<float>(clampParam(specOf(ParamId::SrcRAlphaOffset), alphaOff_));
        out.oec_freq_hz = static_cast<float>(fH);
        out.beak_gape = static_cast<float>(gape);
        out.noise_beta_sigma = static_cast<float>(sigmaBeta);

        debug_.targetCents = P;
        debug_.f0Target = f0;
        debug_.nu = nu;
        debug_.alpha = alphaHat;
        debug_.beta = beta;
        debug_.tone = tone;
        debug_.side = static_cast<double>(active);
        debug_.folded = solved.folded;
        debug_.oecHz = fH;
        debug_.uPl = uPl;
        debug_.pHat = pHat_;
        debug_.ornamentCents = ornament;
        debug_.activeSide = active;
    }

    [[nodiscard]] Debug debug() const noexcept { return debug_; }

    [[nodiscard]] static double hzFromCents(double cents) noexcept {
        return 440.0 * std::pow(2.0, (cents - 6900.0) / 1200.0);
    }

    [[nodiscard]] static double centsFromHz(double hz) noexcept {
        if (!(hz > 0.0)) {
            return 0.0;
        }
        return 6900.0 + 1200.0 * std::log2(hz / 440.0);
    }

private:
    static constexpr int kHistMax = 64; // 30 ms at ~1.5 kHz ≈ 45

    [[nodiscard]] static double onePole(double y, double target, double tauSec, double dt) noexcept {
        if (!(tauSec > 1.0e-6) || dt <= 0.0) {
            return target;
        }
        const double a = std::exp(-dt / tauSec);
        return target + (y - target) * a;
    }

    [[nodiscard]] static double playableSplitCents(const InverseMap& map, double gamma,
                                                   double tone) noexcept {
        const double fLo = map.nuMin() * gamma / (2.0 * std::numbers::pi);
        const double fHi = map.nuMax(tone) * gamma / (2.0 * std::numbers::pi);
        return 0.5 * (centsFromHz(fLo) + centsFromHz(fHi));
    }

    void pushHist(double p) noexcept {
        pHist_[static_cast<std::size_t>(pHistIdx_)] = p;
        pHistIdx_ = (pHistIdx_ + 1) % kHistMax;
        if (pHistFill_ < kHistMax) {
            ++pHistFill_;
        }
    }

    /// Other side if |ΔP| over ~30 ms (or this tick) exceeds the threshold; else -1.
    [[nodiscard]] int detectJumpSide(double P, double thresh, int active) const noexcept {
        const double t = std::max(100.0, thresh);
        if (std::abs(P - pPrev_) > t) {
            return active == 0 ? 1 : 0;
        }
        const int n30 = std::clamp(static_cast<int>(std::lround(0.030 * rate_)), 1, kHistMax - 1);
        if (pHistFill_ <= n30) {
            return -1;
        }
        const int i = (pHistIdx_ - n30 - 1 + kHistMax * 4) % kHistMax;
        const double pOld = pHist_[static_cast<std::size_t>(i)];
        if (std::abs(P - pOld) > t) {
            return active == 0 ? 1 : 0;
        }
        return -1;
    }

    double rate_ = 1500.0;
    double dt_ = 1.0 / 1500.0;
    const InverseMap* map_ = nullptr;
    InverseMap localMap_{};
    GestureSource* src_ = nullptr;
    SideSwitcher switcher_{};
    model::RespiratoryDrive resp_{};
    Ornaments ornaments_{};
    double pNote_ = 6900.0;
    double pPrev_ = 6900.0;
    double pUnsnapped_ = 6900.0;
    double pHist_[kHistMax]{};
    int pHistFill_ = 0;
    int pHistIdx_ = 0;
    bool wasGate_ = false;
    double alphaOff_ = 0.0;
    double pHat_ = 0.0;
    double betaHoldL_ = 0.3;
    double betaHoldR_ = 0.3;
    bool holdOutgoing_ = false;
    int outgoingSide_ = 0;
    double fadeBetaL_ = 0.3;
    double fadeBetaR_ = 0.3;
    std::uint32_t noteIndex_ = 0;
    double lambda_ = 0.0;
    Debug debug_{};
};

} // namespace cuicui::control
