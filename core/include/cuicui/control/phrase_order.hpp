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
/// `reserve` (D-030): leave the release syllables out (they end a long key press instead). A phrase then
/// closes up over a skipped syllable: the next one starts as much earlier as the skipped one lasted, so the
/// silences around it remain. `reserve` also keeps the syllables flagged "no_note" / "no_tap" (D-032) from
/// starting a key press: a random first pick that lands on one is drawn again from the singer's stream, so every
/// other pick is unchanged; a phrase start moves to its first syllable that may start keys. They still sing inside
/// phrases. With `reserve` false the order is the R1 / PROTO1 one, draw for draw.
class PhraseOrder {
public:
    /// First syllable (starts at 0 s). `phrasePointer` is shared by all singers (PhraseStart mode).
    [[nodiscard]] OrderStep begin(const SyllableBank& bank, OrderMode mode, dsp::Rng& rng, int& phrasePointer,
                                  bool reserve = false) noexcept {
        mode_ = mode;
        reserve_ = reserve && !bank.playableSyllables().empty();
        if (mode == OrderMode::Shuffled) {
            last_ = pickFirst(bank, rng);
            left_ = 4 + uniformIndex(rng, 6);
            return {last_, 0.0};
        }
        if (mode == OrderMode::RandomStart) {
            const int pick = pickFirst(bank, rng);
            phrase_ = bank.syllable(pick).phrase;
            index_ = bank.syllable(pick).index;
        } else {
            enterNextPhrase(bank, rng, phrasePointer);
            for (int tries = 0; reserve_ && !bank.tapSyllables().empty(); ++tries) { // a phrase start that may start keys
                while (index_ < bank.phrase(phrase_).count && !bank.isTapSyllable(bank.syllableOf(phrase_, index_))) {
                    ++index_;
                }
                if (index_ < bank.phrase(phrase_).count) {
                    break;
                }
                if (tries >= 4 * bank.numPhrases()) { // cannot happen while a tap syllable exists; stay safe
                    const int s = bank.tapSyllables().front();
                    phrase_ = bank.syllable(s).phrase;
                    index_ = bank.syllable(s).index;
                    break;
                }
                enterNextPhrase(bank, rng, phrasePointer);
            }
        }
        return {bank.syllableOf(phrase_, index_), 0.0};
    }

    /// The syllable after the one that started at `prevStartS`.
    [[nodiscard]] OrderStep next(const SyllableBank& bank, double prevStartS, double pauseS, dsp::Rng& rng,
                                 int& phrasePointer) noexcept {
        if (mode_ == OrderMode::Shuffled) {
            const int s = pickSyllable(bank, rng);
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
        int j = index_ + 1;
        double skippedS = 0.0;
        while (reserve_ && j < bank.phrase(phrase_).count && bank.isReleaseSyllable(bank.syllableOf(phrase_, j))) {
            skippedS += static_cast<double>(bank.syllable(bank.syllableOf(phrase_, j)).lengthMs) / 1000.0;
            ++j;
        }
        if (j < bank.phrase(phrase_).count) {
            index_ = j;
            const int s = bank.syllableOf(phrase_, index_);
            return {s, prevStartS + static_cast<double>(bank.syllable(s).tMs - bank.syllable(prev).tMs) / 1000.0 -
                           skippedS};
        }
        const double end = prevStartS + static_cast<double>(bank.syllable(prev).lengthMs) / 1000.0;
        enterNextPhrase(bank, rng, phrasePointer);
        return {bank.syllableOf(phrase_, index_), end + pauseS};
    }

    [[nodiscard]] OrderMode mode() const noexcept { return mode_; }

private:
    /// The first syllable of a key press (D-032): with reserve, a pick that may not start keys is drawn again
    /// (bounded; then the first one that may). Banks without such flags draw exactly as before.
    int pickFirst(const SyllableBank& bank, dsp::Rng& rng) const noexcept {
        int pick = pickSyllable(bank, rng);
        if (!reserve_ || bank.tapSyllables().empty()) {
            return pick;
        }
        for (int tries = 0; tries < kFirstPickTries && !bank.isTapSyllable(pick); ++tries) {
            pick = pickSyllable(bank, rng);
        }
        return bank.isTapSyllable(pick) ? pick : bank.tapSyllables().front();
    }

    static constexpr int kFirstPickTries = 64;

    /// A syllable to start from, uniformly: any (R1), or only the playable ones (reserve).
    int pickSyllable(const SyllableBank& bank, dsp::Rng& rng) const noexcept {
        if (!reserve_) {
            return uniformIndex(rng, bank.numSyllables());
        }
        const auto& ok = bank.playableSyllables();
        return ok[static_cast<std::size_t>(uniformIndex(rng, static_cast<int>(ok.size())))];
    }

    /// Next phrase, entered at its first syllable that may be sung (with reserve, a phrase made only of
    /// release syllables is passed over).
    void enterNextPhrase(const SyllableBank& bank, dsp::Rng& rng, int& phrasePointer) noexcept {
        for (int tries = 0;; ++tries) {
            phrase_ = nextPhrase(bank, rng, phrasePointer);
            index_ = 0;
            if (!reserve_) {
                return;
            }
            while (index_ < bank.phrase(phrase_).count && bank.isReleaseSyllable(bank.syllableOf(phrase_, index_))) {
                ++index_;
            }
            if (index_ < bank.phrase(phrase_).count) {
                return;
            }
            if (tries >= 4 * bank.numPhrases()) { // cannot happen while a playable syllable exists; stay safe
                const int s = bank.playableSyllables().front();
                phrase_ = bank.syllable(s).phrase;
                index_ = bank.syllable(s).index;
                return;
            }
        }
    }

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
    bool reserve_ = false;
    int phrase_ = 0;
    int index_ = 0;
    int last_ = 0;
    int left_ = 0;
};

} // namespace cuicui::control
