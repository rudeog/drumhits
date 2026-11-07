#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"

namespace drumhits::ui
{
// Drum-level settings for the selected drum.
class DrumPropertiesPanel final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit DrumPropertiesPanel (Controller& controllerToUse);
    ~DrumPropertiesPanel() override;

    void setDrum (int drum);
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onNameChanged;
    std::function<void()> onNoteChanged;

private:
    void timerCallback() override;
    void setLearning (bool shouldLearn);
    void refresh();
    void commit (const std::function<void (DrumProperties&)>& edit);
    void commitName();
    void commitNote();
    void showChokedByMenu();
    void updateChokedByText (const DrumProperties& p);
    void addLabelled (juce::Label& label, const juce::String& text, juce::Component& control);

    Controller& controller;
    int drum = 0;
    bool updating = false;
    bool learning = false;
    uint32_t learnBaseline = 0; // note counter when learning started

    juce::Label nameLabel, noteLabel, modeLabel, chokeLabel, chokedByLabel, polyLabel, volumeLabel, panLabel, outputLabel;
    juce::Label nameEdit, noteEdit, polyEdit;
    juce::TextButton learnButton { "Learn" };
    juce::ComboBox playMode, chokeGroup, output;
    juce::TextButton chokedByButton;
    juce::Slider volume, pan;
};
} // namespace drumhits::ui
