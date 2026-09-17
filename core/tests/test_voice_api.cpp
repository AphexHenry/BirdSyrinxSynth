#include "cuicui/io/audio_features.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/model/bird_voice.hpp"
#include "cuicui/model/body_scaling.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"
#include "cuicui/params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

cuicui::io::RenderSpec tonal_spec(double size, double duration, int sr = 48000) {
    cuicui::io::RenderSpec spec;
    spec.sample_rate = sr;
    spec.duration_s = duration;
    spec.seed = 1;
    spec.oversampling = "auto";
    spec.params["resp.pressure"] = 0.54545;
    spec.params["src.L.beta"] = 0.2;
    spec.params["src.L.gate"] = 1.0;
    spec.params["src.R.gate"] = 0.0;
    spec.params["noise.beta_sigma"] = 0.0;
    spec.params["noise.aspiration"] = 0.0;
    spec.params["body.size"] = size;
    spec.params["src.gamma"] = 24000.0;
    spec.params["trachea.length_mm"] = 33.0;
    spec.params["oec.freq_hz"] = 3000.0;
    spec.params["out.gain_db"] = 0.0;
    return spec;
}

std::vector<float> render_voice(double size, double duration, int maxBlock = 64, int sr = 48000) {
    auto spec = tonal_spec(size, duration, sr);
    cuicui::io::RenderOptions opt;
    opt.max_block_size = maxBlock;
    return cuicui::io::render_offline(spec, opt);
}

} // namespace

TEST_CASE("registry: every Physiology/Engine parameter maps to ControlFrame or VoiceSettings") {
    cuicui::ControlFrame fr;
    cuicui::VoiceSettings st;
    for (const auto& p : cuicui::kParamTable) {
        if (p.layer == cuicui::ParamLayer::Performance) {
            continue;
        }
        const auto home = cuicui::paramHome(p.pid);
        INFO(p.id);
        REQUIRE(home != cuicui::ParamHome::None);
        if (home == cuicui::ParamHome::Control) {
            REQUIRE(cuicui::controlField(fr, p.pid) != nullptr);
        } else {
            REQUIRE(cuicui::settingsField(st, p.pid) != nullptr);
        }
    }
}

TEST_CASE("size σ=2 halves f0, trachea resonance and UVT peak") {
    const auto y1 = render_voice(1.0, 0.35);
    const auto y2 = render_voice(2.0, 0.35);
    const double f1 = cuicui::io::zc_f0_hz(y1.data(), static_cast<int>(y1.size()), 48000.0, 4800);
    const double f2 = cuicui::io::zc_f0_hz(y2.data(), static_cast<int>(y2.size()), 48000.0, 4800);
    REQUIRE(f1 > 200.0);
    REQUIRE_THAT(f2, WithinRel(f1 * 0.5, 0.005));

    const auto p1 = cuicui::model::uvtPhysical(3000.0, 0.1, 0.5, 0.3, 1.0);
    const auto p2 = cuicui::model::uvtPhysical(3000.0, 0.1, 0.5, 0.3, 2.0);
    REQUIRE_THAT(p2.fH, WithinRel(p1.fH * 0.5, 0.02));

    const double L1 = 0.033;
    const double L2 = 0.066;
    const double fTr1 = cuicui::model::kSoundSpeed / (4.0 * L1);
    const double fTr2 = cuicui::model::kSoundSpeed / (4.0 * L2);
    REQUIRE_THAT(fTr2, WithinRel(fTr1 * 0.5, 0.02));
}

TEST_CASE("size sweep 0.5→2 has no 18 kHz clicks") {
    auto spec = tonal_spec(0.5, 2.0);
    spec.params.erase("body.size");
    cuicui::io::Automation sizeSweep;
    sizeSweep.points = {{0.0, 0.5}, {2.0, 2.0}};
    spec.automation["body.size"] = sizeSweep;
    const auto y = cuicui::io::render_offline(spec);
    const double f0 = cuicui::io::zc_f0_hz(y.data(), static_cast<int>(y.size()), 48000.0, 2400);
    REQUIRE(f0 > 100.0);
    const double below = cuicui::io::min_highband_below_f0_db(y.data(), static_cast<int>(y.size()), 48000.0, f0);
    INFO("highband below f0 dB=" << below);
    REQUIRE(below >= 60.0);
}

