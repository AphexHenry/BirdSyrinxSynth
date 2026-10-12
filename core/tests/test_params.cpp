#include "cuicui/params.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

using Catch::Matchers::WithinAbs;

TEST_CASE("findParam locates registry entries") {
    const auto* p = cuicui::findParam("src.L.beta");
    REQUIRE(p != nullptr);
    REQUIRE(p->pid == cuicui::ParamId::SrcLBeta);
    REQUIRE(p->fast);
    REQUIRE(p->min == Catch::Approx(-0.3));
    REQUIRE(cuicui::findParam("not.a.param") == nullptr);
}

TEST_CASE("normalise/denormalise round trip") {
    for (const auto& s : cuicui::kParamTable) {
        const double mid = 0.5 * (s.min + s.max);
        const double u = cuicui::toNormalised(s, mid);
        const double back = cuicui::fromNormalised(s, u);
        REQUIRE_THAT(back, WithinAbs(mid, 1e-6));
        REQUIRE_THAT(cuicui::fromNormalised(s, 0.0), WithinAbs(s.min, 1e-12));
        REQUIRE_THAT(cuicui::fromNormalised(s, 1.0), WithinAbs(s.max, 1e-9));
        REQUIRE_THAT(cuicui::fromNormalised(s, cuicui::toNormalised(s, s.def)), WithinAbs(s.def, 1e-6));
    }
}

TEST_CASE("clampParam respects min/max") {
    const auto& s = cuicui::specOf(cuicui::ParamId::OutGainDb);
    REQUIRE_THAT(cuicui::clampParam(s, -100.0), WithinAbs(s.min, 1e-15));
    REQUIRE_THAT(cuicui::clampParam(s, 40.0), WithinAbs(s.max, 1e-15));
}

TEST_CASE("test.waveform names") {
    cuicui::TestWaveform w{};
    REQUIRE(cuicui::parseTestWaveform("saw", w));
    REQUIRE(w == cuicui::TestWaveform::Saw);
    REQUIRE(std::string(cuicui::testWaveformName(w)) == "saw");
}

TEST_CASE("version hint from sinceWP and oversampling choice map") {
    REQUIRE(cuicui::versionHintFromSinceWP("A2") == 1);
    REQUIRE(cuicui::versionHintFromSinceWP("B6") == 2);
    REQUIRE(cuicui::versionHintFromSinceWP("C1") == 3);
    REQUIRE(cuicui::oversamplingChoiceIndexFromValue(0) == 0);
    REQUIRE(cuicui::oversamplingChoiceIndexFromValue(4) == 3);
    REQUIRE(cuicui::oversamplingValueFromChoiceIndex(3) == 4);
    REQUIRE(cuicui::findParam("out.master_db") != nullptr);
    REQUIRE(cuicui::specOf(cuicui::ParamId::OutMasterDb).def == Catch::Approx(-6.0));
    REQUIRE(cuicui::findParam("perf.breath") != nullptr);
    REQUIRE(cuicui::specOf(cuicui::ParamId::PerfBreath).def == Catch::Approx(0.7));
    REQUIRE(cuicui::findParam("perf.leash") != nullptr);
    REQUIRE(cuicui::specOf(cuicui::ParamId::PerfLeash).def == Catch::Approx(0.0));
    REQUIRE(cuicui::findParam("perf.variation") != nullptr);
    REQUIRE(cuicui::findParam("perf.density") != nullptr);
    REQUIRE(cuicui::findParam("perf.polyphony") != nullptr);
    // D-025 (WP-R1): 4 keys sing at once by default, as in the PROTO1 prototype (C5 had 1).
    REQUIRE(cuicui::specOf(cuicui::ParamId::PerfPolyphony).def == Catch::Approx(4.0));
    int div = -1;
    REQUIRE(cuicui::parsePerfSyncDivisionToken("1/16", div));
    REQUIRE(div == 2);
    REQUIRE_THAT(cuicui::syncDivisionQuarterNotes(2), WithinAbs(0.25, 1e-15));
    int vb = -1;
    REQUIRE(cuicui::parsePerfVoiceBankToken("archetypes", vb));
    REQUIRE(vb == 0);
    REQUIRE(cuicui::findParam("perf.mode") != nullptr);
    int mode = -1;
    REQUIRE(cuicui::parsePerfModeToken("perform", mode));
    REQUIRE(mode == 0);
    int sc = -1;
    REQUIRE(cuicui::parsePerfScaleToken("pentatonic_minor", sc));
    REQUIRE(sc == 4);
}

TEST_CASE("WP-R1 parameters: ids, defaults, version hint") {
    REQUIRE(cuicui::versionHintFromSinceWP("R1") == 8);
    struct Expect {
        const char* id;
        double def, min, max;
    };
    for (const auto& e : {Expect{"voice.type", 0.0, 0.0, 1.0}, Expect{"voice.harmonics", 1.0, 0.0, 1.0},
                          Expect{"perf.note_rule", 0.0, 0.0, 3.0}, Expect{"perf.mapping", 0.0, 0.0, 1.0},
                          Expect{"perf.first_on_note", 1.0, 0.0, 1.0}, Expect{"perf.order", 0.0, 0.0, 2.0},
                          Expect{"perf.pause_s", 0.5, 0.1, 2.0}, Expect{"perf.size_st", 0.0, -12.0, 12.0},
                          Expect{"out.room", 1.0, 0.0, 1.0}}) {
        const auto* p = cuicui::findParam(e.id);
        REQUIRE(p != nullptr);
        CHECK(p->def == Catch::Approx(e.def));
        CHECK(p->min == Catch::Approx(e.min));
        CHECK(p->max == Catch::Approx(e.max));
        CHECK(std::string(p->sinceWP) == "R1");
    }
    CHECK(cuicui::kNoteRuleNames.size() == 4);
    CHECK(cuicui::kOrderNames.size() == 3);
    CHECK(cuicui::enumIndexFromValue(2.4, 3) == 2);
    CHECK(cuicui::enumIndexFromValue(7.0, 3) == 2);
}
