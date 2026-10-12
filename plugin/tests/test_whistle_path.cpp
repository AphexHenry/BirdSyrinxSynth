// WP-R1: the whistle path through the plugin processor (modes A + B, latency, real time).
#include "PluginProcessor.h"
#include "alloc_count.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace {

constexpr double kSr = 48000.0;

struct Render {
    std::vector<float> left, right;
    std::vector<cuicui::control::SyllableLogEntry> log;
    int latency = 0;
};

Render render(SyrinxAudioProcessor& proc, int block, int n, const std::vector<std::pair<int, juce::MidiMessage>>& ev) {
    Render out;
    out.log.resize(4096);
    proc.setSyllableLog(out.log.data(), static_cast<int>(out.log.size()));
    proc.prepareToPlay(kSr, block);
    out.latency = proc.getLatencySamples();
    out.left.assign(static_cast<std::size_t>(n), 0.0f);
    out.right.assign(static_cast<std::size_t>(n), 0.0f);
    juce::AudioBuffer<float> buf(2, block);
    std::size_t ei = 0;
    for (int done = 0; done < n;) {
        const int m = std::min(block, n - done);
        buf.setSize(2, m, false, false, true);
        juce::MidiBuffer midi;
        while (ei < ev.size() && ev[ei].first < done + m) {
            midi.addEvent(ev[ei].second, ev[ei].first - done);
            ++ei;
        }
        proc.processBlock(buf, midi);
        for (int i = 0; i < m; ++i) {
            out.left[static_cast<std::size_t>(done + i)] = buf.getSample(0, i);
            out.right[static_cast<std::size_t>(done + i)] = buf.getSample(1, i);
        }
        done += m;
    }
    out.log.resize(static_cast<std::size_t>(proc.syllableLogCount()));
    proc.setSyllableLog(nullptr, 0);
    proc.releaseResources();
    return out;
}

/// Sounding regions (|x| above −80 dBFS, gaps shorter than 5 ms bridged).
std::vector<std::pair<int, int>> regions(const std::vector<float>& x) {
    std::vector<std::pair<int, int>> r;
    const float thr = 1e-4f;
    int start = -1, last = -1;
    for (int i = 0; i < static_cast<int>(x.size()); ++i) {
        if (std::abs(x[static_cast<std::size_t>(i)]) > thr) {
            if (start < 0 || i - last > 240) {
                if (start >= 0) {
                    r.emplace_back(start, last);
                }
                start = i;
            }
            last = i;
        }
    }
    if (start >= 0) {
        r.emplace_back(start, last);
    }
    return r;
}

void dryPlugin(SyrinxAudioProcessor& p) {
    p.setParamPlain("out.room", 0.0f);
    p.setParamPlain("out.master_db", 0.0f);
    p.setParamPlain("engine.seed", 7.0f);
}

} // namespace

TEST_CASE("whistle: it is the default voice, with polyphony 4, harmonics and room on") {
    SyrinxAudioProcessor p;
    CHECK(p.whistleSelected());
    CHECK(p.apvts.getParameter("perf.polyphony")->getCurrentValueAsText() == "4");
    CHECK(p.apvts.getRawParameterValue("voice.harmonics")->load() == 1.0f);
    CHECK(p.apvts.getRawParameterValue("out.room")->load() == 1.0f);
    CHECK(p.apvts.getRawParameterValue("perf.first_on_note")->load() == 1.0f);
    CHECK(p.syllableBank().numSyllables() == 123);
    CHECK(p.syllableBank().name() == "zf_22938");
}

TEST_CASE("whistle A: a 30 ms tap sounds one syllable that starts within 5 ms of the note-on") {
    for (int block : {64, 256, 1024}) {
        SyrinxAudioProcessor p;
        dryPlugin(p);
        const int on = 4800 + 13;
        const auto r = render(p, block, 48000,
                              {{on, juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100))},
                               {on + 1440, juce::MidiMessage::noteOff(1, 69)}});
        REQUIRE(r.log.size() == 1);
        CHECK(r.log[0].sample == on);
        const auto reg = regions(r.left); // one syllable (some have a short silence inside: several regions)
        REQUIRE_FALSE(reg.empty());
        int firstNonZero = 0;
        while (r.left[static_cast<std::size_t>(firstNonZero)] == 0.0f) {
            ++firstNonZero;
        }
        // the syllable's first sample is exactly 0 (its amplitude curve starts at 0): sound from the next one
        CHECK(firstNonZero == on + r.latency + 1);
        const int startDelay = reg[0].first - on; // audible (> −80 dBFS)
        CHECK(startDelay >= 0);
        CHECK(startDelay <= 240); // 5 ms, latency included
        WARN("tap at block " << block << ": first sample " << firstNonZero - on << " samples after the note-on "
                             << "(latency " << r.latency << "), audible after " << startDelay << " samples");
        const auto& syl = p.syllableBank().syllable(r.log[0].request.syllable);
        const int len = static_cast<int>(std::llround(syl.lengthMs * 48.0));
        CHECK(reg.back().second - reg.front().first <= len);
        CHECK(reg.back().second - reg.front().first > len / 2);
    }
}

