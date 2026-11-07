#include "ui/HitListPanel.h"

#include "ui/Tooltips.h"

#include <algorithm>

namespace drumhits::ui
{
HitListPanel::HitListPanel (Controller& controllerToUse)
    : controller (controllerToUse)
{
    list.setRowHeight (24);
    addAndMakeVisible (list);

    list.setMultipleSelectionEnabled (true);

    addButton.onClick = [this] { showAddMenu(); };
    otherButton.onClick = [this] { showOtherMenu(); };
    for (auto* b : { &addButton, &otherButton })
        addAndMakeVisible (b);
    setTooltip ("List of hits for this drum", { &list });
    setTooltip ("Add files or loaded waves as hits on this drum", { &addButton });
    setTooltip ("Delete, copy, and other hit actions", { &otherButton });
}

void HitListPanel::setDrum (int newDrum)
{
    const bool changed = newDrum != drum;
    drum = newDrum;

    if (changed)
        list.deselectAllRows();

    refresh();

    if (changed && getNumRows() > 0)
        list.selectRow (0);
}

void HitListPanel::refresh()
{
    list.updateContent();
    list.repaint();
}

int HitListPanel::getSelectedSlot() const
{
    if (list.getNumSelectedRows() != 1)
        return -1;
    const auto slots = controller.getHitSlots (drum);
    const auto row = list.getSelectedRow (0);
    return row >= 0 && row < static_cast<int> (slots.size()) ? slots[static_cast<size_t> (row)] : -1;
}

void HitListPanel::selectHitSlot (int slot)
{
    const auto slots = controller.getHitSlots (drum);
    const auto it = std::find (slots.begin(), slots.end(), slot);
    if (it == slots.end())
        return;

    list.deselectAllRows();
    list.selectRow (static_cast<int> (it - slots.begin()));
}

void HitListPanel::selectedRowsChanged (int)
{
    if (onSelectionChanged)
        onSelectionChanged();
}

void HitListPanel::resized()
{
    auto area = getLocalBounds().reduced (2);
    auto buttons = area.removeFromBottom (28);
    addButton.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (4);
    otherButton.setBounds (buttons.removeFromLeft (40));
    area.removeFromBottom (4);
    list.setBounds (area);
}

int HitListPanel::getNumRows()
{
    return controller.getHitCount (drum);
}

void HitListPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto slots = controller.getHitSlots (drum);
    if (row < 0 || row >= static_cast<int> (slots.size()))
        return;

    if (selected)
        g.fillAll (juce::Colour (0xff2f6fa8));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (controller.getHitName (drum, slots[static_cast<size_t> (row)]), 8, 0, width - 16, height, juce::Justification::centredLeft, true);
}

void HitListPanel::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu())
        return;

    if (! list.isRowSelected (row))
    {
        list.deselectAllRows();
        list.selectRow (row);
    }

    showOtherMenu (true);
}

void HitListPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown() || row < 0 || row >= getNumRows())
        return;

    list.selectRow (row);
    if (onPreview)
        onPreview();
}

juce::var HitListPanel::getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe)
{
    if (rowsToDescribe.isEmpty())
        return {};

    const auto slots = controller.getHitSlots (drum);
    juce::String description = "drumhits-hits:" + juce::String (drum) + ":";
    bool first = true;
    for (int i = 0; i < rowsToDescribe.size(); ++i)
    {
        const int row = rowsToDescribe[i];
        if (row < 0 || row >= static_cast<int> (slots.size()))
            continue;

        if (! first)
            description << ",";
        description << slots[static_cast<size_t> (row)];
        first = false;
    }

    return first ? juce::var() : juce::var (description);
}

std::vector<int> HitListPanel::getSelectedSlots() const
{
    std::vector<int> result;
    const auto slots = controller.getHitSlots (drum);
    const auto selected = list.getSelectedRows();
    for (int i = 0; i < selected.size(); ++i)
        if (selected[i] >= 0 && selected[i] < static_cast<int> (slots.size()))
            result.push_back (slots[static_cast<size_t> (selected[i])]);
    return result;
}

void HitListPanel::notifyHitsChanged()
{
    refresh();
    if (onHitsChanged)
        onHitsChanged();
}

