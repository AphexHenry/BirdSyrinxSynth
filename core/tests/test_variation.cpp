// WP-R3: birds merged at load time, the shape model (control/variation.hpp), queries for R6.
#include "cuicui/control/syllable_engine.hpp"
#include "cuicui/control/variation.hpp"
#include "cuicui/io/syllable_bank_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace cuicui::control;

namespace {

SyllableBank load(const char* name) {
    return cuicui::io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/" + name + "/bank.json");
}

} // namespace

TEST_CASE("variation: two birds merge into one bank, each syllable keeping its bird and model") {
    const auto a = load("hf_22938");
    const auto b = load("hf_22937");
    REQUIRE(a.hasVariation());
    REQUIRE(b.hasVariation());
    const auto m = mergeBanks({&a, &b});
    CHECK(m.numSyllables() == a.numSyllables() + b.numSyllables());
    CHECK(m.numPhrases() == a.numPhrases() + b.numPhrases());
    CHECK(m.numModels() == a.numModels() + b.numModels());
    CHECK(m.birds() == std::vector<std::string>{"hf_22938", "hf_22937"});
    CHECK(m.hasVariation());
    CHECK(m.releaseSyllables().size() == a.releaseSyllables().size() + b.releaseSyllables().size());
    for (int i = 0; i < b.numSyllables(); i += 17) { // the second bird's syllables, unchanged
        const int j = a.numSyllables() + i;
        const auto& s = b.syllable(i);
        const auto& t = m.syllable(j);
        CHECK(t.bird == 1);
        CHECK(t.lengthMs == s.lengthMs);
        CHECK(t.phrase == a.numPhrases() + s.phrase);
        CHECK(m.label(j) == b.label(i));
        CHECK(t.model == (s.model >= 0 ? a.numModels() + s.model : -1));
        CHECK(m.logHz()[t.offset + t.lengthMs / 2] == b.logHz()[s.offset + s.lengthMs / 2]);
        CHECK(t.anchorCents == s.anchorCents);
    }
    const double c = m.centreCents(NoteRule::Rule); // the median over both birds
    CHECK(c >= std::min(a.centreCents(NoteRule::Rule), b.centreCents(NoteRule::Rule)) - 1e-9);
    CHECK(c <= std::max(a.centreCents(NoteRule::Rule), b.centreCents(NoteRule::Rule)) + 1e-9);
}

TEST_CASE("variation: a bank without a shape model (zf_22938) ignores the shape amount") {
    const auto zf = load("zf_22938");
    CHECK_FALSE(zf.hasVariation());
    std::vector<double> lg(4096);
    std::vector<float> am(4096);
    const auto z = variantDraws(42);
    const auto v = makeVariant(zf, 10, 1.0, z, 1.0, 1.0, lg.data(), am.data(), 4096);
    CHECK(v.lengthMs == zf.syllable(10).lengthMs);
    CHECK(v.noteShiftCents == 0.0);
    for (int i = 0; i < v.lengthMs; ++i) {
        CHECK(lg[static_cast<std::size_t>(i)] == zf.logHz()[zf.syllable(10).offset + i]);
    }
}

TEST_CASE("variation: v = 0 is the real syllable; v = 1 stays within the bird's range; capacity is respected") {
    const auto bank = load("hf_22938");
    std::vector<double> lg(4096);
    std::vector<float> am(4096);
    int maxLenChange = 0;
    double maxNote = 0.0;
    for (int i = 0; i < bank.numSyllables(); ++i) {
        const auto& s = bank.syllable(i);
        const auto z0 = variantDraws(static_cast<std::uint64_t>(i) + 1);
        const auto r = makeVariant(bank, i, 0.0, z0, 1.0, 1.0, lg.data(), am.data(), 4096);
        REQUIRE(r.lengthMs == s.lengthMs);
        CHECK(lg[static_cast<std::size_t>(s.lengthMs / 3)] == bank.logHz()[s.offset + s.lengthMs / 3]);
        const auto v = makeVariant(bank, i, 1.0, z0, 1.0, 1.0, lg.data(), am.data(), 4096);
        maxLenChange = std::max(maxLenChange, std::abs(v.lengthMs - s.lengthMs));
        maxNote = std::max(maxNote, std::abs(v.noteShiftCents));
        for (int k = 0; k < v.lengthMs; ++k) {
            REQUIRE(std::isfinite(lg[static_cast<std::size_t>(k)]));
            REQUIRE(am[static_cast<std::size_t>(k)] >= 0.0f);
        }
    }
    INFO("largest length change " << maxLenChange << " ms, largest note change " << maxNote << " c");
    CHECK(maxNote < 200.0);       // a few sd of ±26 c (the type's own spread for the far renditions)
    CHECK(maxLenChange < 60);     // the long calls move the most (±2.5 % typical)
    // a long stretch is clamped to the buffer
    const auto st = makeVariant(bank, 0, 1.0, variantDraws(1), 50.0, 1.0, lg.data(), am.data(), 300);
    CHECK(st.lengthMs == 300);
}

TEST_CASE("variation: queries for playing modes (R6) find syllables by label, length, sweep and bird") {
    const auto a = load("hf_22938");
    const auto b = load("hf_22937");
    const auto m = mergeBanks({&a, &b});
    SyllableBank::Query q;
    const auto all = m.find(q);
    CHECK(all.size() == m.playableSyllables().size());
    q.labelPrefix = "peep";
    q.maxMs = 80;
    for (int i : m.find(q)) {
        CHECK(m.label(i).rfind("peep", 0) == 0);
        CHECK(m.syllable(i).lengthMs <= 80);
    }
    SyllableBank::Query up;
    up.minSweepCents = 300.0;
    up.bird = 1;
    const auto ups = m.find(up);
    CHECK_FALSE(ups.empty());
    for (int i : ups) {
        CHECK(m.syllable(i).sweepCents >= 300.0);
        CHECK(m.syllable(i).bird == 1);
    }
    SyllableBank::Query calls;
    calls.labelPrefix = "alarm";
    calls.playableOnly = false;
    CHECK(m.find(calls).size() == m.releaseSyllables().size());
}

