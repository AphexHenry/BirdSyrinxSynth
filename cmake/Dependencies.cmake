cmake_minimum_required(VERSION 3.25)

include(FetchContent)

# Catch2 v3 — C++ tests
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)

# nlohmann/json — offline/IO only, never on the audio thread
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2 nlohmann_json)

# JUCE 8.x + clap-juce-extensions (WP-C1). JUCE 8 has no native CLAP (see decisions.md D-016).
if(CUICUI_BUILD_PLUGIN)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
    )

    FetchContent_Declare(
        clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
        GIT_SUBMODULES_RECURSE TRUE
    )

    FetchContent_MakeAvailable(juce clap_juce_extensions)
endif()

# nanobind — Python module cuicui_core (A2). Prefer a pip/uv install (pypi);
# fall back to FetchContent if CMake config is not on CMAKE_PREFIX_PATH.
if(CUICUI_BUILD_BINDINGS)
    find_package(Python 3.11 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG QUIET)
    if(NOT nanobind_FOUND)
        FetchContent_Declare(
            nanobind
            GIT_REPOSITORY https://github.com/wjakob/nanobind.git
            GIT_TAG v2.4.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(nanobind)
    endif()
endif()
