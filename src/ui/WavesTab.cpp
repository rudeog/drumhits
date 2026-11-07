#include "ui/WavesTab.h"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

namespace drumhits::ui
{
namespace
{
constexpr int drumMenuBase = 100;
constexpr int usageMenuBase = 1000;

class SearchResultsDialog final : public juce::Component,
                                  private juce::ListBoxModel
{
public:
    SearchResultsDialog (std::vector<MissingWaveMatch> found,
                         std::function<void (const std::vector<std::pair<int, juce::File>>&)> onResolve)
        : matches (std::move (found)), resolve (std::move (onResolve))
    {
        list.setModel (this);
        list.setRowHeight (52);
        addAndMakeVisible (list);

        confirmButton.setButtonText ("Resolve Selected");
        confirmButton.onClick = [this] { resolveSelected(); };
        addAndMakeVisible (confirmButton);

        cancelButton.setButtonText ("Cancel");
        cancelButton.onClick = [this] { close (0); };
        addAndMakeVisible (cancelButton);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto buttons = area.removeFromBottom (28);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (6);
        confirmButton.setBounds (buttons.removeFromRight (140));
        area.removeFromBottom (6);
        list.setBounds (area);
    }

private:
    int getNumRows() override { return static_cast<int> (matches.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (matches.size()))
            return;

        if (selected)
            g.fillAll (juce::Colour (0xff2f6fa8));

        const auto& match = matches[static_cast<size_t> (row)];
        const auto box = juce::Rectangle<float> (8.0f, static_cast<float> (height / 2 - 7), 14.0f, 14.0f);
        g.setColour (match.file != juce::File() ? juce::Colours::white : juce::Colours::grey);
        g.drawRect (box, 1.0f);
        if (match.file != juce::File() && checked[static_cast<size_t> (row)])
        {
            g.drawLine (box.getX() + 3.0f, box.getCentreY(), box.getX() + 6.0f, box.getBottom() - 3.0f, 2.0f);
            g.drawLine (box.getX() + 6.0f, box.getBottom() - 3.0f, box.getRight() - 2.0f, box.getY() + 3.0f, 2.0f);
        }

        g.setColour (match.file != juce::File() ? juce::Colours::white : juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        const auto waveLabel = match.filename.isNotEmpty() && match.filename != match.waveName
                                   ? match.waveName + " (" + match.filename + ")"
                                   : match.waveName;
        g.drawText (waveLabel, 32, 3, width - 40, 20, juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (match.file != juce::File() ? match.file.getFullPathName() : "No matching WAV file found",
                    32, 24, width - 40, 24, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < static_cast<int> (matches.size())
            && matches[static_cast<size_t> (row)].file != juce::File())
        {
            checked[static_cast<size_t> (row)] = ! checked[static_cast<size_t> (row)];
            list.repaintRow (row);
        }
    }

    void resolveSelected()
    {
        std::vector<std::pair<int, juce::File>> selected;
        for (size_t i = 0; i < matches.size(); ++i)
            if (checked[i] && matches[i].file != juce::File())
                selected.emplace_back (matches[i].waveId, matches[i].file);

        if (resolve)
            resolve (selected);
        close (1);
    }

    void close (int result)
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState (result);
    }

    std::vector<MissingWaveMatch> matches;
    std::vector<bool> checked = std::vector<bool> (matches.size(), true);
    std::function<void (const std::vector<std::pair<int, juce::File>>&)> resolve;
    juce::ListBox list;
    juce::TextButton confirmButton, cancelButton;
};

class FunctionModalCallback final : public juce::ModalComponentManager::Callback
{
public:
    explicit FunctionModalCallback (std::function<void (int)> functionToCall)
        : function (std::move (functionToCall))
    {
    }

    void modalStateFinished (int result) override
    {
        if (function)
            function (result);
    }

private:
    std::function<void (int)> function;
};
} // namespace

WavesTab::WavesTab (Controller& controllerToUse)
    : controller (controllerToUse)
{
    auto& header = table.getHeader();
    header.addColumn ("Name", nameColumn, 200);
    header.addColumn ("Path", pathColumn, 450);
    header.addColumn ("Uses", usageColumn, 60);
    header.addColumn ("Status", statusColumn, 100);
    table.setRowHeight (24);
    table.setMultipleSelectionEnabled (true);
    addAndMakeVisible (table);
    waveView.setSelectionEnabled (true);
    waveView.onPlay = [this] { playSelection(); };
    addAndMakeVisible (waveView);

    addButton.onClick = [this] { chooseFilesToLoad(); };
    actionsButton.onClick = [this] { showActionsMenu (false); };
    searchMissingButton.onClick = [this] { searchMissing(); };
    addAndMakeVisible (addButton);
    addAndMakeVisible (actionsButton);
    addAndMakeVisible (searchMissingButton);

    toDrumButton.onClick = [this] { showDrumMenu(); };
    waveView.setExtraButton (&toDrumButton, 82);

    refresh();
}

void WavesTab::refresh()
{
    const auto selectedIds = getSelectedWaveIds();
    const juce::ScopedValueSetter<bool> guard (updatingSelection, true);
    waveIds = controller.getWaveIds();
    sortWaves();
    table.updateContent();
    juce::SparseSet<int> selectedRows;
    for (size_t row = 0; row < waveIds.size(); ++row)
        if (std::find (selectedIds.begin(), selectedIds.end(), waveIds[row]) != selectedIds.end())
            selectedRows.addRange (juce::Range<int>::withStartAndLength (static_cast<int> (row), 1));
    table.setSelectedRows (selectedRows, juce::dontSendNotification);
    table.repaint();
    updateWaveView();
    updateActionButtons();
}

void WavesTab::sortWaves()
{
    if (sortColumn == 0)
        return;

    std::vector<WavePoolSortEntry> entries;
    entries.reserve (waveIds.size());
    for (const int id : waveIds)
        entries.push_back ({ id, controller.getWaveName (id), controller.getWaveFile (id).getFullPathName(),
                             controller.getWaveUsageCount (id), controller.isWaveMissing (id) });
    sortWavePoolEntries (entries, static_cast<WavePoolSortColumn> (sortColumn), sortForwards);
    for (size_t i = 0; i < entries.size(); ++i)
        waveIds[i] = entries[i].id;
}

void WavesTab::sortOrderChanged (int column, bool forwards)
{
    sortColumn = column;
    sortForwards = forwards;
    refresh();
}

void WavesTab::showWave (int waveId, int64_t start, int64_t end)
{
    refresh();
    const auto it = std::find (waveIds.begin(), waveIds.end(), waveId);
    if (it == waveIds.end())
        return;

    table.selectRow (static_cast<int> (it - waveIds.begin()));
    updateWaveView();
    waveView.setSelection (start, end);
}

void WavesTab::selectedRowsChanged (int)
{
    if (updatingSelection)
        return;
    updateWaveView();
    updateActionButtons();
}

void WavesTab::updateWaveView()
{
    const auto selection = table.getSelectedRows();
    const bool single = selection.size() == 1;
    const int row = single ? selection[0] : -1;
    const Wave* wave = nullptr;
    int waveId = -1;

    if (row >= 0 && row < static_cast<int> (waveIds.size()))
    {
        waveId = waveIds[static_cast<size_t> (row)];
        wave = controller.getWave (waveId);
    }

    if (wave != displayedWave || waveId != displayedWaveId)
    {
        waveView.setWave (wave);
        displayedWave = wave;
        displayedWaveId = waveId;
    }
    const bool available = single && wave != nullptr;
    waveView.setEnabled (available);
    toDrumButton.setEnabled (available);
    waveView.setAlpha (single || selection.isEmpty() ? 1.0f : 0.35f);
    updateActionButtons();
}

void WavesTab::updateActionButtons()
{
    searchMissingButton.setEnabled (controller.getMissingWaveCount() > 0 && ! searchingMissing);
}

void WavesTab::resized()
{
    auto area = getLocalBounds().reduced (8);
    waveView.setBounds (area.removeFromBottom (juce::jmax (120, area.getHeight() / 3)));
    area.removeFromBottom (6);
    auto actions = area.removeFromBottom (28);
    actionsButton.setBounds (actions.removeFromLeft (40));
    actions.removeFromLeft (6);
    addButton.setBounds (actions.removeFromLeft (120));
    actions.removeFromLeft (6);
    searchMissingButton.setBounds (actions.removeFromLeft (150));
    area.removeFromBottom (6);
    table.setBounds (area);
}

int WavesTab::getSelectedWaveId() const
{
    const auto selection = table.getSelectedRows();
    if (selection.size() != 1)
        return -1;
    const int row = selection[0];
    return row >= 0 && row < static_cast<int> (waveIds.size()) ? waveIds[static_cast<size_t> (row)] : -1;
}

bool WavesTab::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty();
}

void WavesTab::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> droppedFiles;
    for (const auto& path : files)
        droppedFiles.add (juce::File (path));
    loadFiles (droppedFiles);
}

