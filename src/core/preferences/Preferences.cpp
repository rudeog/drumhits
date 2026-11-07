#include "core/preferences/Preferences.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace drumhits
{
namespace
{
const char* const defaultDrumNotesKey = "defaultDrumNotes";

juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DrumHits";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = "DrumHits";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}
} // namespace

Preferences::Preferences (const juce::File& f)
    : file (f, makeOptions())
{
}

juce::File Preferences::getDefaultFile()
{
    return makeOptions().getDefaultFile();
}

int Preferences::parseNoteName (const juce::String& name)
{
    const auto t = name.trim();
    if (t.length() < 2)
        return -1;

    static constexpr int semitones[] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
    const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);
    if (letter < 'A' || letter > 'G')
        return -1;

    int semitone = semitones[letter - 'A'];
    int pos = 1;
    if (t[pos] == '#')
        ++semitone, ++pos;
    else if (t[pos] == 'b' || t[pos] == 'B')
        --semitone, ++pos;

    const auto octaveText = t.substring (pos);
    if (! octaveText.containsOnly ("-0123456789") || octaveText.isEmpty() || octaveText.length() > 3
        || octaveText.substring (1).contains ("-") || octaveText == "-")
        return -1;

    const int note = (octaveText.getIntValue() + 1) * 12 + semitone;
    return note >= 0 && note <= 127 ? note : -1;
}

DrumNoteList Preferences::parseDrumNotes (const juce::String& text)
{
    DrumNoteList list;
    if (text.trim().isEmpty())
        return list;

    juce::StringArray parts;
    parts.addTokens (text, ",", "");

    for (const auto& part : parts)
    {
        const int note = parseNoteName (part);
        if (note < 0)
        {
            list.result = juce::Result::fail ("Invalid note \"" + part.trim() + "\"");
            list.notes.clear();
            return list;
        }

        if (static_cast<int> (list.notes.size()) < maxDrums)
            list.notes.push_back (note);
        else
            list.truncated = true;
    }

    juce::StringArray names;
    for (const int n : list.notes)
        names.add (juce::MidiMessage::getMidiNoteName (n, true, true, 4));
    list.text = names.joinIntoString (",");
    return list;
}

juce::String Preferences::getDefaultDrumNotesText() const
{
    return parseDrumNotes (file.getValue (defaultDrumNotesKey)).text;
}

std::vector<int> Preferences::getDefaultDrumNotes() const
{
    return parseDrumNotes (file.getValue (defaultDrumNotesKey)).notes;
}

juce::Result Preferences::setDefaultDrumNotes (const juce::String& text, juce::String* stored)
{
    const auto list = parseDrumNotes (text);
    if (list.result.failed())
        return list.result;

    file.setValue (defaultDrumNotesKey, list.text);
    file.saveIfNeeded();
    if (stored != nullptr)
        *stored = list.text;
    return juce::Result::ok();
}
} // namespace drumhits
