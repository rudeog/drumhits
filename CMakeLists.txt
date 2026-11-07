cmake_minimum_required(VERSION 3.22)

project(DrumHits VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(external/JUCE)

# Non-UI code (engine and controller) lives in this library so tests can link it.
add_library(drumhits_core STATIC
    src/core/controller/Controller.cpp
    src/core/controller/OfflineResampler.cpp
    src/core/preferences/Preferences.cpp
    src/core/engine/Engine.cpp)
target_include_directories(drumhits_core PUBLIC src)
target_compile_features(drumhits_core PUBLIC cxx_std_20)
target_link_libraries(drumhits_core PUBLIC juce::juce_audio_formats juce::juce_data_structures)
set_target_properties(drumhits_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(drumhits_core PRIVATE -Wall -Wextra -Wconversion -Wshadow)

option(DRUMHITS_BUILD_TESTS "Build unit tests" ON)
if(DRUMHITS_BUILD_TESTS)
    include(CTest)
    add_subdirectory(external/Catch2)
    add_subdirectory(tests)
endif()

set(DRUMHITS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DRUMHITS_FORMATS AU)
endif()

juce_add_plugin(DrumHits
    COMPANY_NAME "DrumHits"
    PLUGIN_MANUFACTURER_CODE DrHt
    PLUGIN_CODE DrHs
    FORMATS ${DRUMHITS_FORMATS}
    PRODUCT_NAME "DrumHits"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DrumHits PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/ui/DrumGrid.cpp
    src/ui/DrumGrid.h
    src/ui/DrumPropertiesPanel.cpp
    src/ui/DrumPropertiesPanel.h
    src/ui/HitDetailsPanel.cpp
    src/ui/HitDetailsPanel.h
    src/ui/HitListPanel.cpp
    src/ui/HitListPanel.h
    src/ui/MainTab.cpp
    src/ui/MainTab.h
    src/ui/NoteNames.h
    src/ui/Tooltips.h
    src/ui/SettingsTab.cpp
    src/ui/SettingsTab.h
    src/ui/WaveView.cpp
    src/ui/WaveView.h
    src/ui/WavesTab.cpp
    src/ui/WavesTab.h
    src/ui/WavePoolSort.h)

target_compile_definitions(DrumHits PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DrumHits
    PRIVATE
        drumhits_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
