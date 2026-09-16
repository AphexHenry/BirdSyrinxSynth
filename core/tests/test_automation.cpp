#include "cuicui/io/render_spec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using cuicui::io::Automation;
using cuicui::io::Breakpoint;
using Catch::Matchers::WithinAbs;

TEST_CASE("automation empty uses fallback") {
    Automation a;
    a.fallback = 7.5;
    REQUIRE_THAT(a.valueAt(0.0), WithinAbs(7.5, 1e-15));
    REQUIRE_THAT(a.valueAt(10.0), WithinAbs(7.5, 1e-15));
}

TEST_CASE("automation single breakpoint is constant") {
    Automation a;
    a.points.push_back(Breakpoint{1.0, 3.0});
    REQUIRE_THAT(a.valueAt(0.0), WithinAbs(3.0, 1e-15));
    REQUIRE_THAT(a.valueAt(1.0), WithinAbs(3.0, 1e-15));
    REQUIRE_THAT(a.valueAt(2.0), WithinAbs(3.0, 1e-15));
}

TEST_CASE("automation holds first and last, interpolates linearly between") {
    Automation a;
    a.points = {{0.0, 0.0}, {1.0, 10.0}, {3.0, 0.0}};
    REQUIRE_THAT(a.valueAt(-1.0), WithinAbs(0.0, 1e-15));
    REQUIRE_THAT(a.valueAt(0.0), WithinAbs(0.0, 1e-15));
    REQUIRE_THAT(a.valueAt(0.5), WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(a.valueAt(1.0), WithinAbs(10.0, 1e-15));
    REQUIRE_THAT(a.valueAt(2.0), WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(a.valueAt(3.0), WithinAbs(0.0, 1e-15));
    REQUIRE_THAT(a.valueAt(4.0), WithinAbs(0.0, 1e-15));
}

TEST_CASE("automation sort puts unsorted breakpoints in time order") {
    Automation a;
    a.points = {{1.0, 10.0}, {0.0, 0.0}};
    a.sort();
    REQUIRE_THAT(a.valueAt(0.5), WithinAbs(5.0, 1e-12));
}
