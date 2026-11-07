#include "ui/MainTab.h"

namespace drumhits::ui
{
MainTab::MainTab (Controller& controllerToUse)
    : grid (controllerToUse),
      properties (controllerToUse),
      hits (controllerToUse),
      details (controllerToUse),
      controller (controllerToUse)
{
    grid.onDrumSelected = [this] (int drum) { selectDrum (drum); };
    grid.onDrumContextMenu = [this] (int drum, juce::Point<int> position) { showDrumActions (drum, position); };
    grid.onFilesDropped = [this] (int drum, const juce::StringArray& files) { addDroppedFiles (drum, files); };
    grid.onHitsDropped = [this] (int sourceDrum, const std::vector<int>& slots, int targetDrum)
    { copyDroppedHits (sourceDrum, slots, targetDrum); };
    hits.onHitsChanged = [this] { grid.repaint(); updateHitWave(); };
    hits.onSelectionChanged = [this] { updateHitWave(); };
    hits.onPreview = [this] { playHit(); };
    hits.onJumpToWave = [this] (int drum, int slot)
    {
        const auto props = controller.getHitProperties (drum, slot);
        if (props.has_value() && onJumpToWave)
            onJumpToWave (controller.getHitWaveId (drum, slot), props->startPos, props->endPos);
    };
    properties.onNameChanged = [this] { grid.repaint(); };
    properties.onNoteChanged = [this] { grid.repaint(); };
    hitWave.setMarkersEditable (true);
    hitWave.onMarkersChanged = [this] { commitHitMarkers(); };
    hitWave.onPlay = [this] { playHit(); };

    addAndMakeVisible (grid);
    addAndMakeVisible (properties);
    details.onNameChanged = [this] { hits.refresh(); };
    addAndMakeVisible (hits);
    addAndMakeVisible (details);
    for (auto* label : { &waveNameLabel, &wavePathLabel })
    {
        label->setFont (juce::FontOptions (12.0f));
        label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        label->setJustificationType (juce::Justification::centredLeft);
        label->setMinimumHorizontalScale (1.0f);
        addChildComponent (*label);
    }
    addChildComponent (hitWave);
    selectDrum (0);
}

void MainTab::selectDrum (int drum)
{
    activeDrum = drum;
    grid.setActiveDrum (drum);
    properties.setDrum (drum);
    hits.setDrum (drum);
    updateHitWave();
}

void MainTab::showDrumActions (int drum, juce::Point<int> screenPosition)
{
    juce::PopupMenu duplicateMenu;
    bool hasTargets = false;
    for (int target = 0; target < maxDrums; ++target)
    {
        if (target == drum || controller.getHitCount (target) != 0)
            continue;

        auto name = controller.getDrumName (target);
        if (name.isEmpty())
            name = "Drum " + juce::String (target + 1);
        duplicateMenu.addItem (target + 1, juce::String (target + 1) + " - " + name);
        hasTargets = true;
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Duplicate drum", duplicateMenu, hasTargets);
    menu.addSeparator();
    menu.addItem (1000, "Reset to empty");
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (&grid)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe = juce::Component::SafePointer<MainTab> (this), drum] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            if (result == 1000)
                            {
                                safe->controller.resetDrum (drum);
                                safe->selectDrum (drum);
                                safe->grid.repaint();
                                return;
                            }

                            const int target = result - 1;
                            if (! safe->controller.duplicateDrum (drum, target))
                            {
                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                        "Could not duplicate drum",
                                                                        "The target drum is no longer empty.");
                                return;
                            }

                            safe->selectDrum (target);
                            safe->grid.repaint();
                        });
}

void MainTab::addDroppedFiles (int drum, const juce::StringArray& files)
{
    selectDrum (drum);
    juce::Array<juce::File> droppedFiles;
    for (const auto& path : files)
        droppedFiles.add (juce::File (path));
    hits.addFiles (droppedFiles);
}

void MainTab::copyDroppedHits (int sourceDrum, const std::vector<int>& hitSlots, int targetDrum)
{
    bool full = false;
    for (const int slot : hitSlots)
        full |= controller.copyHit (sourceDrum, slot, targetDrum) < 0;

    selectDrum (targetDrum);
    hits.refresh();
    grid.repaint();
    updateHitWave();

    if (full)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Maximum hits reached",
                                                "The target drum has no room for all the hits.");
}

