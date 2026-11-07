#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"

namespace drumhits::ui
{
// Name, volume, pitch, velocity range and velocity sensitivity of one hit. Disabled unless a single hit is shown.
class HitDetailsPanel final : public juce::Component
{
public:
    explicit HitDetailsPanel (Controller& controllerToUse);

    // A slot of -1 means no single hit is selected.
    void setHit (int newDrum, int newSlot);

    // Fires after the hit's name changed.
    std::function<void()> onNameChanged;

    static constexpr int rowCount = 5;
    static constexpr int rowHeight = 26;

    void resized() override;

private:
    void commit (const std::function<void (HitProperties&)>& edit);
    void addLabelled (juce::Label& label, const juce::String& text, juce::Component& control);
    void updateVelocityLabels();

    Controller& controller;
    int drum = 0;
    int slot = -1;
    bool updating = false;

    juce::Label nameLabel, volumeLabel, pitchLabel, velocityLabel, sensitivityLabel;
    juce::Label nameEdit;
    juce::Slider volume, semitones, cents, sensitivity;
    juce::Slider velocityRange { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::Label velocityMinLabel, velocityMaxLabel;
};
} // namespace drumhits::ui