std::vector<int> WavesTab::getSelectedWaveIds() const
{
    std::vector<int> result;
    const auto selection = table.getSelectedRows();
    for (int i = 0; i < selection.size(); ++i)
        if (selection[i] >= 0 && selection[i] < static_cast<int> (waveIds.size()))
            result.push_back (waveIds[static_cast<size_t> (selection[i])]);
    return result;
}

void WavesTab::cellClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu() || row < 0 || row >= getNumRows())
        return;

    if (! table.isRowSelected (row))
        table.selectRow (row);
    showActionsMenu (true);
}

void WavesTab::cellDoubleClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown() || row < 0 || row >= getNumRows())
        return;

    table.selectRow (row);
    playSelection();
}

void WavesTab::showActionsMenu (bool atMousePosition)
{
    const auto selected = getSelectedWaveIds();
    std::vector<std::pair<int, int>> destinations;
    auto menu = makeActionsMenu (selected, destinations);

    auto options = juce::PopupMenu::Options().withTargetComponent (actionsButton);
    if (atMousePosition)
        options = options.withMousePosition();
    menu.showMenuAsync (options, [safe = juce::Component::SafePointer<WavesTab> (this), selected, destinations] (int result)
    {
        if (safe == nullptr)
            return;
        if (result >= drumMenuBase && result < drumMenuBase + maxDrums)
        {
            const auto outcome = safe->addWavesToDrum (result - drumMenuBase, selected);
            if (outcome.failed())
                safe->showMessage ("Could not add waves", outcome.getErrorMessage());
            else
                safe->refresh();
        }
        else if (result >= usageMenuBase
                 && static_cast<size_t> (result - usageMenuBase) < destinations.size())
        {
            const auto [drum, slot] = destinations[static_cast<size_t> (result - usageMenuBase)];
            if (safe->onJumpToHit && safe->controller.getHitWaveId (drum, slot) == selected.front())
                safe->onJumpToHit (drum, slot);
        }
        else
        {
            switch (result)
            {
                case 1: safe->deleteSelectedWaves(); break;
                case 2: safe->renameSelectedWave(); break;
                case 3: safe->chooseFileToReplace(); break;
                default: break;
            }
        }
    });
}

