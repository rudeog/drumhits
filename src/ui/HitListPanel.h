#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"

namespace drumhits::ui
{
// The hits of the active drum, with Add... and ... actions.
class HitListPanel final : public juce::Component,
                           private juce::ListBoxModel
{
public:
    explicit HitListPanel (Controller& controllerToUse);

    std::function<void()> onHitsChanged;
    std::function<void()> onSelectionChanged;
    std::function<void()> onPreview;
    std::function<void (int drum, int slot)> onJumpToWave;

    void setDrum (int newDrum);
    void refresh();
    void addFiles (const juce::Array<juce::File>& files);

    // Slot of the selected hit, or -1 unless exactly one hit is selected.
    int getSelectedSlot() const;
    void selectHitSlot (int slot);

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& event) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe) override;

    void selectedRowsChanged (int lastRowSelected) override;
    void chooseFilesToAdd();
    void showAddMenu();
    void addWave (int waveId);
    void deleteSelectedHits();
    void copySelectedHitsTo (int targetDrum, const std::vector<int>& slots);
    enum MenuItem
    {
        menuJump = 1,
        menuDuplicate,
        menuNormalize,
        menuMoveUp,
        menuMoveDown,
        menuVelocitySplit,
        menuDelete,
        menuCopyToBase = 2000
    };

    void showOtherMenu (bool useMousePosition = false);
    void runOtherAction (int action, const std::vector<int>& slots, const std::vector<int>& availableWaves);
    void selectSlots (const std::vector<int>& slots);
    std::vector<int> getSelectedSlots() const;
    void notifyHitsChanged();

    Controller& controller;
    int drum = 0;
    juce::ListBox list { {}, this };
    juce::TextButton addButton { "Add..." };
    juce::TextButton otherButton { "..." };
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace drumhits::ui
