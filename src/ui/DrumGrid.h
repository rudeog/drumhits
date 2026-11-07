#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/controller/Controller.h"

namespace drumhits::ui
{
// The 8x2 grid of drum pads. Fill shows whether a drum has hits, and the border shows the active drum.
class DrumGrid final : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit DrumGrid (Controller& controllerToUse);
    ~DrumGrid() override;

    std::function<void (int)> onDrumSelected;
    std::function<void (int, juce::Point<int>)> onDrumContextMenu;
    std::function<void (int, const juce::StringArray&)> onFilesDropped;
    std::function<void (int sourceDrum, const std::vector<int>& hitSlots, int targetDrum)> onHitsDropped;

    void setActiveDrum (int drum);
    int getActiveDrum() const noexcept { return activeDrum; }
    int getAuditionVelocity() const noexcept { return auditionVelocity; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    void timerCallback() override;

    static constexpr int columns = 8;
    static constexpr int rows = 2;

    juce::Rectangle<int> cellBounds (int drum) const;
    int cellAt (juce::Point<int> position) const;
    void setDropTarget (int drum);

    Controller& controller;
    int activeDrum = 0;
    int auditionVelocity = 100;
    int dropTarget = -1;
    std::array<float, maxDrums> flash {}; // 1 when a note was just received, fading to 0
};
} // namespace drumhits::ui