void MainTab::showHit (int drum, int slot)
{
    if (drum < 0 || drum >= maxDrums || ! controller.getHitProperties (drum, slot).has_value())
        return;

    selectDrum (drum);
    hits.selectHitSlot (slot);
}

void MainTab::playHit()
{
    const auto slot = hits.getSelectedSlot();
    if (slot >= 0)
        controller.auditionHit (activeDrum, slot, grid.getAuditionVelocity());
}

void MainTab::commitHitMarkers()
{
    const auto slot = hits.getSelectedSlot();
    const auto markers = hitWave.getMarkers();
    auto props = slot >= 0 ? controller.getHitProperties (activeDrum, slot) : std::nullopt;
    if (! props.has_value() || ! markers.has_value())
        return;

    props->startPos = markers->start;
    props->endPos = markers->end;
    props->fadeInLength = markers->fadeInLength;
    props->fadeOutLength = markers->fadeOutLength;
    controller.setHitProperties (activeDrum, slot, *props);

    // Show what the controller actually accepted.
    if (const auto applied = controller.getHitProperties (activeDrum, slot))
        hitWave.setMarkers (WaveView::Markers { applied->startPos, applied->endPos, applied->fadeInLength, applied->fadeOutLength });
}

void MainTab::updateHitWave()
{
    const auto slot = hits.getSelectedSlot();
    const auto props = slot >= 0 ? controller.getHitProperties (activeDrum, slot) : std::nullopt;
    details.setHit (activeDrum, props.has_value() ? slot : -1);
    const int waveId = props.has_value() ? controller.getHitWaveId (activeDrum, slot) : -1;
    const auto* wave = waveId >= 0 ? controller.getWave (waveId) : nullptr;
    const auto name = waveId >= 0 ? controller.getWaveName (waveId) : juce::String();
    const auto path = waveId >= 0 ? controller.getWaveFile (waveId).getFullPathName() : juce::String();
    waveNameLabel.setText ("Wave: " + name, juce::dontSendNotification);
    wavePathLabel.setText ("Path: " + path, juce::dontSendNotification);
    waveNameLabel.setTooltip (name);
    wavePathLabel.setTooltip (path);
    waveNameLabel.setVisible (waveId >= 0);
    wavePathLabel.setVisible (waveId >= 0);

    hitWave.setVisible (wave != nullptr);
    hitWave.setWave (wave);
    if (wave != nullptr)
        hitWave.setMarkers (WaveView::Markers { props->startPos, props->endPos, props->fadeInLength, props->fadeOutLength });
    else
        hitWave.setMarkers (std::nullopt);
}

void MainTab::refresh()
{
    grid.repaint();
    properties.setDrum (activeDrum);
    hits.refresh();
    updateHitWave();
}

void MainTab::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (hitDetailsArea.toFloat(), 6.0f);

}

void MainTab::resized()
{
    auto area = getLocalBounds().reduced (8);
    constexpr int detailsHeight = HitDetailsPanel::rowCount * HitDetailsPanel::rowHeight;
    constexpr int metadataHeight = 36;
    constexpr int minWaveHeight = 64;
    const int maxGridHeight = std::max (0, area.getHeight() - 130 - 12 - detailsHeight - metadataHeight - minWaveHeight);
    grid.setBounds (area.removeFromTop (std::min ({ 200, area.getHeight() / 3, maxGridHeight })));
    area.removeFromTop (6);
    properties.setBounds (area.removeFromTop (130));
    area.removeFromTop (6);

    hits.setBounds (area.removeFromLeft (area.getWidth() * 2 / 5));
    area.removeFromLeft (6);
    hitDetailsArea = area;
    details.setBounds (area.removeFromTop (detailsHeight).reduced (6, 4));
    const int waveHeight = std::min (110, std::max (0, area.getHeight() - metadataHeight));
    hitWave.setBounds (area.removeFromBottom (waveHeight).reduced (4));
    auto metadata = area.removeFromBottom (metadataHeight).reduced (4, 0);
    waveNameLabel.setBounds (metadata.removeFromTop (metadataHeight / 2));
    wavePathLabel.setBounds (metadata);
}
} // namespace drumhits::ui
