#include "PluginProcessor.h"
#include "test_helpers.h"
#include "SafetyLimiter.h"
#include "alloc_counter.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/wav_writer.hpp"
#include "cuicui/model/bird_voice.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
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

float rmsDb(const float* x, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    if (n <= 0 || acc <= 0.0) {
        return -120.0f;
    }
    return static_cast<float>(10.0 * std::log10(acc / static_cast<double>(n)));
}

double zcF0Hz(const std::vector<float>& x, double sr, int start, int n) {
    int zc = 0;
    const int end = std::min(static_cast<int>(x.size()), start + n);
    for (int i = start + 1; i < end; ++i) {
        if ((x[static_cast<std::size_t>(i - 1)] < 0.0f) != (x[static_cast<std::size_t>(i)] < 0.0f)) {
            ++zc;
        }
    }
    if (n <= 1 || zc < 4) {
        return 0.0;
    }
    return 0.5 * sr * static_cast<double>(zc) / static_cast<double>(end - start - 1);
}

double maxAbsDiffDb(const std::vector<float>& a, const std::vector<float>& b) {
    const int n = static_cast<int>(std::min(a.size(), b.size()));
    float d = 0.0f;
    for (int i = 0; i < n; ++i) {
        d = std::max(d, std::abs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]));
    }
    if (d <= 1e-12f) {
        return -120.0;
    }
    return 20.0 * std::log10(static_cast<double>(d));
}

std::vector<float> renderProcessor(SyrinxAudioProcessor& proc, double sr, int block, int n,
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

std::filesystem::path pluginRenderBin() {
    return std::filesystem::path(CUICUI_BINARY_DIR) / "bin" / "plugin_render";
}

std::filesystem::path birdRenderBin() {
    return std::filesystem::path(CUICUI_BINARY_DIR) / "bin" / "bird_render";
}

} // namespace

TEST_CASE("SafetyLimiter is bit-transparent below -1 dBFS and never exceeds 0 dBFS") {
    const float t = SafetyLimiter::kThreshold;
    for (float x : {-t, -t * 0.5f, 0.0f, t * 0.25f, t}) {
        REQUIRE(SafetyLimiter::processSample(x) == x);
    }
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> dist(-8.0f, 8.0f);
    for (int i = 0; i < 10000; ++i) {
        const float y = SafetyLimiter::processSample(dist(rng));
        REQUIRE(std::isfinite(y));
        REQUIRE(std::abs(y) <= 1.0f + 1e-6f);
    }
}

TEST_CASE("note-on produces sound, note-off dies, latency matches BirdVoice") {
    SyrinxAudioProcessor proc;
    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    proc.setParamPlain("out.master_db", 0.0f);
    proc.setParamPlain("out.gain_db", 0.0f);
    proc.setParamPlain("noise.aspiration", 0.0f);
    proc.setParamPlain("noise.beta_sigma", 0.0f);

    const double sr = 48000.0;
    const int block = 256;
    proc.prepareToPlay(sr, block);
    REQUIRE(proc.getLatencySamples() == proc.voiceLatencySamples());

    cuicui::ProcessSpec spec;
    spec.hostSampleRate = sr;
    spec.maxBlockSize = 32;
    spec.oversampling = 0;
    cuicui::BirdVoice v;
    v.prepare(spec);
    REQUIRE(proc.getLatencySamples() == v.latencySamples());

    const int nOn = static_cast<int>(0.05 * sr);
    juce::AudioBuffer<float> buf(2, block);
    int produced = 0;
    bool heard = false;
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 0);
        buf.setSize(2, block, false, false, true);
        buf.clear();
        proc.processBlock(buf, midi);
        produced += block;
        if (peakAbs(buf.getReadPointer(0), block) > 1e-4f) {
            heard = true;
        }
    }
    while (produced < nOn && !heard) {
        juce::MidiBuffer midi;
        buf.setSize(2, block, false, false, true);
        buf.clear();
        proc.processBlock(buf, midi);
        produced += block;
        if (peakAbs(buf.getReadPointer(0), block) > 1e-4f) {
            heard = true;
        }
    }
    REQUIRE(heard);

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
    buf.clear();
    proc.processBlock(buf, off);

    const int nOff = static_cast<int>(0.5 * sr);
    int done = 0;
    float lastPeak = 1.0f;
    while (done < nOff) {
        juce::MidiBuffer midi;
        buf.setSize(2, block, false, false, true);
        buf.clear();
        proc.processBlock(buf, midi);
        done += block;
        lastPeak = peakAbs(buf.getReadPointer(0), block);
    }
    REQUIRE(20.0 * std::log10(static_cast<double>(std::max(lastPeak, 1e-12f))) < -60.0);
    proc.releaseResources();
}

