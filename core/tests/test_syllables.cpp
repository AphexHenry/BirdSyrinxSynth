// WP-R1: syllable bank, placement, phrase order, modes A + B.
#include "cuicui/control/mode_ab.hpp"
#include "cuicui/control/phrase_order.hpp"
#include "cuicui/control/placement.hpp"
#include "cuicui/control/syllable_bank.hpp"
#include "cuicui/io/syllable_bank_io.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace cuicui::control;
using Catch::Matchers::WithinAbs;

namespace {

const SyllableBank& realBank() {
    static const SyllableBank bank =
        cuicui::io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/zf_22938/bank.json");
    return bank;
}

struct Run {
    std::vector<SyllableRequest> reqs;
    std::vector<long long> samples; // absolute start sample of each request
};

/// Drive a ModeAB with timed events (absolute samples) in blocks of `block` samples.
Run drive(ModeAB& mode, const std::vector<std::pair<long long, PlayEvent>>& events, long long total, int block,
          const PlayContext& ctx) {
    Run run;
    std::size_t ei = 0;
    std::vector<PlayEvent> evs;
    std::vector<SyllableRequest> out(256);
    for (long long pos = 0; pos < total; pos += block) {
        const int n = static_cast<int>(std::min<long long>(block, total - pos));
        evs.clear();
        while (ei < events.size() && events[ei].first < pos + n) {
            PlayEvent e = events[ei].second;
            e.offset = static_cast<int>(events[ei].first - pos);
            evs.push_back(e);
            ++ei;
        }
        const int k = mode.process(evs.data(), static_cast<int>(evs.size()), n, ctx, out.data(), 256);
        for (int i = 0; i < k; ++i) {
            run.reqs.push_back(out[static_cast<std::size_t>(i)]);
            run.samples.push_back(pos + out[static_cast<std::size_t>(i)].offset);
        }
    }
    return run;
}

PlayEvent noteOn(int note, float vel = 0.8f) {
    PlayEvent e;
    e.type = PlayEvent::Type::NoteOn;
    e.note = note;
    e.value = vel;
    return e;
}

PlayEvent noteOff(int note) {
    PlayEvent e;
    e.type = PlayEvent::Type::NoteOff;
    e.note = note;
    return e;
}

constexpr double kSr = 48000.0;

} // namespace

TEST_CASE("syllable bank: the committed zf_22938 bank loads and matches the prototype's") {
    const auto& b = realBank();
    REQUIRE(b.numPhrases() == 12);
    REQUIRE(b.numSyllables() == 123);
    REQUIRE(b.totalContourMs() == 13515);
    CHECK_THAT(b.centreCents(NoteRule::Rule), WithinAbs(10502.9, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Steady), WithinAbs(10470.5, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Landing), WithinAbs(10428.1, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Average), WithinAbs(10411.4, 1e-9));
    for (int i = 0; i < b.numSyllables(); ++i) {
        const auto& s = b.syllable(i);
        REQUIRE(s.lengthMs >= 2);
        REQUIRE(s.ruleSpanBegin < s.ruleSpanEnd);
        REQUIRE(s.ruleSpanEnd <= s.lengthMs);
        REQUIRE(b.syllableOf(s.phrase, s.index) == i);
    }
    CHECK(b.name() == "zf_22938");
    CHECK(b.label(0) == "swipe");
}

TEST_CASE("syllable bank: invalid documents are rejected with a clear message") {
    using cuicui::io::parseSyllableBank;
    using cuicui::io::SyllableBankError;
    CHECK_THROWS_AS(parseSyllableBank("not json"), SyllableBankError);
    CHECK_THROWS_AS(parseSyllableBank(R"({"format":"cuicui.gesture_score","version":1})"), SyllableBankError);
    const std::string head =
        R"({"format":"cuicui.syllable_bank","version":1,"contour_rate_hz":1000,)"
        R"("centres_cents":{"rule":1,"steady":1,"landing":1,"average":1},"phrases":[{"id":0,"syllables":[)";
    const std::string anchors = R"("anchors":{"rule":1,"steady":1,"landing":1,"average":1})";
    CHECK_NOTHROW(parseSyllableBank(head + R"({"t_ms":0,"f_hz":[1000,1000],"a":[0,1],)" + anchors + "}]}]}"));
    CHECK_THROWS_AS(parseSyllableBank(head + R"({"t_ms":0,"f_hz":[1000,1000],"a":[0],)" + anchors + "}]}]}"),
                    SyllableBankError);
    CHECK_THROWS_AS(parseSyllableBank(head + R"({"t_ms":0,"f_hz":[1000,-5],"a":[0,1],)" + anchors + "}]}]}"),
                    SyllableBankError);
    CHECK_THROWS_AS(parseSyllableBank(head + R"({"t_ms":0,"f_hz":[1000,1000,1000],"a":[0,1,0],)" + anchors +
                                      R"(},{"t_ms":2,"f_hz":[1000,1000],"a":[0,1],)" + anchors + "}]}]}"),
                    SyllableBankError); // overlaps the previous syllable
}

