include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)

add_executable(DrumHitsTests
    ControllerTests.cpp
    EngineTests.cpp
    LimitsTests.cpp
    PreferencesTests.cpp)

target_compile_definitions(DrumHitsTests PRIVATE DRUMHITS_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata")

target_link_libraries(DrumHitsTests
    PRIVATE
        drumhits_core
        Catch2::Catch2WithMain)

catch_discover_tests(DrumHitsTests)

add_executable(DrumHitsProcessorTests ProcessorTests.cpp WavesTabTests.cpp MainTabTests.cpp)
target_link_libraries(DrumHitsProcessorTests PRIVATE DrumHits drumhits_core Catch2::Catch2WithMain)
catch_discover_tests(DrumHitsProcessorTests)
