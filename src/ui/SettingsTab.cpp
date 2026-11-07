#include "ui/SettingsTab.h"

namespace drumhits::ui
{
SettingsTab::SettingsTab (Controller& controllerToUse, Preferences& preferencesToUse)
    : controller (controllerToUse), preferences (preferencesToUse)
{
    preferencesHeader.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (preferencesHeader);
    defaultNotesLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (defaultNotesLabel);

    defaultNotes.setEditable (true);
    defaultNotes.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    defaultNotes.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    defaultNotes.setJustificationType (juce::Justification::centredLeft);
    defaultNotes.onTextChange = [this] { commitDefaultNotes(); };
    addAndMakeVisible (defaultNotes);

    defaultNotesInfo.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (defaultNotesInfo);

    loadPatchButton.onClick = [this] { chooseAndLoadPatch(); };
    savePatchButton.onClick = [this] { chooseAndSavePatch(); };
    addAndMakeVisible (loadPatchButton);
    addAndMakeVisible (savePatchGroup);
    addAndMakeVisible (savePatchButton);
    addAndMakeVisible (purgeUnusedToggle);
    addAndMakeVisible (consolidateToggle);
    consolidateInfo.setFont (juce::FontOptions (13.0f));
    consolidateInfo.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (consolidateInfo);

    refresh();
}

void SettingsTab::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    defaultNotes.setText (preferences.getDefaultDrumNotesText(), juce::dontSendNotification);
    defaultNotesInfo.setText ("Comma separated note names, e.g. D2,E2 (max " + juce::String (maxDrums)
                                  + "). Applies to new patches. Empty uses the built-in layout.",
                              juce::dontSendNotification);
}

juce::File SettingsTab::defaultPatchDirectory() const
{
    if (lastPatchDirectory.isDirectory())
        return lastPatchDirectory;

    // The DAW project directory is not available to the plugin yet.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

void SettingsTab::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (text).withButton ("OK"),
                                  nullptr);
}

void SettingsTab::chooseAndLoadPatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Load patch", defaultPatchDirectory(), "*.dhpatch");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              lastPatchDirectory = file.getParentDirectory();
                              const auto result = controller.loadPatch (file);
                              if (result.failed())
                              {
                                  showMessage ("Load patch failed", result.getErrorMessage());
                                  return;
                              }

                              refresh();

                              const int missing = controller.getMissingWaveCount();
                              if (missing > 0 && onMissingWavesLoaded)
                                  onMissingWavesLoaded (missing);
                          });
}

void SettingsTab::chooseAndSavePatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Save patch", defaultPatchDirectory(), "*.dhpatch");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              if (! file.hasFileExtension ("dhpatch"))
                                  file = file.withFileExtension ("dhpatch");

                              lastPatchDirectory = file.getParentDirectory();
                              SavePatchOptions options;
                              options.purgeUnused = purgeUnusedToggle.getToggleState();
                              options.consolidate = consolidateToggle.getToggleState();

                              const auto result = controller.savePatch (file, options);
                              if (result.failed())
                                  showMessage ("Save patch failed", result.getErrorMessage());
                          });
}

void SettingsTab::commitDefaultNotes()
{
    if (updating)
        return;

    const auto list = Preferences::parseDrumNotes (defaultNotes.getText());
    if (list.result.failed())
    {
        defaultNotesInfo.setText (list.result.getErrorMessage(), juce::dontSendNotification);
        defaultNotesInfo.setColour (juce::Label::textColourId, juce::Colours::orangered);
        return;
    }

    preferences.setDefaultDrumNotes (list.text);
    controller.setDefaultDrumNotes (list.notes);
    defaultNotesInfo.setColour (juce::Label::textColourId, juce::Colours::grey);
    refresh();

    if (list.truncated)
        defaultNotesInfo.setText ("Only the first " + juce::String (maxDrums) + " notes were kept.", juce::dontSendNotification);
}

void SettingsTab::resized()
{
    auto area = getLocalBounds().reduced (16);
    const int rowHeight = 26, labelWidth = 160, gap = 8;

    loadPatchButton.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));
    area.removeFromTop (12);

    auto saveArea = area.removeFromTop (158);
    savePatchGroup.setBounds (saveArea);
    saveArea.reduce (12, 12);
    saveArea.removeFromTop (gap);
    savePatchButton.setBounds (saveArea.removeFromTop (rowHeight).removeFromLeft (120));
    saveArea.removeFromTop (gap);
    purgeUnusedToggle.setBounds (saveArea.removeFromTop (rowHeight));
    consolidateToggle.setBounds (saveArea.removeFromTop (rowHeight));
    consolidateInfo.setBounds (saveArea.removeFromTop (rowHeight));

    area.removeFromTop (16);

    preferencesHeader.setBounds (area.removeFromTop (rowHeight));
    auto row = area.removeFromTop (rowHeight);
    defaultNotesLabel.setBounds (row.removeFromLeft (labelWidth));
    row.removeFromLeft (gap);
    defaultNotes.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), 400)));

    row = area.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth + gap);
    defaultNotesInfo.setBounds (row);
}
} // namespace drumhits::ui