TEST_CASE("placement: own height picks the octave nearest the bird, follow is the key + 3 octaves") {
    const double centre = 10502.9; // ≈ A7 (3526 Hz)
    // A (pc 9): 10500 is nearest.
    CHECK(placementTarget(69, centre, KeyMapping::OwnHeight, 0.0) == 10500.0);
    CHECK(placementTarget(57, centre, KeyMapping::OwnHeight, 0.0) == 10500.0); // octave played does not matter
    // D# (pc 3): candidates 9900 (−603) and 11100 (+597) → 11100.
    CHECK(placementTarget(63, centre, KeyMapping::OwnHeight, 0.0) == 11100.0);
    // E (pc 4): 10000 (−503) vs 11200 (+697) → 10000.
    CHECK(placementTarget(64, centre, KeyMapping::OwnHeight, 0.0) == 10000.0);
    // Size moves the bird: +12 semitones → one octave up.
    CHECK(placementTarget(69, centre, KeyMapping::OwnHeight, 12.0) == 11700.0);
    // An exact tie keeps the lower octave (prototype rule): F# with the bird on C (9600 c) → 9000 or 10200.
    CHECK(placementTarget(66, 9600.0, KeyMapping::OwnHeight, 0.0) == 9000.0);
    // Follow: note + 36 (+ size) semitones.
    CHECK(placementTarget(60, centre, KeyMapping::Follow, 0.0) == 9600.0);
    CHECK(placementTarget(72, centre, KeyMapping::Follow, -2.0) == 10600.0);
}

TEST_CASE("placement: snap 1 puts the anchor on the target, snap 0 keeps the bird's melody") {
    const double target = 10500.0, centre = 10502.9;
    for (double a : {9800.0, 10300.0, 10502.9, 11000.0}) {
        CHECK_THAT(a + placementShift(a, target, centre, 1.0), WithinAbs(target, 1e-9));
        CHECK_THAT(placementShift(a, target, centre, 0.0), WithinAbs(target - centre, 1e-9));
        // halfway: half of the anchor's distance to the bird's centre remains
        CHECK_THAT(a + placementShift(a, target, centre, 0.5), WithinAbs(target + 0.5 * (a - centre), 1e-9));
    }
    CHECK_THAT(velocityGain(1.0), WithinAbs(0.40, 1e-12));
    CHECK_THAT(velocityGain(0.0), WithinAbs(0.12, 1e-12));
}

TEST_CASE("phrase order: real phrases follow the bank order with real onset gaps, then pause") {
    const auto& b = realBank();
    cuicui::dsp::Rng rng(7);
    int pointer = 0;
    PhraseOrder o;
    auto st = o.begin(b, OrderMode::PhraseStart, rng, pointer);
    REQUIRE(st.syllable == 0);
    REQUIRE(pointer == 1);
    double t = 0.0;
    for (int i = 1; i < b.phrase(0).count; ++i) {
        const auto nx = o.next(b, t, 0.5, rng, pointer);
        REQUIRE(nx.syllable == i);
        CHECK_THAT(nx.startS - t, WithinAbs((b.syllable(i).tMs - b.syllable(i - 1).tMs) / 1000.0, 1e-12));
        t = nx.startS;
    }
    const int last = b.phrase(0).count - 1;
    const auto nx = o.next(b, t, 0.5, rng, pointer);
    CHECK(nx.syllable == b.phrase(1).first); // phrase start mode: the next phrase in order
    CHECK_THAT(nx.startS, WithinAbs(t + b.syllable(last).lengthMs / 1000.0 + 0.5, 1e-12));
}

TEST_CASE("phrase order: shuffled gaps are 20-70 ms with a phrase pause every 4-9 syllables") {
    const auto& b = realBank();
    cuicui::dsp::Rng rng(11);
    int pointer = 0;
    PhraseOrder o;
    auto st = o.begin(b, OrderMode::Shuffled, rng, pointer);
    double t = st.startS;
    int prev = st.syllable;
    int sincePause = 0;
    int pauses = 0;
    for (int i = 0; i < 400; ++i) {
        const auto nx = o.next(b, t, 1.25, rng, pointer);
        const double gap = nx.startS - (t + b.syllable(prev).lengthMs / 1000.0);
        ++sincePause;
        if (std::abs(gap - 1.25) < 1e-9) {
            CHECK(sincePause >= 4);
            CHECK(sincePause <= 9);
            sincePause = 0;
            ++pauses;
        } else {
            CHECK(gap >= 0.02);
            CHECK(gap < 0.07);
        }
        t = nx.startS;
        prev = nx.syllable;
    }
    CHECK(pauses >= 40);
}