juce::PopupMenu WavesTab::makeActionsMenu (const std::vector<int>& selected,
                                           std::vector<std::pair<int, int>>& destinations) const
{
    juce::PopupMenu menu;
    menu.addItem (1, "Delete", ! selected.empty());
    menu.addItem (2, "Rename", selected.size() == 1);
    menu.addItem (3, "Choose New File...", selected.size() == 1);

    const bool canAdd = ! selected.empty()
                        && std::all_of (selected.begin(), selected.end(), [this] (int id)
                                        { return controller.getWave (id) != nullptr; });
    juce::PopupMenu drums;
    for (int d = 0; d < maxDrums; ++d)
    {
        const auto name = controller.getDrumName (d);
        const auto available = maxHitsPerDrum - controller.getHitCount (d);
        drums.addItem (drumMenuBase + d, juce::String (d + 1) + (name.isNotEmpty() ? " - " + name : juce::String()),
                       canAdd && selected.size() <= static_cast<size_t> (available));
    }
    menu.addSubMenu ("To drum", drums, canAdd);

    juce::PopupMenu usage;
    if (selected.size() == 1)
        for (int drum = 0; drum < maxDrums; ++drum)
        {
            juce::PopupMenu hits;
            const auto name = controller.getDrumName (drum);
            for (const int slot : controller.getHitSlots (drum))
            {
                if (controller.getHitWaveId (drum, slot) != selected.front())
                    continue;
                const int itemId = usageMenuBase + static_cast<int> (destinations.size());
                destinations.emplace_back (drum, slot);
                hits.addItem (itemId, controller.getHitName (drum, slot));
            }
            if (hits.getNumItems() > 0)
                usage.addSubMenu (juce::String (drum + 1) + (name.isNotEmpty() ? " - " + name : juce::String()), hits);
        }
    if (destinations.empty())
        usage.addItem (-1, "No hits use this wave", false);
    menu.addSubMenu ("Usage", usage, selected.size() == 1);
    return menu;
}

