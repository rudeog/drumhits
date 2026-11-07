#pragma once

#include <vector>

namespace drumhits
{
// Band-limited, zero-phase conversion of a finite signal, with edge extension.
std::vector<float> resampleOffline (const float* input, int inputLength, int outputLength, double inputPerOutput);
}
