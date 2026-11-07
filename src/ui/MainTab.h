#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"
#include "ui/DrumGrid.h"
#include "ui/DrumPropertiesPanel.h"
#include "ui/HitDetailsPanel.h"
#include "ui/HitListPanel.h"
#include "ui/WaveView.h"

namespace drumhits::ui
{
// Top: drum grid. Middle: drum properties. Bottom left: hit list. Bottom right: hit details.
class MainTab final : public juce::Component,
                      public juce::DragAndDropContainer
{
public:
    explicit MainTab (Controller& controllerToUse);

    // Re-reads controller state (e.g. after waves changed on another tab).
    void refresh();
    void showHit (int drum, int slot);

    // Fires with the wave id and the hit's start and end when the user asks to jump to a hit's wave.
    std::function<void (int waveId, int64_t start, int64_t end)> onJumpToWave;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    friend struct MainTabTestAccess;
    void selectDrum (int drum);
    void showDrumActions (int drum, juce::Point<int> screenPosition);
    void addDroppedFiles (int drum, const juce::StringArray& files);
    void copyDroppedHits (int sourceDrum, const std::vector<int>& hitSlots, int targetDrum);
    void updateHitWave();
    void commitHitMarkers();
    void playHit();

    DrumGrid grid;
    DrumPropertiesPanel properties;
    HitListPanel hits;
    HitDetailsPanel details;
    juce::Label waveNameLabel, wavePathLabel;
    WaveView hitWave;
    Controller& controller;
    int activeDrum = 0;
    juce::Rectangle<int> hitDetailsArea;
};
} // namespace drumhits::ui
