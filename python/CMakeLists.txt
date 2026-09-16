# Python module cuicui_core (nanobind). Used as a subdirectory of the top-level
# CMake project, or as a standalone scikit-build-core root.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(cuicui_core_bindings LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CUICUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    set(CUICUI_BUILD_BINDINGS ON)
    include("${CUICUI_ROOT}/cmake/Dependencies.cmake")
    add_library(cuicui_core INTERFACE)
    target_include_directories(cuicui_core INTERFACE
        "${CUICUI_ROOT}/core/include"
        "${CMAKE_BINARY_DIR}/generated"
    )
    target_link_libraries(cuicui_core INTERFACE nlohmann_json::nlohmann_json)
    set(CUICUI_GIT_HASH "nogit")
    configure_file(
        "${CUICUI_ROOT}/core/include/cuicui/version.hpp.in"
        "${CMAKE_BINARY_DIR}/generated/cuicui/version.hpp"
        @ONLY
    )
    function(cuicui_set_warnings target)
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    endfunction()
else()
    set(CUICUI_ROOT "${CMAKE_SOURCE_DIR}")
endif()

nanobind_add_module(cuicui_core_nb bindings/module.cpp)
set_target_properties(cuicui_core_nb PROPERTIES OUTPUT_NAME cuicui_core)
target_link_libraries(cuicui_core_nb PRIVATE cuicui_core)
# Nanobind headers are third-party; keep -Werror for core/apps only (02_architecture.md §5.1).

# Drop the extension next to the pure-Python package so `pythonpath = ["."]` finds it.
set_target_properties(cuicui_core_nb PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(SKBUILD)
    install(TARGETS cuicui_core_nb LIBRARY DESTINATION . RUNTIME DESTINATION .)
endif()
