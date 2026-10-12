#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cuicui::control {

/// Labels of the syllables kept for the end of a long key press (D-030): the bird's long rising call
/// ("alarm", "alarm_grillon" in the human's annotations). They never start or continue a phrase; a key
/// held long enough ends with one when it is released.
inline constexpr std::string_view kReleaseLabelPrefix = "alarm";

/// Which part of a syllable is its "note" (D-019). Index into the anchor/centre arrays.
/// Rule = longest steady part (±50 c, ≥ 20 ms), else the ending (default).
enum class NoteRule { Rule = 0, Steady = 1, Landing = 2, Average = 3 };
inline constexpr int kNoteRuleCount = 4;

/// One real syllable. Its 1 ms curves live in SyllableBank's flat arrays at [offset, offset + lengthMs).
struct BankSyllable {
    int phrase = 0;   ///< phrase index
    int index = 0;    ///< position inside its phrase
    int tMs = 0;      ///< onset relative to the phrase's first syllable (ms)
    int lengthMs = 0; ///< number of 1 ms points (≥ 2)
    int offset = 0;   ///< first point in SyllableBank::logHz() / amp()
    std::array<double, kNoteRuleCount> anchorCents{}; ///< note under each rule, MIDI cents (6900 = A4)
    int ruleSpanBegin = 0; ///< contour span [begin, end) in ms that the default rule used
    int ruleSpanEnd = 0;
};

struct BankPhrase {
    int first = 0;  ///< index of its first syllable in SyllableBank::syllables()
    int count = 0;
    double startS = 0.0; ///< where the phrase starts in the source recording
};

/// A bird's real syllables (WP-R1 "VoiceBank"; file format `cuicui.syllable_bank`, D-026).
/// Built off the audio thread by `io::parseSyllableBank`; immutable afterwards, so the audio thread
/// only reads it.
class SyllableBank {
public:
    [[nodiscard]] bool empty() const noexcept { return syllables_.empty(); }
    [[nodiscard]] int numSyllables() const noexcept { return static_cast<int>(syllables_.size()); }
    [[nodiscard]] int numPhrases() const noexcept { return static_cast<int>(phrases_.size()); }
    [[nodiscard]] const BankSyllable& syllable(int i) const noexcept {
        return syllables_[static_cast<std::size_t>(i)];
    }
    [[nodiscard]] const BankPhrase& phrase(int i) const noexcept { return phrases_[static_cast<std::size_t>(i)]; }
    /// Syllable `index` of phrase `p`.
    [[nodiscard]] int syllableOf(int p, int index) const noexcept {
        return phrases_[static_cast<std::size_t>(p)].first + index;
    }
    /// Natural log of the pitch in Hz, one value per ms, all syllables back to back.
    [[nodiscard]] const double* logHz() const noexcept { return logHz_.data(); }
    /// Amplitude (normalised per phrase, max 1), one value per ms.
    [[nodiscard]] const float* amp() const noexcept { return amp_.data(); }
    /// The bird's centre for a rule: median of all syllables' anchors (MIDI cents).
    [[nodiscard]] double centreCents(NoteRule r) const noexcept {
        return centreCents_[static_cast<std::size_t>(r)];
    }
    [[nodiscard]] double anchorCents(int syl, NoteRule r) const noexcept {
        return syllables_[static_cast<std::size_t>(syl)].anchorCents[static_cast<std::size_t>(r)];
    }
    [[nodiscard]] const std::string& label(int syl) const noexcept { return labels_[static_cast<std::size_t>(syl)]; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& species() const noexcept { return species_; }
    [[nodiscard]] const std::string& hash() const noexcept { return hash_; }
    [[nodiscard]] int totalContourMs() const noexcept { return static_cast<int>(amp_.size()); }

    /// D-030: a syllable kept for the end of a long key press (see kReleaseLabelPrefix).
    [[nodiscard]] bool isReleaseSyllable(int syl) const noexcept {
        return releaseFlag_[static_cast<std::size_t>(syl)] != 0;
    }
    /// The release syllables, and all the others ("playable": what phrases, taps and shuffles may use).
    [[nodiscard]] const std::vector<int>& releaseSyllables() const noexcept { return release_; }
    [[nodiscard]] const std::vector<int>& playableSyllables() const noexcept { return playable_; }
    /// Median silence the bird leaves before a release syllable inside its phrases (ms); 60 if unknown.
    [[nodiscard]] double releaseGapMs() const noexcept { return releaseGapMs_; }

private:
    friend struct SyllableBankBuilder;
    std::vector<double> logHz_;
    std::vector<float> amp_;
    std::vector<BankSyllable> syllables_;
    std::vector<BankPhrase> phrases_;
    std::vector<std::string> labels_;
    std::vector<unsigned char> releaseFlag_;
    std::vector<int> release_, playable_;
    std::vector<double> releaseGaps_;
    double releaseGapMs_ = 60.0;
    std::array<double, kNoteRuleCount> centreCents_{};
    std::string name_, species_, hash_;
};

/// Write access for loaders and tests (keeps SyllableBank read-only for everyone else).
struct SyllableBankBuilder {
    SyllableBank bank;

    void setInfo(std::string name, std::string species, std::string hash) {
        bank.name_ = std::move(name);
        bank.species_ = std::move(species);
        bank.hash_ = std::move(hash);
    }
    void setCentres(const std::array<double, kNoteRuleCount>& c) { bank.centreCents_ = c; }
    void beginPhrase(double startS) {
        BankPhrase p;
        p.first = bank.numSyllables();
        p.startS = startS;
        bank.phrases_.push_back(p);
    }
    /// Append a syllable to the current phrase. `hz` and `amp` have one value per ms.
    void addSyllable(int tMs, const std::vector<double>& hz, const std::vector<float>& amp,
                     const std::array<double, kNoteRuleCount>& anchors, int spanBegin, int spanEnd,
                     std::string label) {
        auto& p = bank.phrases_.back();
        BankSyllable s;
        s.phrase = bank.numPhrases() - 1;
        s.index = p.count;
        s.tMs = tMs;
        s.lengthMs = static_cast<int>(hz.size());
        s.offset = static_cast<int>(bank.logHz_.size());
        s.anchorCents = anchors;
        s.ruleSpanBegin = spanBegin;
        s.ruleSpanEnd = spanEnd;
        for (std::size_t i = 0; i < hz.size(); ++i) {
            bank.logHz_.push_back(std::log(hz[i]));
            bank.amp_.push_back(amp[i]);
        }
        const bool release = std::string_view(label).substr(0, kReleaseLabelPrefix.size()) == kReleaseLabelPrefix;
        const int index = static_cast<int>(bank.syllables_.size());
        if (release && p.count > 0) { // the silence before it, after the previous syllable of its phrase
            const auto& prev = bank.syllables_.back();
            bank.releaseGaps_.push_back(static_cast<double>(tMs - (prev.tMs + prev.lengthMs)));
            auto sorted = bank.releaseGaps_;
            std::sort(sorted.begin(), sorted.end());
            const std::size_t m = sorted.size();
            bank.releaseGapMs_ = std::max(0.0, m % 2 == 1 ? sorted[m / 2] : 0.5 * (sorted[m / 2 - 1] + sorted[m / 2]));
        }
        (release ? bank.release_ : bank.playable_).push_back(index);
        bank.releaseFlag_.push_back(release ? 1 : 0);
        bank.syllables_.push_back(s);
        bank.labels_.push_back(std::move(label));
        ++p.count;
    }
};

} // namespace cuicui::control
