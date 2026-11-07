#pragma once

#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "core/Limits.h"

namespace drumhits
{
// Result of validating a comma delimited list of note names.
struct DrumNoteList
{
    juce::Result result = juce::Result::ok();
    std::vector<int> notes;   // at most maxDrums entries
    juce::String text;        // canonical text, e.g. "D2,E2"; empty when no notes were given
    bool truncated = false;   // more than maxDrums values were given and the extras were dropped
};

// Global (not per patch) user preferences, persisted with a juce::PropertiesFile.
class Preferences
{
public:
    // Stores the preferences in the given file. Use getDefaultFile() for the real location.
    explicit Preferences (const juce::File& file);

    static juce::File getDefaultFile();

    // Parses names like "D2", "F#1", "Bb3" (case-insensitive, middle C is C4). Returns -1 if invalid.
    static int parseNoteName (const juce::String& name);

    // Validates a comma delimited list. Every value must be a valid note name; values beyond
    // maxDrums are dropped. An empty string is valid and means "use the built-in defaults".
    static DrumNoteList parseDrumNotes (const juce::String& text);

    // Canonical text of the stored list. Empty if never set.
    juce::String getDefaultDrumNotesText() const;
    std::vector<int> getDefaultDrumNotes() const;

    // Stores the list if valid. On success the canonical list is returned in 'stored' (optional).
    juce::Result setDefaultDrumNotes (const juce::String& text, juce::String* stored = nullptr);

private:
    juce::PropertiesFile file;
};
} // namespace drumhits
