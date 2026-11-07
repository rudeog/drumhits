#include "ui/DrumGrid.h"

#include "ui/NoteNames.h"

namespace drumhits::ui
{
namespace
{
constexpr int timerHz = 30;
constexpr float flashDecayPerTick = 0.12f;

bool parseHitDragDescription (const juce::var& description, int& sourceDrum, std::vector<int>& hitSlots)
{
    const auto text = description.toString();
    const juce::String prefix = "drumhits-hits:";
    if (! text.startsWith (prefix))
        return false;

    const auto payload = text.substring (prefix.length());
    const int separator = payload.indexOfChar (':');
    if (separator <= 0 || separator == payload.length() - 1)
        return false;

    const auto drumText = payload.substring (0, separator);
    sourceDrum = drumText.getIntValue();
    if (juce::String (sourceDrum) != drumText || sourceDrum < 0 || sourceDrum >= maxDrums)
        return false;

    auto remaining = payload.substring (separator + 1);
    while (remaining.isNotEmpty())
    {
        const int comma = remaining.indexOfChar (',');
        const auto slotText = comma < 0 ? remaining : remaining.substring (0, comma);
        const int slot = slotText.getIntValue();
        if (slotText.isEmpty() || juce::String (slot) != slotText || slot < 0 || slot >= maxHitsPerDrum)
            return false;

        hitSlots.push_back (slot);
        if (comma < 0)
            break;
        remaining = remaining.substring (comma + 1);
    }

    return ! hitSlots.empty();
}
} // namespace

DrumGrid::DrumGrid (Controller& controllerToUse)
    : controller (controllerToUse)
{
    startTimerHz (timerHz);
}

DrumGrid::~DrumGrid()
{
    stopTimer();
}

void DrumGrid::timerCallback()
{
    const auto received = controller.takeReceivedNotes();
    bool changed = false;

    for (int d = 0; d < maxDrums; ++d)
    {
        auto& level = flash[static_cast<size_t> (d)];
        const int note = controller.getDrumProperties (d).note;

        if (note >= 0 && note < 128 && ((received[static_cast<size_t> (note >> 6)] >> (note & 63)) & 1) != 0)
        {
            level = 1.0f;
            changed = true;
        }
        else if (level > 0.0f)
        {
            level = std::max (0.0f, level - flashDecayPerTick);
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void DrumGrid::setActiveDrum (int drum)
{
    activeDrum = juce::jlimit (0, maxDrums - 1, drum);
    repaint();
}

juce::Rectangle<int> DrumGrid::cellBounds (int drum) const
{
    const int w = getWidth() / columns;
    const int h = getHeight() / rows;
    return juce::Rectangle<int> ((drum % columns) * w, (drum / columns) * h, w, h).reduced (4);
}

int DrumGrid::cellAt (juce::Point<int> position) const
{
    if (! getLocalBounds().contains (position) || getWidth() <= 0 || getHeight() <= 0)
        return -1;

    const int column = std::min (columns - 1, position.x * columns / getWidth());
    const int row = std::min (rows - 1, position.y * rows / getHeight());
    return row * columns + column;
}

void DrumGrid::paint (juce::Graphics& g)
{
    for (int d = 0; d < maxDrums; ++d)
    {
        const auto bounds = cellBounds (d).toFloat();
        const bool hasHits = controller.getHitCount (d) > 0;
        const bool active = d == activeDrum;

        g.setColour (hasHits ? juce::Colour (0xff2f6fa8) : juce::Colour (0xff3a3a3a));
        g.fillRoundedRectangle (bounds, 6.0f);

        if (const float level = flash[static_cast<size_t> (d)]; level > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (level * 0.6f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        if (d == dropTarget)
        {
            g.setColour (juce::Colour (0xff5d9b55).withAlpha (0.45f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        g.setColour (active ? juce::Colour (0xffffb020) : juce::Colour (0xff606060));
        g.drawRoundedRectangle (bounds.reduced (active ? 1.5f : 0.5f), 6.0f, active ? 3.0f : 1.0f);

        const auto name = controller.getDrumName (d);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (name.isNotEmpty() ? name : "Drum " + juce::String (d + 1),
                    bounds.reduced (6.0f, 4.0f).removeFromTop (bounds.getHeight() * 0.5f), juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (noteName (controller.getDrumProperties (d).note),
                    bounds.reduced (6.0f, 4.0f).removeFromBottom (bounds.getHeight() * 0.5f), juce::Justification::centredLeft, true);
    }
}

void DrumGrid::mouseDown (const juce::MouseEvent& event)
{
    const int drum = cellAt (event.getPosition());
    if (drum < 0)
        return;

    setActiveDrum (drum);
    if (event.mods.isPopupMenu())
    {
        if (onDrumSelected)
            onDrumSelected (drum);
        if (onDrumContextMenu)
            onDrumContextMenu (drum, localPointToGlobal (event.getPosition()));
        return;
    }

    const int note = controller.getDrumProperties (drum).note;
    if (note >= 0)
        controller.sendNote (note, auditionVelocity);

    if (onDrumSelected)
        onDrumSelected (drum);
}

bool DrumGrid::isInterestedInDragSource (const SourceDetails& details)
{
    int sourceDrum = -1;
    std::vector<int> slots;
    if (! parseHitDragDescription (details.description, sourceDrum, slots))
        return false;

    return std::all_of (slots.begin(), slots.end(), [this, sourceDrum] (int slot)
                        { return controller.getHitProperties (sourceDrum, slot).has_value(); });
}

void DrumGrid::itemDragEnter (const SourceDetails& details)
{
    setDropTarget (cellAt (details.localPosition));
}

void DrumGrid::itemDragMove (const SourceDetails& details)
{
    setDropTarget (cellAt (details.localPosition));
}

void DrumGrid::itemDragExit (const SourceDetails&)
{
    setDropTarget (-1);
}

void DrumGrid::itemDropped (const SourceDetails& details)
{
    int sourceDrum = -1;
    std::vector<int> slots;
    const int targetDrum = cellAt (details.localPosition);
    setDropTarget (-1);
    if (targetDrum >= 0 && parseHitDragDescription (details.description, sourceDrum, slots)
        && sourceDrum != targetDrum && onHitsDropped)
        onHitsDropped (sourceDrum, slots, targetDrum);
}

bool DrumGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty();
}

void DrumGrid::fileDragEnter (const juce::StringArray&, int x, int y)
{
    setDropTarget (cellAt ({ x, y }));
}

void DrumGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    setDropTarget (cellAt ({ x, y }));
}

void DrumGrid::fileDragExit (const juce::StringArray&)
{
    setDropTarget (-1);
}

void DrumGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int targetDrum = cellAt ({ x, y });
    setDropTarget (-1);
    if (targetDrum >= 0 && onFilesDropped)
        onFilesDropped (targetDrum, files);
}

void DrumGrid::setDropTarget (int drum)
{
    if (dropTarget == drum)
        return;
    dropTarget = drum;
    repaint();
}
} // namespace drumhits::ui
