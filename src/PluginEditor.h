#pragma once

#include <functional>

#include "PluginProcessor.h"

namespace drumhits::ui
{
class MainTab;
class WavesTab;
class SettingsTab;
} // namespace drumhits::ui

class DrumHitsEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DrumHitsEditor (DrumHitsProcessor&);

    void resized() override;

private:
    class Tabs final : public juce::TabbedComponent
    {
    public:
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}

        std::function<void (int)> onTabChanged;

        void currentTabChanged (int index, const juce::String&) override
        {
            if (onTabChanged)
                onTabChanged (index);
        }
    };

    void refreshTab (int index);
    void showMissingWavesDialog (int count, const juce::String& details = {});

    juce::TooltipWindow tooltipWindow { this, 600 };
    Tabs tabs;
    juce::TextButton panicButton { "Panic" };
    drumhits::ui::MainTab* mainTab = nullptr; // owned by tabs
    drumhits::ui::WavesTab* wavesTab = nullptr; // owned by tabs
    drumhits::ui::SettingsTab* settingsTab = nullptr; // owned by tabs
    bool missingWavesDialogOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumHitsEditor)
};
