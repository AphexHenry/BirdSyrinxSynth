#pragma once

#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/dsp/rng.hpp"

#include <cstdint>

namespace cuicui::control {

/// What the bird sings next while a key is held (WP-R1 §3.2). Same choices as the PROTO1 prototype
/// (`Orders.real` / `Orders.shuffled`).
enum class OrderMode {
    RandomStart = 0, ///< a random syllable, then the rest of its real phrase, pause, a random next phrase
    PhraseStart = 1, ///< phrases from their first syllable, in the bank's order (shared pointer)
    Shuffled = 2     ///< random syllables, 20–70 ms apart, a phrase pause every 4–9 syllables
};

/// Exact, portable random helpers (the Python reference replays them bit for bit).
/// uniformIndex = floor(u·n) with u = (next() >> 8)·2⁻²⁴, computed in integers.
[[nodiscard]] inline int uniformIndex(dsp::Rng& rng, int n) noexcept {
    const std::uint64_t u24 = rng.next() >> 8;
    return static_cast<int>((u24 * static_cast<std::uint64_t>(n > 0 ? n : 1)) >> 24);
}

[[nodiscard]] inline double uniform01(dsp::Rng& rng) noexcept {
    return static_cast<double>(rng.next() >> 8) * 0x1.0p-24;
}

struct OrderStep {
    int syllable = 0;    ///< index into the bank
    double startS = 0.0; ///< onset, seconds after the singer's note-on
};

/// Per-singer order state. Times are seconds from the singer's note-on.
class PhraseOrder {
public:
    /// First syllable (starts at 0 s). `phrasePointer` is shared by all singers (PhraseStart mode).
    [[nodiscard]] OrderStep begin(const SyllableBank& bank, OrderMode mode, dsp::Rng& rng,
                                  int& phrasePointer) noexcept {
        mode_ = mode;
        if (mode == OrderMode::Shuffled) {
            last_ = uniformIndex(rng, bank.numSyllables());
            left_ = 4 + uniformIndex(rng, 6);
            return {last_, 0.0};
        }
        if (mode == OrderMode::RandomStart) {
            const int pick = uniformIndex(rng, bank.numSyllables());
            phrase_ = bank.syllable(pick).phrase;
            index_ = bank.syllable(pick).index;
        } else {
            phrase_ = nextPhrase(bank, rng, phrasePointer);
            index_ = 0;
        }
        return {bank.syllableOf(phrase_, index_), 0.0};
    }

    /// The syllable after the one that started at `prevStartS`.
    [[nodiscard]] OrderStep next(const SyllableBank& bank, double prevStartS, double pauseS, dsp::Rng& rng,
                                 int& phrasePointer) noexcept {
        if (mode_ == OrderMode::Shuffled) {
            const int s = uniformIndex(rng, bank.numSyllables());
            const double end = prevStartS + static_cast<double>(bank.syllable(last_).lengthMs) / 1000.0;
            double gap = 0.02 + uniform01(rng) * 0.05;
            if (--left_ <= 0) {
                left_ = 4 + uniformIndex(rng, 6);
                gap = pauseS;
            }
            last_ = s;
            return {s, end + gap};
        }
        const int prev = bank.syllableOf(phrase_, index_);
        if (index_ + 1 < bank.phrase(phrase_).count) {
            ++index_;
            const int s = bank.syllableOf(phrase_, index_);
            return {s, prevStartS + static_cast<double>(bank.syllable(s).tMs - bank.syllable(prev).tMs) / 1000.0};
        }
        const double end = prevStartS + static_cast<double>(bank.syllable(prev).lengthMs) / 1000.0;
        phrase_ = nextPhrase(bank, rng, phrasePointer);
        index_ = 0;
        return {bank.syllableOf(phrase_, 0), end + pauseS};
    }

    [[nodiscard]] OrderMode mode() const noexcept { return mode_; }

private:
    int nextPhrase(const SyllableBank& bank, dsp::Rng& rng, int& phrasePointer) const noexcept {
        const int n = bank.numPhrases();
        if (mode_ == OrderMode::RandomStart) {
            return uniformIndex(rng, n);
        }
        const int p = ((phrasePointer % n) + n) % n;
        phrasePointer = (p + 1) % n;
        return p;
    }

    OrderMode mode_ = OrderMode::RandomStart;
    int phrase_ = 0;
    int index_ = 0;
    int last_ = 0;
    int left_ = 0;
};

} // namespace cuicui::control
