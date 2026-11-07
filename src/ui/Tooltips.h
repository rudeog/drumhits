#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace drumhits::ui
{
// Sets the same tooltip on every component that supports one.
inline void setTooltip (const juce::String& text, std::initializer_list<juce::Component*> components)
{
    for (auto* c : components)
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (c))
            client->setTooltip (text);
}
} // namespace drumhits::ui
