#include "cuicui/dsp/rng.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

TEST_CASE("PRNG same seed yields the same sequence") {
    cuicui::dsp::Rng a(42);
    cuicui::dsp::Rng b(42);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(a.next() == b.next());
    }
}

TEST_CASE("PRNG different seeds differ") {
    cuicui::dsp::Rng a(1);
    cuicui::dsp::Rng b(2);
    bool differ = false;
    for (int i = 0; i < 16; ++i) {
        if (a.next() != b.next()) {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("PRNG uniform is in [0, 1)") {
    cuicui::dsp::Rng rng(7);
    for (int i = 0; i < 10000; ++i) {
        const float u = rng.uniform();
        REQUIRE(u >= 0.0f);
        REQUIRE(u < 1.0f);
    }
}

TEST_CASE("PRNG gaussian has roughly zero mean and unit variance") {
    cuicui::dsp::Rng rng(99);
    constexpr int n = 20000;
    double sum = 0.0;
    double sumsq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = rng.gaussian();
        sum += x;
        sumsq += x * x;
    }
    const double mean = sum / n;
    const double var = sumsq / n - mean * mean;
    REQUIRE(std::abs(mean) < 0.05);
    REQUIRE(std::abs(var - 1.0) < 0.08);
}

TEST_CASE("PRNG setSeed restores a previous sequence") {
    cuicui::dsp::Rng rng(123);
    std::vector<std::uint32_t> first(8);
    for (auto& x : first) {
        x = rng.next();
    }
    rng.setSeed(123);
    for (auto x : first) {
        REQUIRE(rng.next() == x);
    }
}
