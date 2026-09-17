#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace cuicui {

/// Control layer. Physiology = layer 1, Performance = layer 3, Engine = technical/test.
enum class ParamLayer { Physiology, Performance, Engine };

enum class ParamId {
#define CUICUI_PARAM(Enum, id, name, unit, min, max, def, skew, smooth, layer, fast, since) Enum,
#include "cuicui/params.def"
#undef CUICUI_PARAM
    Count
};

inline constexpr int kParamCount = static_cast<int>(ParamId::Count);

struct ParamSpec {
    ParamId pid;
    const char* id;
    const char* name;
    const char* unit;
    double min;
    double max;
    double def;
    double skew;        ///< fromNormalised(u) = min + (max-min) * u^(1/skew). 1 = linear.
    double smoothingMs; ///< 0 = no smoothing (snap).
    ParamLayer layer;
    bool fast;
    const char* sinceWP;
};

inline constexpr std::array<ParamSpec, static_cast<std::size_t>(kParamCount)> kParamTable{{
#define CUICUI_PARAM(Enum, id, name, unit, min, max, def, skew, smooth, layer, fast, since)                        \
    {ParamId::Enum, id, name, unit, min, max, def, skew, smooth, ParamLayer::layer, fast, since},
#include "cuicui/params.def"
#undef CUICUI_PARAM
}};

inline const char* layerName(ParamLayer layer) noexcept {
    switch (layer) {
    case ParamLayer::Physiology:
        return "Physiology";
    case ParamLayer::Performance:
        return "Performance";
    case ParamLayer::Engine:
        return "Engine";
    }
    return "Engine";
}

[[nodiscard]] inline const ParamSpec* findParam(std::string_view id) noexcept {
    for (const auto& p : kParamTable) {
        if (id == p.id) {
            return &p;
        }
    }
    return nullptr;
}

[[nodiscard]] inline const ParamSpec& specOf(ParamId id) noexcept {
    return kParamTable[static_cast<std::size_t>(id)];
}

[[nodiscard]] inline double clampParam(const ParamSpec& s, double v) noexcept {
    return std::clamp(v, s.min, s.max);
}

/// Map 0–1 to the parameter range. See params.def for the skew convention (JUCE-shaped power curve).
[[nodiscard]] inline double fromNormalised(const ParamSpec& s, double u) noexcept {
    u = std::clamp(u, 0.0, 1.0);
    const double exp = (s.skew == 0.0) ? 1.0 : (1.0 / s.skew);
    return s.min + (s.max - s.min) * std::pow(u, exp);
}

[[nodiscard]] inline double toNormalised(const ParamSpec& s, double x) noexcept {
    x = std::clamp(x, s.min, s.max);
    const double span = s.max - s.min;
    const double t = (span == 0.0) ? 0.0 : (x - s.min) / span;
    return std::pow(std::clamp(t, 0.0, 1.0), s.skew);
}

inline std::string knownParamIdsList() {
    std::string out;
    for (std::size_t i = 0; i < kParamTable.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += kParamTable[i].id;
    }
    return out;
}

/// test.waveform stored as 0=sine, 1=saw, 2=noise.
enum class TestWaveform { Sine = 0, Saw = 1, Noise = 2 };

[[nodiscard]] inline const char* testWaveformName(TestWaveform w) noexcept {
    switch (w) {
    case TestWaveform::Saw:
        return "saw";
    case TestWaveform::Noise:
        return "noise";
    case TestWaveform::Sine:
    default:
        return "sine";
    }
}

[[nodiscard]] inline bool parseTestWaveform(std::string_view s, TestWaveform& out) noexcept {
    if (s == "sine") {
        out = TestWaveform::Sine;
        return true;
    }
    if (s == "saw") {
        out = TestWaveform::Saw;
        return true;
    }
    if (s == "noise") {
        out = TestWaveform::Noise;
        return true;
    }
    return false;
}

/// Host parameter version hint from `sinceWP` (A=1, B=2, C=3, …). WP-C1.
[[nodiscard]] inline int versionHintFromSinceWP(std::string_view since) noexcept {
    if (since.empty()) {
        return 1;
    }
    switch (since.front()) {
    case 'A':
        return 1;
    case 'B':
        return 2;
    case 'C':
        return 3;
    case 'D':
        return 4;
    case 'E':
        return 5;
    case 'F':
        return 6;
    case 'G':
        return 7;
    default:
        return 1;
    }
}

/// engine.oversampling choice indices: 0=auto, 1=1, 2=2, 3=4, 4=8.
inline constexpr std::array<int, 5> kOversamplingChoiceValues{{0, 1, 2, 4, 8}};

[[nodiscard]] inline int oversamplingValueFromChoiceIndex(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(kOversamplingChoiceValues.size())) {
        return 0;
    }
    return kOversamplingChoiceValues[static_cast<std::size_t>(index)];
}

[[nodiscard]] inline int oversamplingChoiceIndexFromValue(int value) noexcept {
    switch (value) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 4:
        return 3;
    case 8:
        return 4;
    default:
        return 0;
    }
}

/// engine.oversampling: 0 = auto, else 1, 2, 4, 8.
[[nodiscard]] inline bool parseOversamplingToken(std::string_view s, int& out) noexcept {
    if (s == "auto" || s == "0") {
        out = 0;
        return true;
    }
    if (s == "1") {
        out = 1;
        return true;
    }
    if (s == "2") {
        out = 2;
        return true;
    }
    if (s == "4") {
        out = 4;
        return true;
    }
    if (s == "8") {
        out = 8;
        return true;
    }
    return false;
}

} // namespace cuicui
