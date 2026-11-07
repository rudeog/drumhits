#include "PluginEditor.h"

#include "ui/MainTab.h"
#include "ui/SettingsTab.h"
#include "ui/WavesTab.h"

#include <cmath>

namespace
{
enum TabIndex
{
    mainTabIndex,
    wavesTabIndex,
    settingsTabIndex
};
} // namespace

DrumHitsEditor::DrumHitsEditor (DrumHitsProcessor& p)
    : AudioProcessorEditor (&p)
{
    const auto background = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);

    mainTab = new drumhits::ui::MainTab (p.getController());
    wavesTab = new drumhits::ui::WavesTab (p.getController());

    settingsTab = new drumhits::ui::SettingsTab (p.getController(), p.getPreferences());

    tabs.addTab ("Main", background, mainTab, true);
    tabs.addTab ("Wave Pool", background, wavesTab, true);
    tabs.addTab ("Settings", background, settingsTab, true);
    mainTab->onJumpToWave = [this] (int waveId, int64_t start, int64_t end)
    {
        tabs.setCurrentTabIndex (wavesTabIndex);
        wavesTab->showWave (waveId, start, end);
    };
    wavesTab->onJumpToHit = [this] (int drum, int slot)
    {
        tabs.setCurrentTabIndex (mainTabIndex);
        mainTab->showHit (drum, slot);
    };
    tabs.onTabChanged = [this] (int index) { refreshTab (index); };
    addAndMakeVisible (tabs);

    settingsTab->onMissingWavesLoaded = [safe = juce::Component::SafePointer<DrumHitsEditor> (this)] (int count)
    {
        if (safe != nullptr)
            safe->showMissingWavesDialog (count);
    };
    p.onMissingWavesLoaded = [safe = juce::Component::SafePointer<DrumHitsEditor> (this)] (int count)
    {
        if (safe != nullptr)
            safe->showMissingWavesDialog (count);
    };
    p.onWavesReloaded = [safe = juce::Component::SafePointer<DrumHitsEditor> (this)] (const juce::String& error, int count)
    {
        if (safe == nullptr)
            return;
        safe->mainTab->refresh();
        safe->wavesTab->refresh();
        if (error.isNotEmpty())
        {
            if (count > 0)
                safe->showMissingWavesDialog (count, error);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Sample-rate change failed", error);
        }
    };

    panicButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb02a2a));
    panicButton.onClick = [&p] { p.getController().sendAllNotesOff(); };
    addAndMakeVisible (panicButton); // after tabs so it sits on top of the tab bar

    setResizable (true, true);
    setResizeLimits (700, 500, 2000, 1400);
    setSize (900, 650);

    if (p.getController().getMissingWaveCount() > 0)
        showMissingWavesDialog (p.getController().getMissingWaveCount(), p.getSampleRateError());
    else if (p.getSampleRateError().isNotEmpty()
             && std::abs (p.getController().getSampleRate() - p.getSampleRate()) >= 1e-6)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Sample-rate change failed", p.getSampleRateError());
}

void DrumHitsEditor::refreshTab (int index)
{
    if (index == mainTabIndex)
        mainTab->refresh();
    else if (index == wavesTabIndex)
        wavesTab->refresh();
    else if (index == settingsTabIndex)
        settingsTab->refresh();
}

void DrumHitsEditor::showMissingWavesDialog (int count, const juce::String& details)
{
    mainTab->refresh();
    wavesTab->refresh();
    if (missingWavesDialogOpen || count <= 0)
        return;

    missingWavesDialogOpen = true;
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("Missing wave files")
            .withMessage (juce::String (count) + " wave file(s) could not be found."
                          + (details.isNotEmpty() ? "\n\n" + details : juce::String()))
            .withButton ("Search Missing")
            .withButton ("OK"),
        [safe = juce::Component::SafePointer<DrumHitsEditor> (this)] (int result)
        {
            if (safe == nullptr)
                return;

            safe->missingWavesDialogOpen = false;
            if (result == 1)
            {
                safe->tabs.setCurrentTabIndex (wavesTabIndex);
                safe->wavesTab->searchMissing();
            }
        });
}

void DrumHitsEditor::resized()
{
    tabs.setBounds (getLocalBounds());
    panicButton.setBounds (getWidth() - 80 - 6, 4, 80, tabs.getTabBarDepth() - 8);
}
