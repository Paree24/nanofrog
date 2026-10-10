cmake_minimum_required(VERSION 3.22)
project(NanoFrog VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(NanoFrogAssets
  HEADER_NAME NanoFrogAssets.h
  NAMESPACE NanoFrogAssets
  SOURCES
    Assets/Lato-Regular.ttf
    Assets/Lato-Bold.ttf
    Assets/Lato-Black.ttf
)

juce_add_plugin(NanoFrog
    COMPANY_NAME "NanoFrog"
    PLUGIN_MANUFACTURER_CODE NbAg
    PLUGIN_CODE Nb01
    FORMATS VST3 Standalone
    PRODUCT_NAME "NanoFrog"
    DESCRIPTION "Compact bitimbral virtual-analog synthesizer"
    COMPANY_WEBSITE "https://example.com"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(NanoFrog PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetBank.cpp
    Source/PresetBrowser.cpp
)

target_include_directories(NanoFrog PRIVATE Source)

target_compile_definitions(NanoFrog PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NanoFrog PRIVATE
    NanoFrogAssets
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

# Factory bank ships as data (no recompile to change presets): sync the JSON
# into the VST3 bundle Resources and next to the Standalone binary.
# This is an ALWAYS-run target (not POST_BUILD): the bank lives outside the
# build graph, and a stale bank ships silently if the copy only runs on
# relink. The file is small; the copy is instant.
add_custom_target(NanoFrogFactoryBank ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/NanoFrog_artefacts/$<CONFIG>/VST3/NanoFrog.vst3/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/FactoryBank/NanoFrogFactory.json"
        "${CMAKE_CURRENT_BINARY_DIR}/NanoFrog_artefacts/$<CONFIG>/VST3/NanoFrog.vst3/Contents/Resources/NanoFrogFactory.json"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/FactoryBank/NanoFrogFactory.json"
        "${CMAKE_CURRENT_BINARY_DIR}/NanoFrog_artefacts/$<CONFIG>/Standalone/NanoFrogFactory.json"
)
add_dependencies(NanoFrog NanoFrogFactoryBank)

# Headless verification (LESSONS.md #2, #12, #17, #21): layout asserts +
# preset round-trip. Run with no display/audio hardware.
juce_add_console_app(NanoFrogHeadlessTest NEEDS_WEB_BROWSER FALSE)

target_sources(NanoFrogHeadlessTest PRIVATE
    HeadlessTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetBank.cpp
    Source/PresetBrowser.cpp
)

target_include_directories(NanoFrogHeadlessTest PRIVATE Source)

target_compile_definitions(NanoFrogHeadlessTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(NanoFrogHeadlessTest PRIVATE
    NanoFrogAssets
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags
)