TEST_CASE("physiology MIDI: C and D differ, octave is near 2x") {
    auto hold = [](int note) {
        SyrinxAudioProcessor proc;
        useAnatomicalVoice(proc);
        proc.setNonRealtime(true);
        proc.setParamPlain("perf.mode", 1.0f); // physiology: C1 empirical map
        proc.setParamPlain("out.master_db", 0.0f);
        proc.setParamPlain("out.gain_db", -6.0f);
        proc.setParamPlain("noise.aspiration", 0.0f);
        proc.setParamPlain("noise.beta_sigma", 0.0f);
        const double sr = 48000.0;
        const int n = static_cast<int>(0.35 * sr);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        ev.push_back({0, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100))});
        return renderProcessor(proc, sr, 256, n, ev);
    };
    const auto c4 = hold(60);
    const auto d4 = hold(62);
    const auto c5 = hold(72);
    const int skip = static_cast<int>(0.08 * 48000);
    const int win = static_cast<int>(0.2 * 48000);
    const double fC = zcF0Hz(c4, 48000.0, skip, win);
    const double fD = zcF0Hz(d4, 48000.0, skip, win);
    const double fOct = zcF0Hz(c5, 48000.0, skip, win);
    REQUIRE(fC > 200.0);
    REQUIRE(fD / fC > 1.08);
    REQUIRE(fOct / fC > 1.6);
    REQUIRE(fOct / fC < 2.3);
}

TEST_CASE("state round trip of 10 random parameters") {
    SyrinxAudioProcessor a;
    useAnatomicalVoice(a);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> pick(0, cuicui::kParamCount - 1);
    std::vector<cuicui::ParamId> ids;
    std::vector<float> values;
    for (int n = 0; n < 10; ++n) {
        const auto& spec = cuicui::kParamTable[static_cast<std::size_t>(pick(rng))];
        if (std::string_view(spec.id) == "engine.oversampling" || std::string_view(spec.id) == "perf.mode" ||
            std::string_view(spec.id) == "perf.scale") {
            continue;
        }
        std::uniform_real_distribution<float> dist(static_cast<float>(spec.min), static_cast<float>(spec.max));
        float v = dist(rng);
        if (std::string_view(spec.id) == "engine.seed") {
            v = 12345.0f;
        }
        REQUIRE(a.setParamPlain(spec.id, v));
        ids.push_back(spec.pid);
        values.push_back(v);
    }
    juce::MemoryBlock mb;
    a.getStateInformation(mb);
    REQUIRE(mb.getSize() > 0);

    SyrinxAudioProcessor b;

    useAnatomicalVoice(b);
    b.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto* pa = a.apvts.getParameter(cuicui::specOf(ids[i]).id);
        auto* pb = b.apvts.getParameter(cuicui::specOf(ids[i]).id);
        REQUIRE(pa != nullptr);
        REQUIRE(pb != nullptr);
        REQUIRE_THAT(pb->getValue(), WithinAbs(pa->getValue(), 1e-5f));
    }
}

TEST_CASE("zero allocations in processBlock over 10 s of MIDI and param changes") {
    SyrinxAudioProcessor proc;
    useAnatomicalVoice(proc);
    proc.setNonRealtime(false);
    proc.setParamPlain("out.master_db", -6.0f);
    const double sr = 48000.0;
    const int block = 256;
    proc.prepareToPlay(sr, block);

    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(64);
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(90)), 0);
    buf.clear();
    (void) buf.getWritePointer(0);
    (void) buf.getWritePointer(1);
    proc.processBlock(buf, midi); // warm-up

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 1);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 9000), 2);
    buf.clear();
    proc.processBlock(buf, midi);

    const int nBlocks = static_cast<int>(10.0 * sr / block);
    midi.ensureSize(64);
    cuicui::test::reset_alloc_count();
    for (int b = 0; b < nBlocks; ++b) {
        midi.clear();
        if (b % 20 == 0) {
            midi.addEvent(juce::MidiMessage::noteOff(1, 60 + ((b / 20) % 12)), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60 + (((b / 20) + 1) % 12), static_cast<juce::uint8>(80)), 4);
        }
        if (b % 7 == 0) {
            proc.setParamPlain("oec.freq_hz", 800.0f + static_cast<float>(b % 40) * 50.0f);
        }
        buf.clear();
        const int before = cuicui::test::alloc_count();
        proc.processBlock(buf, midi);
        REQUIRE(cuicui::test::alloc_count() == before);
    }
    proc.releaseResources();
}

