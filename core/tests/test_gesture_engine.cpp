#include "cuicui/control/gesture_engine.hpp"
#include "cuicui/control/voice_controller.hpp"
#include "cuicui/model/bird_voice.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

void runPhrase(cuicui::control::GestureEngine& eng, const cuicui::control::GestureEngineContext& cx,
               double dt, int steps, double anchor = 9600.0, std::uint32_t seed = 7) {
    eng.prepare();
    eng.setContext(cx);
    eng.noteOn(anchor, 100, seed);
    for (int i = 0; i < steps; ++i) {
        if (cx.tempoValid && cx.hostPlaying) {
            auto c = cx;
            c.ppq = static_cast<double>(i) * dt * (cx.bpm / 60.0);
            eng.setContext(c);
        }
        (void)eng.next(dt);
    }
}

bool tracesEqual(const cuicui::control::SyllableTrace& a, const cuicui::control::SyllableTrace& b) {
    if (a.nGestures != b.nGestures) {
        return false;
    }
    for (int i = 0; i < a.nGestures; ++i) {
        if (a.types[i] != b.types[i]) {
            return false;
        }
        for (int j = 0; j < 8; ++j) {
            if (std::abs(a.params[i][j] - b.params[i][j]) > 1.0e-12) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("GestureEngine: seed != 0 is deterministic") {
    cuicui::control::GestureEngineContext cx;
    cx.variation = 1.0;
    cx.density = 0.5;
    const double dt = 1.0 / 1500.0;
    const int steps = 8000;
    cuicui::control::GestureEngine a, b;
    runPhrase(a, cx, dt, steps, 9600.0, 42);
    runPhrase(b, cx, dt, steps, 9600.0, 42);
    REQUIRE(a.traceCount() == b.traceCount());
    REQUIRE(a.traceCount() > 4);
    for (int i = 0; i < a.traceCount(); ++i) {
        REQUIRE(tracesEqual(a.trace(i), b.trace(i)));
        REQUIRE_THAT(a.trace(i).onsetSec, WithinAbs(b.trace(i).onsetSec, 1.0e-12));
    }
}

TEST_CASE("GestureEngine: seed 0 path differs across note-ons (hashed counter)") {
    cuicui::control::GestureEngine eng;
    cuicui::control::GestureEngineContext cx;
    eng.prepare();
    eng.setContext(cx);
    const double dt = 1.0 / 1500.0;
    eng.noteOn(9600.0, 100, cuicui::control::hashPhraseSeed(0, 96, 1));
    for (int i = 0; i < 4000; ++i) {
        (void)eng.next(dt);
    }
    std::vector<int> typesA;
    for (int i = 0; i < eng.traceCount(); ++i) {
        typesA.push_back(eng.trace(i).types[0]);
    }
    eng.noteOn(9600.0, 100, cuicui::control::hashPhraseSeed(0, 96, 2));
    for (int i = 0; i < 4000; ++i) {
        (void)eng.next(dt);
    }
    std::vector<int> typesB;
    for (int i = 0; i < eng.traceCount(); ++i) {
        typesB.push_back(eng.trace(i).types[0]);
    }
    REQUIRE(typesA.size() >= 2);
    REQUIRE(typesB.size() >= 2);
    REQUIRE_FALSE(typesA == typesB);
}

TEST_CASE("GestureEngine: density syllables/s increase monotonically") {
    const double dt = 1.0 / 1500.0;
    const int steps = static_cast<int>(30.0 / dt);
    const double dens[] = {0.2, 0.5, 0.8};
    double rate[3]{};
    for (int d = 0; d < 3; ++d) {
        cuicui::control::GestureEngine eng;
        cuicui::control::GestureEngineContext cx;
        cx.density = dens[d];
        cx.variation = 0.0;
        cx.motion = 0.3;
        runPhrase(eng, cx, dt, steps, 9600.0, 11);
        rate[d] = static_cast<double>(eng.syllableCount()) / 30.0;
        WARN("density " << dens[d] << " rate " << rate[d] << " n " << eng.syllableCount());
    }
    REQUIRE(rate[0] < rate[1]);
    REQUIRE(rate[1] < rate[2]);
    REQUIRE(rate[0] > 0.2);
}

TEST_CASE("GestureEngine: Speed 2 median syllable duration is 0.5x ± 15%") {
    const double dt = 1.0 / 1500.0;
    const int steps = static_cast<int>(12.0 / dt);
    auto medianDur = [&](double speed) {
        cuicui::control::GestureEngine eng;
        cuicui::control::GestureEngineContext cx;
        cx.speed = speed;
        cx.variation = 0.0;
        cx.density = 0.6;
        runPhrase(eng, cx, dt, steps, 9600.0, 3);
        std::vector<double> durs;
        for (int i = 0; i < eng.traceCount(); ++i) {
            durs.push_back(eng.trace(i).durationMs);
        }
        REQUIRE(durs.size() > 4);
        std::sort(durs.begin(), durs.end());
        return durs[durs.size() / 2];
    };
    const double d1 = medianDur(1.0);
    const double d2 = medianDur(2.0);
    const double ratio = d2 / d1;
    WARN("speed d1 " << d1 << " d2 " << d2 << " ratio " << ratio);
    REQUIRE_THAT(ratio, WithinAbs(0.5, 0.15 * 0.5 + 1e-6)); // 0.5 ± 15% → [0.425, 0.575]
    REQUIRE(ratio >= 0.425);
    REQUIRE(ratio <= 0.575);
}

TEST_CASE("GestureEngine: Variation 0 repeats motif parameters exactly") {
    cuicui::control::GestureEngine eng;
    cuicui::control::GestureEngineContext cx;
    cx.variation = 0.0;
    cx.density = 0.7;
    const double dt = 1.0 / 1500.0;
    runPhrase(eng, cx, dt, static_cast<int>(8.0 / dt), 9600.0, 5);
    REQUIRE(eng.motifLength() >= 2);
    int compared = 0;
    for (int i = eng.motifLength(); i < eng.traceCount(); ++i) {
        const auto& t = eng.trace(i);
        REQUIRE(t.fromMotif);
        const auto& m = eng.trace(i % eng.motifLength());
        REQUIRE(tracesEqual(t, m));
        ++compared;
    }
    REQUIRE(compared >= 4);
}

TEST_CASE("GestureEngine: Variation 1 motif-repeat proportion is 0.6 ± 0.1") {
    cuicui::control::GestureEngine eng;
    cuicui::control::GestureEngineContext cx;
    cx.variation = 1.0;
    cx.density = 0.75;
    const double dt = 1.0 / 1500.0;
    runPhrase(eng, cx, dt, static_cast<int>(40.0 / dt), 9600.0, 9);
    REQUIRE(eng.traceCount() > eng.motifLength() + 10);
    int rep = 0;
    int tot = 0;
    for (int i = eng.motifLength(); i < eng.traceCount(); ++i) {
        ++tot;
        if (eng.trace(i).fromMotif) {
            ++rep;
        }
    }
    const double p = static_cast<double>(rep) / static_cast<double>(tot);
    WARN("variation1 p " << p << " rep " << rep << " tot " << tot);
    REQUIRE_THAT(p, WithinAbs(0.6, 0.1));
}

TEST_CASE("GestureEngine: tempo sync 120 BPM 1/16 onsets within ±2 ms") {
    cuicui::control::GestureEngine eng;
    cuicui::control::GestureEngineContext cx;
    cx.tempoSync = true;
    cx.tempoValid = true;
    cx.hostPlaying = true;
    cx.bpm = 120.0;
    cx.syncDivision = 2; // 1/16
    cx.density = 0.9;
    cx.variation = 0.0;
    const double dt = 1.0 / 1500.0;
    const double gridSec = 0.125; // 1/16 at 120 BPM = 0.125 s?  quarter=0.5s, 1/16=0.125s. Yes.
    // Wait: 120 BPM → quarter = 0.5 s. 1/16 = 0.5/4 = 0.125 s. Yes.
    eng.prepare();
    eng.setContext(cx);
    eng.noteOn(9600.0, 100, 13);
    const int steps = static_cast<int>(8.0 / dt);
    for (int i = 0; i < steps; ++i) {
        cx.ppq = static_cast<double>(i) * dt * (120.0 / 60.0);
        eng.setContext(cx);
        (void)eng.next(dt);
    }
    REQUIRE(eng.traceCount() >= 8);
    int checked = 0;
    for (int i = 1; i < eng.traceCount(); ++i) {
        const double t = eng.trace(i).onsetSec;
        const double q = std::round(t / gridSec) * gridSec;
        const double errMs = std::abs(t - q) * 1000.0;
        INFO("onset " << t << " err_ms " << errMs);
        REQUIRE(errMs <= 2.0);
        ++checked;
    }
    REQUIRE(checked >= 6);
}

TEST_CASE("GestureEngine: freeze replays motif types for the next 10 syllables") {
    cuicui::control::GestureEngine eng;
    cuicui::control::GestureEngineContext cx;
    cx.variation = 0.4;
    cx.density = 0.8;
    const double dt = 1.0 / 1500.0;
    eng.prepare();
    eng.setContext(cx);
    eng.noteOn(9600.0, 100, 21);
    for (int i = 0; i < static_cast<int>(3.0 / dt); ++i) {
        (void)eng.next(dt);
    }
    const int motifN = eng.motifLength();
    REQUIRE(motifN >= 2);
    std::vector<int> motifTypes;
    for (int i = 0; i < motifN; ++i) {
        motifTypes.push_back(eng.trace(i).types[0]);
    }
    cx.freeze = true;
    eng.setContext(cx);
    const int before = eng.traceCount();
    for (int i = 0; i < static_cast<int>(10.0 / dt); ++i) {
        (void)eng.next(dt);
    }
    REQUIRE(eng.traceCount() >= before + 10);
    bool matched = false;
    for (int off = 0; off < motifN; ++off) {
        bool ok = true;
        for (int k = 0; k < 10; ++k) {
            const auto& t = eng.trace(before + k);
            if (!t.fromMotif || t.types[0] != motifTypes[static_cast<std::size_t>((off + k) % motifN)]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            matched = true;
            break;
        }
    }
    REQUIRE(matched);
}

TEST_CASE("GestureEngine: syllable generation < 0.2 ms") {
    cuicui::control::GestureEngine eng;
    eng.prepare();
    cuicui::control::GestureEngineContext cx;
    eng.setContext(cx);
    eng.noteOn(9600.0, 100, 1);
    (void)eng.next(1.0 / 1500.0);
    double worst = 0.0;
    for (int i = 0; i < 400; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        eng.benchmarkGenerate();
        const auto t1 = std::chrono::steady_clock::now();
        worst = std::max(worst, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    WARN("syllable generate worst_ms " << worst);
    REQUIRE(worst < 0.2);
}

TEST_CASE("VoiceController leash=0 with engine is bit-identical to NullGestureSource") {
    cuicui::control::InverseMap map;
    cuicui::ProcessSpec spec;
    spec.hostSampleRate = 48000.0;
    spec.maxBlockSize = 32;
    spec.oversampling = 4;
    auto render = [&](cuicui::control::GestureSource* src) {
        cuicui::control::VoiceController vc;
        cuicui::BirdVoice voice;
        voice.prepare(spec);
        voice.setSource(cuicui::VoiceSource::Labial);
        voice.reset(1);
        voice.primeLabial(-0.1, 0.3);
        vc.prepare(48000.0 / 32.0, map);
        vc.reset();
        vc.setGestureSource(src);
        cuicui::VoiceSettings st;
        st.seed = 1;
        voice.setSettings(st);
        const int n = 48000 / 4;
        std::vector<float> y(static_cast<std::size_t>(n), 0.0f);
        cuicui::ControlFrame last{};
        cuicui::control::PerformInputs in;
        in.gate = true;
        in.noteCents = 9600.0;
        in.velocity01 = 0.8;
        in.breath = 0.7;
        in.tone = 0.6;
        in.leash = 0.0;
        in.seed = 1;
        in.noteOnEvent = true;
        int pos = 0;
        while (pos < n) {
            const int m = std::min(32, n - pos);
            cuicui::ControlFrame to{};
            vc.tick(in, st, to);
            in.noteOnEvent = false;
            voice.process(last, to, y.data() + pos, m);
            last = to;
            pos += m;
        }
        return y;
    };
    cuicui::control::NullGestureSource null;
    cuicui::control::GestureEngine eng;
    eng.prepare();
    const auto a = render(&null);
    const auto b = render(&eng);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i] == b[i]);
    }
}
