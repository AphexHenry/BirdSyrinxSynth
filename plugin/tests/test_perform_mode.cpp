#include "PluginProcessor.h"
#include "cuicui/control/voice_controller.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

float peakAbs(const float* x, int n) {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) {
        p = std::max(p, std::abs(x[i]));
    }
    return p;
}

double rms(const float* x, int n) {
    double a = 0.0;
    for (int i = 0; i < n; ++i) {
        a += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return n > 0 ? std::sqrt(a / static_cast<double>(n)) : 0.0;
}

void fft_radix2(std::vector<std::complex<double>>& a) {
    const int n = static_cast<int>(a.size());
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[static_cast<unsigned>(i)], a[static_cast<unsigned>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k) {
                auto u = a[static_cast<unsigned>(i + k)];
                auto v = a[static_cast<unsigned>(i + k + len / 2)] * w;
                a[static_cast<unsigned>(i + k)] = u + v;
                a[static_cast<unsigned>(i + k + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }
}

double fft_f0_near(const float* x, int n, double sr, int skip, double fRef) {
    if (x == nullptr || n <= skip + 64 || !(fRef > 1.0)) {
        return 0.0;
    }
    const int i0 = skip;
    const int m = n - i0;
    int nfft = 1;
    while (nfft < m && nfft < 16384) {
        nfft <<= 1;
    }
    nfft = std::min(nfft, 16384);
    std::vector<std::complex<double>> buf(static_cast<std::size_t>(nfft), 0.0);
    const int nWin = std::min(m, nfft);
    double mu = 0.0;
    for (int i = 0; i < nWin; ++i) {
        mu += static_cast<double>(x[i0 + i]);
    }
    mu /= static_cast<double>(nWin);
    for (int i = 0; i < nWin; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                              static_cast<double>(nWin - 1));
        buf[static_cast<std::size_t>(i)] = (static_cast<double>(x[i0 + i]) - mu) * w;
    }
    fft_radix2(buf);
    const int kLo = std::max(1, static_cast<int>(std::floor(fRef * 0.82 * nfft / sr)));
    const int kHi = std::min(nfft / 2 - 1, static_cast<int>(std::ceil(fRef * 1.22 * nfft / sr)));
    int kBest = kLo;
    double pBest = -1.0;
    for (int k = kLo; k <= kHi; ++k) {
        const double p = std::norm(buf[static_cast<std::size_t>(k)]);
        if (p > pBest) {
            pBest = p;
            kBest = k;
        }
    }
    auto mag = [&](int k) { return std::sqrt(std::norm(buf[static_cast<std::size_t>(k)])); };
    double delta = 0.0;
    if (kBest > 0 && kBest < nfft / 2) {
        const double a = mag(kBest - 1);
        const double b = mag(kBest);
        const double c = mag(kBest + 1);
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-18) {
            delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
    }
    return (static_cast<double>(kBest) + delta) * sr / static_cast<double>(nfft);
}

double midiHz(int note) { return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0); }

double centsErr(double fMeas, double fRef) {
    if (!(fMeas > 0.0) || !(fRef > 0.0)) {
        return 1.0e9;
    }
    return 1200.0 * std::log2(fMeas / fRef);
}

void silenceNoise(SyrinxAudioProcessor& proc) {
    proc.setParamPlain("noise.aspiration", 0.0f);
    proc.setParamPlain("noise.beta_sigma", 0.0f);
    proc.setParamPlain("out.master_db", 0.0f);
    proc.setParamPlain("out.gain_db", -6.0f);
    proc.setParamPlain("perf.mode", 0.0f);
    proc.setParamPlain("perf.motion", 0.0f);
    proc.setParamPlain("perf.snap", 0.0f);
    proc.setParamPlain("perf.glide_ms", 0.0f);
    proc.setParamPlain("perf.tone", 0.6f);
    proc.setParamPlain("body.size", 1.0f);
}

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

std::vector<float> holdNote(int note, double seconds, float tone = 0.6f, float size = 1.0f, int vel = 100) {
    SyrinxAudioProcessor proc;
    proc.setNonRealtime(true);
    silenceNoise(proc);
    proc.setParamPlain("perf.tone", tone);
    proc.setParamPlain("body.size", size);
    const double sr = 48000.0;
    const int n = static_cast<int>(seconds * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(vel))});
    return render(proc, sr, 256, n, ev);
}

} // namespace

