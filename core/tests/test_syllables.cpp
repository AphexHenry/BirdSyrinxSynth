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
#include <cstdint>
#include <fstream>
#include <sstream>
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
    // WP-R5 bank (rule E + 11 notes set by ear, D-031/D-032); R1's were 10502.9 / 10470.5 / 10428.1 / 10411.4
    CHECK_THAT(b.centreCents(NoteRule::Rule), WithinAbs(10545.7, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Steady), WithinAbs(10531.2, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Landing), WithinAbs(10471.2, 1e-9));
    CHECK_THAT(b.centreCents(NoteRule::Average), WithinAbs(10424.5, 1e-9));
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
    ctx.releaseAlarm = false; // the R1 rule: every syllable of a phrase (D-030 is tested below)
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
    ctx.releaseAlarm = false;           // the R1 rule (with D-030 a long press ends with the call, below)
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


// ------------------------------------------------------------------ D-030: the release alarm
namespace {

bool isCall(const SyllableBank& b, int syl) {
    return b.label(syl).rfind("alarm", 0) == 0;
}

PlayEvent pedal(bool down) {
    PlayEvent e;
    e.type = PlayEvent::Type::Sustain;
    e.value = down ? 1.0f : 0.0f;
    return e;
}

long long endOf(const SyllableBank& b, const Run& run, std::size_t i) {
    return run.samples[i] + static_cast<long long>(std::llround(b.syllable(run.reqs[i].syllable).lengthMs * 48.0));
}

} // namespace

TEST_CASE("release alarm: the bank knows its calls and the bird's lead-in silence before them") {
    const auto& b = realBank();
    REQUIRE(b.releaseSyllables().size() == 5); // 4 "alarm" + 1 "alarm_grillon" in 22938
    for (int s : b.releaseSyllables()) {
        CHECK(isCall(b, s));
        CHECK(b.isReleaseSyllable(s));
    }
    CHECK(static_cast<int>(b.playableSyllables().size() + b.releaseSyllables().size()) == b.numSyllables());
    CHECK(b.releaseGapMs() >= 47.0); // the real gaps before the calls are 47-67 ms
    CHECK(b.releaseGapMs() <= 67.0);
}

TEST_CASE("release alarm: never in taps, phrases or shuffles; only after a long press, once") {
    const auto& b = realBank();
    for (auto order : {OrderMode::RandomStart, OrderMode::PhraseStart, OrderMode::Shuffled}) {
        PlayContext ctx;
        ctx.order = order;
        ModeAB m;
        m.prepare(kSr);
        m.reset(1000 + static_cast<int>(order));
        m.setBank(&b);
        std::vector<std::pair<long long, PlayEvent>> ev;
        long long t = 480;
        int longPresses = 0;
        cuicui::dsp::Rng r(31);
        for (int k = 0; k < 60; ++k) { // taps of 30-590 ms, and holds of 0.6-6 s
            const int note = 57 + (k * 7) % 24;
            const bool hold = k % 3 == 0;
            const long long len = hold ? 28800 + static_cast<long long>(r.uniform() * 259200.0f)
                                       : 1440 + static_cast<long long>(r.uniform() * 26800.0f);
            ev.push_back({t, noteOn(note)});
            ev.push_back({t + len, noteOff(note)});
            longPresses += hold ? 1 : 0;
            t += len + 24000;
        }
        const auto run = drive(m, ev, t + 96000, 256, ctx);
        int calls = 0;
        for (std::size_t i = 0; i < run.reqs.size(); ++i) {
            const auto& q = run.reqs[i];
            CHECK(isCall(b, q.syllable) == q.releaseAlarm); // a call only as the release alarm
            if (q.releaseAlarm) {
                ++calls;
                CHECK_FALSE(q.first);
            }
        }
        INFO("order " << static_cast<int>(order));
        CHECK(calls == longPresses); // one per long press, none for the taps
    }
}