void HitListPanel::showAddMenu()
{
    juce::PopupMenu waveMenu;
    std::vector<int> waveIds;
    for (const int waveId : controller.getWaveIds())
    {
        auto label = controller.getWaveName (waveId);
        const auto fileName = controller.getWaveFile (waveId).getFileName();
        if (fileName.isNotEmpty() && fileName != label)
            label += " (" + fileName + ")";
        if (controller.isWaveMissing (waveId))
            label += " [missing]";

        waveMenu.addItem (1000 + static_cast<int> (waveIds.size()), label);
        waveIds.push_back (waveId);
    }

    if (waveMenu.getNumItems() == 0)
        waveMenu.addItem (-1, "No waves loaded", false);

    juce::PopupMenu menu;
    menu.addItem (1, "Add file(s)");
    menu.addSubMenu ("Add wave", waveMenu, ! waveIds.empty());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                        [safe = juce::Component::SafePointer<HitListPanel> (this), waveIds] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 1)
                                safe->chooseFilesToAdd();
                            else if (result >= 1000 && result - 1000 < static_cast<int> (waveIds.size()))
                                safe->addWave (waveIds[static_cast<size_t> (result - 1000)]);
                        });
}

void HitListPanel::addWave (int waveId)
{
    if (controller.getHitCount (drum) >= maxHitsPerDrum)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Maximum hits reached",
                                                "This drum already has the maximum number of hits.");
        return;
    }

    const int slot = controller.addHit (drum, waveId);
    if (slot < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not add wave",
                                                "The selected wave could not be added as a hit.");
        return;
    }

    refresh();
    const auto slots = controller.getHitSlots (drum);
    const auto it = std::find (slots.begin(), slots.end(), slot);
    if (it != slots.end())
        list.selectRow (static_cast<int> (it - slots.begin()));
    notifyHitsChanged();
}

void HitListPanel::chooseFilesToAdd()
{
    chooser = std::make_unique<juce::FileChooser> ("Add wave files", juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<HitListPanel> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr)
                                  safe->addFiles (fc.getResults());
                          });
}

void HitListPanel::addFiles (const juce::Array<juce::File>& files)
{
    juce::StringArray errors;
    bool maxReached = false;
    int firstAddedSlot = -1;

    for (const auto& file : files)
    {
        const auto result = controller.loadWave (file);
        if (result.status == LoadWaveResult::Status::failed)
        {
            errors.add (file.getFileName() + ": " + result.error);
            continue;
        }

        if (controller.getHitCount (drum) >= maxHitsPerDrum)
        {
            maxReached = true;
            break;
        }

        const int slot = controller.addHit (drum, result.waveId);
        if (slot < 0)
            errors.add (file.getFileName() + ": could not add hit");
        else if (firstAddedSlot < 0)
            firstAddedSlot = slot;
    }

    refresh();
    if (firstAddedSlot >= 0)
    {
        const auto slots = controller.getHitSlots (drum);
        const auto it = std::find (slots.begin(), slots.end(), firstAddedSlot);
        list.selectRow (static_cast<int> (it - slots.begin()));
    }
    notifyHitsChanged();

    if (maxReached)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Maximum hits reached",
                                                "This drum already has the maximum number of hits, so some files were not added.");
    if (! errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not add some files", errors.joinIntoString ("\n"));
}

void HitListPanel::deleteSelectedHits()
{
    const auto slots = getSelectedSlots();
    if (slots.empty())
        return;

    const int firstRow = list.getSelectedRow (0);
    for (const int slot : slots)
        controller.removeHit (drum, slot);

    list.deselectAllRows();
    refresh();

    // Select the hit before the deleted one, or else the one that followed it.
    const int remaining = getNumRows();
    if (remaining > 0)
        list.selectRow (std::clamp (firstRow - 1, 0, remaining - 1));

    notifyHitsChanged();
}

void HitListPanel::copySelectedHitsTo (int targetDrum, const std::vector<int>& slots)
{
    bool full = false;
    for (const int slot : slots)
        full |= controller.copyHit (drum, slot, targetDrum) < 0;

    if (full)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Maximum hits reached",
                                                "The target drum has no room for all the hits.");
    notifyHitsChanged();
}