TEST_CASE("zero allocations in generative processBlock (leash 1)") {
    SyrinxAudioProcessor proc;
    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    proc.setParamPlain("out.master_db", -6.0f);
    proc.setParamPlain("perf.leash", 1.0f);
    proc.setParamPlain("perf.seed", 5.0f);
    proc.setParamPlain("perf.density", 0.6f);
    const double sr = 48000.0;
    const int block = 256;
    proc.prepareToPlay(sr, block);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(64);
    midi.addEvent(juce::MidiMessage::noteOn(1, 96, static_cast<juce::uint8>(100)), 0);
    buf.clear();
    proc.processBlock(buf, midi);
    midi.clear();
    const int nBlocks = static_cast<int>(60.0 * sr / block);
    for (int b = 0; b < nBlocks; ++b) {
        buf.clear();
        const int before = cuicui::test::alloc_count();
        proc.processBlock(buf, midi);
        REQUIRE(cuicui::test::alloc_count() == before);
    }
    proc.releaseResources();
}

TEST_CASE("limiter fuzz: output never exceeds 0 dBFS") {
    SyrinxAudioProcessor proc;
    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    proc.setParamPlain("out.master_db", 6.0f);
    proc.setParamPlain("out.gain_db", 12.0f);
    proc.setParamPlain("resp.pressure", 1.0f);
    proc.setParamPlain("trachea.input_gain", 4.0f);
    const double sr = 48000.0;
    const int block = 256;
    const int n = static_cast<int>(2.0 * sr);
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    ev.push_back({0, juce::MidiMessage::noteOn(1, 84, static_cast<juce::uint8>(127))});
    const auto y = renderProcessor(proc, sr, block, n, ev);
    const float p = peakAbs(y.data(), static_cast<int>(y.size()));
    REQUIRE(p <= 1.0f + 1e-5f);
}

TEST_CASE("processor matches BirdVoice render_offline within -60 dB (no MIDI)") {
    cuicui::io::RenderSpec spec;
    spec.sample_rate = 48000;
    spec.duration_s = 0.35;
    spec.seed = 1;
    spec.oversampling = "4";
    spec.params["resp.pressure"] = 0.55;
    spec.params["src.L.beta"] = 0.3;
    spec.params["src.L.gate"] = 1.0;
    spec.params["src.R.gate"] = 0.0;
    spec.params["noise.beta_sigma"] = 0.0;
    spec.params["noise.aspiration"] = 0.0;
    spec.params["out.gain_db"] = -12.0;
    spec.params["body.size"] = 1.0;
    auto bird = cuicui::io::render_offline(spec, {});

    SyrinxAudioProcessor proc;

    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    proc.setParamPlain("engine.seed", 1.0f);
    proc.setParamPlain("engine.oversampling", 4.0f);
    proc.setParamPlain("resp.pressure", 0.55f);
    proc.setParamPlain("src.L.beta", 0.3f);
    proc.setParamPlain("src.L.gate", 1.0f);
    proc.setParamPlain("src.R.gate", 0.0f);
    proc.setParamPlain("noise.beta_sigma", 0.0f);
    proc.setParamPlain("noise.aspiration", 0.0f);
    proc.setParamPlain("out.gain_db", -12.0f);
    proc.setParamPlain("out.master_db", 0.0f);
    proc.setParamPlain("body.size", 1.0f);
    proc.setParamPlain("perf.mode", 1.0f); // host params drive the voice (C1 path)
    const int n = static_cast<int>(bird.size());
    auto plug = renderProcessor(proc, 48000.0, 256, n, {});
    const double db = maxAbsDiffDb(bird, plug);
    REQUIRE(db < -60.0);
}

