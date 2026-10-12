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
    case 'R':
        return 8;
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

/// perf.mode: 0 = perform, 1 = physiology.
enum class PerfMode { Perform = 0, Physiology = 1 };

inline constexpr std::array<const char*, 2> kPerfModeNames{{"perform", "physiology"}};

[[nodiscard]] inline int perfModeIndexFromValue(double v) noexcept {
    return (v >= 0.5) ? 1 : 0;
}

[[nodiscard]] inline bool parsePerfModeToken(std::string_view s, int& out) noexcept {
    if (s == "perform" || s == "0") {
        out = 0;
        return true;
    }
    if (s == "physiology" || s == "1") {
        out = 1;
        return true;
    }
    return false;
}

/// perf.scale: 0 chromatic … 5 whole_tone. See 01_research.md §7.5.
enum class PerfScale {
    Chromatic = 0,
    Major = 1,
    Minor = 2,
    PentatonicMajor = 3,
    PentatonicMinor = 4,
    WholeTone = 5
};

inline constexpr std::array<const char*, 6> kPerfScaleNames{
    {"chromatic", "major", "minor", "pentatonic_major", "pentatonic_minor", "whole_tone"}};

[[nodiscard]] inline int perfScaleIndexFromValue(double v) noexcept {
    const int i = static_cast<int>(std::lround(v));
    return std::clamp(i, 0, 5);
}

[[nodiscard]] inline bool parsePerfScaleToken(std::string_view s, int& out) noexcept {
    for (int i = 0; i < static_cast<int>(kPerfScaleNames.size()); ++i) {
        if (s == kPerfScaleNames[static_cast<std::size_t>(i)]) {
            out = i;
            return true;
        }
    }
    if (s == "0" || s == "1" || s == "2" || s == "3" || s == "4" || s == "5") {
        out = s[0] - '0';
        return true;
    }
    return false;
}

/// engine.oversampling: 0 = auto, else 1, 2, 4, 8.
/// perf.sync_division: 0=1/4, 1=1/8, 2=1/16, 3=1/32, 4=1/8T, 5=1/16T. WP-C5.
enum class PerfSyncDivision { Quarter = 0, Eighth = 1, Sixteenth = 2, ThirtySecond = 3, EighthT = 4, SixteenthT = 5 };

inline constexpr std::array<const char*, 6> kPerfSyncDivisionNames{{"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"}};

[[nodiscard]] inline int perfSyncDivisionIndexFromValue(double v) noexcept {
    const int i = static_cast<int>(std::lround(v));
    return std::clamp(i, 0, 5);
}

[[nodiscard]] inline bool parsePerfSyncDivisionToken(std::string_view s, int& out) noexcept {
    for (int i = 0; i < static_cast<int>(kPerfSyncDivisionNames.size()); ++i) {
        if (s == kPerfSyncDivisionNames[static_cast<std::size_t>(i)]) {
            out = i;
            return true;
        }
    }
    if (s.size() == 1 && s[0] >= '0' && s[0] <= '5') {
        out = s[0] - '0';
        return true;
    }
    return false;
}

/// Grid length in quarter-notes for `perf.sync_division`.
[[nodiscard]] inline double syncDivisionQuarterNotes(int index) noexcept {
    switch (std::clamp(index, 0, 5)) {
    case 0:
        return 1.0;
    case 1:
        return 0.5;
    case 2:
        return 0.25;
    case 3:
        return 0.125;
    case 4:
        return 1.0 / 3.0;
    case 5:
        return 1.0 / 6.0;
    default:
        return 0.25;
    }
}

/// perf.voice_bank: 0 = archetypes; 1 = learned placeholder (F3). WP-C5.
enum class PerfVoiceBank { Archetypes = 0, Learned = 1 };

inline constexpr std::array<const char*, 2> kPerfVoiceBankNames{{"archetypes", "learned"}};

[[nodiscard]] inline int perfVoiceBankIndexFromValue(double v) noexcept {
    const int i = static_cast<int>(std::lround(v));
    return std::clamp(i, 0, 1);
}

[[nodiscard]] inline bool parsePerfVoiceBankToken(std::string_view s, int& out) noexcept {
    if (s == "archetypes" || s == "0") {
        out = 0;
        return true;
    }
    if (s == "learned" || s == "1") {
        out = 1;
        return true;
    }
    return false;
}

/// voice.type: 0 = whistle (default, D-022), 1 = anatomical engine (experimental, WP-R2). WP-R1.
inline constexpr std::array<const char*, 2> kVoiceTypeNames{{"whistle", "anatomical"}};
/// perf.note_rule: which part of a syllable is its note (D-019). Same order as control::NoteRule.
inline constexpr std::array<const char*, 4> kNoteRuleNames{{"rule", "steady", "landing", "average"}};
/// perf.mapping: 0 = own height (default), 1 = follow the keyboard (D-020).
inline constexpr std::array<const char*, 2> kMappingNames{{"own_height", "follow"}};
/// perf.order: 0 = random start (default), 1 = phrase start, 2 = shuffled. Same order as control::OrderMode.
inline constexpr std::array<const char*, 3> kOrderNames{{"random", "phrase", "shuffled"}};

/// Choice index from a stored enum parameter value (rounded, clamped to [0, count − 1]).
[[nodiscard]] inline int enumIndexFromValue(double v, int count) noexcept {
    return std::clamp(static_cast<int>(std::lround(v)), 0, std::max(0, count - 1));
}

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