TEST_CASE("whistle B: hold 2 s → real order and gaps; release mid-syllable → it completes, nothing after") {
    SyrinxAudioProcessor p;
    dryPlugin(p);
    p.setReleaseAlarm(false); // the R1 rule (a long press ending with the call: D-030, below)
    p.setParamPlain("perf.order", 1.0f); // phrase start: phrase 0, 1, … in the bank's order
    const int on = 2400;
    const int off = on + 96000;
    const auto r = render(p, 256, 200000,
                          {{on, juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90))},
                           {off, juce::MidiMessage::noteOff(1, 64)}});
    const auto& b = p.syllableBank();
    REQUIRE(r.log.size() >= 6);
    CHECK(r.log[0].request.syllable == 0);
    for (std::size_t i = 1; i < r.log.size(); ++i) {
        const auto& prev = b.syllable(r.log[i - 1].request.syllable);
        const auto& cur = b.syllable(r.log[i].request.syllable);
        const double gapMs = static_cast<double>(r.log[i].sample - r.log[i - 1].sample) / 48.0;
        if (cur.phrase == prev.phrase) {
            CHECK(cur.index == prev.index + 1);
            CHECK(std::abs(gapMs - (cur.tMs - prev.tMs)) <= 1.0);
        } else {
            CHECK(cur.phrase == (prev.phrase + 1) % b.numPhrases());
            CHECK(std::abs(gapMs - (prev.lengthMs + 500.0)) <= 1.0);
        }
        CHECK(r.log[i].sample < off);
    }
    // the last syllable started before the release and is heard to its end; nothing after it
    const auto& last = r.log.back();
    const int lastEnd = static_cast<int>(last.sample) + r.latency +
                        static_cast<int>(std::llround(b.syllable(last.request.syllable).lengthMs * 48.0));
    const auto reg = regions(r.left);
    REQUIRE_FALSE(reg.empty());
    CHECK(reg.back().second <= lastEnd);
    CHECK(reg.back().second > lastEnd - 200);
}

TEST_CASE("whistle B: a release during the phrase pause is silent") {
    SyrinxAudioProcessor p;
    dryPlugin(p);
    p.setReleaseAlarm(false); // the R1 rule
    p.setParamPlain("perf.order", 1.0f);
    p.setParamPlain("perf.pause_s", 1.0f);
    const auto& b = p.syllableBank();
    const auto& s2 = b.syllable(2); // last syllable of phrase 0
    const int on = 1000;
    const int phraseEnd = on + (s2.tMs + s2.lengthMs) * 48;
    const auto r = render(p, 512, 192000,
                          {{on, juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90))},
                           {phraseEnd + 24000, juce::MidiMessage::noteOff(1, 64)}});
    REQUIRE(r.log.size() == 3);
    const auto reg = regions(r.left);
    REQUIRE_FALSE(reg.empty());
    CHECK(reg.back().second <= phraseEnd + r.latency);
}

TEST_CASE("whistle: a long press ends with the bird's rising call; short presses never bring it (D-030)") {
    SyrinxAudioProcessor p;
    dryPlugin(p);
    const auto& b = p.syllableBank();
    // three short presses (0.1, 0.3, 0.55 s), then a 1.5 s hold; one key at a time
    const int a = 4800;
    const std::vector<std::pair<int, juce::MidiMessage>> ev{
        {a, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100))},
        {a + 4800, juce::MidiMessage::noteOff(1, 60)},
        {a + 48000, juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100))},
        {a + 48000 + 14400, juce::MidiMessage::noteOff(1, 62)},
        {a + 96000, juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100))},
        {a + 96000 + 26400, juce::MidiMessage::noteOff(1, 64)},
        {a + 144000, juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100))},
        {a + 144000 + 72000, juce::MidiMessage::noteOff(1, 69)}};
    const auto r = render(p, 256, a + 144000 + 72000 + 48000, ev);
    int calls = 0;
    for (const auto& e : r.log) {
        const bool call = b.label(e.request.syllable).rfind("alarm", 0) == 0;
        CHECK(call == e.request.releaseAlarm); // the call never appears in phrases or taps
        if (e.request.releaseAlarm) {
            ++calls;
            CHECK(e.request.note == 69);
            CHECK(e.sample >= a + 144000 + 72000); // only after the release
        }
    }
    CHECK(calls == 1);
    // and it is heard: the last sound is the call, to its end
    const auto& last = r.log.back();
    REQUIRE(last.request.releaseAlarm);
    const int end = static_cast<int>(last.sample) + r.latency +
                    static_cast<int>(std::llround(b.syllable(last.request.syllable).lengthMs * 48.0));
    const auto reg = regions(r.left);
    REQUIRE_FALSE(reg.empty());
    CHECK(reg.back().second <= end);
    CHECK(reg.back().second > end - 2400);
}

