#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace drumhits
{
// Lock-free single-producer/single-consumer ring buffer. Capacity is fixed at
// construction. One thread may call push(), and one other thread may call pop().
template <typename T>
class SpscQueue
{
public:
    explicit SpscQueue (size_t minimumCapacity)
    {
        size_t cap = 2;
        while (cap < minimumCapacity + 1)
            cap <<= 1;
        storage.resize (cap);
        mask = cap - 1;
    }

    size_t capacity() const noexcept { return mask; }

    bool push (const T& item)
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & mask;
        if (next == readIndex.load (std::memory_order_acquire))
            return false;
        storage[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item)
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;
        item = storage[r];
        readIndex.store ((r + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> storage;
    size_t mask = 0;
    alignas (64) std::atomic<size_t> writeIndex { 0 };
    alignas (64) std::atomic<size_t> readIndex { 0 };
};
} // namespace drumhits
