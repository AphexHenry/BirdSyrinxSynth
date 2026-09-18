#include "cuicui/control/archetypes.hpp"
#include "cuicui/control/score_player.hpp"
#include "cuicui/io/gesture_score.hpp"
#include "cuicui/io/offline_render.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

cuicui::io::ScoreGestureEvent eventFromArchetype(const std::string& name,
                                                 double t,
                                                 double anchorHz,
                                                 std::uint64_t seed = 1) {
    auto inst = cuicui::control::generateArchetype(name, cuicui::control::ArchetypeParamBag{}, seed);
    cuicui::io::ScoreGestureEvent e;
    e.t_s = t;
    e.duration_ms = inst.durationMs;
    e.type = "archetype:" + name;
    e.anchor_hz = anchorHz;
    e.curves = inst.curves;
    e.hints = inst.hints;
    e.has_curves = true;
    return e;
}

double medianAbsCents(const std::vector<float>& y, double sr, double fRef, double t0, double t1) {
    const int i0 = std::max(0, static_cast<int>(t0 * sr));
    const int i1 = std::min(static_cast<int>(y.size()), static_cast<int>(t1 * sr));
    if (i1 - i0 < 256 || !(fRef > 1.0)) {
        return 1.0e9;
    }
    int crossings = 0;
    for (int i = i0 + 1; i < i1; ++i) {
        if (y[static_cast<std::size_t>(i - 1)] <= 0.0f && y[static_cast<std::size_t>(i)] > 0.0f) {
            ++crossings;
        }
    }
    const double dur = static_cast<double>(i1 - i0) / sr;
    if (dur <= 0.0 || crossings < 4) {
        return 1.0e9;
    }
    const double f0 = static_cast<double>(crossings) / dur;
    return std::abs(1200.0 * std::log2(f0 / fRef));
}

} // namespace

TEST_CASE("gesture score JSON write-read-write is byte-identical") {
    cuicui::io::GestureScore s;
    s.name = "roundtrip";
    s.created_by = "WP-C4";
    s.created_at = "2026-09-18T00:00:00Z";
    s.settings["body.size"] = 1.0;
    s.settings["perf.breath"] = 0.7;
    s.gestures.push_back(eventFromArchetype("whistle", 0.0, 2093.0));
    s.gestures.push_back(eventFromArchetype("chip", 0.3, 2093.0));
    const std::string a = cuicui::io::dump_gesture_score(s);
    const auto parsed = cuicui::io::parse_gesture_score_string(a);
    const std::string b = cuicui::io::dump_gesture_score(parsed);
    REQUIRE(a == b);
}

TEST_CASE("invalid scores produce clear errors") {
    REQUIRE_THROWS_AS(cuicui::io::parse_gesture_score_string(R"({"format":"nope","version":1})"),
                      cuicui::io::GestureScoreError);
    try {
        cuicui::io::parse_gesture_score_string(R"({"format":"nope","version":1})");
    } catch (const cuicui::io::GestureScoreError& e) {
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("cuicui.gesture_score"));
    }
    try {
        cuicui::io::parse_gesture_score_string(
            R"({"format":"cuicui.gesture_score","version":9})");
    } catch (const cuicui::io::GestureScoreError& e) {
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("version"));
    }
    try {
        cuicui::io::parse_gesture_score_string(R"({
          "format":"cuicui.gesture_score","version":1,
          "physio":{"control_rate_hz":1000,"tracks":{"resp.pressure":[0,1],"src.L.beta":[0]}}
        })");
    } catch (const cuicui::io::GestureScoreError& e) {
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("equal length"));
    }
}

TEST_CASE("physio-track playback is bit-identical to render_controls") {
    cuicui::io::GestureScore s;
    s.has_physio = true;
    s.control_rate_hz = 1000.0;
    s.settings["body.size"] = 1.0;
    s.settings["src.gamma"] = 24000.0;
    const int n = 200;
    std::vector<float> p(n), b(n), g(n);
    for (int i = 0; i < n; ++i) {
        p[static_cast<std::size_t>(i)] = (i > 10 && i < 160) ? 0.6f : 0.0f;
        b[static_cast<std::size_t>(i)] = 0.35f;
        g[static_cast<std::size_t>(i)] = 1.0f;
    }
    s.tracks["resp.pressure"] = p;
    s.tracks["src.L.beta"] = b;
    s.tracks["src.L.gate"] = g;
    s.tracks["src.R.gate"] = std::vector<float>(n, 0.0f);
    s.tracks["noise.beta_sigma"] = std::vector<float>(n, 0.0f);

    cuicui::control::ScorePlayer player;
    player.load(s);
    const auto a = player.render(48000.0, 0);
    cuicui::VoiceSettings st = cuicui::io::settings_from_score(s);
    const auto b2 = cuicui::io::render_controls(st, s.tracks, 1000.0, 48000.0, st.seed, 0);
    REQUIRE(a.size() == b2.size());
    REQUIRE(a == b2);
}

TEST_CASE("both physio and gestures: physio wins, gestures ignored") {
    cuicui::io::GestureScore s;
    s.has_physio = true;
    s.control_rate_hz = 1000.0;
    s.tracks["resp.pressure"] = std::vector<float>(50, 0.0f);
    s.gestures.push_back(eventFromArchetype("whistle", 0.0, 2093.0));
    const std::string text = cuicui::io::dump_gesture_score(s);
    const auto parsed = cuicui::io::parse_gesture_score_string(text);
    REQUIRE(parsed.has_physio);
    REQUIRE(parsed.ignore_gestures);
}

TEST_CASE("gesture-event whistle f0 follows the arch within 15 cents") {
    cuicui::io::GestureScore s;
    s.settings["body.size"] = 1.0;
    s.settings["perf.breath"] = 0.7;
    auto inst = cuicui::control::generateArchetype(
        "whistle", std::unordered_map<std::string, double>{{"arch", 0.0}}, 1);
    cuicui::io::ScoreGestureEvent e;
    e.t_s = 0.02;
    e.duration_ms = inst.durationMs;
    e.type = "archetype:whistle";
    e.anchor_hz = 2093.0;
    e.curves = inst.curves;
    e.hints = inst.hints;
    e.has_curves = true;
    s.gestures.push_back(e);
    cuicui::control::ScorePlayer player;
    player.load(s);
    const double sr = 48000.0;
    const auto y = player.render(sr, 0);
    REQUIRE(y.size() > 1000);
    const double err = medianAbsCents(y, sr, 2093.0, 0.08, 0.22);
    REQUIRE(err <= 15.0);
}

TEST_CASE("trill at 22 Hz: measured element rate within 5%") {
    auto inst = cuicui::control::generateArchetype("trill", cuicui::control::ArchetypeParamBag{}, 1);
    const double durS = inst.durationMs * 0.001;
    const double nEl = std::round(22.0 * durS);
    const double meas = nEl / durS;
    REQUIRE(std::abs(meas - 22.0) / 22.0 <= 0.05);
    int dips = 0;
    bool low = false;
    for (double a : inst.curves.ampDb) {
        if (!low && a < -20.0) {
            low = true;
            ++dips;
        } else if (low && a > -6.0) {
            low = false;
        }
    }
    REQUIRE(dips >= static_cast<int>(nEl) - 2);
}
