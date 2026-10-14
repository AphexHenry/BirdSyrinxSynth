#include "alloc_counter.hpp"
#include "cuicui/control/variation.hpp"
#include "cuicui/control/archetypes.hpp"
#include "cuicui/control/gesture_engine.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/score_player.hpp"
#include "cuicui/control/syllable_engine.hpp"
#include "cuicui/control/voice_controller.hpp"
#include "cuicui/io/gesture_score.hpp"
#include "cuicui/io/syllable_bank_io.hpp"
#include "cuicui/model/bird_voice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
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

TEST_CASE("BirdVoice two-source process allocates nothing after prepare") {
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 128;
    spec.oversampling = 4;
    cuicui::BirdVoice voice;
    voice.prepare(spec);
    voice.reset(1);
    voice.setSource(cuicui::VoiceSource::Labial);
    voice.primeLabial(-0.1, 0.3, 3000.0, 0.5, 0.0, 0.5);
    cuicui::VoiceSettings s;
    s.src_gamma = 24000.0;
    s.src_coupling = 0.05;
    voice.setSettings(s);
    voice.setOutGainRamp(0.0f, 0.0f);

    std::vector<float> out(128, 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = 0.4f;
    to.resp_pressure = 0.5f;
    from.src_L_beta = 0.3f;
    to.src_L_beta = 0.35f;
    from.src_R_beta = 0.5f;
    to.src_R_beta = 0.55f;
    from.src_L_gate = 1.0f;
    to.src_L_gate = 0.7f;
    cuicui::test::reset_alloc_count();
    voice.process(from, to, out.data(), 128);
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("BirdVoice process with body.size changes allocates nothing") {
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
    s.body_size = 1.0;
    voice.setSettings(s);
    voice.setOutGainRamp(0.0f, 0.0f);

    std::vector<float> out(128, 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.5f;
    from.src_L_beta = to.src_L_beta = 0.3f;
    from.src_L_gate = to.src_L_gate = 1.0f;
    voice.process(from, to, out.data(), 128);

    cuicui::test::reset_alloc_count();
    s.body_size = 2.0;
    voice.setSettings(s);
    voice.process(from, to, out.data(), 128);
    s.body_size = 0.5;
    voice.setSettings(s);
    voice.process(from, to, out.data(), 128);
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("VoiceController::tick allocates nothing") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(48000.0 / 32.0, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    cuicui::control::PerformInputs in;
    in.gate = true;
    in.noteCents = 9600.0;
    in.velocity01 = 0.8;
    vc.tick(in, st, fr);
    cuicui::test::reset_alloc_count();
    in.noteOnEvent = false;
    for (int i = 0; i < 256; ++i) {
        vc.tick(in, st, fr);
    }
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("InverseMap::solve allocates nothing") {
    cuicui::control::InverseMap map;
    (void)map.solve(0.5, 0.6, 0.2, 0.0, true);
    cuicui::test::reset_alloc_count();
    for (int i = 0; i < 1000; ++i) {
        (void)map.solve(0.4 + 0.0001 * static_cast<double>(i), 0.6, 0.2, 0.0, true);
    }
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("ScorePlayer::process allocates nothing after prepare") {
    auto inst = cuicui::control::generateArchetype("chip", cuicui::control::ArchetypeParamBag{}, 1);
    cuicui::io::ScoreGestureEvent e;
    e.t_s = 0.0;
    e.duration_ms = inst.durationMs;
    e.type = "archetype:chip";
    e.anchor_hz = 2093.0;
    e.curves = inst.curves;
    e.hints = inst.hints;
    e.has_curves = true;
    cuicui::io::GestureScore s;
    s.settings["body.size"] = 1.0;
    s.gestures.push_back(e);
    cuicui::control::ScorePlayer player;
    player.load(s);
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 128;
    spec.oversampling = 4;
    player.prepare(spec);
    std::vector<float> buf(64, 0.0f);
    player.process(buf.data(), 64);
    cuicui::test::reset_alloc_count();
    player.process(buf.data(), 64);
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("GestureEngine::next allocates nothing after noteOn") {
    cuicui::control::GestureEngine eng;
    eng.prepare();
    cuicui::control::GestureEngineContext cx;
    cx.variation = 1.0;
    cx.density = 0.7;
    eng.setContext(cx);
    eng.noteOn(9600.0, 100, 11);
    const double dt = 1.0 / 1500.0;
    for (int i = 0; i < 64; ++i) {
        (void)eng.next(dt);
    }
    cuicui::test::reset_alloc_count();
    for (int i = 0; i < 1500 * 8; ++i) {
        (void)eng.next(dt);
    }
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("VoiceController+GestureEngine tick allocates nothing") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    cuicui::control::GestureEngine eng;
    eng.prepare();
    vc.prepare(48000.0 / 32.0, map);
    vc.setGestureSource(&eng);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    cuicui::control::PerformInputs in;
    in.gate = true;
    in.noteCents = 9600.0;
    in.velocity01 = 0.8;
    in.leash = 1.0;
    in.variation = 1.0;
    in.phraseSeed = 7;
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    cuicui::test::reset_alloc_count();
    for (int i = 0; i < 4096; ++i) {
        vc.tick(in, st, fr);
    }
    REQUIRE(cuicui::test::alloc_count() == 0);
}

TEST_CASE("SyllableEngine::process allocates nothing over 60 s of dense playing (WP-R1)") {
    const auto bank = cuicui::io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) +
                                                   "/assets/voicebanks/zf_22938/bank.json");
    cuicui::control::SyllableEngine eng;
    eng.prepare(48000.0);
    eng.setBank(&bank);
    eng.reset(9);
    cuicui::control::WhistleSettings s;
    s.play.polyphony = 6;
    std::vector<float> l(256), r(256);
    cuicui::dsp::Rng rng(5);
    std::vector<cuicui::control::PlayEvent> ev;
    ev.reserve(16);
    int held[6] = {-1, -1, -1, -1, -1, -1};
    cuicui::test::reset_alloc_count();
    for (int blk = 0; blk < 60 * 48000 / 256; ++blk) {
        ev.clear();
        if (rng.uniform() < 0.08f) { // a key change every ~70 ms
            const int k = static_cast<int>(rng.uniform() * 6.0f);
            cuicui::control::PlayEvent e;
            e.offset = static_cast<int>(rng.uniform() * 255.0f);
            if (held[k] >= 0) {
                e.type = cuicui::control::PlayEvent::Type::NoteOff;
                e.note = held[k];
                held[k] = -1;
            } else {
                e.type = cuicui::control::PlayEvent::Type::NoteOn;
                e.note = 48 + static_cast<int>(rng.uniform() * 40.0f);
                e.value = rng.uniform();
                held[k] = e.note;
            }
            ev.push_back(e);
        }
        if (blk % 997 == 0) {
            cuicui::control::PlayEvent w;
            w.type = cuicui::control::PlayEvent::Type::ModWheel;
            w.value = rng.uniform();
            ev.push_back(w);
            s.play.order = static_cast<cuicui::control::OrderMode>(blk % 3);
            s.room = (blk / 997) % 4 != 3;
            s.harmonics = (blk / 997) % 2 == 0;
        }
        eng.process(ev.data(), static_cast<int>(ev.size()), 256, s, l.data(), r.data());
    }
    REQUIRE(cuicui::test::alloc_count() == 0);
    REQUIRE(eng.samplesProcessed() == static_cast<long long>(60 * 48000 / 256) * 256);
}

TEST_CASE("SyllableEngine allocates nothing while singing new renditions (merged birds)") {
    const auto a = cuicui::io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/hf_22938/bank.json");
    const auto b = cuicui::io::loadSyllableBank(std::string(CUICUI_SOURCE_DIR) + "/assets/voicebanks/hf_22937/bank.json");
    const auto bank = cuicui::control::mergeBanks({&a, &b});
    cuicui::control::SyllableEngine eng;
    eng.prepare(48000.0);
    eng.setBank(&bank);
    eng.reset(3);
    cuicui::control::WhistleSettings s;
    s.play.variation = 1.0;
    s.play.polyphony = 6;
    std::vector<float> l(256), r(256);
    std::vector<cuicui::control::PlayEvent> ev(2);
    cuicui::test::reset_alloc_count();
    int on = 0;
    for (int blk = 0; blk < 20 * 48000 / 256; ++blk) {
        int ne = 0;
        if (blk % 40 == 0) {
            ev[0] = cuicui::control::PlayEvent{cuicui::control::PlayEvent::Type::NoteOn, 7, 50 + (blk / 40) % 30, 0.8f};
            on = ev[0].note;
            ne = 1;
        } else if (blk % 40 == 30) {
            ev[0] = cuicui::control::PlayEvent{cuicui::control::PlayEvent::Type::NoteOff, 3, on, 0.0f};
            ne = 1;
        }
        eng.process(ev.data(), ne, 256, s, l.data(), r.data());
    }
    CHECK(cuicui::test::alloc_count() == 0);
}
