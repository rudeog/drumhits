#include "core/controller/OfflineResampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drumhits
{
std::vector<float> resampleOffline (const float* input, int inputLength, int outputLength, double inputPerOutput)
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double lobes = 32.0;
    const double cutoff = std::min (1.0, 1.0 / inputPerOutput);
    const double radius = lobes / cutoff;
    std::vector<float> output (static_cast<size_t> (outputLength));

    for (int i = 0; i < outputLength; ++i)
    {
        const double position = static_cast<double> (i) * inputPerOutput;
        const auto first = static_cast<int64_t> (std::ceil (position - radius));
        const auto last = static_cast<int64_t> (std::floor (position + radius));
        double sum = 0.0, weightSum = 0.0;
        for (auto sample = first; sample <= last; ++sample)
        {
            const double x = (position - static_cast<double> (sample)) * cutoff;
            const double ax = std::abs (x);
            if (ax >= lobes)
                continue;
            const double sinc = ax < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double window = 0.42 + 0.5 * std::cos (pi * x / lobes)
                                  + 0.08 * std::cos (2.0 * pi * x / lobes);
            const double weight = sinc * window;
            const auto index = std::clamp<int64_t> (sample, 0, inputLength - 1);
            sum += static_cast<double> (input[index]) * weight;
            weightSum += weight;
        }
        output[static_cast<size_t> (i)] = static_cast<float> (sum / weightSum);
    }
    return output;
}
} // namespace drumhits
