#pragma once

#include "cuicui/control/syllable_bank.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace cuicui::io {

/// Invalid or unreadable syllable bank (message says what and where).
struct SyllableBankError : std::runtime_error {
    explicit SyllableBankError(const std::string& message) : std::runtime_error(message) {}
};

/// Parse and validate a `cuicui.syllable_bank` v1 document (schemas/syllable_bank.schema.json, D-026).
/// Offline only (allocates, throws): call it on the message thread or before playing.
inline control::SyllableBank parseSyllableBank(std::string_view text) {
    using nlohmann::json;
    json j;
    try {
        j = json::parse(text.begin(), text.end());
    } catch (const json::parse_error& e) {
        throw SyllableBankError(std::string("syllable bank: not valid JSON: ") + e.what());
    }
    auto fail = [](const std::string& what) { throw SyllableBankError("syllable bank: " + what); };
    if (!j.is_object() || j.value("format", std::string()) != "cuicui.syllable_bank") {
        fail("format must be \"cuicui.syllable_bank\"");
    }
    if (j.value("version", 0) != 1) {
        fail("unsupported version (this build reads version 1)");
    }
    if (j.value("contour_rate_hz", 0) != 1000) {
        fail("contour_rate_hz must be 1000");
    }
    static constexpr std::array<const char*, control::kNoteRuleCount> kRules{{"rule", "steady", "landing", "average"}};
    auto perRule = [&](const json& obj, const std::string& where) {
        std::array<double, control::kNoteRuleCount> out{};
        if (!obj.is_object()) {
            fail(where + " must be an object");
        }
        for (std::size_t r = 0; r < kRules.size(); ++r) {
            if (!obj.contains(kRules[r]) || !obj[kRules[r]].is_number()) {
                fail(where + "." + kRules[r] + " missing or not a number");
            }
            out[r] = obj[kRules[r]].get<double>();
            if (!std::isfinite(out[r])) {
                fail(where + "." + kRules[r] + " is not finite");
            }
        }
        return out;
    };

    control::SyllableBankBuilder b;
    b.setInfo(j.value("name", std::string()), j.value("species", std::string()), j.value("hash", std::string()));
    if (!j.contains("centres_cents")) {
        fail("centres_cents missing");
    }
    b.setCentres(perRule(j["centres_cents"], "centres_cents"));
    if (!j.contains("phrases") || !j["phrases"].is_array() || j["phrases"].empty()) {
        fail("phrases must be a non-empty array");
    }
    b.setBirds({j.value("name", std::string())});
    // WP-R3 (D-036): the optional shape model ("variation" section + per-syllable type / model / scores)
    std::vector<int> modelModes; // modes per model, to check the syllables' scores
    if (j.contains("variation")) {
        const auto& v = j["variation"];
        if (!v.is_object() || v.value("format", std::string()) != "cuicui.variation_model" || v.value("version", 0) != 1) {
            fail("variation must be a cuicui.variation_model v1 object");
        }
        const int points = v.value("points", 0);
        if (points < 2 || points > control::kMaxModelPoints) {
            fail("variation.points must be 2.." + std::to_string(control::kMaxModelPoints));
        }
        auto num = [&](const json& o, const char* key, const std::string& where) {
            if (!o.contains(key) || !o[key].is_number() || !std::isfinite(o[key].get<double>())) {
                fail(where + "." + key + " missing or not a finite number");
            }
            return o[key].get<double>();
        };
        auto pair = [&](const json& o, const char* key, const std::string& where) {
            if (!o.contains(key) || !o[key].is_array() || o[key].size() != 2) {
                fail(where + "." + key + " must be [mean, sd]");
            }
            const auto x = o[key].get<std::vector<double>>();
            if (!std::isfinite(x[0]) || !std::isfinite(x[1]) || x[1] < 0.0) {
                fail(where + "." + key + " must be finite with sd ≥ 0");
            }
            return x;
        };
        auto curve = [&](const json& c, const std::string& where) {
            if (!c.is_array() || static_cast<int>(c.size()) != points) {
                fail(where + " must have variation.points values");
            }
            std::array<double, control::kMaxModelPoints> out{};
            for (int i = 0; i < points; ++i) {
                out[static_cast<std::size_t>(i)] = c[static_cast<std::size_t>(i)].get<double>();
                if (!std::isfinite(out[static_cast<std::size_t>(i)])) {
                    fail(where + " has a non-finite value");
                }
            }
            return out;
        };
        if (!v.contains("spread_median") || !v["spread_median"].is_object()) {
            fail("variation.spread_median missing");
        }
        const auto& sp = v["spread_median"];
        control::VariationSpread spread{num(sp, "note", "variation.spread_median"),
                                        num(sp, "logdur", "variation.spread_median"),
                                        num(sp, "peak_db", "variation.spread_median")};
        b.setVariation(points, {spread});
        if (!v.contains("types") || !v["types"].is_array()) {
            fail("variation.types must be an array");
        }
        int ti = 0;
        for (const auto& t : v["types"]) {
            const std::string w = "variation.types[" + std::to_string(ti++) + "]";
            control::TypeModel m;
            const auto note = pair(t, "note", w);
            const auto logdur = pair(t, "logdur", w);
            const auto peak = pair(t, "peak_db", w);
            m.noteMean = note[0];
            m.noteSd = note[1];
            m.logdurMean = logdur[0];
            m.logdurSd = logdur[1];
            m.peakMean = peak[0];
            m.peakSd = peak[1];
            if (!t.contains("mean") || !t.contains("modes") || !t.contains("mode_sd") || !t["modes"].is_array() ||
                !t["mode_sd"].is_array() || t["modes"].size() != t["mode_sd"].size() ||
                t["modes"].size() > static_cast<std::size_t>(control::kMaxModes)) {
                fail(w + ": mean, modes and mode_sd are required (at most " + std::to_string(control::kMaxModes) +
                     " modes)");
            }
            m.mean = curve(t["mean"], w + ".mean");
            m.numModes = static_cast<int>(t["modes"].size());
            for (int k = 0; k < m.numModes; ++k) {
                m.modes[static_cast<std::size_t>(k)] = curve(t["modes"][static_cast<std::size_t>(k)], w + ".modes");
                const auto& sd = t["mode_sd"][static_cast<std::size_t>(k)];
                if (!sd.is_number() || !std::isfinite(sd.get<double>()) || sd.get<double>() < 0.0) {
                    fail(w + ".mode_sd must be finite and ≥ 0");
                }
                m.modeSd[static_cast<std::size_t>(k)] = sd.get<double>();
            }
            b.addModel(m);
            modelModes.push_back(m.numModes);
        }
    }
    int pi = 0;
    for (const auto& p : j["phrases"]) {
        const std::string pw = "phrase " + std::to_string(pi);
        if (p.value("id", -1) != pi) {
            fail(pw + ": ids must be 0..n-1 in order");
        }
        if (!p.contains("syllables") || !p["syllables"].is_array() || p["syllables"].empty()) {
            fail(pw + ": syllables must be a non-empty array");
        }
        b.beginPhrase(p.value("start_s", 0.0));
        int prevEnd = -1;
        int si = 0;
        for (const auto& s : p["syllables"]) {
            const std::string sw = pw + " syllable " + std::to_string(si);
            if (!s.contains("f_hz") || !s.contains("a") || !s["f_hz"].is_array() || !s["a"].is_array()) {
                fail(sw + ": f_hz and a arrays are required");
            }
            const auto hz = s["f_hz"].get<std::vector<double>>();
            const auto amp = s["a"].get<std::vector<float>>();
            if (hz.size() < 2 || hz.size() != amp.size()) {
                fail(sw + ": f_hz and a need the same length (≥ 2)");
            }
            for (std::size_t i = 0; i < hz.size(); ++i) {
                if (!(hz[i] > 0.0) || !std::isfinite(hz[i]) || !(amp[i] >= 0.0f) || !std::isfinite(amp[i])) {
                    fail(sw + ": contour values must be finite, f_hz > 0 and a ≥ 0");
                }
            }
            const int tMs = s.value("t_ms", -1);
            if (tMs < 0 || (si == 0 && tMs != 0)) {
                fail(sw + ": t_ms must be ≥ 0 (and 0 for a phrase's first syllable)");
            }
            if (tMs < prevEnd) {
                fail(sw + ": starts before the previous syllable ends");
            }
            prevEnd = tMs + static_cast<int>(hz.size());
            if (!s.contains("anchors")) {
                fail(sw + ": anchors missing");
            }
            const auto anchors = perRule(s["anchors"], sw + ".anchors");
            int spanB = 0;
            int spanE = static_cast<int>(hz.size());
            if (s["anchors"].contains("rule_span_ms")) {
                const auto span = s["anchors"]["rule_span_ms"].get<std::vector<int>>();
                if (span.size() != 2 || span[0] < 0 || span[0] >= span[1] || span[1] > static_cast<int>(hz.size())) {
                    fail(sw + ": rule_span_ms outside the contour");
                }
                spanB = span[0];
                spanE = span[1];
            }
            bool tap = true; // D-032: "no_note" / "no_tap" keep it from starting a key press
            bool hop = false; // WP-R3: a tracker jump (its pitch range is never widened)
            if (s.contains("flags")) {
                if (!s["flags"].is_array()) {
                    fail(sw + ": flags must be an array of strings");
                }
                for (const auto& f : s["flags"]) {
                    if (!f.is_string()) {
                        fail(sw + ": flags must be an array of strings");
                    }
                    const auto flag = f.get<std::string>();
                    tap = tap && flag != control::kNoNoteFlag && flag != control::kNoTapFlag;
                    hop = hop || flag == "hop";
                }
            }
            b.addSyllable(tMs, hz, amp, anchors, spanB, spanE, s.value("label", std::string()), tap, hop);
            if (s.contains("type") || s.contains("model")) { // WP-R3 (D-036)
                const int type = s.value("type", -1);
                const int model = s.value("model", -1);
                std::array<double, control::kMaxModes> scores{};
                if (model >= 0) {
                    if (model >= static_cast<int>(modelModes.size())) {
                        fail(sw + ": model " + std::to_string(model) + " does not exist");
                    }
                    if (!s.contains("scores") || !s["scores"].is_array() ||
                        static_cast<int>(s["scores"].size()) != modelModes[static_cast<std::size_t>(model)]) {
                        fail(sw + ": scores must have one value per mode of its model");
                    }
                    for (int k = 0; k < modelModes[static_cast<std::size_t>(model)]; ++k) {
                        scores[static_cast<std::size_t>(k)] = s["scores"][static_cast<std::size_t>(k)].get<double>();
                    }
                }
                b.setSyllableVariation(0, type, model, scores);
            }
            ++si;
        }
        ++pi;
    }
    return std::move(b.bank);
}

/// Read a bank file and parse it (see parseSyllableBank).
inline control::SyllableBank loadSyllableBank(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw SyllableBankError("syllable bank: cannot open " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseSyllableBank(ss.str());
}

} // namespace cuicui::io