juce::Result WavesTab::addWavesToDrum (int drum, const std::vector<int>& selected)
{
    if (drum < 0 || drum >= maxDrums || selected.empty())
        return juce::Result::fail ("The selected drum or waves are no longer available.");
    if (std::any_of (selected.begin(), selected.end(), [this] (int id) { return controller.getWave (id) == nullptr; }))
        return juce::Result::fail ("One or more selected waves are missing or no longer available.");
    if (selected.size() > static_cast<size_t> (maxHitsPerDrum - controller.getHitCount (drum)))
        return juce::Result::fail ("The drum does not have enough room for all selected waves.");

    std::vector<int> added;
    for (const int id : selected)
    {
        const int slot = controller.addHit (drum, id);
        if (slot < 0)
        {
            for (const int addedSlot : added)
                controller.removeHit (drum, addedSlot);
            return juce::Result::fail ("A selected wave could not be added; no hits were added.");
        }
        added.push_back (slot);
    }
    return juce::Result::ok();
}

void WavesTab::showDrumMenu()
{
    if (getSelectedWaveId() < 0)
        return;

    juce::PopupMenu menu;
    for (int d = 0; d < maxDrums; ++d)
    {
        const auto name = controller.getDrumName (d);
        const bool full = controller.getHitCount (d) >= maxHitsPerDrum;
        menu.addItem (d + 1, juce::String (d + 1) + (name.isNotEmpty() ? " - " + name : juce::String()), ! full);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toDrumButton),
                        [safe = juce::Component::SafePointer<WavesTab> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->addSelectionToDrum (result - 1);
                        });
}

std::pair<int64_t, int64_t> WavesTab::getSelectedPortion (int waveId) const
{
    if (const auto sel = waveView.getSelection(); sel.has_value() && sel->second > sel->first)
        return *sel;
    return { 0, controller.getWaveLength (waveId) };
}

void WavesTab::playSelection()
{
    const int id = getSelectedWaveId();
    if (id < 0)
        return;

    const auto [start, end] = getSelectedPortion (id);
    controller.previewWave (id, start, end);
}

void WavesTab::addSelectionToDrum (int drum)
{
    const int id = getSelectedWaveId();
    if (id < 0)
        return;

    NewHit hit;
    hit.endPos = -1;
    if (const auto sel = waveView.getSelection(); sel.has_value() && sel->second > sel->first)
    {
        hit.startPos = sel->first;
        hit.endPos = sel->second;
    }

    if (controller.addHit (drum, id, hit) < 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not add hit",
                                                "The drum has no room for another hit.");
    else
        refresh();
}

int WavesTab::getNumRows()
{
    return static_cast<int> (waveIds.size());
}

void WavesTab::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (0xff2f6fa8));
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));
}

