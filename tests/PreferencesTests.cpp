#include <catch2/catch_test_macros.hpp>

#include "core/controller/Controller.h"
#include "core/preferences/Preferences.h"

using namespace drumhits;

TEST_CASE ("Note names are parsed")
{
    CHECK (Preferences::parseNoteName ("C1") == 36);
    CHECK (Preferences::parseNoteName (" d1 ") == 38);
    CHECK (Preferences::parseNoteName ("F#0") == 30);
    CHECK (Preferences::parseNoteName ("Bb2") == 58);
    CHECK (Preferences::parseNoteName ("C-2") == 0);
    CHECK (Preferences::parseNoteName ("G8") == 127);
    CHECK (Preferences::parseNoteName ("G#8") == -1);
    CHECK (Preferences::parseNoteName ("H1") == -1);
    CHECK (Preferences::parseNoteName ("C") == -1);
    CHECK (Preferences::parseNoteName ("C1x") == -1);
    CHECK (Preferences::parseNoteName ("") == -1);
}

TEST_CASE ("Drum note lists are validated and trimmed")
{
    auto list = Preferences::parseDrumNotes ("d1, E1 ,f#1");
    REQUIRE (list.result.wasOk());
    CHECK (list.notes == std::vector<int> { 38, 40, 42 });
    CHECK (list.text == "D1,E1,F#1");

    CHECK (Preferences::parseDrumNotes ("D2,X9").result.failed());
    CHECK (Preferences::parseDrumNotes ("D2,,E2").result.failed());

    list = Preferences::parseDrumNotes ("");
    CHECK (list.result.wasOk());
    CHECK (list.notes.empty());

    list = Preferences::parseDrumNotes ("C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,C1,D2,E2");
    CHECK (list.notes.size() == static_cast<size_t> (maxDrums));
    CHECK (list.truncated);
}

TEST_CASE ("Preferences persist")
{
    juce::TemporaryFile temp (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drumhits_prefs.settings"));
    {
        Preferences prefs (temp.getFile());
        CHECK (prefs.getDefaultDrumNotes().empty());
        CHECK (prefs.setDefaultDrumNotes ("d1,e1").wasOk());
        CHECK (prefs.setDefaultDrumNotes ("nope").failed());
        CHECK (prefs.getDefaultDrumNotesText() == "D1,E1");
    }
    Preferences reloaded (temp.getFile());
    CHECK (reloaded.getDefaultDrumNotes() == std::vector<int> { 38, 40 });
}

TEST_CASE ("Controller uses configured default drum notes")
{
    Engine engine { 256 };
    Controller controller { engine };
    controller.setDefaultDrumNotes ({ 38, 40 });
    controller.initialiseDefaultDrums();
    CHECK (controller.getDrumProperties (0).note == 38);
    CHECK (controller.getDrumProperties (1).note == 40);
    CHECK (controller.getDrumProperties (2).note == 36 + 2 * 2);
}