TEST_CASE("variation: length and pitch-range draws stay in the spans the human chose (STRETCH1)") {
    const StretchSpans sp;
    CHECK(drawStretch(123, 0.0, false, false).length == 1.0); // Variation 0: the real syllable
    CHECK(drawStretch(123, 0.0, false, false).range == 1.0);
    std::vector<double> len, lenSwipe, rng;
    double maxHopRange = 0.0;
    for (std::uint64_t seed = 1; seed <= 4000; ++seed) {
        const auto a = drawStretch(seed, 1.0, false, false);
        const auto b = drawStretch(seed, 1.0, true, false);
        const auto h = drawStretch(seed, 1.0, false, true);
        REQUIRE(a.length >= sp.lengthLo - 1e-12);
        REQUIRE(a.length <= sp.lengthHi + 1e-12);
        REQUIRE(b.length <= sp.lengthHiSwipe + 1e-12);
        REQUIRE(a.range >= sp.rangeLo - 1e-12);
        REQUIRE(a.range <= sp.rangeHi + 1e-12);
        maxHopRange = std::max(maxHopRange, h.range);
        len.push_back(a.length);
        lenSwipe.push_back(b.length);
        rng.push_back(a.range);
        const auto half = drawStretch(seed, 0.5, false, false); // half the Variation: half the log span
        CHECK(std::abs(std::log(half.length) - 0.5 * std::log(a.length)) < 1e-12);
    }
    CHECK(maxHopRange <= 1.0); // tracker jumps are never widened
    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    // log-uniform: the median is the geometric mean of the span (shorter and longer equally likely)
    CHECK(std::abs(median(len) - std::sqrt(0.8 * 1.6)) < 0.02);
    CHECK(std::abs(median(lenSwipe) - std::sqrt(0.8 * 2.0)) < 0.03);
    CHECK(std::abs(median(rng) - std::sqrt(0.6 * 1.6)) < 0.02);
    CHECK(*std::max_element(lenSwipe.begin(), lenSwipe.end()) > 1.9);
}

TEST_CASE("variation: the pitch range scales the movement around the note, which stays put") {
    const auto bank = load("hf_22938");
    std::vector<double> lg(4096);
    std::vector<float> am(4096);
    const int syl = 40;
    const auto& s = bank.syllable(syl);
    const double note = s.anchorCents[0];
    const double ln2 = std::log(2.0);
    for (double k : {0.6, 1.6}) {
        const auto v = makeVariant(bank, syl, 0.0, VariantDraws{}, 1.0, k, lg.data(), am.data(), 4096);
        REQUIRE(v.lengthMs == s.lengthMs);
        for (int i = 0; i < v.lengthMs; i += 7) {
            const double c0 = 1200.0 * (bank.logHz()[s.offset + i] - std::log(440.0)) / ln2 + 6900.0;
            const double c1 = 1200.0 * (lg[static_cast<std::size_t>(i)] - std::log(440.0)) / ln2 + 6900.0;
            CHECK(std::abs((c1 - note) - k * (c0 - note)) < 1e-6);
        }
    }
}

TEST_CASE("variation: a stretched syllable moves the next one later, keeping the bird's real gaps") {
    const auto a = load("hf_22938");
    const auto b = load("hf_22937");
    const auto bank = mergeBanks({&a, &b});
    for (double v : {0.0, 1.0}) {
        SyllableEngine eng;
        eng.prepare(48000.0);
        eng.setBank(&bank);
        eng.reset(5);
        std::vector<SyllableLogEntry> log(512);
        eng.setLog(log.data(), static_cast<int>(log.size()));
        WhistleSettings s;
        s.play.variation = v;
        s.play.order = OrderMode::PhraseStart;
        s.play.releaseAlarm = false;
        std::vector<float> l(256), r(256);
        PlayEvent on{PlayEvent::Type::NoteOn, 0, 64, 0.8f};
        for (int blk = 0; blk < 10 * 48000 / 256; ++blk) {
            eng.process(&on, blk == 0 ? 1 : 0, 256, s, l.data(), r.data());
        }
        const int n = eng.logCount();
        REQUIRE(n > 10);
        int checked = 0;
        for (int i = 1; i < n; ++i) {
            const auto& p = log[static_cast<std::size_t>(i - 1)];
            const auto& c = log[static_cast<std::size_t>(i)];
            const auto& ps = bank.syllable(p.request.syllable);
            const auto& cs = bank.syllable(c.request.syllable);
            if (cs.phrase != ps.phrase) {
                continue;
            }
            const int lenMs = stretchedLengthMs(ps.lengthMs, p.request.lengthScale);
            const double gapMs = static_cast<double>(c.sample - p.sample) / 48.0 - lenMs;
            const int realGap = cs.tMs - (ps.tMs + ps.lengthMs);
            CHECK(std::abs(gapMs - realGap) <= 1.0);
            if (v == 0.0) {
                CHECK(p.request.lengthScale == 1.0);
                CHECK(p.request.rangeScale == 1.0);
            }
            ++checked;
        }
        CHECK(checked > 8);
    }
}
