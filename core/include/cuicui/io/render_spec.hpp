#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "cuicui/params.hpp"

namespace cuicui::io {

inline bool is_known_param_id(std::string_view id) noexcept { return findParam(id) != nullptr; }

inline std::string known_param_ids_list() { return knownParamIdsList(); }

struct RenderSpecError : std::runtime_error {
    int exit_code;
    explicit RenderSpecError(const std::string& message, int code = 1)
        : std::runtime_error(message), exit_code(code) {}
};

struct Breakpoint {
    double t = 0.0;
    double v = 0.0;
};

/// Piecewise-linear automation. Value before the first breakpoint is the first value;
/// after the last is the last value. Empty → `fallback`.
struct Automation {
    std::vector<Breakpoint> points;
    double fallback = 0.0;

    void sort() {
        std::sort(points.begin(), points.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.t < b.t; });
    }

    [[nodiscard]] double valueAt(double t) const noexcept {
        if (points.empty()) {
            return fallback;
        }
        if (t <= points.front().t) {
            return points.front().v;
        }
        if (t >= points.back().t) {
            return points.back().v;
        }
        const auto it = std::upper_bound(
            points.begin(),
            points.end(),
            t,
            [](double x, const Breakpoint& b) { return x < b.t; });
        const Breakpoint& b = *it;
        const Breakpoint& a = *std::prev(it);
        const double dt = b.t - a.t;
        if (dt <= 0.0) {
            return b.v;
        }
        const double u = (t - a.t) / dt;
        return a.v + u * (b.v - a.v);
    }
};

struct RenderSpec {
    int sample_rate = 48000;
    double duration_s = 1.0;
    std::uint64_t seed = 1;
    std::string oversampling = "auto";
    std::unordered_map<std::string, double> params;
    std::unordered_map<std::string, Automation> automation;
    std::string automation_interp = "linear";
    std::optional<double> normalize_peak_dbfs;
    std::string waveform = "sine"; // test.waveform: "sine" | "saw" | "noise"

    [[nodiscard]] double staticParam(const std::string& id, double default_value) const {
        const auto it = params.find(id);
        return it == params.end() ? default_value : it->second;
    }

    [[nodiscard]] Automation automationFor(const std::string& id, double default_value) const {
        const auto it = automation.find(id);
        if (it != automation.end()) {
            Automation a = it->second;
            a.fallback = default_value;
            return a;
        }
        Automation a;
        a.fallback = staticParam(id, default_value);
        return a;
    }
};

