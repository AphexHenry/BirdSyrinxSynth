#include "cuicui/model/body_scaling.hpp"
#include "cuicui/model/trachea.hpp"
#include "cuicui/model/upper_vocal_tract.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("body size scales gamma, length, Helmholtz, time constants") {
    const auto a1 = cuicui::model::scaleAnatomy(1.0, 24000.0, 33.0, 300.0, 8.0, 15.0);
    const auto a2 = cuicui::model::scaleAnatomy(2.0, 24000.0, 33.0, 300.0, 8.0, 15.0);
    REQUIRE_THAT(a2.gamma, WithinRel(a1.gamma * 0.5, 1e-12));
    REQUIRE_THAT(a2.tracheaLengthMm, WithinRel(a1.tracheaLengthMm * 2.0, 1e-12));
    REQUIRE_THAT(a2.tensionBandwidthHz, WithinRel(a1.tensionBandwidthHz * 0.5, 1e-12));
    REQUIRE_THAT(a2.aspirationCentreHz, WithinRel(a1.aspirationCentreHz * 0.5, 1e-12));
    REQUIRE_THAT(a2.respAttackMs, WithinRel(a1.respAttackMs * std::sqrt(2.0), 1e-12));
    REQUIRE_THAT(a2.respReleaseMs, WithinRel(a1.respReleaseMs * std::sqrt(2.0), 1e-12));
    REQUIRE_THAT(a2.sizeScale, WithinAbs(2.0, 1e-15));
}

TEST_CASE("UVT Helmholtz and trachea delay halve/double at σ=2") {
    const auto p1 = cuicui::model::uvtPhysical(3000.0, 0.1, 0.5, 0.3, 1.0);
    const auto p2 = cuicui::model::uvtPhysical(3000.0, 0.1, 0.5, 0.3, 2.0);
    REQUIRE_THAT(p2.fH, WithinRel(p1.fH * 0.5, 0.02));

    cuicui::model::Trachea t1, t2;
    t1.prepare(192000.0);
    t2.prepare(192000.0);
    t1.setParams(33.0, 0.5, 0.3, 1.0);
    t2.setParams(66.0, 0.5, 0.3, 1.0);
    REQUIRE_THAT(t2.delaySamples(), WithinRel(t1.delaySamples() * 2.0, 0.02));
}
