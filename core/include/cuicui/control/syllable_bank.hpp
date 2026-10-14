#pragma once

#include <algorithm>
#include <array>
#include <cctype>
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

/// Bank flags (D-032) that keep a syllable from starting a key press: "no_note" (the listener heard no clear note,
/// TUNE1) and "no_tap" (set by the bank builder). Such syllables still sing inside phrases.
inline constexpr std::string_view kNoNoteFlag = "no_note";
inline constexpr std::string_view kNoTapFlag = "no_tap";

/// Which part of a syllable is its "note" (D-019, D-031). Index into the anchor/centre arrays.
/// Rule = the bank's default: since WP-R5 the loudness-weighted mean ("loud part"; R1 banks: the longest steady
/// part, else the ending). Notes set by ear (D-032) replace every rule's value for their syllables.
enum class NoteRule { Rule = 0, Steady = 1, Landing = 2, Average = 3 };
inline constexpr int kNoteRuleCount = 4;

/// WP-R3 shape model (D-036): limits of a type model's arrays.
inline constexpr int kMaxModelPoints = 64;
inline constexpr int kMaxModes = 4;

/// A syllable type's model (WP-R3, `cuicui.variation_model`): the spread of its renditions. Shapes are
/// time-normalised curves of `SyllableBank::modelPoints()` points, in cents, after removing each
/// rendition's note.
struct TypeModel {
    double noteMean = 0.0, noteSd = 0.0;     ///< the rendition's note (MIDI cents)
    double logdurMean = 0.0, logdurSd = 0.0; ///< natural log of its length in ms
    double peakMean = 0.0, peakSd = 0.0;     ///< its loudest frame (dB re the phrase's loudest)
    int numModes = 0;
    std::array<double, kMaxModelPoints> mean{};
    std::array<std::array<double, kMaxModelPoints>, kMaxModes> modes{};
    std::array<double, kMaxModes> modeSd{};
};

/// A bird's median spread (used for syllables without a type model).
struct VariationSpread {
    double note = 0.0;   ///< cents
    double logdur = 0.0; ///< natural log of the length
    double peakDb = 0.0; ///< dB
};

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
    // WP-R3
    int bird = 0;            ///< which source bank it came from (SyllableBank::birds())
    int type = -1;           ///< its syllable type within its bird (−1 = unknown)
    int model = -1;          ///< index into SyllableBank::model() (−1 = no type model)
    std::array<double, kMaxModes> scores{}; ///< its shape-mode scores (model ≥ 0)
    double peakDb = 0.0;     ///< 20·log10 of its loudest frame
    double sweepCents = 0.0; ///< pitch at the end minus at the start (R6: "sweeps up / down")
    bool swipe = false;      ///< its label names a swipe ("swipe", "Hswipe", "swipe+swipe"…): longer stretches
    bool hop = false;        ///< bank flag "hop": a tracker jump in its curve (never widened)
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
    /// D-032: may this syllable start a key press? False for the bank flags "no_note" / "no_tap" and for the
    /// release syllables. `tapSyllables()` lists the playable ones that may (empty = none flagged… or none left).
    [[nodiscard]] bool isTapSyllable(int syl) const noexcept { return tapFlag_[static_cast<std::size_t>(syl)] != 0; }
    [[nodiscard]] const std::vector<int>& tapSyllables() const noexcept { return tap_; }

    // ---- WP-R3: birds, the shape model (D-036), queries (R6)
    /// The source banks merged into this one (one name per bird; a plain bank has one).
    [[nodiscard]] const std::vector<std::string>& birds() const noexcept { return birds_; }
    /// Does the bank carry a shape model (every bird of it)? Without one, variation leaves syllables real.
    [[nodiscard]] bool hasVariation() const noexcept { return hasVariation_; }
    [[nodiscard]] int modelPoints() const noexcept { return modelPoints_; }
    [[nodiscard]] int numModels() const noexcept { return static_cast<int>(models_.size()); }
    [[nodiscard]] const TypeModel& model(int m) const noexcept { return models_[static_cast<std::size_t>(m)]; }
    [[nodiscard]] const VariationSpread& spread(int bird) const noexcept { return spreads_[static_cast<std::size_t>(bird)]; }
    /// Longest syllable (ms).
    [[nodiscard]] int maxLengthMs() const noexcept { return maxLengthMs_; }

    /// What a playing mode may ask for (R6): every condition left at its default matches everything.
    struct Query {
        std::string_view labelPrefix{}; ///< e.g. "swipe", "peep", "alarm"
        int minMs = 0;
        int maxMs = 1 << 30;
        double minSweepCents = -1e9; ///< e.g. 200 = "sweeps up by at least 2 semitones"
        double maxSweepCents = 1e9;
        int bird = -1; ///< −1 = any
        bool playableOnly = true; ///< leave the release calls out (D-030)
    };
    /// Syllables matching `q`, in bank order. Allocates: call off the audio thread and keep the result.
    [[nodiscard]] std::vector<int> find(const Query& q) const {
        std::vector<int> out;
        for (int i = 0; i < numSyllables(); ++i) {
            const auto& sy = syllables_[static_cast<std::size_t>(i)];
            const auto& lab = labels_[static_cast<std::size_t>(i)];
            if ((q.playableOnly && isReleaseSyllable(i)) || sy.lengthMs < q.minMs || sy.lengthMs > q.maxMs ||
                sy.sweepCents < q.minSweepCents || sy.sweepCents > q.maxSweepCents ||
                (q.bird >= 0 && sy.bird != q.bird) ||
                std::string_view(lab).substr(0, q.labelPrefix.size()) != q.labelPrefix) {
                continue;
            }
            out.push_back(i);
        }
        return out;
    }