TEST_CASE("whistle: output is identical for host blocks of 32, 256 and 1024 samples") {
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    for (int k = 0; k < 8; ++k) {
        const int t = 1000 + k * 9000;
        ev.push_back({t, juce::MidiMessage::noteOn(1, 60 + 3 * k, static_cast<juce::uint8>(60 + 8 * k))});
        ev.push_back({t + 30000, juce::MidiMessage::noteOff(1, 60 + 3 * k)});
    }
    std::sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Render> rs;
    for (int block : {32, 256, 1024}) {
        SyrinxAudioProcessor p; // room on (default)
        p.setParamPlain("engine.seed", 3.0f);
        rs.push_back(render(p, block, 144000, ev));
    }
    for (std::size_t k = 1; k < rs.size(); ++k) {
        REQUIRE(rs[k].log.size() == rs[0].log.size());
        float d = 0.0f;
        for (std::size_t i = 0; i < rs[0].left.size(); ++i) {
            d = std::max(d, std::abs(rs[k].left[i] - rs[0].left[i]));
            d = std::max(d, std::abs(rs[k].right[i] - rs[0].right[i]));
        }
        CHECK(d == 0.0f);
    }
}

TEST_CASE("whistle: switching voices while playing does not explode, and comes back") {
    SyrinxAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    juce::AudioBuffer<float> buf(2, 256);
    float peak = 0.0f;
    for (int blk = 0; blk < 600; ++blk) {
        juce::MidiBuffer midi;
        if (blk % 40 == 0) {
            midi.addEvent(juce::MidiMessage::noteOn(1, 64 + blk % 12, static_cast<juce::uint8>(100)), 10);
        }
        if (blk % 40 == 30) {
            midi.addEvent(juce::MidiMessage::noteOff(1, 64 + (blk - 30) % 12), 5);
        }
        if (blk % 150 == 75) {
            p.setParamPlain("voice.type", p.whistleSelected() ? 1.0f : 0.0f);
        }
        p.processBlock(buf, midi);
        for (int i = 0; i < 256; ++i) {
            REQUIRE(std::isfinite(buf.getSample(0, i)));
            peak = std::max(peak, std::abs(buf.getSample(0, i)));
        }
    }
    CHECK(peak <= 1.0f);
    CHECK(peak > 0.01f);
}

TEST_CASE("whistle: zero allocations in processBlock over 60 s of playing") {
    SyrinxAudioProcessor p;
    p.setParamPlain("perf.polyphony", 6.0f);
    p.prepareToPlay(kSr, 256);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    juce::Random rnd(11);
    int held[6] = {-1, -1, -1, -1, -1, -1};
    // warm-up block
    p.processBlock(buf, midi);
    const int before = cuicui::test::alloc_count();
    for (int blk = 0; blk < 60 * 48000 / 256; ++blk) {
        midi.clear();
        if (rnd.nextFloat() < 0.08f) {
            const int k = rnd.nextInt(6);
            if (held[k] >= 0) {
                midi.addEvent(juce::MidiMessage::noteOff(1, held[k]), rnd.nextInt(256));
                held[k] = -1;
            } else {
                held[k] = 48 + rnd.nextInt(40);
                midi.addEvent(juce::MidiMessage::noteOn(1, held[k], static_cast<juce::uint8>(1 + rnd.nextInt(127))),
                              rnd.nextInt(256));
            }
        }
        if (blk % 500 == 0) {
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, rnd.nextInt(128)), 0);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, blk % 1000 == 0 ? 127 : 0), 1);
        }
        p.processBlock(buf, midi);
    }
    CHECK(cuicui::test::alloc_count() == before);
}

TEST_CASE("whistle: CPU with 6 keys held (report)") {
    SyrinxAudioProcessor p;
    p.setParamPlain("perf.polyphony", 6.0f);
    p.prepareToPlay(kSr, 256);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int k = 0; k < 6; ++k) {
        midi.addEvent(juce::MidiMessage::noteOn(1, 60 + 2 * k, static_cast<juce::uint8>(100)), k);
    }
    p.processBlock(buf, midi);
    midi.clear();
    const int blocks = 20 * 48000 / 256;
    const auto t0 = std::chrono::steady_clock::now();
    int maxVoices = 0;
    for (int b = 0; b < blocks; ++b) {
        p.processBlock(buf, midi);
        maxVoices = std::max(maxVoices, p.whistleTelemetry().voices);
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double pct = 100.0 * sec / 20.0;
    WARN("whistle CPU, 6 keys held, room on: " << pct << " % of one core (max " << maxVoices
                                                 << " voices sounding at once)");
    CHECK(p.whistleTelemetry().singers == 6);
    CHECK(pct < 25.0);
}
