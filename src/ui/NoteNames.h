#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace drumhits::ui
{
// Middle C (note 60) is C4, so note 36 is C2.
inline juce::String noteName (int note)
{
    return note < 0 ? juce::String ("none") : juce::MidiMessage::getMidiNoteName (note, true, true, 4);
}
} // namespace drumhits::ui
