#include "cuicui/control/archetypes.hpp"
#include "cuicui/control/gesture.hpp"
#include "cuicui/dsp/rng.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

bool finiteCurves(const cuicui::control::GestureCurves& c) {
    auto ok = [](double v) { return std::isfinite(v); };
    for (int i = 0; i < cuicui::control::GestureCurves::kN; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (!ok(c.pitchCents[k]) || !ok(c.ampDb[k]) || !ok(c.tone[k]) || !ok(c.side[k])) {
            return false;
        }
    }
    return true;
}

double ampPeak(const cuicui::control::GestureCurves& c) {
    double mx = -1.0e9;
    for (double v : c.ampDb) {
        mx = std::max(mx, v);
    }
    return mx;
}

bool toneSideInRange(const cuicui::control::GestureCurves& c) {
    for (int i = 0; i < cuicui::control::GestureCurves::kN; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (c.tone[k] < -1.0e-9 || c.tone[k] > 1.0 + 1.0e-9) {
            return false;
        }
        if (c.side[k] < -1.0 - 1.0e-9 || c.side[k] > 1.0 + 1.0e-9) {
            return false;
        }
    }
    return true;
}

double maxPitchJump(const cuicui::control::GestureCurves& c) {
    double mx = 0.0;
    for (int i = 0; i < cuicui::control::GestureCurves::kN - 1; ++i) {
        const double d = std::abs(c.pitchCents[static_cast<std::size_t>(i + 1)] -
                                  c.pitchCents[static_cast<std::size_t>(i)]);
        mx = std::max(mx, d);
    }
    return mx;
}

cuicui::control::ArchetypeParamBag randomBag(const cuicui::control::ArchetypeInfo& info,
                                             cuicui::dsp::Rng& rng) {
    cuicui::control::ArchetypeParamBag bag;
    for (int i = 0; i < info.nParams; ++i) {
        const auto& p = info.params[i];
        const double u = static_cast<double>(rng.uniform());
        bag.num[p.name] = p.min + u * (p.max - p.min);
    }
    return bag;
}

} // namespace

TEST_CASE("envDb plateau is 0 dB and ends at -60") {
    REQUIRE_THAT(cuicui::control::envDb(0.0, 0.1, 0.15), WithinAbs(-60.0, 1e-9));
    REQUIRE_THAT(cuicui::control::envDb(1.0, 0.1, 0.15), WithinAbs(-60.0, 1e-9));
    REQUIRE_THAT(cuicui::control::envDb(0.5, 0.1, 0.15), WithinAbs(0.0, 1e-9));
}

TEST_CASE("evaluateCurves is monotone on a rising ramp") {
    cuicui::control::GestureCurves c{};
    for (int i = 0; i < cuicui::control::GestureCurves::kN; ++i) {
        const double u = static_cast<double>(i) / 63.0;
        c.pitchCents[static_cast<std::size_t>(i)] = 100.0 * u;
        c.ampDb[static_cast<std::size_t>(i)] = 0.0;
        c.tone[static_cast<std::size_t>(i)] = 0.5;
        c.side[static_cast<std::size_t>(i)] = -1.0;
    }
    double prev = -1.0e9;
    for (int k = 0; k <= 200; ++k) {
        const double u = static_cast<double>(k) / 200.0;
        const auto e = cuicui::control::evaluateCurves(c, u);
        REQUIRE(e.pitchCents + 1.0e-9 >= prev);
        prev = e.pitchCents;
    }
}