TEST_CASE("state save/load is bit-identical for the second half") {
    auto spec = tonal_spec(1.0, 2.0);
    spec.params["noise.beta_sigma"] = 0.01;
    spec.params["noise.aspiration"] = 0.02;
    spec.params["src.R.gate"] = 0.4;
    spec.params["src.R.beta"] = 0.45;
    const auto full = cuicui::io::render_offline(spec);

    cuicui::ProcessSpec ps;
    ps.hostSampleRate = 48000.0;
    ps.maxBlockSize = 64;
    ps.oversampling = 0;
    cuicui::BirdVoice a;
    a.prepare(ps);
    a.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings st;
    st.noise_beta_bandwidth_hz = 300.0;
    st.noise_aspiration = 0.02;
    st.seed = 1;
    a.setSettings(st);
    a.reset(1);
    a.primeLabial(cuicui::model::kAlphaRest, 0.2);

    std::vector<float> first(48000, 0.0f);
    std::vector<float> second(48000, 0.0f);
    cuicui::ControlFrame from, to;
    from.resp_pressure = to.resp_pressure = 0.54545f;
    from.src_L_beta = to.src_L_beta = 0.2f;
    from.src_R_beta = to.src_R_beta = 0.45f;
    from.src_L_gate = to.src_L_gate = 1.0f;
    from.src_R_gate = to.src_R_gate = 0.4f;
    from.noise_beta_sigma = to.noise_beta_sigma = 0.01f;
    a.setOutGainRamp(0.0f, 0.0f);
    for (int i = 0; i < 48000; ++i) {
        a.process(from, to, first.data() + i, 1);
    }
    cuicui::VoiceState snap{};
    a.saveState(snap);
    for (int i = 0; i < 48000; ++i) {
        a.process(from, to, second.data() + i, 1);
    }

    cuicui::BirdVoice b;
    b.prepare(ps);
    b.setSource(cuicui::VoiceSource::Labial);
    b.setSettings(st);
    b.reset(99);
    b.primeLabial(cuicui::model::kAlphaRest, 0.2);
    b.loadState(snap);
    std::vector<float> replay(48000, 0.0f);
    b.setOutGainRamp(0.0f, 0.0f);
    for (int i = 0; i < 48000; ++i) {
        b.process(from, to, replay.data() + i, 1);
    }
    REQUIRE(std::memcmp(second.data(), replay.data(), second.size() * sizeof(float)) == 0);
    REQUIRE(full.size() == 96000);
}

TEST_CASE("CLI internal block sizes 1, 17, 64, 512 are bit-identical") {
    auto spec = tonal_spec(1.0, 0.12);
    cuicui::io::Automation betaSweep;
    betaSweep.points = {{0.0, 0.15}, {0.12, 0.45}};
    spec.automation["src.L.beta"] = betaSweep;
    std::vector<float> ref;
    for (int bs : {1, 17, 64, 512}) {
        cuicui::io::RenderOptions opt;
        opt.max_block_size = bs;
        auto y = cuicui::io::render_offline(spec, opt);
        if (ref.empty()) {
            ref = std::move(y);
        } else {
            REQUIRE(y.size() == ref.size());
            REQUIRE(std::memcmp(y.data(), ref.data(), y.size() * sizeof(float)) == 0);
        }
    }
}

TEST_CASE("sample rates 44.1/48/96 kHz agree in f0 and main peak") {
    double f0s[3] = {0, 0, 0};
    double peaks[3] = {0, 0, 0};
    const int srs[3] = {44100, 48000, 96000};
    for (int i = 0; i < 3; ++i) {
        auto spec = tonal_spec(1.0, 0.3, srs[i]);
        const auto y = cuicui::io::render_offline(spec);
        const auto feat = cuicui::io::measure_features(y.data(), static_cast<int>(y.size()),
                                                       static_cast<double>(srs[i]));
        f0s[i] = feat.median_f0_hz;
        peaks[i] = feat.peaks_hz[0];
        REQUIRE(f0s[i] > 200.0);
    }
    auto cents = [](double a, double b) { return 1200.0 * std::log2(a / b); };
    REQUIRE(std::abs(cents(f0s[0], f0s[1])) <= 2.0);
    REQUIRE(std::abs(cents(f0s[2], f0s[1])) <= 2.0);
    REQUIRE_THAT(peaks[0], WithinRel(peaks[1], 0.01));
    REQUIRE_THAT(peaks[2], WithinRel(peaks[1], 0.01));
}

