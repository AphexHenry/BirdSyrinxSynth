#include "cuicui/version.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("version string is non-empty") {
    REQUIRE(cuicui::kVersion != nullptr);
    REQUIRE(std::strlen(cuicui::kVersion) > 0);
    REQUIRE(cuicui::kGitHash != nullptr);
    REQUIRE(std::strlen(cuicui::kGitHash) > 0);
}
