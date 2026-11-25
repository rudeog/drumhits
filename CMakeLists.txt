cmake_minimum_required(VERSION 3.22)

project(DrumHits VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreaded$<$<CONFIG:Debug>:Debug>DLL>")
endif()

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
if(MSVC)
    target_compile_options(drumhits_core PRIVATE /W4)
else()
    target_compile_options(drumhits_core PRIVATE -Wall -Wextra -Wconversion -Wshadow)
endif()

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

# Short hash of the latest commit, shown in the settings tab. Re-evaluated when HEAD moves.
set(DRUMHITS_BUILD_HASH "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE DRUMHITS_BUILD_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE drumhits_dirty ERROR_QUIET)
    if(drumhits_dirty)
        string(APPEND DRUMHITS_BUILD_HASH "-dirty")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/HEAD" "${CMAKE_SOURCE_DIR}/.git/index")
endif()
target_compile_definitions(DrumHits PRIVATE DRUMHITS_BUILD_HASH="${DRUMHITS_BUILD_HASH}")

target_link_libraries(DrumHits
    PRIVATE
        drumhits_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(DRUMHITS_WINDOWS_SIGNING "Sign Windows standalone and VST3 binaries" OFF)
if(DRUMHITS_WINDOWS_SIGNING)
    if(NOT WIN32)
        message(FATAL_ERROR "DRUMHITS_WINDOWS_SIGNING requires a Windows build")
    endif()

    set(DRUMHITS_SIGNING_CERT_SHA1 "" CACHE STRING "SHA-1 thumbprint of the signing certificate in CurrentUser\\My")
    set(DRUMHITS_TIMESTAMP_URL "" CACHE STRING "Optional RFC 3161 timestamp server URL")
    string(LENGTH "${DRUMHITS_SIGNING_CERT_SHA1}" thumbprint_length)
    if(NOT thumbprint_length EQUAL 40 OR NOT DRUMHITS_SIGNING_CERT_SHA1 MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "DRUMHITS_SIGNING_CERT_SHA1 must be a 40-character hexadecimal certificate thumbprint")
    endif()

    set(sdk_bin_dirs "")
    foreach(sdk_root "$ENV{WindowsSdkDir}" "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
        if(sdk_root)
            file(GLOB sdk_bins LIST_DIRECTORIES TRUE "${sdk_root}/bin/*/x64")
            list(APPEND sdk_bin_dirs ${sdk_bins})
        endif()
    endforeach()
    list(SORT sdk_bin_dirs COMPARE NATURAL ORDER DESCENDING)
    find_program(DRUMHITS_SIGNTOOL NAMES signtool.exe HINTS ${sdk_bin_dirs}
        DOC "Path to Windows SDK signtool.exe")
    if(NOT DRUMHITS_SIGNTOOL)
        message(FATAL_ERROR "signtool.exe not found; install the Windows SDK or set DRUMHITS_SIGNTOOL")
    endif()

    set(timestamp_args "")
    if(DRUMHITS_TIMESTAMP_URL)
        list(APPEND timestamp_args /tr "${DRUMHITS_TIMESTAMP_URL}" /td SHA256)
    endif()
    foreach(sign_target DrumHits_Standalone DrumHits_VST3)
        add_custom_command(TARGET ${sign_target} POST_BUILD
            COMMAND "${DRUMHITS_SIGNTOOL}" sign /fd SHA256 /s My
                /sha1 "${DRUMHITS_SIGNING_CERT_SHA1}" ${timestamp_args}
                "$<TARGET_FILE:${sign_target}>"
            COMMENT "Signing ${sign_target}"
            VERBATIM)
    endforeach()
endif()
