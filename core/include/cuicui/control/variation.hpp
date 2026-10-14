#pragma once

#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/dsp/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cuicui::control {

/// WP-R3 (D-036): the shape model's draws for one varied syllable: standard normals for its note, length,
/// loudness and shape modes, in that order.
inline constexpr int kVariantDraws = 3 + kMaxModes;
inline constexpr double kVariantZCap = 2.0; ///< draws are capped at ±2 sd (no rare extremes)
using VariantDraws = std::array<double, kVariantDraws>;

/// The draws for one request, from its seed (independent of every other random stream, so variation 0
/// leaves the R1 / R5 playing untouched, draw for draw). Real-time safe.
[[nodiscard]] inline VariantDraws variantDraws(std::uint64_t seed) noexcept {
    dsp::Rng rng(seed == 0 ? 1ULL : seed);
    VariantDraws z{};
    for (auto& v : z) {
        v = static_cast<double>(rng.gaussian());
    }
    return z;
}

/// A request's variant seed: from the singer's own seed and how many syllables it asked for.
[[nodiscard]] inline std::uint64_t variantSeed(std::uint64_t singerSeed, std::uint32_t count) noexcept {
    std::uint64_t x = singerSeed ^ (0xd1b54a32d192ed03ULL * (static_cast<std::uint64_t>(count) + 1ULL));
    return dsp::splitmix64(x);
}

/// WP-R3, the plugin's variation (the human, after STRETCH1, "that's working nicely"): each syllable a little
/// shorter or longer, and with a narrower or wider pitch sweep around its note. The spans are the ones the human
/// chose by ear; Variation v scales them: a draw is log-uniform between lo^v and hi^v (shorter and longer
/// equally likely), so v = 0 is the real syllable and v = 1 the full spans.
struct StretchSpans {
    double lengthLo = 0.8, lengthHi = 1.6; ///< STRETCH1: 80–160 %
    double lengthHiSwipe = 2.0;             ///< "swipes could actually get a length stretch up to 200%"
    double rangeLo = 0.6, rangeHi = 1.6;    ///< STRETCH1: 60–160 %
};

struct Stretch {
    double length = 1.0; ///< time scale of the syllable
    double range = 1.0;  ///< scale of its pitch movement around its note
};

/// A request's stretch, from its variant seed (independent of every other random stream). Syllables whose
/// curve has a tracker jump (bank flag "hop") are only narrowed, never widened: a wider jump is a louder error.
[[nodiscard]] inline Stretch drawStretch(std::uint64_t seed, double v, bool swipe, bool hop,
                                         const StretchSpans& sp = {}) noexcept {
    if (v <= 0.0) {
        return {};
    }
    std::uint64_t x = seed ^ 0x5851f42d4c957f2dULL;
    const double u1 = static_cast<double>(dsp::splitmix64(x) >> 11) * 0x1.0p-53;
    const double u2 = static_cast<double>(dsp::splitmix64(x) >> 11) * 0x1.0p-53;
    const double hiL = swipe ? sp.lengthHiSwipe : sp.lengthHi;
    auto logUniform = [&](double lo, double hi, double u) {
        return std::exp(v * (std::log(lo) + u * (std::log(hi) - std::log(lo))));
    };
    Stretch s{logUniform(sp.lengthLo, hiL, u1), logUniform(sp.rangeLo, sp.rangeHi, u2)};
    if (hop) {
        s.range = std::min(s.range, 1.0);
    }
    return s;
}

/// Length (ms) of a syllable of `n0` ms stretched by `lengthScale` (shape model off): the engine, the playing
/// mode's scheduling and the display agree on it.
[[nodiscard]] inline int stretchedLengthMs(int n0, double lengthScale) noexcept {
    return std::max(8, static_cast<int>(std::floor(static_cast<double>(n0) * lengthScale + 0.5)));
}

/// `np.interp(np.linspace(0, len − 1, n), arange(len), x)` exactly (the Python reference's resampling).
template <typename In, typename Out>
inline void resampleLinear(const In* x, int len, Out* out, int n) noexcept {
    if (n <= 0) {
        return;
    }
    const double step = n > 1 ? static_cast<double>(len - 1) / static_cast<double>(n - 1) : 0.0;
    for (int i = 0; i < n; ++i) {
        const double pos = (i == n - 1 && n > 1) ? static_cast<double>(len - 1) : static_cast<double>(i) * step;
        const int j = static_cast<int>(std::floor(pos));
        if (j >= len - 1) {
            out[i] = static_cast<Out>(x[len - 1]);
        } else {
            const double a = static_cast<double>(x[j]);
            const double b = static_cast<double>(x[j + 1]);
            out[i] = static_cast<Out>((b - a) * (pos - static_cast<double>(j)) + a);
        }
    }
}

struct Variant {
    int lengthMs = 0;            ///< points written (1 ms each)
    double noteShiftCents = 0.0; ///< how far its note moved (placement cancels k·this, see SyllableEngine)
};

