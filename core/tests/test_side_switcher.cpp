#include "cuicui/control/side_switcher.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("side switcher equal-power crossfade and duration") {
    cuicui::control::SideSwitcher sw;
    constexpr double rate = 1000.0;
    sw.prepare(rate);
    sw.setCrossfadeMs(4.0);
    REQUIRE(sw.activeSide() == 0);

    auto idle = sw.next(0.2, 0.8);
    REQUIRE_THAT(idle.gateL, WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(idle.gateR, WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(idle.betaL, WithinAbs(0.2, 1e-15));
    REQUIRE_THAT(idle.betaR, WithinAbs(0.8, 1e-15));

    sw.setActive(1);
    const int nCross = static_cast<int>(std::lround(4.0 * 0.001 * rate));
    REQUIRE(nCross == 4);
    int fadeSamples = 0;
    double lastGL = 1.0, lastGR = 0.0;
    for (int i = 0; i < 20; ++i) {
        const auto t = sw.next(0.21, 0.81);
        REQUIRE_THAT(t.betaL, WithinAbs(0.21, 1e-15));
        REQUIRE_THAT(t.betaR, WithinAbs(0.81, 1e-15));
        const double power = t.gateL * t.gateL + t.gateR * t.gateR;
        REQUIRE_THAT(power, WithinAbs(1.0, 1e-9));
        ++fadeSamples;
        lastGL = t.gateL;
        lastGR = t.gateR;
        if (!sw.fading() && sw.activeSide() == 1) {
            break;
        }
    }
    REQUIRE(fadeSamples == nCross);
    REQUIRE_THAT(lastGL, WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(lastGR, WithinAbs(1.0, 1e-12));
    REQUIRE(sw.activeSide() == 1);

    const auto held = sw.next(0.3, 0.9);
    REQUIRE_THAT(held.gateL, WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(held.gateR, WithinAbs(1.0, 1e-12));
}

TEST_CASE("side switcher midpoint is equal-power") {
    cuicui::control::SideSwitcher sw;
    sw.prepare(1000.0);
    sw.setCrossfadeMs(4.0);
    sw.setActive(1);
    // 4 steps: u = 0.25, 0.5, 0.75, 1
    (void)sw.next(0.2, 0.5);
    const auto mid = sw.next(0.2, 0.5);
    const double g = std::sin(0.5 * std::numbers::pi * 0.5);
    REQUIRE_THAT(mid.gateR, WithinAbs(g, 1e-12));
    REQUIRE_THAT(mid.gateL, WithinAbs(g, 1e-12));
}

TEST_CASE("side switcher inactive beta keeps tracking") {
    cuicui::control::SideSwitcher sw;
    sw.prepare(2000.0);
    sw.setCrossfadeMs(2.0);
    std::vector<double> betaR;
    for (int i = 0; i < 10; ++i) {
        const double bR = 0.4 + 0.01 * static_cast<double>(i);
        const auto t = sw.next(0.2, bR);
        REQUIRE_THAT(t.gateL, WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(t.betaR, WithinAbs(bR, 1e-15));
        betaR.push_back(t.betaR);
    }
    REQUIRE(betaR.front() != betaR.back());
}