TEST_CASE("mode A: a 30 ms tap sings exactly one syllable, starting on the note-on sample") {
    for (int block : {32, 256, 1000}) {
        ModeAB m;
        m.prepare(kSr);
        m.reset(1234);
        m.setBank(&realBank());
        PlayContext ctx;
        const long long on = 4800 + 17;
        const auto run = drive(m, {{on, noteOn(69)}, {on + 1440, noteOff(69)}}, 96000, block, ctx);
        REQUIRE(run.reqs.size() == 1);
        CHECK(run.samples[0] == on);
        CHECK(run.reqs[0].first);
        // first syllable on the note: its rule anchor + shift = the target
        const auto& r = run.reqs[0];
        const double target = placementTarget(69, realBank().centreCents(NoteRule::Rule), KeyMapping::OwnHeight, 0.0);
        CHECK_THAT(realBank().anchorCents(r.syllable, NoteRule::Rule) + r.shiftCents, WithinAbs(target, 1e-9));
    }
}

TEST_CASE("mode B: holding 2 s follows the real phrase from the start syllable with real onset gaps") {
    const auto& b = realBank();
    ModeAB m;
    m.prepare(kSr);
    m.reset(42);
    m.setBank(&b);
    PlayContext ctx;
    ctx.pauseS = 0.5;
    const long long on = 1000;
    const auto run = drive(m, {{on, noteOn(64)}, {on + 96000, noteOff(64)}}, 200000, 256, ctx);
    REQUIRE(run.reqs.size() >= 2);
    // replay the order with the singer's stream to know where the phrase pause falls
    for (std::size_t i = 1; i < run.reqs.size(); ++i) {
        const auto& prev = b.syllable(run.reqs[i - 1].syllable);
        const auto& cur = b.syllable(run.reqs[i].syllable);
        const long long gap = run.samples[i] - run.samples[i - 1];
        if (cur.phrase == prev.phrase && cur.index == prev.index + 1) {
            const double expect = (cur.tMs - prev.tMs) / 1000.0 * kSr;
            CHECK(std::abs(static_cast<double>(gap) - expect) <= 48.0); // ±1 ms
        } else {
            REQUIRE(prev.index == b.phrase(prev.phrase).count - 1); // only at the end of a phrase
            REQUIRE(cur.index == 0);
            const double expect = (prev.lengthMs / 1000.0 + 0.5) * kSr;
            CHECK(std::abs(static_cast<double>(gap) - expect) <= 48.0);
        }
        CHECK(run.samples[i] < on + 96000); // nothing starts after the release
        CHECK_FALSE(run.reqs[i].first);
    }
}

TEST_CASE("mode B: release mid-syllable stops the phrase; release in a phrase pause is silent") {
    const auto& b = realBank();
    PlayContext ctx;
    ctx.order = OrderMode::PhraseStart; // phrase 0: 3 syllables, then a 0.5 s pause
    ctx.pauseS = 0.5;
    const long long on = 480;
    // Phrase 0 timing from the bank.
    const auto& s1 = b.syllable(1);
    const auto& s2 = b.syllable(2);
    {
        ModeAB m;
        m.prepare(kSr);
        m.reset(5);
        m.setBank(&b);
        // release halfway through the second syllable
        const long long rel = on + static_cast<long long>((s1.tMs + s1.lengthMs / 2) * 48);
        const auto run = drive(m, {{on, noteOn(70)}, {rel, noteOff(70)}}, 96000, 128, ctx);
        REQUIRE(run.reqs.size() == 2);
        CHECK(run.reqs[1].syllable == 1);
        CHECK(run.samples[1] == on + s1.tMs * 48);
    }
    {
        ModeAB m;
        m.prepare(kSr);
        m.reset(5);
        m.setBank(&b);
        // release in the pause after phrase 0
        const long long endPhrase = on + static_cast<long long>((s2.tMs + s2.lengthMs) * 48);
        const auto run = drive(m, {{on, noteOn(70)}, {endPhrase + 4800, noteOff(70)}}, 200000, 128, ctx);
        REQUIRE(run.reqs.size() == 3);
    }
}