TEST_CASE("release alarm: 0.6 s is the threshold; the call follows the sounding syllable and the lead-in") {
    const auto& b = realBank();
    const long long lead = std::llround(b.releaseGapMs() * 48.0);
    for (long long held : {28700LL, 28800LL, 48000LL, 96000LL}) { // 0.598 s, 0.6 s, 1 s, 2 s
        PlayContext ctx;
        ModeAB m;
        m.prepare(kSr);
        m.reset(77);
        m.setBank(&b);
        const long long on = 960;
        const long long off = on + held;
        const auto run = drive(m, {{on, noteOn(66)}, {off, noteOff(66)}}, off + 96000, 128, ctx);
        REQUIRE_FALSE(run.reqs.empty());
        const auto& last = run.reqs.back();
        INFO("held " << held / 48.0 << " ms");
        if (held < 28800) {
            CHECK_FALSE(last.releaseAlarm);
            continue;
        }
        REQUIRE(last.releaseAlarm);
        REQUIRE(run.reqs.size() >= 2);
        const std::size_t before = run.reqs.size() - 2; // the last syllable started before the release
        CHECK(run.samples[before] < off);
        CHECK(run.samples.back() == std::max(off, endOf(b, run, before) + lead));
        CHECK(last.note == 66);
        CHECK(last.gain == run.reqs[before].gain); // the key's loudness and place
        CHECK(last.pan == run.reqs[before].pan);
    }
}

TEST_CASE("release alarm: after a release in a phrase pause it starts at once") {
    const auto& b = realBank();
    PlayContext ctx;
    ctx.order = OrderMode::PhraseStart; // phrase 0 (3 syllables), then a 1 s pause
    ctx.pauseS = 1.0;
    const auto& s2 = b.syllable(2);
    ModeAB m;
    m.prepare(kSr);
    m.reset(5);
    m.setBank(&b);
    const long long on = 480;
    const long long rel = on + (s2.tMs + s2.lengthMs) * 48 + 24000; // 0.5 s into the pause
    REQUIRE(rel - on >= 28800);
    const auto run = drive(m, {{on, noteOn(70)}, {rel, noteOff(70)}}, rel + 96000, 64, ctx);
    REQUIRE(run.reqs.size() == 4);
    CHECK(run.reqs[3].releaseAlarm);
    CHECK(run.samples[3] == rel);
}

TEST_CASE("release alarm: with the pedal, the press lasts until the pedal is lifted") {
    const auto& b = realBank();
    PlayContext ctx;
    { // a 0.2 s tap held on by the pedal for 1.5 s: the bird sang long enough, it ends with the call
        ModeAB m;
        m.prepare(kSr);
        m.reset(9);
        m.setBank(&b);
        const auto run = drive(m, {{100, pedal(true)}, {480, noteOn(61)}, {480 + 9600, noteOff(61)}, {72480, pedal(false)}},
                               200000, 256, ctx);
        REQUIRE_FALSE(run.reqs.empty());
        CHECK(run.reqs.back().releaseAlarm);
        CHECK(run.samples.back() >= 72480);
    }
    { // tap and pedal both short (0.4 s in all): no call
        ModeAB m;
        m.prepare(kSr);
        m.reset(9);
        m.setBank(&b);
        const auto run = drive(m, {{100, pedal(true)}, {480, noteOn(61)}, {480 + 4800, noteOff(61)}, {19680, pedal(false)}},
                               200000, 256, ctx);
        for (const auto& q : run.reqs) {
            CHECK_FALSE(q.releaseAlarm);
        }
    }
}