private:
    friend struct SyllableBankBuilder;
    std::vector<double> logHz_;
    std::vector<float> amp_;
    std::vector<BankSyllable> syllables_;
    std::vector<BankPhrase> phrases_;
    std::vector<std::string> labels_;
    std::vector<unsigned char> releaseFlag_, tapFlag_;
    std::vector<int> release_, playable_, tap_;
    std::vector<double> releaseGaps_;
    double releaseGapMs_ = 60.0;
    std::array<double, kNoteRuleCount> centreCents_{};
    std::string name_, species_, hash_;
    std::vector<std::string> birds_;
    std::vector<TypeModel> models_;
    std::vector<VariationSpread> spreads_;
    int modelPoints_ = 0;
    int maxLengthMs_ = 0;
    bool hasVariation_ = false;
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
    /// WP-R3: the bank's birds (default: one, named after the bank) and its shape model.
    void setBirds(std::vector<std::string> names) { bank.birds_ = std::move(names); }
    void setVariation(int points, std::vector<VariationSpread> spreads) {
        bank.modelPoints_ = points;
        bank.spreads_ = std::move(spreads);
        bank.hasVariation_ = true;
    }
    void addModel(const TypeModel& m) { bank.models_.push_back(m); }
    /// Overwrite the last added syllable's pitch curve and measures with exact values (merging).
    void setLastExact(const double* logHz, double peakDb, double sweepCents) {
        auto& sy = bank.syllables_.back();
        std::copy(logHz, logHz + sy.lengthMs, bank.logHz_.begin() + sy.offset);
        sy.peakDb = peakDb;
        sy.sweepCents = sweepCents;
    }
    /// The last added syllable's bird, type, model and scores.
    void setSyllableVariation(int bird, int type, int model, const std::array<double, kMaxModes>& scores) {
        auto& sy = bank.syllables_.back();
        sy.bird = bird;
        sy.type = type;
        sy.model = model;
        sy.scores = scores;
    }
    void beginPhrase(double startS) {
        BankPhrase p;
        p.first = bank.numSyllables();
        p.startS = startS;
        bank.phrases_.push_back(p);
    }
    /// Append a syllable to the current phrase. `hz` and `amp` have one value per ms. `tap` = false: it never
    /// starts a key press (bank flags "no_note" / "no_tap", D-032).
    void addSyllable(int tMs, const std::vector<double>& hz, const std::vector<float>& amp,
                     const std::array<double, kNoteRuleCount>& anchors, int spanBegin, int spanEnd,
                     std::string label, bool tap = true, bool hop = false) {
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
        s.hop = hop;
        {
            std::string lower = label;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            s.swipe = lower.find("swipe") != std::string::npos;
        }
        float peak = 0.0f;
        for (std::size_t i = 0; i < hz.size(); ++i) {
            bank.logHz_.push_back(std::log(hz[i]));
            bank.amp_.push_back(amp[i]);
            peak = std::max(peak, amp[i]);
        }
        s.peakDb = 20.0 * std::log10(std::max(static_cast<double>(peak), 1e-6));
        s.sweepCents = hz.size() >= 2 ? 1200.0 * std::log2(hz.back() / hz.front()) : 0.0;
        bank.maxLengthMs_ = std::max(bank.maxLengthMs_, s.lengthMs);
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
        const bool startsKeys = tap && !release;
        bank.tapFlag_.push_back(startsKeys ? 1 : 0);
        if (startsKeys) {
            bank.tap_.push_back(index);
        }
        bank.syllables_.push_back(s);
        bank.labels_.push_back(std::move(label));
        ++p.count;
    }
};

