#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"
#include "ui/WavePoolSort.h"
#include "ui/WaveView.h"

namespace drumhits::ui
{
struct MissingWaveMatch
{
    int waveId = -1;
    juce::String waveName;
    juce::String filename;
    juce::File file;
};

// The wave pool: list of loaded waves, a panel of wave actions, and a view of the selected wave.
class WavesTab final : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::TableListBoxModel
{
public:
    explicit WavesTab (Controller& controllerToUse);

    void refresh();

    // Selects the wave in the list and selects the given portion in the wave view.
    void showWave (int waveId, int64_t start, int64_t end);
    void searchMissing();
    std::function<void (int drum, int slot)> onJumpToHit;

    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    friend struct WavesTabTestAccess;
    enum Column
    {
        nameColumn = static_cast<int> (WavePoolSortColumn::name),
        pathColumn = static_cast<int> (WavePoolSortColumn::path),
        usageColumn = static_cast<int> (WavePoolSortColumn::usage),
        statusColumn = static_cast<int> (WavePoolSortColumn::status)
    };

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int column, bool forwards) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void sortWaves();
    void showActionsMenu (bool atMousePosition);
    juce::PopupMenu makeActionsMenu (const std::vector<int>& selected,
                                     std::vector<std::pair<int, int>>& destinations) const;
    juce::Result addWavesToDrum (int drum, const std::vector<int>& selected);
    void updateWaveView();
    void showDrumMenu();
    void addSelectionToDrum (int drum);
    void playSelection();
    std::pair<int64_t, int64_t> getSelectedPortion (int waveId) const;
    int getSelectedWaveId() const;
    std::vector<int> getSelectedWaveIds() const;
    void updateActionButtons();
    void chooseFilesToLoad();
    void chooseFileToReplace();
    void loadFiles (const juce::Array<juce::File>& files);
    void loadFile (const juce::File& file);
    void deleteSelectedWaves();
    void deleteWaves (const std::vector<int>& ids);
    void renameSelectedWave();
    void chooseNewFileForWave (int waveId);
    void chooseSearchDirectory();
    void searchMissingInDirectory (const juce::File& directory);
    void showSearchResults (const std::vector<MissingWaveMatch>& matches);
    bool searchingMissing = false;
    void showMessage (const juce::String& title, const juce::String& message);

    Controller& controller;
    std::vector<int> waveIds;
    int sortColumn = 0;
    bool sortForwards = true;
    bool updatingSelection = false;
    juce::TableListBox table { {}, this };
    juce::TextButton toDrumButton { "To drum..." };
    WaveView waveView;
    const Wave* displayedWave = nullptr;
    int displayedWaveId = -1;
    juce::TextButton actionsButton { "..." };
    juce::TextButton addButton { "Add..." };
    juce::TextButton searchMissingButton { "Search Missing..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool searchPool { 1 };
};
} // namespace drumhits::ui
