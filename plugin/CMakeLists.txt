# Syrinx plugin (JUCE) + plugin_render + processor tests. WP-C1.

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target" FORCE)
endif()

set(SYRINX_PLUGIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/SafetyLimiter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ParameterLayout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ParameterLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/VoiceAllocator.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/VoiceAllocator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/SyllableFeed.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ParamHelp.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/StandaloneMidi.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/StandaloneMidi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Theme.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/LookAndFeel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/LookAndFeel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Widgets.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/SongDisplay.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/SongDisplay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/BirdKeyboard.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/BirdKeyboard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PresetBar.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PresetBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PlayPage.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/PlayPage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/AdvancedPage.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/AdvancedPage.cpp"
)

set(SYRINX_JUCE_COMPILE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# WP-R1: the factory syllable bank is compiled in (BinaryData::bank_json), so the plugin needs no files.
# WP-R4: so are the factory presets (assets/presets/*.preset.json, D-028).
file(GLOB SYRINX_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/presets/*.preset.json")
juce_add_binary_data(SyrinxBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES "${CMAKE_SOURCE_DIR}/assets/voicebanks/zf_22938/bank.json" ${SYRINX_FACTORY_PRESETS}
)
set_target_properties(SyrinxBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Syrinx
    VERSION 0.1.0
    COMPANY_NAME "Cuicui Instruments"
    BUNDLE_ID com.cuicuiinstruments.syrinx
    PLUGIN_MANUFACTURER_CODE Cuic
    PLUGIN_CODE Syrx
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Syrinx"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED FALSE
)

target_sources(Syrinx PRIVATE ${SYRINX_PLUGIN_SOURCES})
target_include_directories(Syrinx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(Syrinx PUBLIC ${SYRINX_JUCE_COMPILE_DEFINITIONS})
target_link_libraries(Syrinx
    PRIVATE
        cuicui_core
        SyrinxBinaryData
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

if(COMMAND clap_juce_extensions_plugin)
    clap_juce_extensions_plugin(
        TARGET Syrinx
        CLAP_ID "com.cuicuiinstruments.syrinx"
        CLAP_FEATURES instrument synthesizer
    )
endif()

# Offline renderer hosts the processor (no GUI).
juce_add_console_app(plugin_render PRODUCT_NAME "plugin_render")
target_sources(plugin_render PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/plugin_render/main.cpp"
    ${SYRINX_PLUGIN_SOURCES}
)
target_include_directories(plugin_render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(plugin_render PUBLIC ${SYRINX_JUCE_COMPILE_DEFINITIONS})
target_link_libraries(plugin_render
    PRIVATE
        cuicui_core
        SyrinxBinaryData
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
)

add_executable(syrinx_plugin_tests
    tests/main.cpp
    tests/test_processor.cpp
    tests/test_perform_mode.cpp
    tests/test_generative.cpp
    tests/test_whistle_path.cpp
    tests/test_presets.cpp
    tests/test_editor.cpp
    ${SYRINX_PLUGIN_SOURCES}
)
target_include_directories(syrinx_plugin_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/core/tests"
)
target_compile_definitions(syrinx_plugin_tests PRIVATE
    ${SYRINX_JUCE_COMPILE_DEFINITIONS}
    JUCE_MODAL_LOOPS_PERMITTED=1
    CUICUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CUICUI_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_link_libraries(syrinx_plugin_tests PRIVATE
    cuicui_core
    SyrinxBinaryData
    Catch2::Catch2
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

set_target_properties(plugin_render syrinx_plugin_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# One process for the whole Catch2 binary. catch_discover_tests spawned a process
# per case; JUCE teardown then SIGABRT'd after "All tests passed".
add_test(NAME syrinx_plugin_tests COMMAND syrinx_plugin_tests)
