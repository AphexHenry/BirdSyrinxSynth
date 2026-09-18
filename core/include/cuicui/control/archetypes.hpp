#pragma once

#include "cuicui/control/gesture.hpp"
#include "cuicui/dsp/rng.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cuicui::control {

/// Raised-cosine 0→1 for x in [0, 1].
[[nodiscard]] inline double raisedCosine01(double x) noexcept {
    x = std::clamp(x, 0.0, 1.0);
    return 0.5 * (1.0 - std::cos(std::numbers::pi * x));
}

/// Amplitude envelope in dB: attack fraction `a`, release `r`, plateau 0 dB, ends −60 dB.
/// See WP-C4 §4.2. Real-time safe.
[[nodiscard]] inline double envDb(double u, double a, double r) noexcept {
    constexpr double kFloor = -60.0;
    u = std::clamp(u, 0.0, 1.0);
    a = std::max(0.0, a);
    r = std::max(0.0, r);
    if (a + r > 1.0 && a + r > 1.0e-12) {
        const double split = a / (a + r);
        if (u <= split) {
            const double t = (split > 0.0) ? u / split : 1.0;
            return kFloor + (0.0 - kFloor) * raisedCosine01(t);
        }
        const double t = ((1.0 - split) > 0.0) ? (1.0 - u) / (1.0 - split) : 1.0;
        return kFloor + (0.0 - kFloor) * raisedCosine01(t);
    }
    if (a > 0.0 && u < a) {
        return kFloor + (0.0 - kFloor) * raisedCosine01(u / a);
    }
    if (r > 0.0 && u > 1.0 - r) {
        return kFloor + (0.0 - kFloor) * raisedCosine01((1.0 - u) / r);
    }
    return 0.0;
}

[[nodiscard]] inline double sigmoidS(double u, double k) noexcept {
    u = std::clamp(u, 0.0, 1.0);
    if (std::abs(k) < 1.0e-6) {
        return u;
    }
    const double den = 1.0 - std::exp(-k);
    if (std::abs(den) < 1.0e-18) {
        return u;
    }
    return (1.0 - std::exp(-k * u)) / den;
}

struct ArchetypeParamSpec {
    const char* name;
    double def;
    double min;
    double max;
    bool isChoice; // e.g. anchor_at: 0 = start, 1 = end
};

struct ArchetypeInfo {
    const char* name;
    const ArchetypeParamSpec* params;
    int nParams;
};

