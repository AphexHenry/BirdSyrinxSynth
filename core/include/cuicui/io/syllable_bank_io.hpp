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
                }
            }
            b.addSyllable(tMs, hz, amp, anchors, spanB, spanE, s.value("label", std::string()), tap);
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
