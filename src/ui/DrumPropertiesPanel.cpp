#include "ui/DrumPropertiesPanel.h"

#include "core/Limits.h"
#include "ui/NoteNames.h"
#include "ui/Tooltips.h"

namespace drumhits::ui
{
namespace
{
int parseNote (const juce::String& text)
{
    const auto t = text.trim().replace (" ", "");
    if (t.isEmpty())
        return -1;

    for (int n = 0; n < 128; ++n)
        if (noteName (n).replace (" ", "").equalsIgnoreCase (t))
            return n;

    return -2; // invalid
}

void setupEdit (juce::Label& l)
{
    l.setEditable (true);
    l.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    l.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    l.setJustificationType (juce::Justification::centredLeft);
}
} // namespace

DrumPropertiesPanel::DrumPropertiesPanel (Controller& controllerToUse)
    : controller (controllerToUse)
{
    addLabelled (nameLabel, "Name", nameEdit);
    addLabelled (noteLabel, "Note", noteEdit);
    addLabelled (modeLabel, "Play mode", playMode);
    addLabelled (chokeLabel, "Choke group", chokeGroup);
    addLabelled (chokedByLabel, "Choked by", chokedByButton);
    addLabelled (polyLabel, "Max poly", polyEdit);
    addLabelled (volumeLabel, "Volume", volume);
    addLabelled (panLabel, "Pan", pan);
    addLabelled (outputLabel, "Output", output);
    learnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb02a2a));
    addAndMakeVisible (learnButton);

    for (auto* l : { &nameEdit, &noteEdit, &polyEdit })
        setupEdit (*l);

    nameEdit.onTextChange = [this] { commitName(); };
    noteEdit.onTextChange = [this] { commitNote(); };
    polyEdit.onTextChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.maxPoly = std::max (0, polyEdit.getText().getIntValue()); });
        refresh();
    };
    for (int n = 1; n <= maxOutputs; ++n)
        output.addItem (juce::String (n), n);
    output.onChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.output = output.getSelectedId() - 1; });
    };

    playMode.addItem ("Regular", 1);
    playMode.addItem ("Random round robin", 2);
    playMode.onChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.playMode = playMode.getSelectedId() == 2 ? PlayMode::randomRoundRobin : PlayMode::regular; });
    };

    chokeGroup.addItem ("None", 1);
    for (int g = 1; g <= maxChokeGroups; ++g)
        chokeGroup.addItem (juce::String (g), g + 1);
    chokeGroup.onChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.choke = chokeGroup.getSelectedId() - 1; });
    };

    chokedByButton.onClick = [this] { showChokedByMenu(); };

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    volume.setRange (-60.0, 12.0, 0.1);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextValueSuffix (" dB");
    volume.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.volumeDb = static_cast<float> (volume.getValue()); });
    };

    pan.setSliderStyle (juce::Slider::LinearHorizontal);
    pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.onValueChange = [this]
    {
        if (! updating)
            commit ([this] (DrumProperties& p) { p.pan = static_cast<float> (pan.getValue()); });
    };

    learnButton.onClick = [this] { setLearning (! learning); };

    setTooltip ("Edit the displayed name of the drum", { &nameLabel, &nameEdit });
    setTooltip ("Which MIDI note this drum responds to", { &noteLabel, &noteEdit });
    setTooltip ("Send a MIDI note from your controller", { &learnButton });
    setTooltip ("Regular mode: all hits respond. Random round robin: a hit is chosen at random", { &modeLabel, &playMode });
    setTooltip ("The choke group associated with this drum", { &chokeLabel, &chokeGroup });
    setTooltip ("Which choke groups stop this drum from playing", { &chokedByLabel, &chokedByButton });
    setTooltip ("Maximum simultaneously playing sounds for this drum", { &polyLabel, &polyEdit });
    setTooltip ("This drum's volume", { &volumeLabel, &volume });
    setTooltip ("This drum's pan", { &panLabel, &pan });
    setTooltip ("Which output this drum is assigned to", { &outputLabel, &output });
    refresh();
}

void DrumPropertiesPanel::addLabelled (juce::Label& label, const juce::String& text, juce::Component& control)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (label);
    addAndMakeVisible (control);
}

DrumPropertiesPanel::~DrumPropertiesPanel()
{
    stopTimer();
}

