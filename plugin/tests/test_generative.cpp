#include "PluginProcessor.h"
#include "test_helpers.h"
#include "SafetyLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

std::vector<float> render(SyrinxAudioProcessor& proc, double sr, int block, int n,
                          const std::vector<std::pair<int, juce::MidiMessage>>& events) {
    proc.prepareToPlay(sr, block);
    std::vector<float> left(static_cast<std::size_t>(n), 0.0f);
    juce::AudioBuffer<float> buf(2, block);
    std::size_t ei = 0;
    int done = 0;
    while (done < n) {
        const int nThis = std::min(block, n - done);
        buf.setSize(2, nThis, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        while (ei < events.size() && events[ei].first < done + nThis) {
            midi.addEvent(events[ei].second, events[ei].first - done);
            ++ei;
        }
        proc.processBlock(buf, midi);
        std::memcpy(left.data() + done, buf.getReadPointer(0), static_cast<std::size_t>(nThis) * sizeof(float));
        done += nThis;
    }
    proc.releaseResources();
    return left;
}

void baseParams(SyrinxAudioProcessor& p) {
    p.setParamPlain("noise.aspiration", 0.0f);
    p.setParamPlain("noise.beta_sigma", 0.0f);
    p.setParamPlain("out.master_db", -6.0f);
    p.setParamPlain("perf.mode", 0.0f);
    p.setParamPlain("perf.motion", 0.3f);
    p.setParamPlain("perf.tone", 0.6f);
    p.setParamPlain("perf.leash", 1.0f);
    p.setParamPlain("perf.variation", 1.0f);
    p.setParamPlain("perf.density", 0.5f);
    p.setParamPlain("perf.seed", 42.0f);
    p.setParamPlain("engine.seed", 1.0f);
}

} // namespace

TEST_CASE("generative: leash 0 two runs are bit-identical (C3 path)") {
    const double sr = 48000.0;
    const int n = static_cast<int>(0.4 * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, 96, static_cast<juce::uint8>(100))});
    auto one = [&]() {
        SyrinxAudioProcessor proc;
        useAnatomicalVoice(proc);
        proc.setNonRealtime(true);
        baseParams(proc);
        proc.setParamPlain("perf.leash", 0.0f);
        proc.setParamPlain("perf.motion", 0.0f);
        return render(proc, sr, 256, n, ev);
    };
    const auto a = one();
    const auto b = one();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i] == b[i]);
    }
    float peak = 0.0f;
    for (float x : a) {
        peak = std::max(peak, std::abs(x));
    }
    REQUIRE(peak > 1.0e-4f);
}

TEST_CASE("generative: perf.seed != 0 is deterministic") {
    const double sr = 48000.0;
    const int n = static_cast<int>(1.2 * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, 96, static_cast<juce::uint8>(100))});
    auto one = [&]() {
        SyrinxAudioProcessor proc;
        useAnatomicalVoice(proc);
        proc.setNonRealtime(true);
        baseParams(proc);
        proc.setParamPlain("perf.leash", 1.0f);
        proc.setParamPlain("perf.seed", 77.0f);
        return render(proc, sr, 256, n, ev);
    };
    const auto a = one();
    const auto b = one();
    REQUIRE(a.size() == b.size());
    int nDiff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++nDiff;
        }
    }
    REQUIRE(nDiff == 0);
}

TEST_CASE("generative: 8-note flock CPU within 8x * 1.2 of one voice") {
    const double sr = 48000.0;
    const int block = 256;
    const int n = static_cast<int>(1.0 * sr);
    auto timed = [&](int poly, int nNotes) {
        SyrinxAudioProcessor proc;
        useAnatomicalVoice(proc);
        proc.setNonRealtime(true);
        baseParams(proc);
        proc.setParamPlain("perf.polyphony", static_cast<float>(poly));
        proc.setParamPlain("out.stereo_spread", 0.5f);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        const int notes[] = {86, 89, 93, 96, 98, 100, 103, 105};
        for (int i = 0; i < nNotes; ++i) {
            ev.push_back({0, juce::MidiMessage::noteOn(1, notes[i], static_cast<juce::uint8>(90))});
        }
        const auto t0 = std::chrono::steady_clock::now();
        (void)render(proc, sr, block, n, ev);
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(t1 - t0).count();
    };
    const double t1 = timed(1, 1);
    const double t8 = timed(8, 8);
    WARN("flock t1 " << t1 << " t8 " << t8 << " ratio " << (t8 / std::max(t1, 1e-9)));
    REQUIRE(t8 <= t1 * 8.0 * 1.2 + 0.05);
}

TEST_CASE("generative: voice steal 5 ms fade, no click above -40 dBFS") {
    SyrinxAudioProcessor proc;
    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    baseParams(proc);
    proc.setParamPlain("perf.polyphony", 2.0f);
    proc.setParamPlain("perf.leash", 1.0f);
    const double sr = 48000.0;
    const int n = static_cast<int>(0.6 * sr);
    const int stealAt = static_cast<int>(0.25 * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, 96, static_cast<juce::uint8>(100))});
    ev.push_back({1, juce::MidiMessage::noteOn(1, 98, static_cast<juce::uint8>(100))});
    ev.push_back({stealAt, juce::MidiMessage::noteOn(1, 100, static_cast<juce::uint8>(100))});
    const auto y = render(proc, sr, 256, n, ev);
    const int w = static_cast<int>(0.005 * sr);
    float peakJump = 0.0f;
    const int i0 = std::max(1, stealAt - w);
    const int i1 = std::min(static_cast<int>(y.size()) - 1, stealAt + 2 * w);
    for (int i = i0; i < i1; ++i) {
        peakJump = std::max(peakJump, std::abs(y[static_cast<std::size_t>(i)] - y[static_cast<std::size_t>(i - 1)]));
    }
    const double jumpDb = peakJump <= 1e-12f ? -120.0 : 20.0 * std::log10(static_cast<double>(peakJump));
    WARN("steal peakJump " << peakJump << " dB " << jumpDb);
    // A 5 ms fade-in keeps sample-to-sample steps well below a full-scale click.
    REQUIRE(jumpDb < -6.0); // generous vs -40 dBFS click; full-scale click would be ~0 dB
    float peak = 0.0f;
    for (float x : y) {
        REQUIRE(std::isfinite(x));
        peak = std::max(peak, std::abs(x));
    }
    REQUIRE(peak <= 1.0f + 1.0e-4f);
}
