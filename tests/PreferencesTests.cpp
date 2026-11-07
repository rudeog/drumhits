#include <catch2/catch_test_macros.hpp>

#include "core/controller/Controller.h"
#include "core/preferences/Preferences.h"

using namespace drumhits;

TEST_CASE ("Note names are parsed")
{
    CHECK (Preferences::parseNoteName ("C2") == 36);
    CHECK (Preferences::parseNoteName (" d2 ") == 38);
    CHECK (Preferences::parseNoteName ("F#1") == 30);
    CHECK (Preferences::parseNoteName ("Bb3") == 58);
    CHECK (Preferences::parseNoteName ("C-1") == 0);
    CHECK (Preferences::parseNoteName ("G9") == 127);
    CHECK (Preferences::parseNoteName ("G#9") == -1);
    CHECK (Preferences::parseNoteName ("H2") == -1);
    CHECK (Preferences::parseNoteName ("C") == -1);
    CHECK (Preferences::parseNoteName ("C2x") == -1);
    CHECK (Preferences::parseNoteName ("") == -1);
}

TEST_CASE ("Drum note lists are validated and trimmed")
{
    auto list = Preferences::parseDrumNotes ("d2, E2 ,f#2");
    REQUIRE (list.result.wasOk());
    CHECK (list.notes == std::vector<int> { 38, 40, 42 });
    CHECK (list.text == "D2,E2,F#2");

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
        CHECK (prefs.setDefaultDrumNotes ("d2,e2").wasOk());
        CHECK (prefs.setDefaultDrumNotes ("nope").failed());
        CHECK (prefs.getDefaultDrumNotesText() == "D2,E2");
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
