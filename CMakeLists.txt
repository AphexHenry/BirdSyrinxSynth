cmake_minimum_required(VERSION 3.25)
project(syrinx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUICUI_WERROR "Treat warnings as errors for core/ and apps/" OFF)
option(CUICUI_BUILD_BINDINGS "Build the nanobind module cuicui_core" ON)
option(CUICUI_BUILD_PLUGIN "Build the JUCE plugin, plugin_render, and processor tests" ON)

if(APPLE AND CUICUI_BUILD_PLUGIN)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include("${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake")

# Git hash injected into version.hpp (fallback "nogit" before the first commit, or without git).
set(CUICUI_GIT_HASH "nogit")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0 AND NOT _git_hash STREQUAL "")
        set(CUICUI_GIT_HASH "${_git_hash}")
    endif()
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/core/include/cuicui/version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/cuicui/version.hpp"
    @ONLY
)

add_library(cuicui_core INTERFACE)
target_include_directories(cuicui_core INTERFACE
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(cuicui_core INTERFACE nlohmann_json::nlohmann_json)

function(cuicui_set_warnings target)
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    if(CUICUI_WERROR)
        target_compile_options("${target}" PRIVATE -Werror)
    endif()
endfunction()

add_executable(bird_render apps/bird_render/main.cpp)
target_link_libraries(bird_render PRIVATE cuicui_core)
cuicui_set_warnings(bird_render)

add_executable(bird_bench apps/bird_bench/main.cpp)
target_link_libraries(bird_bench PRIVATE cuicui_core)
cuicui_set_warnings(bird_bench)

add_executable(cuicui_core_tests
    core/tests/test_automation.cpp
    core/tests/test_rng.cpp
    core/tests/test_wav.cpp
    core/tests/test_render_spec.cpp
    core/tests/test_tone.cpp
    core/tests/test_version.cpp
    core/tests/test_params.cpp
    core/tests/test_smoother.cpp
    core/tests/test_oversampling.cpp
    core/tests/test_bird_voice.cpp
    core/tests/test_alloc.cpp
    core/tests/test_labial_oscillator.cpp
    core/tests/test_trachea.cpp
    core/tests/test_upper_vocal_tract.cpp
    core/tests/test_respiration_noise.cpp
    core/tests/test_bilateral_source.cpp
    core/tests/test_side_switcher.cpp
    core/tests/test_body_scaling.cpp
    core/tests/test_voice_api.cpp
    core/tests/test_voice_golden.cpp
    core/tests/test_inverse_map.cpp
    core/tests/test_voice_controller.cpp
    core/tests/test_archetypes.cpp
    core/tests/test_gesture_score.cpp
)
target_link_libraries(cuicui_core_tests PRIVATE cuicui_core Catch2::Catch2WithMain)
target_compile_definitions(cuicui_core_tests PRIVATE
    CUICUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CUICUI_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
cuicui_set_warnings(cuicui_core_tests)

enable_testing()
include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
catch_discover_tests(cuicui_core_tests)

if(CUICUI_BUILD_BINDINGS)
    add_subdirectory(python)
endif()

if(CUICUI_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