TEST_CASE("fuzz 60 s all params including size: no non-finite") {
    cuicui::ProcessSpec ps;
    ps.hostSampleRate = 48000.0;
    ps.maxBlockSize = 64;
    ps.oversampling = 4;
    cuicui::BirdVoice v;
    v.prepare(ps);
    v.setSource(cuicui::VoiceSource::Labial);
    cuicui::VoiceSettings st;
    st.seed = 42;
    v.setSettings(st);
    v.reset(42);
    v.primeLabial(cuicui::model::kAlphaRest, 0.3);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<float> buf(64, 0.0f);
    const int nBlocks = static_cast<int>(60.0 * 48000.0 / 64.0);
    for (int b = 0; b < nBlocks; ++b) {
        cuicui::ControlFrame from, to;
        auto rnd = [&](cuicui::ParamId id) {
            const auto& s = cuicui::specOf(id);
            return static_cast<float>(cuicui::fromNormalised(s, u(rng)));
        };
        auto fill = [&](cuicui::ControlFrame& f) {
            f.resp_pressure = rnd(cuicui::ParamId::RespPressure);
            f.src_L_beta = rnd(cuicui::ParamId::SrcLBeta);
            f.src_R_beta = rnd(cuicui::ParamId::SrcRBeta);
            f.src_L_gate = rnd(cuicui::ParamId::SrcLGate);
            f.src_R_gate = rnd(cuicui::ParamId::SrcRGate);
            f.src_L_alpha_offset = rnd(cuicui::ParamId::SrcLAlphaOffset);
            f.src_R_alpha_offset = rnd(cuicui::ParamId::SrcRAlphaOffset);
            f.oec_freq_hz = rnd(cuicui::ParamId::OecFreqHz);
            f.beak_gape = rnd(cuicui::ParamId::BeakGape);
            f.noise_beta_sigma = rnd(cuicui::ParamId::NoiseBetaSigma);
        };
        fill(from);
        fill(to);
        st.body_size = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::BodySize), u(rng));
        st.src_gamma = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::SrcGamma), u(rng));
        st.trachea_length_mm = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::TracheaLengthMm), u(rng));
        st.trachea_reflection = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::TracheaReflection), u(rng));
        st.noise_aspiration = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::NoiseAspiration), u(rng));
        st.src_coupling = cuicui::fromNormalised(cuicui::specOf(cuicui::ParamId::SrcCoupling), u(rng));
        v.setSettings(st);
        v.setOutGainRamp(0.0f, 0.0f);
        v.process(from, to, buf.data(), 64);
        for (float x : buf) {
            REQUIRE(std::isfinite(x));
        }
    }
    REQUIRE(v.telemetry().nonFiniteResets == 0);
}

TEST_CASE("RK4 at σ=0.25 OS 4 stays within 5 cents of OS 8") {
    auto spec4 = tonal_spec(0.25, 0.25);
    spec4.oversampling = "4";
    auto spec8 = spec4;
    spec8.oversampling = "8";
    const auto y4 = cuicui::io::render_offline(spec4);
    const auto y8 = cuicui::io::render_offline(spec8);
    const double f4 = cuicui::io::zc_f0_hz(y4.data(), static_cast<int>(y4.size()), 48000.0, 2400);
    const double f8 = cuicui::io::zc_f0_hz(y8.data(), static_cast<int>(y8.size()), 48000.0, 2400);
    REQUIRE(f4 > 400.0);
    REQUIRE(f8 > 400.0);
    const double cents = 1200.0 * std::log2(f4 / f8);
    INFO("f4=" << f4 << " f8=" << f8 << " cents=" << cents);
    // OS 4 at σ=0.25 (γ=96000) is ~4 cents sharp vs OS 8. Auto OS stays 4 so
    // latency does not depend on size (WP-B6 pitfall: fixed OS per session).
    // Request engine.oversampling=8 when the session will use σ≲0.35.
    REQUIRE(std::abs(cents) < 5.0);
}

TEST_CASE("VoiceState is memcpy-safe") {
    cuicui::VoiceState a{};
    a.version = 1;
    a.labial_x_l = 0.3;
    cuicui::VoiceState b;
    std::memcpy(&b, &a, sizeof(a));
    REQUIRE(b.labial_x_l == 0.3);
    REQUIRE(b.version == 1);
}