TEST_CASE("mode A+B: results do not depend on the host block size") {
    const auto& b = realBank();
    PlayContext ctx;
    ctx.polyphony = 4;
    std::vector<std::pair<long long, PlayEvent>> ev;
    cuicui::dsp::Rng r(99);
    long long t = 100;
    for (int i = 0; i < 40; ++i) {
        const int note = 55 + static_cast<int>(r.uniform() * 24.0f);
        ev.push_back({t, noteOn(note, 0.3f + 0.7f * r.uniform())});
        ev.push_back({t + 200 + static_cast<long long>(r.uniform() * 60000.0f), noteOff(note)});
        t += 500 + static_cast<long long>(r.uniform() * 20000.0f);
    }
    std::stable_sort(ev.begin(), ev.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    std::vector<Run> runs;
    for (int block : {1, 64, 511, 4096}) {
        ModeAB m;
        m.prepare(kSr);
        m.reset(77);
        m.setBank(&b);
        runs.push_back(drive(m, ev, t + 96000, block, ctx));
    }
    REQUIRE(runs[0].reqs.size() > 40);
    for (std::size_t k = 1; k < runs.size(); ++k) {
        REQUIRE(runs[k].reqs.size() == runs[0].reqs.size());
        for (std::size_t i = 0; i < runs[0].reqs.size(); ++i) {
            CHECK(runs[k].samples[i] == runs[0].samples[i]);
            CHECK(runs[k].reqs[i].syllable == runs[0].reqs[i].syllable);
            CHECK(runs[k].reqs[i].shiftCents == runs[0].reqs[i].shiftCents);
        }
    }
}

TEST_CASE("mode A+B: polyphony releases the oldest key; the same key restarts; sustain holds") {
    const auto& b = realBank();
    PlayContext ctx;
    ctx.polyphony = 2;
    ModeAB m;
    m.prepare(kSr);
    m.reset(3);
    m.setBank(&b);
    std::vector<PlayEvent> ev{noteOn(60), noteOn(62), noteOn(64)};
    ev[1].offset = 10;
    ev[2].offset = 20;
    std::vector<SyllableRequest> out(64);
    CHECK(m.process(ev.data(), 3, 64, ctx, out.data(), 64) == 3);
    CHECK(m.activeSingers() == 2); // 60 was released
    PlayEvent again = noteOn(64);
    CHECK(m.process(&again, 1, 64, ctx, out.data(), 64) == 1);
    CHECK(m.activeSingers() == 2); // the old 64 released, a new 64 sings

    // sustain: key up with the pedal down keeps singing until the pedal goes up
    PlayEvent ped;
    ped.type = PlayEvent::Type::Sustain;
    ped.value = 1.0f;
    PlayEvent off = noteOff(64);
    std::vector<PlayEvent> ev2{ped, off};
    m.process(ev2.data(), 2, 64, ctx, out.data(), 64);
    CHECK(m.activeSingers() == 2);
    ped.value = 0.0f;
    m.process(&ped, 1, 64, ctx, out.data(), 64);
    CHECK(m.activeSingers() == 1);
    PlayEvent all;
    all.type = PlayEvent::Type::AllNotesOff;
    m.process(&all, 1, 64, ctx, out.data(), 64);
    CHECK(m.activeSingers() == 0);
}

TEST_CASE("mode A+B: the mod wheel adds to snap for the following syllables") {
    const auto& b = realBank();
    PlayContext ctx;
    ctx.snap = 0.0;
    ModeAB m;
    m.prepare(kSr);
    m.reset(8);
    m.setBank(&b);
    PlayEvent wheel;
    wheel.type = PlayEvent::Type::ModWheel;
    wheel.value = 1.0f;
    std::vector<PlayEvent> ev{wheel, noteOn(69)};
    std::vector<SyllableRequest> out(64);
    m.process(ev.data(), 2, 64, ctx, out.data(), 64);
    // run 3 s: every following syllable lands on the note (snap 0 + wheel 1 = 1)
    const double target = placementTarget(69, b.centreCents(NoteRule::Rule), KeyMapping::OwnHeight, 0.0);
    int checked = 0;
    for (int blk = 0; blk < 3 * 48000 / 512; ++blk) {
        const int k = m.process(nullptr, 0, 512, ctx, out.data(), 64);
        for (int i = 0; i < k; ++i) {
            const auto& r = out[static_cast<std::size_t>(i)];
            CHECK_THAT(b.anchorCents(r.syllable, NoteRule::Rule) + r.shiftCents, WithinAbs(target, 1e-9));
            ++checked;
        }
    }
    CHECK(checked > 3);
}