TEST_CASE("release alarm: pressing the key again cancels it; the polyphony limit and all-notes-off never call") {
    const auto& b = realBank();
    PlayContext ctx;
    { // long press, released 10 ms into a syllable (the call waits for its end), the same key again: no call
        ModeAB m;
        m.prepare(kSr);
        m.reset(12);
        m.setBank(&b);
        std::vector<SyllableRequest> out(64);
        PlayEvent on = noteOn(64);
        m.process(&on, 1, 1, ctx, out.data(), 64);
        long long now = 1;
        long long startAfterHold = -1;
        while (startAfterHold < 0 && now < 10 * 48000) { // the first syllable to start after 0.7 s
            const int k = m.process(nullptr, 0, 1, ctx, out.data(), 64);
            if (k > 0 && now >= 33600) {
                startAfterHold = now;
            }
            ++now;
        }
        REQUIRE(startAfterHold > 0);
        for (int i = 0; i < 479; ++i) { // 10 ms into that syllable
            m.process(nullptr, 0, 1, ctx, out.data(), 64);
        }
        PlayEvent off = noteOff(64);
        m.process(&off, 1, 1, ctx, out.data(), 64);
        REQUIRE(m.pendingReleaseAlarms() == 1);
        PlayEvent again = noteOn(64);
        m.process(&again, 1, 1, ctx, out.data(), 64);
        CHECK(m.pendingReleaseAlarms() == 0);
        for (int k = 0; k < 48000 / 64; ++k) {
            const int n = m.process(nullptr, 0, 64, ctx, out.data(), 64);
            for (int i = 0; i < n; ++i) {
                CHECK_FALSE(out[static_cast<std::size_t>(i)].releaseAlarm);
            }
        }
    }
    { // polyphony 1: a second key takes over after 1 s; the first key's bird does not call
        ModeAB m;
        m.prepare(kSr);
        m.reset(13);
        m.setBank(&b);
        PlayContext mono = ctx;
        mono.polyphony = 1;
        const auto run = drive(m, {{480, noteOn(60)}, {48480, noteOn(67)}, {48480 + 2400, noteOff(67)}, {96000, noteOff(60)}},
                               200000, 256, mono);
        for (const auto& q : run.reqs) {
            CHECK_FALSE(q.releaseAlarm); // 67 was a short press; 60 was taken over before its release
        }
    }
    { // all-notes-off after a long press: no call
        ModeAB m;
        m.prepare(kSr);
        m.reset(14);
        m.setBank(&b);
        PlayEvent all;
        all.type = PlayEvent::Type::AllNotesOff;
        const auto run = drive(m, {{480, noteOn(62)}, {72480, all}}, 200000, 256, ctx);
        for (const auto& q : run.reqs) {
            CHECK_FALSE(q.releaseAlarm);
        }
    }
}

TEST_CASE("release alarm: a phrase closes up over a skipped call, keeping the silences around it") {
    const auto& b = realBank();
    // phrase 7: ... Lpeep (index 9), alarm (index 10), swipe (index 11) ...
    const int p7 = 7;
    REQUIRE(isCall(b, b.syllableOf(p7, 10)));
    PhraseOrder order;
    cuicui::dsp::Rng rng(1);
    int pointer = p7;
    OrderStep st = order.begin(b, OrderMode::PhraseStart, rng, pointer, true);
    REQUIRE(st.syllable == b.syllableOf(p7, 0));
    for (int i = 1; i <= 9; ++i) {
        st = order.next(b, st.startS, 0.5, rng, pointer);
        REQUIRE(st.syllable == b.syllableOf(p7, i));
    }
    const double at9 = st.startS;
    st = order.next(b, st.startS, 0.5, rng, pointer);
    CHECK(st.syllable == b.syllableOf(p7, 11));
    const auto& s9 = b.syllable(b.syllableOf(p7, 9));
    const auto& s10 = b.syllable(b.syllableOf(p7, 10));
    const auto& s11 = b.syllable(b.syllableOf(p7, 11));
    CHECK_THAT(st.startS - at9, WithinAbs((s11.tMs - s9.tMs - s10.lengthMs) / 1000.0, 1e-12));
    // ... and with reserve off, the R1 order sings the call in place
    PhraseOrder r1;
    int pointer1 = p7;
    OrderStep q = r1.begin(b, OrderMode::PhraseStart, rng, pointer1, false);
    for (int i = 1; i <= 10; ++i) {
        q = r1.next(b, q.startS, 0.5, rng, pointer1);
    }
    CHECK(q.syllable == b.syllableOf(p7, 10));
}

