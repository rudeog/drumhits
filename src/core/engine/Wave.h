#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace drumhits
{
// Immutable stereo sample data created and owned by the controller. The engine
// only increments/decrements the reference count of waves it was handed.
// The controller starts with a count of 1 and frees the wave (outside the
// audio thread) once it has dropped its own reference and the count is 0.
class Wave
{
public:
    Wave (std::vector<float> leftSamples, std::vector<float> rightSamples)
        : left (std::move (leftSamples)), right (std::move (rightSamples))
    {
    }

    Wave (const Wave&) = delete;
    Wave& operator= (const Wave&) = delete;

    int64_t numSamples() const noexcept { return static_cast<int64_t> (left.size()); }
    const float* leftData() const noexcept { return left.data(); }
    const float* rightData() const noexcept { return right.data(); }

    void addRef() noexcept { refCount.fetch_add (1, std::memory_order_relaxed); }
    void release() noexcept { refCount.fetch_sub (1, std::memory_order_acq_rel); }
    int referenceCount() const noexcept { return refCount.load (std::memory_order_acquire); }

private:
    const std::vector<float> left;
    const std::vector<float> right;
    std::atomic<int> refCount { 1 };
};
} // namespace drumhits
