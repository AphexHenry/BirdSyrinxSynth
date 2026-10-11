#pragma once

#include "cuicui/control/archetypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cuicui::control {

/// Built-in grammar over C4 archetypes. See WP-C5 §4.1 and 01_research.md §7.8.
/// Indices match `kArchetypes`.
inline constexpr int kArchWhistle = 0;
inline constexpr int kArchUpsweep = 1;
inline constexpr int kArchDownsweep = 2;
inline constexpr int kArchArch = 3;
inline constexpr int kArchTrill = 4;
inline constexpr int kArchBuzz = 5;
inline constexpr int kArchStack = 6;
inline constexpr int kArchChip = 7;
inline constexpr int kArchTwoVoice = 8;
inline constexpr int kArchSwitchSweep = 9;
inline constexpr int kArchIntro = 10;

/// Base choice weights (intro is phrase-start only, weight 0 in the main table).
/// Tuned toward short phonemes (chips, short whistles) after C5 listening: long
/// sweeps/arches read as a flute / “cat on a piano”, not a bird.
inline constexpr double kGrammarBaseWeight[kNumArchetypes] = {
    1.10, // whistle
    0.28, // upsweep
    0.28, // downsweep
    0.18, // arch
    0.55, // trill
    0.22, // buzz
    0.40, // stack
    2.60, // chip
    0.06, // two_voice
    0.06, // switch_sweep
    0.00, // intro
};

/// Generative duration sits between the archetype min and default (0 = min).
inline constexpr double kGenDurBlend = 0.38;
/// Generative pitch-depth sits near the archetype min so syllables stay around the note.
inline constexpr double kGenDepthBlend = 0.20;
/// Syllable-anchor jitter (cents) at Range = 1. Spec wrote 150; 55 keeps the flock in-tune.
inline constexpr double kAnchorSigmaCents = 55.0;
/// Motion bias exponent (spec 4). Lower = high Motion does not collapse onto huge sweeps.
inline constexpr double kMotionBias = 2.0;
/// Gap median = (kGapOffsetMs + kGapSpanMs · (1 − Density)) / Speed.
inline constexpr double kGapOffsetMs = 16.0;
inline constexpr double kGapSpanMs = 200.0;

/// RMS pitch velocity of the default curves, computed once in `initGrammarMovement`.
inline double kGrammarMove[kNumArchetypes]{};
inline double kGrammarMoveMean = 0.0;
inline bool kGrammarMoveReady = false;

inline void initGrammarMovement() noexcept {
    if (kGrammarMoveReady) {
        return;
    }
    double acc = 0.0;
    int n = 0;
    for (int i = 0; i < kNumArchetypes; ++i) {
        const auto g = generateArchetypeIndexed(i, nullptr, 0, 1);
        kGrammarMove[static_cast<std::size_t>(i)] = movementScore(g.curves);
        acc += kGrammarMove[static_cast<std::size_t>(i)];
        ++n;
    }
    kGrammarMoveMean = n > 0 ? acc / static_cast<double>(n) : 0.0;
    kGrammarMoveReady = true;
}

[[nodiscard]] inline bool paramIsDuration(const char* name) noexcept {
    return std::strcmp(name, "dur") == 0;
}

[[nodiscard]] inline bool paramIsRate(const char* name) noexcept {
    return std::strcmp(name, "rate") == 0;
}

[[nodiscard]] inline bool paramIsPitchDepth(const char* name) noexcept {
    return std::strcmp(name, "arch") == 0 || std::strcmp(name, "depth") == 0 ||
           std::strcmp(name, "element_depth") == 0 || std::strcmp(name, "wobble") == 0 ||
           std::strcmp(name, "droop") == 0 || std::strcmp(name, "interval") == 0;
}

/// Gesture-count weights for k = 1..4. Prefer 1–2 phonemes even at high Motion.
inline void syllableCountWeights(double motion, double w[4]) noexcept {
    const double m = std::clamp(motion, 0.0, 1.0);
    const double lo[4] = {4.8, 1.1, 0.10, 0.02};
    const double hi[4] = {2.4, 2.0, 0.50, 0.10};
    for (int i = 0; i < 4; ++i) {
        w[i] = lo[i] + m * (hi[i] - lo[i]);
    }
}

/// Fill `out` with Motion/Tone-biased archetype weights. Intro stays 0 here.
inline void gestureTypeWeights(double motion, double tone, double out[kNumArchetypes]) noexcept {
    initGrammarMovement();
    const double m = std::clamp(motion, 0.0, 1.0);
    const double tn = std::clamp(tone, 0.0, 1.0);
    const double mean = kGrammarMoveMean;
    for (int i = 0; i < kNumArchetypes; ++i) {
        double w = kGrammarBaseWeight[static_cast<std::size_t>(i)];
        const double move = kGrammarMove[static_cast<std::size_t>(i)];
        w *= std::exp(kMotionBias * (m - 0.5) * (move - mean));
        if (i == kArchBuzz || i == kArchStack) {
            w *= (1.0 - tn);
        }
        if (i == kArchWhistle) {
            w *= std::max(0.05, tn);
        }
        out[static_cast<std::size_t>(i)] = std::max(0.0, w);
    }
}

} // namespace cuicui::control
