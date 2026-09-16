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
