#include "cuicui/dsp/control_ramp.hpp"
#include "cuicui/dsp/smoother.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("smoother step response reaches 63.2% after tau") {
    constexpr double sr = 48000.0;
    constexpr double tau_s = 0.020;
    cuicui::dsp::Smoother sm;
    sm.prepare(sr);
    sm.setTimeConstantMs(tau_s * 1000.0);
    sm.snap(0.0f);
    sm.setTarget(1.0f);

    const int n = static_cast<int>(std::lround(tau_s * sr));
    float y = 0.0f;
    for (int i = 0; i < n; ++i) {
        y = sm.next();
    }
    const double expected = 1.0 - std::exp(-1.0); // 0.63212...
    REQUIRE_THAT(static_cast<double>(y), WithinRel(expected, 0.02));
}

TEST_CASE("smoother tau 0 snaps immediately") {
    cuicui::dsp::Smoother sm;
    sm.prepare(48000.0);
    sm.setTimeConstantMs(0.0);
    sm.snap(0.0f);
    sm.setTarget(5.0f);
    REQUIRE_THAT(sm.next(), WithinAbs(5.0f, 1e-6f));
}

TEST_CASE("control ramp interpolates linearly without allocation") {
    cuicui::dsp::ControlRamp ramp;
    ramp.set(0.0f, 10.0f, 5);
    REQUIRE_THAT(ramp.next(), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(ramp.next(), WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(ramp.next(), WithinAbs(4.0f, 1e-6f));
    REQUIRE_THAT(ramp.next(), WithinAbs(6.0f, 1e-6f));
    REQUIRE_THAT(ramp.next(), WithinAbs(8.0f, 1e-6f));
}
