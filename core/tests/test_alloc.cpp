#include "alloc_counter.hpp"
#include "cuicui/model/bird_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("BirdVoice::process allocates nothing after prepare") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 128;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.reset(1);
    voice.setTestWaveform(cuicui::TestWaveform::Sine);
    voice.setTestRamp(440.0f, 880.0f, -12.0f, -6.0f);
    voice.setOutGainRamp(0.0f, -3.0f);

    std::vector<float> out(128, 0.0f);
    cuicui::ControlFrame from, to;
    // Warm-up so any first-call lazy paths are done (should already be in prepare).
    voice.process(from, to, out.data(), 128);

    cuicui::test::reset_alloc_count();
    voice.process(from, to, out.data(), 128);
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("BirdVoice labial process allocates nothing after prepare") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 128;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.reset(1);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.primeLabial(-0.1, 0.3);
    cuicui::VoiceSettings s;
    s.src_gamma = 24000.0;
    voice.setSettings(s);
    voice.setOutGainRamp(0.0f, 0.0f);

    std::vector<float> out(128, 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = 0.4f;
    to.resp_pressure = 0.5f;
    from.src_L_beta = 0.3f;
    to.src_L_beta = 0.35f;
    voice.process(from, to, out.data(), 128);

    cuicui::test::reset_alloc_count();
    voice.process(from, to, out.data(), 128);
    REQUIRE(cuicui::test::alloc_count() == 0);
}