TEST_CASE("side evaluation keeps a mid-grid step") {
    cuicui::control::GestureCurves c{};
    for (int i = 0; i < cuicui::control::GestureCurves::kN; ++i) {
        const double u = static_cast<double>(i) / 63.0;
        c.side[static_cast<std::size_t>(i)] = (u < 0.5) ? 1.0 : -1.0;
        c.pitchCents[static_cast<std::size_t>(i)] = 0.0;
        c.ampDb[static_cast<std::size_t>(i)] = 0.0;
        c.tone[static_cast<std::size_t>(i)] = 0.5;
    }
    REQUIRE_THAT(cuicui::control::evaluateCurves(c, 0.25).side, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(cuicui::control::evaluateCurves(c, 0.75).side, WithinAbs(-1.0, 1e-9));
}

TEST_CASE("every archetype at defaults: finite, amp peak 0 dB, ranges") {
    for (const auto& info : cuicui::control::kArchetypes) {
        auto g = cuicui::control::generateArchetype(info.name, cuicui::control::ArchetypeParamBag{}, 1);
        REQUIRE(finiteCurves(g.curves));
        REQUIRE_THAT(ampPeak(g.curves), WithinAbs(0.0, 0.01));
        REQUIRE(toneSideInRange(g.curves));
        const bool trill = std::string(info.name) == "trill";
        const bool sw = std::string(info.name) == "switch_sweep";
        if (!trill) {
            REQUIRE(maxPitchJump(g.curves) < 400.0);
        }
        (void)sw;
        REQUIRE(g.durationMs > 0.0);
    }
}

TEST_CASE("every archetype at 50 random parameter sets within range") {
    for (const auto& info : cuicui::control::kArchetypes) {
        cuicui::dsp::Rng rng(cuicui::dsp::deriveStreamSeed(99, static_cast<int>(info.name[0])));
        for (int n = 0; n < 50; ++n) {
            auto bag = randomBag(info, rng);
            auto g = cuicui::control::generateArchetype(info.name, bag, static_cast<std::uint64_t>(n + 1));
            REQUIRE(finiteCurves(g.curves));
            REQUIRE_THAT(ampPeak(g.curves), WithinAbs(0.0, 0.01));
            REQUIRE(toneSideInRange(g.curves));
            if (std::string(info.name) != "trill") {
                REQUIRE(maxPitchJump(g.curves) < 400.0);
            }
        }
    }
}

TEST_CASE("same params + seed produce identical curves; buzz differs with seed") {
    cuicui::control::ArchetypeParamBag bag;
    auto a = cuicui::control::generateArchetype("whistle", bag, 1);
    auto b = cuicui::control::generateArchetype("whistle", bag, 1);
    REQUIRE(a.curves.pitchCents == b.curves.pitchCents);
    auto z1 = cuicui::control::generateArchetype("buzz", bag, 1);
    auto z1b = cuicui::control::generateArchetype("buzz", bag, 1);
    auto z2 = cuicui::control::generateArchetype("buzz", bag, 2);
    REQUIRE(z1.curves.pitchCents == z1b.curves.pitchCents);
    REQUIRE(z1.curves.pitchCents != z2.curves.pitchCents);
}

TEST_CASE("whistle arch peaks near +arch at u=0.5") {
    auto g = cuicui::control::generateArchetype("whistle", cuicui::control::ArchetypeParamBag{}, 1);
    const auto e = cuicui::control::evaluateCurves(g.curves, 0.5);
    REQUIRE_THAT(e.pitchCents, WithinAbs(60.0, 1.0));
}

TEST_CASE("upsweep ends at 0, starts near -depth") {
    auto g = cuicui::control::generateArchetype("upsweep", cuicui::control::ArchetypeParamBag{}, 1);
    REQUIRE_THAT(g.curves.pitchCents.front(), WithinAbs(-900.0, 1.0));
    REQUIRE_THAT(g.curves.pitchCents.back(), WithinAbs(0.0, 1.0));
}

TEST_CASE("two_voice sets secondVoiceCents hint and side 0") {
    auto g = cuicui::control::generateArchetype("two_voice", cuicui::control::ArchetypeParamBag{}, 1);
    REQUIRE(cuicui::control::hintSet(g.hints.secondVoiceCents));
    REQUIRE_THAT(g.hints.secondVoiceCents, WithinAbs(380.0, 1e-9));
    REQUIRE_THAT(g.curves.side[0], WithinAbs(0.0, 1e-9));
}

TEST_CASE("stack sets oecRatio hint") {
    auto g = cuicui::control::generateArchetype("stack", cuicui::control::ArchetypeParamBag{}, 1);
    REQUIRE_THAT(g.hints.oecRatio, WithinAbs(5.0, 1e-9));
}

TEST_CASE("switch_sweep side steps from +1 to -1") {
    auto g = cuicui::control::generateArchetype("switch_sweep", cuicui::control::ArchetypeParamBag{}, 1);
    REQUIRE_THAT(g.curves.side.front(), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(g.curves.side.back(), WithinAbs(-1.0, 1e-9));
}

TEST_CASE("movementScore is higher for a sweep than a whistle") {
    auto w = cuicui::control::generateArchetype("whistle", cuicui::control::ArchetypeParamBag{}, 1);
    auto u = cuicui::control::generateArchetype("upsweep", cuicui::control::ArchetypeParamBag{}, 1);
    REQUIRE(cuicui::control::movementScore(u.curves) > cuicui::control::movementScore(w.curves));
}