void WavesTab::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (waveIds.size()))
        return;

    const int id = waveIds[static_cast<size_t> (row)];
    juce::String text;
    switch (column)
    {
        case nameColumn: text = controller.getWaveName (id); break;
        case pathColumn: text = controller.getWaveFile (id).getFullPathName(); break;
        case usageColumn: text = juce::String (controller.getWaveUsageCount (id)); break;
        case statusColumn: text = controller.isWaveMissing (id) ? "Missing file" : juce::String(); break;
        default: break;
    }

    g.setColour (column == statusColumn && controller.isWaveMissing (id) ? juce::Colours::orange
                                                                         : juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void WavesTab::chooseFilesToLoad()
{
    chooser = std::make_unique<juce::FileChooser> ("Load wave files", juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<WavesTab> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr)
                                  safe->loadFiles (fc.getResults());
                          });
}

void WavesTab::loadFiles (const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
        loadFile (file);
    refresh();
}

void WavesTab::loadFile (const juce::File& file)
{
    const auto result = controller.loadWave (file);
    if (result.status == LoadWaveResult::Status::loaded)
        return;

    juce::AlertWindow::showMessageBoxAsync (result.status == LoadWaveResult::Status::alreadyLoaded ? juce::MessageBoxIconType::InfoIcon
                                                                                                     : juce::MessageBoxIconType::WarningIcon,
                                            result.status == LoadWaveResult::Status::alreadyLoaded ? "Wave already loaded" : "Could not load wave",
                                            result.error);
}

void WavesTab::deleteSelectedWaves()
{
    const auto ids = getSelectedWaveIds();
    if (ids.empty())
        return;

    int totalUses = 0;
    for (const int id : ids)
        totalUses += controller.getWaveUsageCount (id);

    if (totalUses == 0)
    {
        deleteWaves (ids);
        return;
    }

    const auto message = ids.size() == 1
                             ? "\"" + controller.getWaveName (ids.front()) + "\" is used by "
                                   + juce::String (totalUses) + " hit(s). Deleting it will also delete those hits."
                             : juce::String (static_cast<int> (ids.size())) + " selected waves are used by "
                                   + juce::String (totalUses) + " hit(s). Deleting them will also delete those hits.";
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle (ids.size() == 1 ? "Delete wave?" : "Delete waves?")
            .withMessage (message)
            .withButton ("Delete")
            .withButton ("Cancel"),
        [safe = juce::Component::SafePointer<WavesTab> (this), ids] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->deleteWaves (ids);
        });
}

void WavesTab::deleteWaves (const std::vector<int>& ids)
{
    const int oldRow = std::max (0, table.getSelectedRow (0));
    int failures = 0;
    for (const int id : ids)
        if (! controller.removeWave (id))
            ++failures;
    table.deselectAllRows();
    refresh();
    if (! waveIds.empty())
        table.selectRow (std::clamp (oldRow, 0, static_cast<int> (waveIds.size()) - 1));
    if (failures > 0)
        showMessage ("Could not delete waves", juce::String (failures) + " selected wave(s) no longer exist.");
}

void WavesTab::renameSelectedWave()
{
    const int id = getSelectedWaveId();
    if (id < 0)
        return;

    auto alert = std::make_unique<juce::AlertWindow> ("Rename wave", "Change the wave's display name.",
                                                     juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", controller.getWaveName (id), "Name:");
    if (auto* editor = alert->getTextEditor ("name"))
    {
        editor->selectAll();
        editor->grabKeyboardFocus();
    }
    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* alertPointer = alert.get();
    const juce::Component::SafePointer<juce::AlertWindow> safeAlert (alertPointer);
    alert->enterModalState (
        true,
        new FunctionModalCallback ([safe = juce::Component::SafePointer<WavesTab> (this), safeAlert, id] (int result)
                                   {
                                       if (safe == nullptr || safeAlert == nullptr || result != 1)
                                           return;
                                       safe->controller.renameWave (id, safeAlert->getTextEditorContents ("name").trim());
                                       safe->refresh();
                                   }),
        true);
    alert.release();

    juce::MessageManager::callAsync ([safeAlert]
                                    {
                                        if (safeAlert == nullptr)
                                            return;
                                        if (auto* editor = safeAlert->getTextEditor ("name"))
                                        {
                                            editor->grabKeyboardFocus();
                                            editor->selectAll();
                                        }
                                    });
}

void WavesTab::chooseFileToReplace()
{
    const int id = getSelectedWaveId();
    if (id >= 0)
        chooseNewFileForWave (id);
}

void WavesTab::chooseNewFileForWave (int waveId)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose new wave file",
                                                   controller.getWaveFile (waveId).getParentDirectory(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<WavesTab> (this), waveId] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              const auto result = safe->controller.replaceWaveFile (waveId, file);
                              if (result.failed())
                                  safe->showMessage ("Could not replace wave", result.getErrorMessage());
                              safe->refresh();
                          });
}

