#pragma once

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>

namespace drumhits::ui
{
enum class WavePoolSortColumn { none, name, path, usage, status };

struct WavePoolSortEntry
{
    int id;
    juce::String name;
    juce::String path;
    int usage;
    bool missing;
};

inline void sortWavePoolEntries (std::vector<WavePoolSortEntry>& entries, WavePoolSortColumn column, bool forwards)
{
    if (column == WavePoolSortColumn::none)
        return;

    std::stable_sort (entries.begin(), entries.end(), [column, forwards] (const auto& a, const auto& b)
    {
        int comparison = 0;
        switch (column)
        {
            case WavePoolSortColumn::name: comparison = a.name.compareNatural (b.name); break;
            case WavePoolSortColumn::path: comparison = a.path.compareNatural (b.path); break;
            case WavePoolSortColumn::usage: comparison = (a.usage > b.usage) - (a.usage < b.usage); break;
            case WavePoolSortColumn::status: comparison = static_cast<int> (a.missing) - static_cast<int> (b.missing); break;
            case WavePoolSortColumn::none: break;
        }
        return forwards ? comparison < 0 : comparison > 0;
    });
}
} // namespace drumhits::ui
