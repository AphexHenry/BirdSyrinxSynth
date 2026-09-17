#include "cuicui/control/gesture_source.hpp"
#include "cuicui/control/inverse_map.hpp"
#include "cuicui/control/scales.hpp"
#include "cuicui/control/snap.hpp"
#include "cuicui/control/voice_controller.hpp"
#include "cuicui/model/labial_oscillator.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kCtrlRate = 48000.0 / 32.0;

cuicui::control::PerformInputs held(double noteCents, double tone = 0.6) {
    cuicui::control::PerformInputs in;
    in.gate = true;
    in.noteCents = noteCents;
    in.velocity01 = 100.0 / 127.0;
    in.breath = 0.7;
    in.tone = tone;
    in.physics = 0.0;
    in.motion = 0.0;
    in.range = 1.0;
    in.speed = 1.0;
    in.snap = 0.0;
    in.glideMs = 0.0;
    in.size = 1.0;
    in.rangeFold = true;
    in.leash = 0.0;
    in.seed = 1;
    in.oecTracking = 0.7;
    in.gapeTracking = 0.15;
    return in;
}

void runTicks(cuicui::control::VoiceController& vc, cuicui::control::PerformInputs in,
              const cuicui::VoiceSettings& st, cuicui::ControlFrame& fr, int n,
              bool noteOnFirst = true) {
    const float oec = fr.oec_freq_hz;
    const float gape = fr.beak_gape;
    const float sig = fr.noise_beta_sigma;
    for (int i = 0; i < n; ++i) {
        in.noteOnEvent = noteOnFirst && i == 0;
        fr.oec_freq_hz = oec;
        fr.beak_gape = gape;
        fr.noise_beta_sigma = sig;
        vc.tick(in, st, fr);
    }
}

class OffsetGesture final : public cuicui::control::GestureSource {
public:
    explicit OffsetGesture(double offsetCents) : offset_(offsetCents) {}
    void noteOn(double anchorCents, int, std::uint32_t) noexcept override { anchor_ = anchorCents; }
    void noteOff() noexcept override {}
    cuicui::control::GestureSample next(double) noexcept override {
        cuicui::control::GestureSample g;
        g.active = true;
        g.pitchCents = (sweeping_ ? sweepP_ : anchor_) + offset_;
        g.ampDb = 0.0;
        g.tone = 0.6;
        return g;
    }
    void setSweep(double p) noexcept {
        sweeping_ = true;
        sweepP_ = p;
    }

private:
    double offset_ = 0.0;
    double anchor_ = 9600.0;
    double sweepP_ = 9600.0;
    bool sweeping_ = false;
};

} // namespace

TEST_CASE("quantizeCents chromatic and major") {
    REQUIRE_THAT(cuicui::control::quantizeCents(9637.0, 0, 0), WithinAbs(9600.0, 1e-9));
    REQUIRE_THAT(cuicui::control::quantizeCents(9660.0, 0, 0), WithinAbs(9700.0, 1e-9));
    // C major: C7=96 is allowed; C#=97 snaps to C or D.
    const double q = cuicui::control::quantizeCents(9700.0, 1, 0);
    REQUIRE((std::abs(q - 9600.0) < 1e-9 || std::abs(q - 9800.0) < 1e-9));
}

TEST_CASE("VoiceController: C7 at tone 0.6 is concert pitch") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0);
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    for (int i = 0; i < static_cast<int>(kCtrlRate * 0.15); ++i) {
        vc.tick(in, st, fr);
    }
    const auto d = vc.debug();
    REQUIRE_THAT(d.f0Target, WithinAbs(cuicui::control::VoiceController::hzFromCents(9600.0), 1.0));
    REQUIRE(fr.src_L_gate + fr.src_R_gate > 0.5f);
    REQUIRE(fr.resp_pressure > 0.2f);
    REQUIRE(std::isfinite(fr.src_L_beta));
}

TEST_CASE("VoiceController: glide 1200 cents, 63.2% at tau") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0); // C7
    in.glideMs = 100.0;
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    const int nHold = static_cast<int>(kCtrlRate * 0.05);
    for (int i = 0; i < nHold; ++i) {
        vc.tick(in, st, fr);
    }
    in.noteCents = 10800.0; // C8, legato
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    const int nTau = static_cast<int>(std::lround(0.100 * kCtrlRate));
    for (int i = 1; i < nTau; ++i) {
        vc.tick(in, st, fr);
    }
    const double P = vc.debug().targetCents;
    const double frac = (P - 9600.0) / 1200.0;
    REQUIRE_THAT(frac, WithinAbs(1.0 - std::exp(-1.0), 0.10));
}

TEST_CASE("VoiceController: pitch bend ±200 cents") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0);
    in.motion = 0.0;
    runTicks(vc, in, st, fr, static_cast<int>(kCtrlRate * 0.08));
    const double f0 = vc.debug().f0Target;
    in.bendCents = 200.0;
    in.noteOnEvent = false;
    for (int i = 0; i < 8; ++i) {
        vc.tick(in, st, fr);
    }
    const double fUp = vc.debug().f0Target;
    REQUIRE_THAT(1200.0 * std::log2(fUp / f0), WithinAbs(200.0, 5.0));
    in.bendCents = -200.0;
    for (int i = 0; i < 8; ++i) {
        vc.tick(in, st, fr);
    }
    const double fDn = vc.debug().f0Target;
    REQUIRE_THAT(1200.0 * std::log2(fDn / f0), WithinAbs(-200.0, 5.0));
}