/// A new rendition of `syl` at amount `v` (WP-R3, the shape model chosen after VARY1; D-036):
/// every value — note, log length, loudness, shape-mode scores — moves from the real rendition's r toward a
/// fresh draw t from its type's spread: r + v·(t − r). v = 0: the real syllable. v = 1: a new rendition at
/// the bird's natural spread. The real rendition's fine detail (what the modes do not explain) is kept.
/// Syllables without a type model move their note and length with the bird's median spread.
/// The loudness stays the real one (the human, after the R3 listening page: a varied loudness sounded like
/// "a syllable too quiet"), and every draw is capped at ±kVariantZCap standard deviations.
/// `lengthScale` stretches the result in time; `rangeScale` scales its pitch movement around its note (the
/// plugin's variation, StretchSpans; R6 can set both). The plugin keeps the shape model off (v = 0, VARY2: not
/// audible); a bank without a shape model ignores v. Writes ln(Hz) and amplitude per ms into the outputs (at
/// most `capacity`). Same arithmetic as `cuicui.variation.variant` (Python). Real-time safe.
inline Variant makeVariant(const SyllableBank& bank, int syl, double v, const VariantDraws& z, double lengthScale,
                           double rangeScale, double* logHzOut, float* ampOut, int capacity) noexcept {
    const auto& s = bank.syllable(syl);
    const int n0 = s.lengthMs;
    const double* lf = bank.logHz() + s.offset;
    const float* am = bank.amp() + s.offset;
    if (!bank.hasVariation()) {
        v = 0.0;
    }
    VariantDraws zc = z;
    for (auto& x : zc) {
        x = std::clamp(x, -kVariantZCap, kVariantZCap);
    }
    if (v == 0.0 && lengthScale == 1.0 && rangeScale == 1.0) {
        const int n = std::min(n0, capacity);
        std::copy(lf, lf + n, logHzOut);
        std::copy(am, am + n, ampOut);
        return {n, 0.0};
    }
    const double noteS = s.anchorCents[static_cast<std::size_t>(NoteRule::Rule)];
    const double logN0 = std::log(static_cast<double>(n0));
    double noteT = noteS;
    double logdT = logN0;
    const TypeModel* m = s.model >= 0 ? &bank.model(s.model) : nullptr;
    if (v != 0.0) {
        if (m != nullptr) {
            noteT = m->noteMean + m->noteSd * zc[0];
            logdT = m->logdurMean + m->logdurSd * zc[1];
        } else {
            const auto& sp = bank.spread(s.bird);
            noteT = noteS + sp.note * zc[0];
            logdT = logN0 + sp.logdur * zc[1];
        }
    }
    const double dnote = v * (noteT - noteS);
    const double nReal = v == 0.0 ? static_cast<double>(n0) * lengthScale // = stretchedLengthMs
                                  : std::exp(logN0 + v * (logdT - logN0)) * lengthScale;
    const int n = std::clamp(static_cast<int>(std::floor(nReal + 0.5)), 8, std::max(8, capacity));
    // pitch: resample the real curve (linear in ln Hz = linear in cents), then the note and shape changes
    resampleLinear(lf, n0, logHzOut, n);
    constexpr double kCentToLog = std::numbers::ln2 / 1200.0;
    const double dLog = dnote * kCentToLog;
    for (int i = 0; i < n; ++i) {
        logHzOut[i] += dLog;
    }
    resampleLinear(am, n0, ampOut, n); // the real loudness, stretched
    if (m != nullptr && v != 0.0 && m->numModes > 0) {
        const int P = bank.modelPoints();
        std::array<double, kMaxModelPoints> delta{};
        for (int k = 0; k < m->numModes; ++k) {
            const double coef = v * (m->modeSd[static_cast<std::size_t>(k)] * zc[3 + static_cast<std::size_t>(k)] -
                                     s.scores[static_cast<std::size_t>(k)]);
            for (int p = 0; p < P; ++p) {
                delta[static_cast<std::size_t>(p)] += coef * m->modes[static_cast<std::size_t>(k)][static_cast<std::size_t>(p)];
            }
        }
        // the change at output point i (P points resampled to n), computed on the fly (no buffer)
        const double step = n > 1 ? static_cast<double>(P - 1) / static_cast<double>(n - 1) : 0.0;
        auto changeAt = [&](int i) {
            const double pos = (i == n - 1 && n > 1) ? static_cast<double>(P - 1) : static_cast<double>(i) * step;
            const int j = static_cast<int>(std::floor(pos));
            return j >= P - 1 ? delta[static_cast<std::size_t>(P - 1)]
                              : (delta[static_cast<std::size_t>(j) + 1] - delta[static_cast<std::size_t>(j)]) *
                                        (pos - static_cast<double>(j)) +
                                    delta[static_cast<std::size_t>(j)];
        };
        // the shape change keeps the note: centre it on the loud part (rule E: loudness² weights over the
        // frames within 20 dB of the peak), so only dnote moves the note
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            peak = std::max(peak, ampOut[i]);
        }
        const double floorA = static_cast<double>(peak) * 0.1;
        double sw = 0.0;
        double swd = 0.0;
        for (int i = 0; i < n; ++i) {
            const double a = static_cast<double>(ampOut[i]);
            if (a >= floorA) {
                sw += a * a;
                swd += a * a * changeAt(i);
            }
        }
        const double centre = sw > 0.0 ? swd / sw : 0.0;
        for (int i = 0; i < n; ++i) {
            logHzOut[i] += (changeAt(i) - centre) * kCentToLog;
        }
    }
    if (rangeScale != 1.0) { // the pitch movement around the (moved) note, narrower or wider
        const double logNote = std::log(440.0) + (noteS + dnote - 6900.0) * kCentToLog;
        for (int i = 0; i < n; ++i) {
            logHzOut[i] = logNote + rangeScale * (logHzOut[i] - logNote);
        }
    }
    return {n, dnote}; // the amplitude is the real one, stretched
}

} // namespace cuicui::control