void HitListPanel::showOtherMenu (bool useMousePosition)
{
    const auto slots = getSelectedSlots();
    const bool any = ! slots.empty();
    const bool single = slots.size() == 1;
    // Moving is pointless when the selection already fills the top (or bottom) of the list.
    const int rows = getNumRows();
    const int selectedRows = list.getNumSelectedRows();
    bool atTop = any, atBottom = any;
    for (int i = 0; i < selectedRows; ++i)
    {
        atTop = atTop && list.getSelectedRow (i) < selectedRows;
        atBottom = atBottom && list.getSelectedRow (i) >= rows - selectedRows;
    }

    juce::PopupMenu menu;
    menu.addItem (menuDelete, "Delete", any);
    juce::PopupMenu copyMenu;
    bool hasCopyTargets = false;
    for (int target = 0; target < maxDrums; ++target)
    {
        if (target == drum || controller.getHitCount (target) >= maxHitsPerDrum)
            continue;

        auto name = controller.getDrumName (target);
        if (name.isEmpty())
            name = "Drum " + juce::String (target + 1);
        copyMenu.addItem (menuCopyToBase + target, juce::String (target + 1) + " - " + name);
        hasCopyTargets = true;
    }
    if (! hasCopyTargets)
        copyMenu.addItem (-1, "No drum has room", false);
    menu.addSubMenu ("Copy to...", copyMenu, any && hasCopyTargets);
    menu.addItem (menuJump, "Jump to wave", single && onJumpToWave != nullptr);
    juce::PopupMenu waveMenu;
    std::vector<int> availableWaves;
    for (const int waveId : controller.getWaveIds())
    {
        if (controller.isWaveMissing (waveId))
            continue;
        const auto waveName = controller.getWaveName (waveId);
        const auto fileName = controller.getWaveFile (waveId).getFileName();
        const auto label = waveName == fileName ? waveName : waveName + " (" + fileName + ")";
        const int itemId = 1000 + static_cast<int> (availableWaves.size());
        waveMenu.addItem (itemId, label);
        availableWaves.push_back (waveId);
    }
    if (waveMenu.getNumItems() == 0)
        waveMenu.addItem (-1, "No available waves", false);
    menu.addSubMenu ("Select wave", waveMenu, any);
    menu.addItem (menuDuplicate, "Duplicate", any && controller.getHitCount (drum) < maxHitsPerDrum);
    menu.addItem (menuNormalize, "Normalize", any);
    menu.addItem (menuMoveUp, "Move up", any && ! atTop);
    menu.addItem (menuMoveDown, "Move down", any && ! atBottom);
    menu.addItem (menuVelocitySplit, "Vel split", controller.getHitCount (drum) > 1);

    auto options = juce::PopupMenu::Options().withTargetComponent (otherButton);
    if (useMousePosition)
        options = options.withMousePosition();
    menu.showMenuAsync (options,
                        [safe = juce::Component::SafePointer<HitListPanel> (this), slots, availableWaves] (int result)
                        {
                            if (safe != nullptr && result > 0)
                            {
                                if (result == menuDelete)
                                {
                                    safe->deleteSelectedHits();
                                    return;
                                }
                                if (result >= menuCopyToBase && result < menuCopyToBase + maxDrums)
                                {
                                    safe->copySelectedHitsTo (result - menuCopyToBase, slots);
                                    return;
                                }
                                safe->runOtherAction (result, slots, availableWaves);
                            }
                        });
}

void HitListPanel::selectSlots (const std::vector<int>& slots)
{
    const auto order = controller.getHitSlots (drum);
    list.deselectAllRows();
    for (const int slot : slots)
    {
        const auto it = std::find (order.begin(), order.end(), slot);
        if (it != order.end())
            list.selectRow (static_cast<int> (it - order.begin()), false, false);
    }
}

void HitListPanel::runOtherAction (int action, const std::vector<int>& slots, const std::vector<int>& availableWaves)
{
    if (action >= 1000 && action - 1000 < static_cast<int> (availableWaves.size()))
    {
        const int waveId = availableWaves[static_cast<size_t> (action - 1000)];
        bool failed = false;
        for (const int slot : slots)
            failed |= ! controller.setHitWave (drum, slot, waveId);
        if (failed)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not select wave",
                                                    "One or more selected hits could not be updated.");
        notifyHitsChanged();
        return;
    }

    switch (action)
    {
        case menuJump:
            if (slots.size() == 1 && onJumpToWave)
                onJumpToWave (drum, slots.front());
            return;

        case menuDuplicate:
        {
            std::vector<int> created;
            bool full = false;
            for (const int slot : slots)
            {
                const int newSlot = controller.duplicateHit (drum, slot);
                if (newSlot >= 0)
                    created.push_back (newSlot);
                else
                    full = true;
            }

            refresh();
            selectSlots (created);
            if (full)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Maximum hits reached",
                                                        "This drum has no room for all the duplicates.");
            break;
        }

        case menuNormalize:
            for (const int slot : slots)
                controller.normalizeHit (drum, slot);
            break;

        case menuMoveUp:
        case menuMoveDown:
            controller.moveHits (drum, slots, action == menuMoveUp);
            refresh();
            selectSlots (slots);
            break;

        case menuVelocitySplit:
            controller.splitVelocityRange (drum);
            break;

        default:
            return;
    }

    notifyHitsChanged();
    if (onSelectionChanged)
        onSelectionChanged();
}
} // namespace drumhits::ui