inline RenderSpec parse_render_spec_json(const nlohmann::json& j) {
    if (!j.is_object()) {
        throw RenderSpecError("render spec must be a JSON object");
    }
    if (!j.contains("format") || j.at("format") != "cuicui.render_spec") {
        throw RenderSpecError("invalid format: expected \"cuicui.render_spec\"");
    }
    if (!j.contains("version") || j.at("version") != 1) {
        throw RenderSpecError("unsupported render spec version (expected 1)");
    }

    RenderSpec spec;
    if (j.contains("sample_rate")) {
        spec.sample_rate = j.at("sample_rate").get<int>();
        if (spec.sample_rate <= 0) {
            throw RenderSpecError("sample_rate must be positive");
        }
    }
    if (j.contains("duration_s")) {
        spec.duration_s = j.at("duration_s").get<double>();
        if (!(spec.duration_s > 0.0)) {
            throw RenderSpecError("duration_s must be positive");
        }
    }
    if (j.contains("seed")) {
        spec.seed = j.at("seed").get<std::uint64_t>();
    }
    if (j.contains("oversampling")) {
        if (j.at("oversampling").is_string()) {
            spec.oversampling = j.at("oversampling").get<std::string>();
        } else {
            spec.oversampling = std::to_string(j.at("oversampling").get<int>());
        }
    }
    if (j.contains("automation_interp")) {
        spec.automation_interp = j.at("automation_interp").get<std::string>();
        if (spec.automation_interp != "linear") {
            throw RenderSpecError("only automation_interp \"linear\" is supported");
        }
    }
    if (j.contains("generator")) {
            throw RenderSpecError(
            "unknown field \"generator\"; use params.\"test.waveform\" with \"sine\", \"saw\" or \"noise\"");
    }
    if (j.contains("output") && j.at("output").is_object()) {
        const auto& out = j.at("output");
        if (out.contains("normalize_peak_dbfs") && !out.at("normalize_peak_dbfs").is_null()) {
            spec.normalize_peak_dbfs = out.at("normalize_peak_dbfs").get<double>();
        }
    }

    auto check_id = [](const std::string& id) {
        if (!is_known_param_id(id)) {
            throw RenderSpecError("unknown parameter ID \"" + id + "\" (known: " + known_param_ids_list() + ")",
                                  2);
        }
    };

    if (j.contains("params")) {
        for (auto it = j.at("params").begin(); it != j.at("params").end(); ++it) {
            check_id(it.key());
            if (it.key() == "test.waveform") {
                if (!it.value().is_string()) {
                    throw RenderSpecError("params.\"test.waveform\" must be a string (sine, saw or noise)");
                }
                spec.waveform = it.value().get<std::string>();
                TestWaveform w{};
                if (!parseTestWaveform(spec.waveform, w)) {
                    throw RenderSpecError("params.\"test.waveform\" must be \"sine\", \"saw\" or \"noise\"");
                }
                spec.params[it.key()] = static_cast<double>(w);
                continue;
            }
            if (it.key() == "engine.oversampling") {
                int os = 0;
                if (it.value().is_string()) {
                    if (!parseOversamplingToken(it.value().get<std::string>(), os)) {
                        throw RenderSpecError("params.\"engine.oversampling\" must be auto, 1, 2, 4 or 8");
                    }
                } else {
                    os = it.value().get<int>();
                    if (os != 0 && os != 1 && os != 2 && os != 4 && os != 8) {
                        throw RenderSpecError("params.\"engine.oversampling\" must be auto, 1, 2, 4 or 8");
                    }
                }
                spec.params[it.key()] = static_cast<double>(os);
                continue;
            }
            spec.params[it.key()] = it.value().get<double>();
            const auto* ps = findParam(it.key());
            if (ps != nullptr) {
                spec.params[it.key()] = clampParam(*ps, spec.params[it.key()]);
            }
        }
    }
    if (j.contains("automation")) {
        for (auto it = j.at("automation").begin(); it != j.at("automation").end(); ++it) {
            check_id(it.key());
            if (it.key() == "test.waveform" || it.key() == "engine.oversampling") {
                throw RenderSpecError("parameter \"" + it.key() + "\" cannot be automated");
            }
            Automation a;
            a.fallback = spec.staticParam(it.key(), 0.0);
            if (!it.value().is_array()) {
                throw RenderSpecError("automation for \"" + it.key() + "\" must be an array of [t, v]");
            }
            for (const auto& bp : it.value()) {
                if (!bp.is_array() || bp.size() != 2) {
                    throw RenderSpecError("breakpoint must be [time_s, value]");
                }
                a.points.push_back(Breakpoint{bp.at(0).get<double>(), bp.at(1).get<double>()});
            }
            a.sort();
            spec.automation[it.key()] = std::move(a);
        }
    }
    if (!j.contains("oversampling") && spec.params.count("engine.oversampling") != 0) {
        const int os = static_cast<int>(spec.params["engine.oversampling"]);
        spec.oversampling = (os == 0) ? "auto" : std::to_string(os);
    }
    if (!j.contains("seed") && spec.params.count("engine.seed") != 0) {
        spec.seed = static_cast<std::uint64_t>(spec.params["engine.seed"]);
    }
    return spec;
}

inline RenderSpec parse_render_spec_string(const std::string& text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        throw RenderSpecError(std::string("JSON parse error: ") + e.what());
    }
    return parse_render_spec_json(j);
}

inline RenderSpec parse_render_spec_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw RenderSpecError("cannot open render spec: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_render_spec_string(ss.str());
}

} // namespace cuicui::io