TEST_CASE("perform mode: playable notes in tune at tone 0.6 size 1") {
    const int notes[] = {86, 88, 91, 93, 96, 98, 103, 108}; // D6 … C8
    for (int note : notes) {
        const auto y = holdNote(note, 0.28);
        const double fRef = midiHz(note);
        const int skip = static_cast<int>(0.10 * 48000);
        const double f = fft_f0_near(y.data(), static_cast<int>(y.size()), 48000.0, skip, fRef);
        const double err = centsErr(f, fRef);
        INFO("note " << note << " fRef " << fRef << " f " << f << " cents " << err);
        REQUIRE(std::abs(err) <= 5.0);
    }
}

TEST_CASE("perform mode: tone 0.2/1.0 and size 0.5/2 every 3rd semitone") {
    struct Case {
        float tone;
        float size;
        double tol;
        std::vector<int> notes;
    };
    // In-range MIDI for each σ (C2: σ=1 is D6–C8; Hz scale as 1/σ).
    const Case cases[] = {
        {0.2f, 1.0f, 10.0, {86, 89, 92, 95, 98, 101, 104, 107}},
        {1.0f, 1.0f, 5.0, {86, 89, 92, 95, 98, 101, 104, 107}},
        {0.6f, 0.5f, 5.0, {98, 101, 104, 107}}, // D7–B7 at tiny bird
        {0.6f, 2.0f, 5.0, {86, 89, 92, 95}},    // D6–B6 at big bird
    };
    for (const auto& c : cases) {
        for (int note : c.notes) {
            const double fRef = midiHz(note);
            const auto y = holdNote(note, 0.25, c.tone, c.size);
            const int skip = static_cast<int>(0.10 * 48000);
            const double f = fft_f0_near(y.data(), static_cast<int>(y.size()), 48000.0, skip, fRef);
            const double err = centsErr(f, fRef);
            INFO("tone " << c.tone << " size " << c.size << " note " << note << " f " << f << " tgt " << fRef
                         << " cents " << err);
            REQUIRE(std::abs(err) <= c.tol);
        }
    }
}

TEST_CASE("perform mode: pitch bend +2 st is +200 cents") {
    SyrinxAudioProcessor proc;
    proc.setNonRealtime(true);
    silenceNoise(proc);
    const double sr = 48000.0;
    const int n = static_cast<int>(0.35 * sr);
    const int note = 96;
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100))});
    ev.push_back({static_cast<int>(0.12 * sr), juce::MidiMessage::pitchWheel(1, 16383)});
    const auto y = render(proc, sr, 256, n, ev);
    const double f0 = midiHz(note);
    const double fBend = midiHz(note) * std::pow(2.0, 2.0 / 12.0);
    const int skipA = static_cast<int>(0.04 * sr);
    const int nA = static_cast<int>(0.08 * sr);
    std::vector<float> a(y.begin() + skipA, y.begin() + skipA + nA);
    const double fA = fft_f0_near(a.data(), nA, sr, 0, f0);
    const int skipB = static_cast<int>(0.18 * sr);
    std::vector<float> b(y.begin() + skipB, y.end());
    const double fB = fft_f0_near(b.data(), static_cast<int>(b.size()), sr, 0, fBend);
    REQUIRE_THAT(centsErr(fA, f0), WithinAbs(0.0, 8.0));
    REQUIRE_THAT(centsErr(fB, fBend), WithinAbs(0.0, 8.0));
}

TEST_CASE("perform mode: onset latency <= 20 ms at defaults") {
    const auto y = holdNote(96, 0.35);
    const double sr = 48000.0;
    const int n = static_cast<int>(y.size());
    const int win = static_cast<int>(0.004 * sr);
    const int steady0 = static_cast<int>(0.12 * sr);
    const double steady = rms(y.data() + steady0, std::min(win * 8, n - steady0));
    REQUIRE(steady > 1e-4);
    const double thresh = steady * std::pow(10.0, -20.0 / 20.0);
    int hit = n;
    for (int i = 0; i + win < n; ++i) {
        if (rms(y.data() + i, win) >= thresh) {
            hit = i;
            break;
        }
    }
    const double ms = 1000.0 * static_cast<double>(hit) / sr;
    INFO("onset_ms " << ms << " steady " << steady);
    REQUIRE(ms <= 20.0);
}