void WavesTab::searchMissing()
{
    if (searchingMissing)
        return;

    if (controller.getMissingWaveCount() == 0)
    {
        showMessage ("Search Missing", "There are no missing waves to search for.");
        return;
    }

    chooseSearchDirectory();
}

void WavesTab::chooseSearchDirectory()
{
    chooser = std::make_unique<juce::FileChooser> ("Select directory to search for missing WAV files",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<WavesTab> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto directory = fc.getResult();
                              if (directory != juce::File())
                                  safe->searchMissingInDirectory (directory);
                          });
}

void WavesTab::searchMissingInDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
    {
        showMessage ("Search Missing", "The selected location is not a directory.");
        return;
    }

    std::vector<MissingWaveMatch> targets;
    for (const int waveId : controller.getWaveIds())
        if (controller.isWaveMissing (waveId))
            targets.push_back ({ waveId, controller.getWaveName (waveId),
                                 controller.getWaveFile (waveId).getFileName(), {} });

    if (targets.empty())
        return;

    searchingMissing = true;
    updateActionButtons();
    searchPool.addJob ([safe = juce::Component::SafePointer<WavesTab> (this), directory,
                        searchTargets = std::move (targets)] () mutable
                       {
                           std::map<juce::String, juce::File> found;
                           for (const auto& target : searchTargets)
                               if (target.filename.isNotEmpty())
                                   found.emplace (target.filename.toLowerCase(), juce::File());

                           for (const auto& entry : juce::RangedDirectoryIterator (
                                    directory, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no))
                           {
                               const auto file = entry.getFile();
                               const auto it = found.find (file.getFileName().toLowerCase());
                               if (it != found.end() && it->second == juce::File())
                                   it->second = file;
                           }

                           std::vector<MissingWaveMatch> matches;
                           matches.reserve (searchTargets.size());
                           for (auto target : searchTargets)
                           {
                               const auto it = found.find (target.filename.toLowerCase());
                               if (it != found.end())
                                   target.file = it->second;
                               matches.push_back (std::move (target));
                           }

                           juce::MessageManager::callAsync ([safe, searchResults = std::move (matches)] () mutable
                                                           {
                                                               if (safe == nullptr)
                                                                   return;
                                                               safe->searchingMissing = false;
                                                               safe->updateActionButtons();
                                                               safe->showSearchResults (searchResults);
                                                           });
                       });
}

void WavesTab::showSearchResults (const std::vector<MissingWaveMatch>& matches)
{
    if (std::none_of (matches.begin(), matches.end(), [] (const auto& match) { return match.file != juce::File(); }))
    {
        showMessage ("Search Missing", "No filename matches were found.");
        return;
    }

    auto content = std::make_unique<SearchResultsDialog> (
        matches, [safe = juce::Component::SafePointer<WavesTab> (this)]
                 (const std::vector<std::pair<int, juce::File>>& selected)
                 {
                     if (safe == nullptr)
                         return;

                     juce::StringArray errors;
                     for (const auto& [waveId, file] : selected)
                     {
                         const auto result = safe->controller.replaceWaveFile (waveId, file);
                         if (result.failed())
                             errors.add (safe->controller.getWaveName (waveId) + ": " + result.getErrorMessage());
                     }

                     safe->refresh();
                     if (! errors.isEmpty())
                         safe->showMessage ("Some waves could not be resolved", errors.joinIntoString ("\n"));
                 });
    content->setSize (640, 420);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Search results for missing waves";
    options.dialogBackgroundColour = findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.content.setOwned (content.release());
    options.launchAsync();
}

void WavesTab::showMessage (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK"),
                                  nullptr);
}
} // namespace drumhits::ui