TEST_CASE("VoiceController: snap pulls +37 cent gesture to the note") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    OffsetGesture gest(37.0);
    vc.setGestureSource(&gest);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0);
    in.leash = 1.0;
    in.snap = 1.0;
    in.motion = 0.0;
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    for (int i = 0; i < static_cast<int>(kCtrlRate * 0.12); ++i) {
        vc.tick(in, st, fr);
    }
    REQUIRE_THAT(vc.debug().targetCents, WithinAbs(9600.0, 3.0));
}

TEST_CASE("VoiceController: snap barely affects a 1200-cent / 100 ms sweep") {
    cuicui::control::InverseMap map;
    auto sweep = [&](double snap) {
        cuicui::control::VoiceController vc;
        vc.prepare(kCtrlRate, map);
        OffsetGesture gest(0.0);
        vc.setGestureSource(&gest);
        cuicui::VoiceSettings st;
        cuicui::ControlFrame fr;
        auto in = held(9600.0);
        in.leash = 1.0;
        in.snap = snap;
        in.motion = 0.0;
        in.glideMs = 0.0;
        in.noteOnEvent = true;
        vc.tick(in, st, fr);
        in.noteOnEvent = false;
        const int n = static_cast<int>(std::lround(0.100 * kCtrlRate));
        std::vector<double> path;
        path.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(std::max(1, n - 1));
            gest.setSweep(9600.0 + 1200.0 * u);
            vc.tick(in, st, fr);
            path.push_back(vc.debug().targetCents);
        }
        return path;
    };
    const auto a = sweep(0.0);
    const auto b = sweep(1.0);
    REQUIRE(a.size() == b.size());
    double maxAbs = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        maxAbs = std::max(maxAbs, std::abs(a[i] - b[i]));
    }
    REQUIRE(maxAbs < 0.10 * 1200.0);
}

TEST_CASE("VoiceController: OEC tracking within 1%") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    const double f0 = cuicui::control::VoiceController::hzFromCents(9600.0);
    const double fRef = 1000.0;
    const double fBase = 3000.0;
    for (double k : {0.0, 0.7, 1.0}) {
        vc.reset();
        cuicui::ControlFrame fr;
        fr.oec_freq_hz = static_cast<float>(fBase);
        auto in = held(9600.0);
        in.oecTracking = k;
        in.motion = 0.0;
        runTicks(vc, in, st, fr, 32);
        const double expect = fBase * std::pow(f0 / fRef, k);
        REQUIRE_THAT(vc.debug().oecHz, WithinRel(expect, 0.01));
    }
}

TEST_CASE("VoiceController: octave jump switches side") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(8600.0); // D6, below split → L
    in.sideSwitchCents = 700.0;
    in.glideMs = 0.0;
    runTicks(vc, in, st, fr, static_cast<int>(kCtrlRate * 0.08));
    REQUIRE(vc.debug().activeSide == 0);
    in.noteCents = 9800.0; // D7, 1200 cents
    in.noteOnEvent = true;
    vc.tick(in, st, fr);
    in.noteOnEvent = false;
    for (int i = 0; i < 16; ++i) {
        vc.tick(in, st, fr);
    }
    REQUIRE(vc.debug().activeSide == 1);
    REQUIRE(fr.src_R_gate > 0.3f);
}

TEST_CASE("VoiceController: alpha_off frozen on release") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0);
    runTicks(vc, in, st, fr, static_cast<int>(kCtrlRate * 0.08));
    const float offHeld = fr.src_L_alpha_offset;
    in.gate = false;
    for (int i = 0; i < static_cast<int>(kCtrlRate * 0.04); ++i) {
        vc.tick(in, st, fr);
    }
    REQUIRE_THAT(fr.src_L_alpha_offset, WithinAbs(offHeld, 1e-6f));
    REQUIRE(fr.resp_pressure == 0.0f);
}

TEST_CASE("VoiceController: physics=1 breath swell keeps f0 target") {
    cuicui::control::InverseMap map;
    cuicui::control::VoiceController vc;
    vc.prepare(kCtrlRate, map);
    cuicui::VoiceSettings st;
    cuicui::ControlFrame fr;
    auto in = held(9600.0);
    in.physics = 1.0;
    in.breath = 0.5;
    runTicks(vc, in, st, fr, static_cast<int>(kCtrlRate * 0.1));
    const double fA = vc.debug().f0Target;
    in.breath = 1.0;
    in.noteOnEvent = false;
    for (int i = 0; i < static_cast<int>(kCtrlRate * 0.15); ++i) {
        vc.tick(in, st, fr);
    }
    const double fB = vc.debug().f0Target;
    REQUIRE_THAT(1200.0 * std::log2(fB / fA), WithinAbs(0.0, 5.0));
}

TEST_CASE("VoiceController: determinism with fixed seed") {
    cuicui::control::InverseMap map;
    auto run = [&]() {
        cuicui::control::VoiceController vc;
        vc.prepare(kCtrlRate, map);
        cuicui::VoiceSettings st;
        cuicui::ControlFrame fr;
        auto in = held(9600.0);
        in.motion = 0.8;
        in.seed = 42;
        std::vector<float> beta;
        runTicks(vc, in, st, fr, 64);
        for (int i = 0; i < 128; ++i) {
            in.noteOnEvent = false;
            vc.tick(in, st, fr);
            beta.push_back(fr.src_L_beta);
        }
        return beta;
    };
    REQUIRE(run() == run());
}
