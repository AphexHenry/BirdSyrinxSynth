#pragma once

#include "cuicui/control/syllable_bank.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cuicui::model {

/// The whistle voice (D-022): one real syllable as a phase-accumulating sine that follows the syllable's
/// pitch (log-frequency interpolation between 1 ms points, times the placement shift) and loudness
/// (linear interpolation), optionally plus the bird's own 2nd and 3rd harmonics (−30 / −33 dB), no breath.
/// Same synthesis as the PROTO1 prototype (`Voice.render`), plus short raised-cosine fades at both edges.
/// Real-time safe: start() and render() never allocate.
class WhistleVoice {
public:
    struct Colour {
        bool harmonics = true;
        double h2Db = -30.0; ///< 2nd harmonic re the fundamental (D-022)
        double h3Db = -33.0; ///< 3rd harmonic
        double edgeFadeMs = 2.5;
    };
    static constexpr double kH2Phase = 0.6; ///< harmonic phase offsets, as in the prototype
    static constexpr double kH3Phase = 0.9;

    void prepare(double sampleRate) noexcept {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        active_ = false;
    }

    /// Start `syllable` after `delaySamples` samples of the next render() call.
    void start(const control::SyllableBank& bank, int syllable, double shiftCents, float gain, float pan,
               int delaySamples, const Colour& colour, std::uint32_t age) noexcept {
        const auto& s = bank.syllable(syllable);
        startCurve(bank.logHz() + s.offset, bank.amp() + s.offset, s.lengthMs, syllable, shiftCents, gain, pan,
                   delaySamples, colour, age);
    }

    /// Start a curve that is not in a bank (WP-R3: a varied rendition). `logHz` / `amp` (one value per ms,
    /// `lengthMs` of them) must stay valid until the voice ends; `syllable` is only reported back.
    void startCurve(const double* logHz, const float* amp, int lengthMs, int syllable, double shiftCents, float gain,
                    float pan, int delaySamples, const Colour& colour, std::uint32_t age) noexcept {
        logF_ = logHz;
        amp_ = amp;
        m_ = lengthMs;
        ratio_ = std::pow(2.0, shiftCents / 1200.0);
        len_ = std::max<std::int64_t>(1, std::llround(static_cast<double>(m_) / 1000.0 * sr_));
        fadeN_ = static_cast<int>(std::min<std::int64_t>(std::llround(colour.edgeFadeMs * 0.001 * sr_), len_ / 2));
        h2_ = colour.harmonics ? std::pow(10.0, colour.h2Db / 20.0) : 0.0;
        h3_ = colour.harmonics ? std::pow(10.0, colour.h3Db / 20.0) : 0.0;
        gain_ = static_cast<double>(gain);
        const double x = 0.5 * (std::clamp(static_cast<double>(pan), -1.0, 1.0) + 1.0); // equal power
        gL_ = static_cast<float>(std::cos(x * std::numbers::pi / 2.0));
        gR_ = static_cast<float>(std::sin(x * std::numbers::pi / 2.0));
        phase_ = 0.0;
        i_ = 0;
        delay_ = std::max(0, delaySamples);
        killLen_ = 0;
        killLeft_ = 0;
        age_ = age;
        syllable_ = syllable;
        shiftCents_ = shiftCents;
        active_ = m_ >= 2;
    }

    /// Add up to n samples to left/right (right == nullptr: mono at the centre level, −3 dB).
    void render(float* left, float* right, int n) noexcept {
        if (!active_) {
            return;
        }
        int k = 0;
        if (delay_ > 0) {
            k = std::min(delay_, n);
            delay_ -= k;
        }
        constexpr double twoPi = 2.0 * std::numbers::pi;
        const float monoG = 0.70710678f;
        for (; k < n; ++k) {
            // Prototype order: time in ms, interpolate, advance the phase, then the sine.
            const double tms = static_cast<double>(i_) / sr_ * 1000.0;
            int j = static_cast<int>(std::floor(tms));
            double fr = tms - static_cast<double>(j);
            if (j >= m_ - 1) {
                j = m_ - 2;
                fr = 1.0;
            }
            const double f = std::exp(logF_[j] * (1.0 - fr) + logF_[j + 1] * fr) * ratio_;
            const double a = static_cast<double>(amp_[j]) * (1.0 - fr) + static_cast<double>(amp_[j + 1]) * fr;
            phase_ += twoPi * f / sr_;
            if (phase_ > twoPi) {
                phase_ -= twoPi;
            }
            double s = std::sin(phase_);
            if (h2_ > 0.0 || h3_ > 0.0) {
                s += h2_ * std::sin(2.0 * phase_ + kH2Phase) + h3_ * std::sin(3.0 * phase_ + kH3Phase);
            }
            double w = 1.0;
            if (fadeN_ > 0) {
                if (i_ < fadeN_) {
                    w = 0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(i_) + 0.5) / fadeN_);
                } else if (i_ >= len_ - fadeN_) {
                    w = 0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(len_ - i_) - 0.5) / fadeN_);
                }
            }
            if (killLen_ > 0) {
                w *= 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(killLeft_) / killLen_);
                --killLeft_;
            }
            const auto y = static_cast<float>(gain_ * a * s * w);
            if (right != nullptr) {
                left[k] += y * gL_;
                right[k] += y * gR_;
            } else {
                left[k] += y * monoG;
            }
            ++i_;
            if (i_ >= len_ || (killLen_ > 0 && killLeft_ <= 0)) { // free right after the last sample, so
                active_ = false;                                // voice allocation never depends on blocks
                return;
            }
        }
    }

    /// Fade out over `samples` (voice stealing, all sound off), then stop.
    void fadeOut(int samples) noexcept {
        if (!active_ || killLen_ > 0) {
            return;
        }
        killLen_ = std::max(1, samples);
        killLeft_ = killLen_;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool fading() const noexcept { return active_ && killLen_ > 0; }
    [[nodiscard]] std::uint32_t age() const noexcept { return age_; }
    [[nodiscard]] int syllable() const noexcept { return syllable_; }
    [[nodiscard]] double shiftCents() const noexcept { return shiftCents_; }
    /// Length of the current syllable in samples.
    [[nodiscard]] std::int64_t lengthSamples() const noexcept { return len_; }

private:
    const double* logF_ = nullptr;
    const float* amp_ = nullptr;
    double sr_ = 48000.0;
    double ratio_ = 1.0;
    double phase_ = 0.0;
    double gain_ = 0.0;
    double h2_ = 0.0;
    double h3_ = 0.0;
    double shiftCents_ = 0.0;
    std::int64_t i_ = 0;
    std::int64_t len_ = 0;
    float gL_ = 0.70710678f;
    float gR_ = 0.70710678f;
    int m_ = 0;
    int fadeN_ = 0;
    int delay_ = 0;
    int killLen_ = 0;
    int killLeft_ = 0;
    int syllable_ = -1;
    std::uint32_t age_ = 0;
    bool active_ = false;
};

} // namespace cuicui::model