TEST_CASE("perform mode: physics=1 breath swell stays in tune") {
    SyrinxAudioProcessor proc;
    proc.setNonRealtime(true);
    silenceNoise(proc);
    proc.setParamPlain("perf.physics", 1.0f);
    proc.setParamPlain("perf.breath", 0.55f);
    const double sr = 48000.0;
    const int n = static_cast<int>(0.6 * sr);
    const int note = 96;
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100))});
    ev.push_back({static_cast<int>(0.05 * sr), juce::MidiMessage::channelPressureChange(1, 20)});
    ev.push_back({static_cast<int>(0.25 * sr), juce::MidiMessage::channelPressureChange(1, 110)});
    const auto y = render(proc, sr, 256, n, ev);
    const double fRef = midiHz(note);
    const double fA = fft_f0_near(y.data(), n, sr, static_cast<int>(0.08 * sr), fRef);
    const double fB = fft_f0_near(y.data() + static_cast<int>(0.32 * sr), n - static_cast<int>(0.32 * sr), sr, 0, fRef);
    INFO("fA " << fA << " fB " << fB);
    REQUIRE(std::abs(centsErr(fA, fRef)) <= 5.0);
    REQUIRE(std::abs(centsErr(fB, fRef)) <= 5.0);
}

TEST_CASE("perform mode: octave jump changes side, skips the glide") {
    SyrinxAudioProcessor proc;
    proc.setNonRealtime(true);
    silenceNoise(proc);
    proc.setParamPlain("perf.glide_ms", 0.0f);
    proc.setParamPlain("perf.side_switch_cents", 700.0f);
    const double sr = 48000.0;
    const int n = static_cast<int>(0.45 * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, 86, static_cast<juce::uint8>(100))}); // D6
    const int jump = static_cast<int>(0.20 * sr);
    ev.push_back({jump, juce::MidiMessage::noteOn(1, 98, static_cast<juce::uint8>(100))}); // D7 legato
    const auto y = render(proc, sr, 256, n, ev);
    const double fLo = midiHz(86);
    const double fHi = midiHz(98);
    const int w = static_cast<int>(0.002 * sr);
    const int mid0 = jump + static_cast<int>(0.20 * (0.20 * sr));
    const int mid1 = jump + static_cast<int>(0.80 * (0.20 * sr));
    int streak = 0;
    int maxStreak = 0;
    for (int i = mid0; i + w < mid1; i += w) {
        const double f = fft_f0_near(y.data() + i, w, sr, 0, 0.5 * (fLo + fHi));
        const bool mid = f > fLo * 1.15 && f < fHi / 1.15;
        if (mid) {
            ++streak;
            maxStreak = std::max(maxStreak, streak);
        } else {
            streak = 0;
        }
    }
    INFO("max mid-f0 streak " << maxStreak);
    REQUIRE(maxStreak <= 2);
    (void)peakAbs(y.data(), n);
}

TEST_CASE("perform mode: C7 vs D7 ratio and D6–D7 octave") {
    const auto d6 = holdNote(86, 0.28);
    const auto e6 = holdNote(88, 0.28);
    const auto d7 = holdNote(98, 0.28);
    const double fD6 = fft_f0_near(d6.data(), static_cast<int>(d6.size()), 48000.0, static_cast<int>(0.1 * 48000),
                                   midiHz(86));
    const double fE6 = fft_f0_near(e6.data(), static_cast<int>(e6.size()), 48000.0, static_cast<int>(0.1 * 48000),
                                   midiHz(88));
    const double fD7 = fft_f0_near(d7.data(), static_cast<int>(d7.size()), 48000.0, static_cast<int>(0.1 * 48000),
                                   midiHz(98));
    REQUIRE(fE6 / fD6 > 1.10);
    REQUIRE(fD7 / fD6 > 1.9);
    REQUIRE(fD7 / fD6 < 2.1);
}