TEST_CASE("release alarm: results do not depend on the host block size") {
    const auto& b = realBank();
    PlayContext ctx;
    std::vector<std::pair<long long, PlayEvent>> ev{
        {480, noteOn(60)}, {9000, noteOn(64)}, {12000, noteOff(64)}, {60000, noteOff(60)},
        {70000, pedal(true)}, {71000, noteOn(67)}, {72000, noteOff(67)}, {130000, pedal(false)},
        {140000, noteOn(69)}, {200000, noteOff(69)}};
    std::vector<Run> runs;
    for (int block : {1, 64, 511, 4096}) {
        ModeAB m;
        m.prepare(kSr);
        m.reset(21);
        m.setBank(&b);
        runs.push_back(drive(m, ev, 320000, block, ctx));
    }
    int calls = 0;
    for (const auto& q : runs[0].reqs) {
        calls += q.releaseAlarm ? 1 : 0;
    }
    CHECK(calls == 3); // 60 (1.2 s), 67 (pedal, 1.2 s), 69 (1.25 s); not 64 (62 ms)
    for (std::size_t k = 1; k < runs.size(); ++k) {
        REQUIRE(runs[k].reqs.size() == runs[0].reqs.size());
        for (std::size_t i = 0; i < runs[0].reqs.size(); ++i) {
            CHECK(runs[k].samples[i] == runs[0].samples[i]);
            CHECK(runs[k].reqs[i].syllable == runs[0].reqs[i].syllable);
            CHECK(runs[k].reqs[i].releaseAlarm == runs[0].reqs[i].releaseAlarm);
        }
    }
}