inline constexpr ArchetypeParamSpec kWhistleParams[] = {
    {"dur", 250.0, 80.0, 800.0, false},
    {"arch", 60.0, -300.0, 300.0, false},
    {"tone", 0.9, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kUpsweepParams[] = {
    {"dur", 120.0, 30.0, 400.0, false},
    {"depth", 900.0, 100.0, 2400.0, false},
    {"curvature", 2.0, -6.0, 6.0, false},
    {"anchor_at", 1.0, 0.0, 1.0, true}, // 1 = end
};
inline constexpr ArchetypeParamSpec kDownsweepParams[] = {
    {"dur", 120.0, 30.0, 400.0, false},
    {"depth", 900.0, 100.0, 2400.0, false},
    {"curvature", 2.0, -6.0, 6.0, false},
    {"anchor_at", 0.0, 0.0, 1.0, true}, // 0 = start
};
inline constexpr ArchetypeParamSpec kArchParams[] = {
    {"dur", 180.0, 80.0, 800.0, false},
    {"depth", 500.0, -1200.0, 1200.0, false},
    {"sharpness", 1.5, 0.5, 4.0, false},
    {"tone", 0.8, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kTrillParams[] = {
    {"dur", 400.0, 100.0, 2000.0, false},
    {"rate", 22.0, 6.0, 40.0, false},
    {"element_depth", 400.0, 50.0, 1500.0, false},
    {"duty", 0.7, 0.2, 1.0, false},
    {"tone", 0.7, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kBuzzParams[] = {
    {"dur", 200.0, 50.0, 800.0, false},
    {"wobble", 30.0, 0.0, 200.0, false},
    {"tone", 0.12, 0.0, 0.3, false},
};
inline constexpr ArchetypeParamSpec kStackParams[] = {
    {"dur", 150.0, 60.0, 400.0, false},
    {"droop", -30.0, -200.0, 50.0, false},
    {"tone", 0.28, 0.0, 0.5, false},
    {"oec_ratio", 5.0, 2.0, 10.0, false},
};
inline constexpr ArchetypeParamSpec kChipParams[] = {
    {"dur", 20.0, 8.0, 40.0, false},
    {"depth", 700.0, 100.0, 2000.0, false},
    {"sharpness", 2.0, 0.5, 6.0, false},
    {"tone", 0.6, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kTwoVoiceParams[] = {
    {"dur", 200.0, 80.0, 800.0, false},
    {"interval", 380.0, -1200.0, 1200.0, false},
    {"tone", 0.5, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kSwitchSweepParams[] = {
    {"dur", 150.0, 60.0, 400.0, false},
    {"depth", 2000.0, 400.0, 2400.0, false},
    {"crossover", 0.5, 0.1, 0.9, false},
    {"tone", 0.8, 0.0, 1.0, false},
};
inline constexpr ArchetypeParamSpec kIntroParams[] = {
    {"dur", 60.0, 30.0, 120.0, false},
    {"depth", 300.0, 50.0, 800.0, false},
    {"tone", 0.35, 0.0, 1.0, false},
};

inline constexpr ArchetypeInfo kArchetypes[] = {
    {"whistle", kWhistleParams, 3},
    {"upsweep", kUpsweepParams, 4},
    {"downsweep", kDownsweepParams, 4},
    {"arch", kArchParams, 4},
    {"trill", kTrillParams, 5},
    {"buzz", kBuzzParams, 3},
    {"stack", kStackParams, 4},
    {"chip", kChipParams, 4},
    {"two_voice", kTwoVoiceParams, 3},
    {"switch_sweep", kSwitchSweepParams, 4},
    {"intro", kIntroParams, 3},
};

[[nodiscard]] inline const ArchetypeInfo* findArchetype(std::string_view name) noexcept {
    for (const auto& a : kArchetypes) {
        if (name == a.name) {
            return &a;
        }
    }
    return nullptr;
}

[[nodiscard]] inline std::vector<std::string> archetypeNames() {
    std::vector<std::string> out;
    out.reserve(std::size(kArchetypes));
    for (const auto& a : kArchetypes) {
        out.emplace_back(a.name);
    }
    return out;
}

struct ArchetypeParamBag {
    std::unordered_map<std::string, double> num;
    std::unordered_map<std::string, std::string> str;

    [[nodiscard]] double get(const char* key, double def) const {
        if (const auto it = num.find(key); it != num.end()) {
            return it->second;
        }
        if (const auto it = str.find(key); it != str.end()) {
            if (it->second == "end") {
                return 1.0;
            }
            if (it->second == "start") {
                return 0.0;
            }
        }
        return def;
    }
};

inline double clampSpec(const ArchetypeParamSpec& s, double v) noexcept {
    return std::clamp(v, s.min, s.max);
}

inline double lookupParam(const ArchetypeInfo& info, const ArchetypeParamBag& bag, const char* key) {
    for (int i = 0; i < info.nParams; ++i) {
        if (std::strcmp(info.params[i].name, key) == 0) {
            return clampSpec(info.params[i], bag.get(key, info.params[i].def));
        }
    }
    return bag.get(key, 0.0);
}

inline void fillU(double* u) noexcept {
    constexpr int n = GestureCurves::kN;
    for (int i = 0; i < n; ++i) {
        u[i] = static_cast<double>(i) / static_cast<double>(n - 1);
    }
}

inline void peakNormAmp(GestureCurves& c) noexcept {
    double mx = -1.0e9;
    for (double v : c.ampDb) {
        mx = std::max(mx, v);
    }
    if (!std::isfinite(mx)) {
        return;
    }
    const double shift = 0.0 - mx;
    if (std::abs(shift) < 1.0e-15) {
        return;
    }
    for (double& v : c.ampDb) {
        v += shift;
    }
}

inline void defaultToneSide(GestureCurves& c, double tone, double side) noexcept {
    for (int i = 0; i < GestureCurves::kN; ++i) {
        c.tone[static_cast<std::size_t>(i)] = std::clamp(tone, 0.0, 1.0);
        c.side[static_cast<std::size_t>(i)] = std::clamp(side, -1.0, 1.0);
    }
}

inline GestureInstance makeBlank(double durMs) noexcept {
    GestureInstance g;
    g.durationMs = durMs;
    g.tStart = 0.0;
    g.anchorCents = 0.0;
    return g;
}

inline GestureInstance generateWhistle(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("whistle");
    const double dur = lookupParam(info, bag, "dur");
    const double arch = lookupParam(info, bag, "arch");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] = arch * 4.0 * uu * (1.0 - uu);
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.1, 0.15);
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateUpsweep(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("upsweep");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double k = lookupParam(info, bag, "curvature");
    const double tone = bag.get("tone", 0.8);
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        const double s = sigmoidS(uu, k);
        g.curves.pitchCents[static_cast<std::size_t>(i)] = -depth * (1.0 - s);
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.1, 0.15);
    }
    defaultToneSide(g.curves, std::clamp(tone, 0.0, 1.0), -1.0);
    peakNormAmp(g.curves);
    (void)lookupParam(info, bag, "anchor_at");
    return g;
}

inline GestureInstance generateDownsweep(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("downsweep");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double k = lookupParam(info, bag, "curvature");
    const double tone = bag.get("tone", 0.8);
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        const double s = sigmoidS(uu, k);
        g.curves.pitchCents[static_cast<std::size_t>(i)] = -depth * s;
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.1, 0.15);
    }
    defaultToneSide(g.curves, std::clamp(tone, 0.0, 1.0), -1.0);
    peakNormAmp(g.curves);
    (void)lookupParam(info, bag, "anchor_at");
    return g;
}

inline GestureInstance generateArch(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("arch");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double p = lookupParam(info, bag, "sharpness");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] =
            depth * std::pow(std::sin(std::numbers::pi * uu), p);
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.1, 0.15);
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateTrill(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("trill");
    const double dur = lookupParam(info, bag, "dur");
    const double rate = lookupParam(info, bag, "rate");
    const double depth = lookupParam(info, bag, "element_depth");
    const double duty = lookupParam(info, bag, "duty");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    const double nEl = std::max(1.0, std::round(rate * (dur * 0.001)));
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        const double phase = uu * nEl;
        double phi = phase - std::floor(phase);
        if (uu >= 1.0) {
            phi = 1.0;
        }
        constexpr double kEdge = 0.1;
        double fade = 1.0;
        if (phi < kEdge) {
            fade = raisedCosine01(phi / kEdge);
        } else if (phi > 1.0 - kEdge) {
            fade = raisedCosine01((1.0 - phi) / kEdge);
        }
        g.curves.pitchCents[static_cast<std::size_t>(i)] = depth * (1.0 - phi) * fade;
        double gateDb = -60.0;
        if (phi <= duty) {
            const double local = (duty > 1.0e-9) ? phi / duty : 1.0;
            gateDb = envDb(local, 0.1, 0.1);
        }
        const double env = envDb(uu, 0.05, 0.08);
        const double lin = std::pow(10.0, env / 20.0) * std::pow(10.0, gateDb / 20.0);
        g.curves.ampDb[static_cast<std::size_t>(i)] = 20.0 * std::log10(std::max(lin, 1.0e-6));
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateBuzz(const ArchetypeParamBag& bag, std::uint64_t seed) {
    const auto& info = *findArchetype("buzz");
    const double dur = lookupParam(info, bag, "dur");
    const double wobble = lookupParam(info, bag, "wobble");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    const double fs = static_cast<double>(GestureCurves::kN - 1) / std::max(dur * 0.001, 1.0e-4);
    const double fc = std::min(20.0, 0.4 * fs);
    const double a = (fs > 0.0) ? std::exp(-2.0 * std::numbers::pi * fc / fs) : 0.0;
    dsp::Rng rng(dsp::deriveStreamSeed(seed == 0 ? 1ULL : seed, dsp::kStreamGesture));
    double walk = 0.0;
    double y = 0.0;
    double mx = 0.0;
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        walk += static_cast<double>(rng.gaussian());
        y = (1.0 - a) * walk + a * y;
        g.curves.pitchCents[static_cast<std::size_t>(i)] = y;
        mx = std::max(mx, std::abs(y));
        const double tSec = uu * (dur * 0.001);
        // 18 Hz AM rattle (B4 listening): syllable, not broadband hiss.
        const double am = 0.55 + 0.45 * (0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 18.0 * tSec));
        const double env = envDb(uu, 0.12, 0.28);
        g.curves.ampDb[static_cast<std::size_t>(i)] = env + 20.0 * std::log10(std::max(am, 1.0e-3));
    }
    if (mx > 1.0e-12 && wobble > 0.0) {
        const double sc = wobble / mx;
        for (int i = 0; i < GestureCurves::kN; ++i) {
            const double uu = u[i];
            const double tSec = uu * (dur * 0.001);
            const double fm = 18.0 * std::sin(2.0 * std::numbers::pi * 18.0 * tSec);
            g.curves.pitchCents[static_cast<std::size_t>(i)] =
                g.curves.pitchCents[static_cast<std::size_t>(i)] * sc + fm;
        }
    } else {
        for (double& v : g.curves.pitchCents) {
            v = 0.0;
        }
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateStack(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("stack");
    const double dur = lookupParam(info, bag, "dur");
    const double droop = lookupParam(info, bag, "droop");
    const double tone = lookupParam(info, bag, "tone");
    const double oec = lookupParam(info, bag, "oec_ratio");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] = droop * uu;
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.12, 0.28);
    }
    defaultToneSide(g.curves, tone, -1.0);
    g.hints.oecRatio = oec;
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateChip(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("chip");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double sharp = lookupParam(info, bag, "sharpness");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] = depth * std::pow(1.0 - uu, sharp);
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.05, 0.4);
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateTwoVoice(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("two_voice");
    const double dur = lookupParam(info, bag, "dur");
    const double interval = lookupParam(info, bag, "interval");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] = -40.0 * uu;
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.15, 0.40);
    }
    defaultToneSide(g.curves, tone, 0.0);
    g.hints.secondVoiceCents = interval;
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateSwitchSweep(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("switch_sweep");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double xo = lookupParam(info, bag, "crossover");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        g.curves.pitchCents[static_cast<std::size_t>(i)] = depth * (1.0 - uu) - depth; // −depth·u
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.08, 0.12);
        g.curves.side[static_cast<std::size_t>(i)] = (uu < xo) ? 1.0 : -1.0;
        g.curves.tone[static_cast<std::size_t>(i)] = std::clamp(tone, 0.0, 1.0);
    }
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateIntro(const ArchetypeParamBag& bag) {
    const auto& info = *findArchetype("intro");
    const double dur = lookupParam(info, bag, "dur");
    const double depth = lookupParam(info, bag, "depth");
    const double tone = lookupParam(info, bag, "tone");
    auto g = makeBlank(dur);
    double u[GestureCurves::kN];
    fillU(u);
    for (int i = 0; i < GestureCurves::kN; ++i) {
        const double uu = u[i];
        const double om = 1.0 - uu;
        g.curves.pitchCents[static_cast<std::size_t>(i)] = depth * om * om;
        g.curves.ampDb[static_cast<std::size_t>(i)] = envDb(uu, 0.1, 0.3);
    }
    defaultToneSide(g.curves, tone, -1.0);
    peakNormAmp(g.curves);
    return g;
}

inline GestureInstance generateArchetype(std::string_view name,
                                         const ArchetypeParamBag& bag,
                                         std::uint64_t seed = 1) {
    if (name == "whistle") {
        return generateWhistle(bag);
    }
    if (name == "upsweep") {
        return generateUpsweep(bag);
    }
    if (name == "downsweep") {
        return generateDownsweep(bag);
    }
    if (name == "arch") {
        return generateArch(bag);
    }
    if (name == "trill") {
        return generateTrill(bag);
    }
    if (name == "buzz") {
        return generateBuzz(bag, seed);
    }
    if (name == "stack") {
        return generateStack(bag);
    }
    if (name == "chip") {
        return generateChip(bag);
    }
    if (name == "two_voice") {
        return generateTwoVoice(bag);
    }
    if (name == "switch_sweep") {
        return generateSwitchSweep(bag);
    }
    if (name == "intro") {
        return generateIntro(bag);
    }
    return {};
}

inline GestureInstance generateArchetype(std::string_view name,
                                         const std::unordered_map<std::string, double>& params,
                                         std::uint64_t seed = 1) {
    ArchetypeParamBag bag;
    bag.num = params;
    return generateArchetype(name, bag, seed);
}

} // namespace cuicui::control