/// WP-R3 (D-036): several birds' banks as one, built when the plugin starts; the files stay separate.
/// Phrases and syllables are appended in order (bank 0 first); each syllable remembers its bird; type
/// models are appended (indices shifted). Centres are the median over all syllables, as the bank builder
/// computes them. The merged bank has a shape model only if every source bank has one.
/// Offline only (allocates).
[[nodiscard]] inline SyllableBank mergeBanks(const std::vector<const SyllableBank*>& banks) {
    SyllableBankBuilder b;
    std::string name, hash;
    std::vector<std::string> birds;
    std::vector<VariationSpread> spreads;
    bool variation = !banks.empty();
    int points = 0;
    for (const auto* src : banks) {
        name += (name.empty() ? "" : "+") + src->name();
        hash += (hash.empty() ? "" : "+") + src->hash();
        variation = variation && src->hasVariation();
        points = std::max(points, src->modelPoints());
    }
    b.setInfo(name, banks.empty() ? std::string() : banks.front()->species(), hash);
    std::vector<std::array<double, kNoteRuleCount>> anchors;
    for (std::size_t bi = 0; bi < banks.size(); ++bi) {
        const auto& src = *banks[bi];
        const int modelBase = b.bank.numModels();
        for (int m = 0; m < src.numModels(); ++m) {
            b.addModel(src.model(m));
        }
        const int birdBase = static_cast<int>(birds.size());
        for (std::size_t k = 0; k < src.birds().size(); ++k) {
            birds.push_back(src.birds()[k]);
            spreads.push_back(src.hasVariation() ? src.spread(static_cast<int>(k)) : VariationSpread{});
        }
        for (int p = 0; p < src.numPhrases(); ++p) {
            const auto& ph = src.phrase(p);
            b.beginPhrase(ph.startS);
            for (int k = 0; k < ph.count; ++k) {
                const int i = ph.first + k;
                const auto& sy = src.syllable(i);
                std::vector<double> hz(static_cast<std::size_t>(sy.lengthMs));
                std::vector<float> amp(static_cast<std::size_t>(sy.lengthMs));
                for (int t = 0; t < sy.lengthMs; ++t) {
                    hz[static_cast<std::size_t>(t)] = std::exp(src.logHz()[sy.offset + t]);
                    amp[static_cast<std::size_t>(t)] = src.amp()[sy.offset + t];
                }
                b.addSyllable(sy.tMs, hz, amp, sy.anchorCents, sy.ruleSpanBegin, sy.ruleSpanEnd, src.label(i),
                              src.isTapSyllable(i) || src.isReleaseSyllable(i), sy.hop);
                b.setSyllableVariation(birdBase + sy.bird, sy.type, sy.model >= 0 ? modelBase + sy.model : -1,
                                       sy.scores);
                b.setLastExact(src.logHz() + sy.offset, sy.peakDb, sy.sweepCents); // no exp/log round trip
                anchors.push_back(sy.anchorCents);
            }
        }
    }
    std::array<double, kNoteRuleCount> centres{};
    for (int r = 0; r < kNoteRuleCount; ++r) {
        std::vector<double> v;
        v.reserve(anchors.size());
        for (const auto& a : anchors) {
            v.push_back(a[static_cast<std::size_t>(r)]);
        }
        std::sort(v.begin(), v.end());
        const std::size_t m = v.size();
        centres[static_cast<std::size_t>(r)] = m == 0 ? 0.0 : (m % 2 == 1 ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]));
    }
    b.setCentres(centres);
    b.setBirds(birds);
    if (variation) {
        b.setVariation(points, spreads);
    }
    return std::move(b.bank);
}

} // namespace cuicui::control