// ------------------------------------------------------------------ D-032: syllables that never start a key press
namespace {

/// The committed bank with the flag `flag` added to every syllable in `ids`.
SyllableBank flaggedBank(const std::vector<int>& ids, const std::string& flag) {
    std::ifstream in(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/zf_22938/bank.json");
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = nlohmann::json::parse(ss.str());
    int k = 0;
    for (auto& p : j["phrases"]) {
        for (auto& s : p["syllables"]) {
            if (std::find(ids.begin(), ids.end(), k) != ids.end()) {
                s["flags"].push_back(flag);
            }
            ++k;
        }
    }
    return cuicui::io::parseSyllableBank(j.dump());
}

/// First syllable of each of 40 taps (one key press each), in the given order mode.
std::vector<int> firstSyllables(const SyllableBank& b, OrderMode order, bool reserve, std::uint64_t seed) {
    PlayContext ctx;
    ctx.order = order;
    ctx.releaseAlarm = reserve;
    ModeAB m;
    m.prepare(kSr);
    m.reset(seed);
    m.setBank(&b);
    std::vector<std::pair<long long, PlayEvent>> ev;
    for (int k = 0; k < 40; ++k) {
        ev.push_back({480 + 24000LL * k, noteOn(60 + k % 12)});
        ev.push_back({480 + 24000LL * k + 2880, noteOff(60 + k % 12)});
    }
    const auto run = drive(m, ev, 480 + 24000LL * 41, 256, ctx);
    std::vector<int> out;
    for (const auto& q : run.reqs) {
        if (q.first) {
            out.push_back(q.syllable);
        }
    }
    return out;
}

} // namespace

TEST_CASE("bank flags: no_note and no_tap keep a syllable from starting a key press") {
    using cuicui::io::parseSyllableBank;
    const std::string head =
        R"({"format":"cuicui.syllable_bank","version":1,"contour_rate_hz":1000,)"
        R"("centres_cents":{"rule":1,"steady":1,"landing":1,"average":1},"phrases":[{"id":0,"syllables":[)";
    const std::string syl = R"({"f_hz":[1000,1000],"a":[0,1],"anchors":{"rule":1,"steady":1,"landing":1,"average":1},)";
    const auto b = parseSyllableBank(head + syl + R"("t_ms":0,"flags":["no_note"]},)" + syl +
                                     R"("t_ms":5,"flags":["hop","no_tap"]},)" + syl + R"("t_ms":10,"flags":["hop"]},)" +
                                     syl + R"("t_ms":15,"label":"alarm"},)" + syl + R"("t_ms":20}]}]})");
    CHECK_FALSE(b.isTapSyllable(0));
    CHECK_FALSE(b.isTapSyllable(1));
    CHECK(b.isTapSyllable(2));
    CHECK_FALSE(b.isTapSyllable(3)); // a release call never starts a key press either
    CHECK(b.isTapSyllable(4));
    CHECK(b.tapSyllables() == std::vector<int>{2, 4});
    CHECK(b.playableSyllables().size() == 4); // flagged syllables still sing in phrases
    CHECK_THROWS_AS(parseSyllableBank(head + syl + R"("t_ms":0,"flags":"no_note"}]}]})"), cuicui::io::SyllableBankError);
    CHECK_THROWS_AS(parseSyllableBank(head + syl + R"("t_ms":0,"flags":[3]}]}]})"), cuicui::io::SyllableBankError);
    for (int i = 0; i < realBank().numSyllables(); ++i) { // the committed bank flags none (TUNE1: no "no clear note")
        CHECK(realBank().isTapSyllable(i) == !realBank().isReleaseSyllable(i));
    }
}

TEST_CASE("bank flags: flagged syllables never start a key press; every other first pick is unchanged") {
    const auto& plain = realBank();
    std::vector<int> flagged; // every third playable syllable
    for (std::size_t i = 0; i < plain.playableSyllables().size(); i += 3) {
        flagged.push_back(plain.playableSyllables()[i]);
    }
    const auto isFlagged = [&](int s) { return std::find(flagged.begin(), flagged.end(), s) != flagged.end(); };
    for (const char* flag : {"no_note", "no_tap"}) {
        const auto b = flaggedBank(flagged, flag);
        for (auto order : {OrderMode::RandomStart, OrderMode::PhraseStart, OrderMode::Shuffled}) {
            for (std::uint64_t seed : {3ULL, 77ULL, 20261013ULL}) {
                INFO(flag << " order " << static_cast<int>(order) << " seed " << seed);
                const auto got = firstSyllables(b, order, true, seed);
                const auto before = firstSyllables(plain, order, true, seed);
                REQUIRE(got.size() == 40);
                REQUIRE(before.size() == 40);
                int kept = 0;
                for (std::size_t k = 0; k < got.size(); ++k) {
                    CHECK_FALSE(isFlagged(got[k]));
                    if (order != OrderMode::PhraseStart && !isFlagged(before[k])) {
                        CHECK(got[k] == before[k]); // a redraw only where the old pick was flagged
                        ++kept;
                    }
                }
                if (order != OrderMode::PhraseStart) {
                    CHECK(kept >= 15);
                }
                // the R1 / PROTO1 playing (no reserve) ignores the flags, draw for draw
                CHECK(firstSyllables(b, order, false, seed) == firstSyllables(plain, order, false, seed));
            }
        }
    }
}

TEST_CASE("bank flags: a held key still sings the flagged syllables inside its phrases") {
    const auto& plain = realBank();
    std::vector<int> flagged(plain.playableSyllables().begin(), plain.playableSyllables().end());
    flagged.erase(flagged.begin()); // all playable syllables but the first one may not start a key press
    const auto b = flaggedBank(flagged, "no_tap");
    REQUIRE(b.tapSyllables().size() == 1);
    PlayContext ctx;
    ModeAB m;
    m.prepare(kSr);
    m.reset(11);
    m.setBank(&b);
    const auto run = drive(m, {{480, noteOn(64)}, {480 + 4 * 48000, noteOff(64)}}, 6 * 48000, 256, ctx);
    REQUIRE(run.reqs.size() > 3);
    CHECK(run.reqs[0].syllable == b.tapSyllables().front());
    int flaggedSung = 0;
    for (std::size_t i = 1; i < run.reqs.size(); ++i) {
        flaggedSung += std::find(flagged.begin(), flagged.end(), run.reqs[i].syllable) != flagged.end() ? 1 : 0;
    }
    CHECK(flaggedSung > 0);
}
