#include "cuicui/io/render_spec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

TEST_CASE("render spec parses a valid document") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "sample_rate": 48000,
      "duration_s": 2.0,
      "seed": 3,
      "params": { "test.freq_hz": 440.0, "test.gain_db": -6.0 },
      "automation": { "test.freq_hz": [[0.0, 100.0], [2.0, 200.0]] },
      "automation_interp": "linear",
      "output": { "normalize_peak_dbfs": null }
    })";
    const auto spec = cuicui::io::parse_render_spec_string(json);
    REQUIRE(spec.sample_rate == 48000);
    REQUIRE_THAT(spec.duration_s, WithinAbs(2.0, 1e-15));
    REQUIRE(spec.seed == 3);
    REQUIRE_THAT(spec.staticParam("test.gain_db", 0.0), WithinAbs(-6.0, 1e-15));
    const auto freq = spec.automationFor("test.freq_hz", 0.0);
    REQUIRE_THAT(freq.valueAt(1.0), WithinAbs(150.0, 1e-12));
}

TEST_CASE("render spec rejects a bad format string") {
    const char* json = R"({"format": "nope", "version": 1})";
    REQUIRE_THROWS_AS(cuicui::io::parse_render_spec_string(json), cuicui::io::RenderSpecError);
    try {
        cuicui::io::parse_render_spec_string(json);
    } catch (const cuicui::io::RenderSpecError& e) {
        REQUIRE(e.exit_code == 1);
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("cuicui.render_spec"));
    }
}

TEST_CASE("render spec unknown parameter ID is exit code 2") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "params": { "src.L.beta": 0.3 }
    })";
    try {
        cuicui::io::parse_render_spec_string(json);
        FAIL("expected RenderSpecError");
    } catch (const cuicui::io::RenderSpecError& e) {
        REQUIRE(e.exit_code == 2);
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("src.L.beta"));
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("test.freq_hz"));
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("test.gain_db"));
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("test.waveform"));
    }
}

TEST_CASE("render spec unknown automation ID is exit code 2") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "automation": { "body.size": [[0.0, 1.0]] }
    })";
    try {
        cuicui::io::parse_render_spec_string(json);
        FAIL("expected RenderSpecError");
    } catch (const cuicui::io::RenderSpecError& e) {
        REQUIRE(e.exit_code == 2);
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("body.size"));
    }
}

TEST_CASE("render spec golden sweep file parses") {
    const auto path = std::filesystem::path(CUICUI_SOURCE_DIR) / "core/tests/golden/test_tone_sweep.render.json";
    const auto spec = cuicui::io::parse_render_spec_file(path);
    REQUIRE(spec.sample_rate == 48000);
    REQUIRE(spec.duration_s > 0.0);
    REQUIRE(spec.automation.count("test.freq_hz") == 1);
    REQUIRE(spec.waveform == "sine");
}

TEST_CASE("render spec test.waveform noise") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "params": { "test.waveform": "noise", "test.gain_db": -12.0 }
    })";
    const auto spec = cuicui::io::parse_render_spec_string(json);
    REQUIRE(spec.waveform == "noise");
}

TEST_CASE("render spec rejects invalid test.waveform") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "params": { "test.waveform": "saw" }
    })";
    REQUIRE_THROWS_AS(cuicui::io::parse_render_spec_string(json), cuicui::io::RenderSpecError);
}

TEST_CASE("render spec rejects top-level generator field") {
    const char* json = R"({
      "format": "cuicui.render_spec",
      "version": 1,
      "generator": "noise"
    })";
    try {
        cuicui::io::parse_render_spec_string(json);
        FAIL("expected RenderSpecError");
    } catch (const cuicui::io::RenderSpecError& e) {
        REQUIRE_THAT(std::string(e.what()), ContainsSubstring("test.waveform"));
    }
}
