#include "ui/HitDetailsPanel.h"

#include "ui/Tooltips.h"

namespace drumhits::ui
{
namespace
{
void setupValueSlider (juce::Slider& s, double min, double max, double interval, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    s.setRange (min, max, interval);
    s.setDoubleClickReturnValue (true, 0.0);
    s.setTextValueSuffix (suffix);
}

void setupValueLabel (juce::Label& l)
{
    l.setJustificationType (juce::Justification::centred);
}
} // namespace

HitDetailsPanel::HitDetailsPanel (Controller& controllerToUse)
    : controller (controllerToUse)
{
    addLabelled (nameLabel, "Name", nameEdit);
    addLabelled (volumeLabel, "Volume", volume);
    addLabelled (pitchLabel, "Pitch", semitones);
    addLabelled (velocityLabel, "Velocity", velocityRange);
    addLabelled (sensitivityLabel, "Vel. sens.", sensitivity);
    addAndMakeVisible (cents);
    addAndMakeVisible (velocityMinLabel);
    addAndMakeVisible (velocityMaxLabel);
    setupValueLabel (velocityMinLabel);
    setupValueLabel (velocityMaxLabel);

    nameEdit.setEditable (true);
    nameEdit.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    nameEdit.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    nameEdit.setJustificationType (juce::Justification::centredLeft);
    nameEdit.onTextChange = [this]
    {
        if (updating || slot < 0)
            return;

        controller.setHitName (drum, slot, nameEdit.getText().trim());
        if (onNameChanged)
            onNameChanged();
    };

    setupValueSlider (volume, -60.0, 12.0, 0.1, " dB");
    volume.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (HitProperties& p) { p.volumeDb = static_cast<float> (volume.getValue()); });
    };

    setupValueSlider (semitones, -24.0, 24.0, 1.0, " st");
    semitones.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (HitProperties& p) { p.semitones = static_cast<int> (semitones.getValue()); });
    };

    setupValueSlider (cents, -100.0, 100.0, 1.0, " ct");
    cents.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (HitProperties& p) { p.cents = static_cast<int> (cents.getValue()); });
    };

    setupValueSlider (sensitivity, 0.0, 100.0, 1.0, " %");
    sensitivity.setDoubleClickReturnValue (true, 100.0);
    sensitivity.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (HitProperties& p) { p.velocitySensitivity = static_cast<int> (sensitivity.getValue()); });
    };

    velocityRange.setRange (0.0, 127.0, 1.0);
    velocityRange.setMinAndMaxValues (0.0, 127.0, juce::dontSendNotification);
    velocityRange.onValueChange = [this]
    {
        updateVelocityLabels();
        if (! updating)
            commit ([this] (HitProperties& p)
                    {
                        p.velocityMin = static_cast<int> (velocityRange.getMinValue());
                        p.velocityMax = static_cast<int> (velocityRange.getMaxValue());
                    });
    };

    setTooltip ("Name associated with this hit", { &nameLabel, &nameEdit });
    setTooltip ("Volume of this hit", { &volumeLabel, &volume });
    setTooltip ("Adjust pitch of this hit in semitones", { &pitchLabel, &semitones });
    setTooltip ("Adjust pitch of this hit in cents (hundredths of a semitone)", { &cents });
    setTooltip ("The velocity range which this hit responds to", { &velocityLabel, &velocityRange, &velocityMinLabel, &velocityMaxLabel });
    setTooltip ("How much the MIDI velocity affects the volume of this hit", { &sensitivityLabel, &sensitivity });

    setHit (0, -1);
}

void HitDetailsPanel::addLabelled (juce::Label& label, const juce::String& text, juce::Component& control)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (label);
    addAndMakeVisible (control);
}

void HitDetailsPanel::updateVelocityLabels()
{
    velocityMinLabel.setText (juce::String (static_cast<int> (velocityRange.getMinValue())), juce::dontSendNotification);
    velocityMaxLabel.setText (juce::String (static_cast<int> (velocityRange.getMaxValue())), juce::dontSendNotification);
}

void HitDetailsPanel::setHit (int newDrum, int newSlot)
{
    drum = newDrum;
    slot = newSlot;

    const auto props = slot >= 0 ? controller.getHitProperties (drum, slot) : std::nullopt;
    const bool valid = props.has_value();

    const juce::ScopedValueSetter<bool> guard (updating, true);
    nameEdit.setText (valid ? controller.getHitName (drum, slot) : juce::String(), juce::dontSendNotification);
    volume.setValue (valid ? props->volumeDb : 0.0, juce::dontSendNotification);
    semitones.setValue (valid ? props->semitones : 0, juce::dontSendNotification);
    cents.setValue (valid ? props->cents : 0, juce::dontSendNotification);
    velocityRange.setMinAndMaxValues (valid ? props->velocityMin : 0, valid ? props->velocityMax : 127, juce::dontSendNotification);
    sensitivity.setValue (valid ? props->velocitySensitivity : 100, juce::dontSendNotification);
    updateVelocityLabels();

    setEnabled (valid);
    setAlpha (valid ? 1.0f : 0.4f);
}

void HitDetailsPanel::commit (const std::function<void (HitProperties&)>& edit)
{
    auto props = slot >= 0 ? controller.getHitProperties (drum, slot) : std::nullopt;
    if (! props.has_value())
        return;

    edit (*props);
    controller.setHitProperties (drum, slot, *props);
}

void HitDetailsPanel::resized()
{
    auto area = getLocalBounds();
    constexpr int labelWidth = 70;
    constexpr int valueWidth = 36;

    auto nextRow = [&] (juce::Label& label)
    {
        auto row = area.removeFromTop (rowHeight).reduced (0, 2);
        label.setBounds (row.removeFromLeft (labelWidth));
        row.removeFromLeft (6);
        return row;
    };

    nameEdit.setBounds (nextRow (nameLabel));
    volume.setBounds (nextRow (volumeLabel));

    auto pitchRow = nextRow (pitchLabel);
    semitones.setBounds (pitchRow.removeFromLeft (pitchRow.getWidth() / 2 - 3));
    pitchRow.removeFromLeft (6);
    cents.setBounds (pitchRow);

    auto velocityRow = nextRow (velocityLabel);
    velocityMinLabel.setBounds (velocityRow.removeFromLeft (valueWidth));
    velocityMaxLabel.setBounds (velocityRow.removeFromRight (valueWidth));
    velocityRange.setBounds (velocityRow);

    sensitivity.setBounds (nextRow (sensitivityLabel));
}
} // namespace drumhits::ui
