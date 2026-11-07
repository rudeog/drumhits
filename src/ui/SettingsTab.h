#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"
#include "core/preferences/Preferences.h"

namespace drumhits::ui
{
// Patch file actions and global preferences (saved per user).
class SettingsTab final : public juce::Component
{
public:
    SettingsTab (Controller& controllerToUse, Preferences& preferencesToUse);

    std::function<void (int)> onMissingWavesLoaded;

    // Re-reads the current values.
    void refresh();

    void resized() override;

private:
    void commitDefaultNotes();
    void chooseAndLoadPatch();
    void chooseAndSavePatch();
    void showMessage (const juce::String& title, const juce::String& text);
    juce::File defaultPatchDirectory() const;

    Controller& controller;
    Preferences& preferences;

    juce::TextButton loadPatchButton { "Load patch..." };
    juce::GroupComponent savePatchGroup;
    juce::TextButton savePatchButton { "Save patch..." };
    juce::ToggleButton purgeUnusedToggle { "Purge unused waves" };
    juce::ToggleButton consolidateToggle { "Consolidate waves into patch folder" };
    juce::Label consolidateInfo { {}, "This option will create a dedicated subfolder in the same folder as the patch file and copy all waves into this subfolder." };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label preferencesHeader { {}, "Global preferences" };
    juce::Label defaultNotesLabel { {}, "Default drum notes" };
    juce::Label defaultNotes;
    juce::Label defaultNotesInfo;

    juce::File lastPatchDirectory;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsTab)
};
} // namespace drumhits::ui
