#include <catch2/catch_test_macros.hpp>

#include "core/Limits.h"

TEST_CASE ("Engine limits match the design document")
{
    REQUIRE (drumhits::maxDrums == 16);
    REQUIRE (drumhits::maxHitsPerDrum == 16);
    REQUIRE (drumhits::maxOutputs == 4);
    REQUIRE (drumhits::maxPolyphony == 64);
    REQUIRE (drumhits::maxChokeGroups == 8);
}
