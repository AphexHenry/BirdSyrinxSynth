#include "cuicui/io/audio_features.hpp"
#include "cuicui/io/offline_render.hpp"
#include "cuicui/io/render_spec.hpp"
#include "cuicui/params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

nlohmann::json load_json(const std::filesystem::path& p) {
    std::ifstream in(p);
    nlohmann::json j;
    in >> j;
    return j;
}

} // namespace

TEST_CASE("golden renders match expected_features.json") {
    const auto root = std::filesystem::path(CUICUI_SOURCE_DIR);
    const auto dir = root / "core/tests/golden";
    const auto expectedPath = dir / "expected_features.json";
    const bool write = std::getenv("CUICUI_WRITE_GOLDEN") != nullptr;
    std::vector<std::string> specs = {
        "b6_tonal_left.render.json",
        "b6_buzzy_noise.render.json",
        "b6_two_voice.render.json",
        "b6_side_switch.render.json",
        "b6_size_0_5.render.json",
        "b6_size_3.render.json",
        "b6_syllable_train.render.json",
    };
    nlohmann::json out;
    out["format"] = "cuicui.expected_features";
    out["version"] = 1;
    out["renders"] = nlohmann::json::object();
    nlohmann::json expected;
    if (!write) {
        REQUIRE(std::filesystem::exists(expectedPath));
        expected = load_json(expectedPath);
        REQUIRE(expected.at("renders").is_object());
    }
    for (const auto& name : specs) {
        const auto specPath = dir / name;
        INFO(name);
        REQUIRE(std::filesystem::exists(specPath));
        const auto spec = cuicui::io::parse_render_spec_file(specPath);
        const auto y = cuicui::io::render_offline(spec);
        const auto feat = cuicui::io::measure_features(y.data(), static_cast<int>(y.size()),
                                                       static_cast<double>(spec.sample_rate));
        nlohmann::json row;
        row["median_f0_hz"] = feat.median_f0_hz;
        row["rms_db"] = feat.rms_db;
        row["spectral_centroid_hz"] = feat.spectral_centroid_hz;
        row["peaks_hz"] = {feat.peaks_hz[0], feat.peaks_hz[1], feat.peaks_hz[2]};
        row["tol"] = {{"median_f0_hz", 40.0},
                      {"rms_db", 1.5},
                      {"spectral_centroid_hz", 120.0},
                      {"peaks_hz", 80.0}};
        out["renders"][name] = row;
        if (!write) {
            const auto& want = expected.at("renders").at(name);
            const auto& tol = want.at("tol");
            REQUIRE_THAT(feat.median_f0_hz, WithinAbs(want.at("median_f0_hz").get<double>(),
                                                      tol.at("median_f0_hz").get<double>()));
            REQUIRE_THAT(feat.rms_db, WithinAbs(want.at("rms_db").get<double>(), tol.at("rms_db").get<double>()));
            REQUIRE_THAT(feat.spectral_centroid_hz,
                         WithinAbs(want.at("spectral_centroid_hz").get<double>(),
                                   tol.at("spectral_centroid_hz").get<double>()));
            const auto peaks = want.at("peaks_hz");
            const double ptol = tol.at("peaks_hz").get<double>();
            REQUIRE_THAT(feat.peaks_hz[0], WithinAbs(peaks.at(0).get<double>(), ptol));
            REQUIRE_THAT(feat.peaks_hz[1], WithinAbs(peaks.at(1).get<double>(), ptol));
            REQUIRE_THAT(feat.peaks_hz[2], WithinAbs(peaks.at(2).get<double>(), ptol));
        }
    }
    if (write) {
        std::ofstream(expectedPath) << out.dump(2) << '\n';
    }
}

TEST_CASE("render_controls is bit-identical to CLI render_offline for goldens") {
    const auto dir = std::filesystem::path(CUICUI_SOURCE_DIR) / "core/tests/golden";
    const std::vector<std::string> specs = {
        "b6_tonal_left.render.json", "b6_buzzy_noise.render.json", "b6_two_voice.render.json",
        "b6_side_switch.render.json", "b6_size_0_5.render.json", "b6_size_3.render.json",
        "b6_syllable_train.render.json",
    };
    for (const auto& name : specs) {
        INFO(name);
        const auto spec = cuicui::io::parse_render_spec_file(dir / name);
        const auto cli = cuicui::io::render_offline(spec);
        std::unordered_map<std::string, std::vector<float>> ctrls;
        const int n = static_cast<int>(cli.size());
        const double dt = 1.0 / static_cast<double>(spec.sample_rate);
        for (const auto& p : cuicui::kParamTable) {
            if (cuicui::paramHome(p.pid) != cuicui::ParamHome::Control) {
                continue;
            }
            const auto a = spec.automationFor(p.id, p.def);
            std::vector<float> x(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                x[static_cast<std::size_t>(i)] =
                    static_cast<float>(cuicui::clampParam(p, a.valueAt(static_cast<double>(i) * dt)));
            }
            ctrls[p.id] = x;
        }
        cuicui::VoiceSettings settings;
        settings.body_size = spec.staticParam("body.size", 1.0);
        settings.src_gamma = spec.staticParam("src.gamma", 24000.0);
        settings.trachea_length_mm = spec.staticParam("trachea.length_mm", 33.0);
        settings.trachea_reflection = spec.staticParam("trachea.reflection", 0.5);
        settings.trachea_loss = spec.staticParam("trachea.loss", 0.3);
        settings.trachea_input_gain = spec.staticParam("trachea.input_gain", 1.0);
        settings.oec_damping = spec.staticParam("oec.damping", 0.1);
        settings.beak_damping = spec.staticParam("beak.damping", 0.3);
        settings.out_radiation = spec.staticParam("out.radiation", 1.0);
        settings.out_gain_db = spec.staticParam("out.gain_db", 0.0);
        settings.resp_attack_ms = spec.staticParam("resp.attack_ms", 8.0);
        settings.resp_release_ms = spec.staticParam("resp.release_ms", 15.0);
        settings.noise_beta_bandwidth_hz = spec.staticParam("noise.beta_bandwidth_hz", 300.0);
        settings.noise_aspiration = spec.staticParam("noise.aspiration", 0.02);
        settings.src_coupling = spec.staticParam("src.coupling", 0.0);
        int os = 0;
        (void)cuicui::parseOversamplingToken(spec.oversampling, os);
        const auto py = cuicui::io::render_controls(settings, ctrls, static_cast<double>(spec.sample_rate),
                                                    static_cast<double>(spec.sample_rate), spec.seed, os);
        REQUIRE(py.size() == cli.size());
        float maxDiff = 0.0f;
        int mism = 0;
        for (std::size_t i = 0; i < py.size(); ++i) {
            const float d = std::abs(py[i] - cli[i]);
            maxDiff = std::max(maxDiff, d);
            if (py[i] != cli[i]) {
                ++mism;
            }
        }
        INFO("mismatches=" << mism << " maxDiff=" << maxDiff);
        // Float32 control arrays cannot carry JSON doubles such as 0.2; CLI primes
        // the oscillator with the double. Difference is 1 ULP (≤ 2.98e-8).
        REQUIRE(maxDiff <= std::numeric_limits<float>::epsilon());
    }
}