TEST_CASE("block-size robustness 32/256/1024") {
    auto run = [](int block) {
        SyrinxAudioProcessor proc;
        useAnatomicalVoice(proc);
        proc.setNonRealtime(true);
        proc.setParamPlain("out.master_db", 0.0f);
        proc.setParamPlain("out.gain_db", -6.0f);
        proc.setParamPlain("noise.aspiration", 0.0f);
        const double sr = 48000.0;
        const int n = static_cast<int>(0.6 * sr);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        ev.push_back({0, juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100))});
        ev.push_back({static_cast<int>(0.4 * sr), juce::MidiMessage::noteOff(1, 72)});
        return renderProcessor(proc, sr, block, n, ev);
    };
    const auto a = run(32);
    const auto b = run(256);
    const auto c = run(1024);
    REQUIRE(maxAbsDiffDb(a, b) < -60.0);
    REQUIRE(maxAbsDiffDb(a, c) < -60.0);
}

TEST_CASE("CPU at 48 kHz / 256 <= 1.5x BirdVoice in-process") {
    const double sr = 48000.0;
    const int block = 256;
    const double audioS = 4.0;
    const int nBlocks = static_cast<int>(audioS * sr / block);

    cuicui::ProcessSpec spec;
    spec.hostSampleRate = sr;
    spec.maxBlockSize = block;
    spec.oversampling = 0;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings st;
    st.out_gain_db = -6.0;
    voice.setSettings(st);
    voice.reset(1);
    voice.primeLabial(cuicui::model::kAlphaRest, 0.3);
    voice.setOutGainRamp(-6.0f, -6.0f);
    std::vector<float> mono(static_cast<std::size_t>(block), 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.55f;
    from.src_L_beta = to.src_L_beta = 0.3f;
    from.src_L_gate = to.src_L_gate = 1.0f;
    for (int i = 0; i < 8; ++i) {
        voice.process(from, to, mono.data(), block);
    }
    const auto tVoice0 = std::chrono::steady_clock::now();
    for (int i = 0; i < nBlocks; ++i) {
        voice.process(from, to, mono.data(), block);
    }
    const auto tVoice1 = std::chrono::steady_clock::now();
    const double voicePct = std::chrono::duration<double>(tVoice1 - tVoice0).count() / audioS * 100.0;

    SyrinxAudioProcessor proc;

    useAnatomicalVoice(proc);
    proc.setNonRealtime(true);
    proc.setParamPlain("out.master_db", -6.0f);
    proc.prepareToPlay(sr, block);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 0);
    buf.clear();
    proc.processBlock(buf, on);
    juce::MidiBuffer empty;
    for (int i = 0; i < 8; ++i) {
        buf.clear();
        empty.clear();
        proc.processBlock(buf, empty);
    }
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < nBlocks; ++i) {
        buf.clear();
        empty.clear();
        proc.processBlock(buf, empty);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double corePct = std::chrono::duration<double>(t1 - t0).count() / audioS * 100.0;
    const double budget = voicePct * 1.5;
    UNSCOPED_INFO("voice core% = " << voicePct << " plugin core% = " << corePct << " budget " << budget);
    REQUIRE(corePct <= budget);
    proc.releaseResources();
}

TEST_CASE("plugin_render is deterministic") {
    const auto bin = pluginRenderBin();
    if (!std::filesystem::exists(bin)) {
        SKIP("plugin_render not built at " + bin.string());
    }
    const auto dir = std::filesystem::temp_directory_path() / "syrinx_c1";
    std::filesystem::create_directories(dir);
    const auto a = dir / "a.wav";
    const auto b = dir / "b.wav";
    const std::string notes = "72:0.0:0.4:100";
    auto run = [&](const std::filesystem::path& out) {
        const std::string cmd = bin.string() + " --notes \"" + notes +
                                "\" --sr 48000 --block 256 --tail 0.3 --seed 1 --duration 0.7 -o " +
                                out.string();
        REQUIRE(std::system(cmd.c_str()) == 0);
    };
    run(a);
    run(b);
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    std::vector<char> ba((std::istreambuf_iterator<char>(fa)), {});
    std::vector<char> bb((std::istreambuf_iterator<char>(fb)), {});
    REQUIRE(ba == bb);
}