void DrumPropertiesPanel::setLearning (bool shouldLearn)
{
    learning = shouldLearn;
    learnButton.setButtonText (learning ? "Cancel" : "Learn");
    learnButton.setToggleState (learning, juce::dontSendNotification);

    if (learning)
    {
        learnBaseline = controller.getNoteCounter();
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void DrumPropertiesPanel::timerCallback()
{
    if (controller.getNoteCounter() == learnBaseline)
        return;

    const int note = controller.getLastNote();
    setLearning (false);

    if (note >= 0)
    {
        commit ([note] (DrumProperties& p) { p.note = note; });
        refresh();
        if (onNoteChanged)
            onNoteChanged();
    }
}

void DrumPropertiesPanel::setDrum (int newDrum)
{
    if (learning && newDrum != drum)
        setLearning (false);

    drum = newDrum;
    refresh();
}

void DrumPropertiesPanel::commit (const std::function<void (DrumProperties&)>& edit)
{
    auto p = controller.getDrumProperties (drum);
    edit (p);
    controller.setDrumProperties (drum, p);
}

void DrumPropertiesPanel::commitName()
{
    if (updating)
        return;

    controller.setDrumName (drum, nameEdit.getText().trim());
    if (onNameChanged)
        onNameChanged();
}

void DrumPropertiesPanel::commitNote()
{
    if (! updating)
    {
        const auto note = parseNote (noteEdit.getText());
        if (note != -2)
        {
            commit ([note] (DrumProperties& p) { p.note = note; });
            if (onNoteChanged)
                onNoteChanged();
        }
    }

    refresh(); // normalises the text, or reverts an invalid entry
}

void DrumPropertiesPanel::updateChokedByText (const DrumProperties& p)
{
    juce::StringArray groups;
    for (int g = 1; g <= maxChokeGroups; ++g)
        if ((p.chokedByMask >> g) & 1)
            groups.add (juce::String (g));

    chokedByButton.setButtonText (groups.isEmpty() ? juce::String ("None") : groups.joinIntoString (", "));
}

void DrumPropertiesPanel::showChokedByMenu()
{
    const auto mask = controller.getDrumProperties (drum).chokedByMask;
    juce::PopupMenu menu;
    for (int g = 1; g <= maxChokeGroups; ++g)
    {
        juce::StringArray drumNames;
        for (int d = 0; d < maxDrums; ++d)
            if (controller.getDrumProperties (d).choke == g)
            {
                auto name = controller.getDrumName (d);
                if (name.isEmpty())
                    name = "Drum " + juce::String (d + 1);
                drumNames.add (juce::String (d + 1) + ": " + name);
            }

        const auto label = "Group " + juce::String (g) + " ("
                           + (drumNames.isEmpty() ? juce::String ("no drums") : drumNames.joinIntoString (", ")) + ")";
        menu.addItem (g, label, true, ((mask >> g) & 1) != 0);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (chokedByButton),
                        [safe = juce::Component::SafePointer<DrumPropertiesPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            safe->commit ([result] (DrumProperties& p)
                                          { p.chokedByMask = static_cast<uint16_t> (p.chokedByMask ^ (1u << result)); });
                            safe->refresh();
                            safe->showChokedByMenu(); // stay open for multi-select; dismiss by clicking away
                        });
}

void DrumPropertiesPanel::refresh()
{
    const auto p = controller.getDrumProperties (drum);
    const juce::ScopedValueSetter<bool> guard (updating, true);

    nameEdit.setText (controller.getDrumName (drum), juce::dontSendNotification);
    noteEdit.setText (p.note >= 0 ? noteName (p.note) : juce::String(), juce::dontSendNotification);
    playMode.setSelectedId (p.playMode == PlayMode::randomRoundRobin ? 2 : 1, juce::dontSendNotification);
    chokeGroup.setSelectedId (p.choke + 1, juce::dontSendNotification);
    updateChokedByText (p);
    polyEdit.setText (juce::String (p.maxPoly), juce::dontSendNotification);
    volume.setValue (p.volumeDb, juce::dontSendNotification);
    pan.setValue (p.pan, juce::dontSendNotification);
    output.setSelectedId (p.output + 1, juce::dontSendNotification);
}

void DrumPropertiesPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
}

void DrumPropertiesPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    constexpr int labelWidth = 80;
    const int colWidth = area.getWidth() / 3;
    const int rowHeight = area.getHeight() / 3;

    auto place = [&] (int col, int row, juce::Label& label, juce::Component& control, int reserveRight = 0)
    {
        auto cell = juce::Rectangle<int> (area.getX() + col * colWidth, area.getY() + row * rowHeight, colWidth - 12, rowHeight).reduced (0, 2);
        label.setBounds (cell.removeFromLeft (labelWidth));
        cell.removeFromLeft (6);
        cell.removeFromRight (reserveRight);
        control.setBounds (cell);
    };

    place (0, 0, nameLabel, nameEdit);
    place (1, 0, noteLabel, noteEdit, 60);
    learnButton.setBounds (noteEdit.getRight() + 6, noteEdit.getY(), 54, noteEdit.getHeight());
    place (2, 0, modeLabel, playMode);

    place (0, 1, chokeLabel, chokeGroup);
    place (1, 1, chokedByLabel, chokedByButton);
    place (2, 1, polyLabel, polyEdit);

    place (0, 2, volumeLabel, volume);
    place (1, 2, panLabel, pan);
    place (2, 2, outputLabel, output);
}
} // namespace drumhits::ui
