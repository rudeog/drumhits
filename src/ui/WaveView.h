#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/engine/Wave.h"

namespace drumhits::ui
{
// Shared wave display. Always shows a whole wave, with the height normalized to the wave's
// peak. Zooms from the whole wave down to one pixel per sample, with a horizontal scroll bar.
class WaveView final : public juce::Component,
                       private juce::ScrollBar::Listener
{
public:
    WaveView();

    // The wave must outlive its use here; pass nullptr to clear.
    void setWave (const Wave* newWave);

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    // Selection (drag, in/out/all buttons) is only offered by some usages.
    void setSelectionEnabled (bool enabled);

    // Adds an optional owner-controlled button beside play; the button must outlive the view.
    void setExtraButton (juce::Button* button, int width);

    // Cursor and selection are sample positions. The cursor is cleared when the wave changes.
    std::optional<int64_t> getCursor() const { return cursor; }
    void setCursor (std::optional<int64_t> position);
    std::optional<std::pair<int64_t, int64_t>> getSelection() const;
    void clearSelection();
    void setSelection (int64_t start, int64_t end);
    void selectAll();
    void setIn();
    void setOut();

    // Hit start/end and fades (fade lengths are measured from start and from end). Shown as flagged
    // lines: i (start) and o (end) flags at the bottom, f (fades) flags at the top. Pass nullopt to hide them.
    struct Markers
    {
        int64_t start = 0;
        int64_t end = 0;
        int64_t fadeInLength = 0;
        int64_t fadeOutLength = 0;
    };
    void setMarkers (std::optional<Markers> newMarkers);

    // When editable, the marker lines and flags can be dragged, and the i/o buttons set start/end
    // from the cursor. onMarkersChanged fires when a drag finishes or i/o is used.
    void setMarkersEditable (bool editable);
    std::optional<Markers> getMarkers() const { return markers; }

    // The green play button. The owner decides what to play.
    std::function<void()> onPlay;
    std::function<void()> onCursorChanged;
    std::function<void()> onSelectionChanged;
    std::function<void()> onMarkersChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct MinMax
    {
        float lo = 0.0f, hi = 0.0f;
    };

    static constexpr int blockSize = 64;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void zoomBy (double factor);
    void setSamplesPerPixel (double value, double anchorSample, double anchorFraction);
    void updateScrollBar();
    double getFitSamplesPerPixel() const;
    double getViewWidth() const { return static_cast<double> (std::max (1, waveArea().getWidth())); }
    juce::Rectangle<int> waveArea() const;
    int64_t sampleAt (int x) const;
    float xForSample (int64_t sample) const;
    void selectionChanged();
    enum MarkerBit { startBit = 1, endBit = 2, fadeInBit = 4, fadeOutBit = 8 };
    int markerPosition (int bit) const;
    int hitTestMarkers (juce::Point<int>) const;
    void dragMarkers (int64_t delta);
    void updateButtons();
    void drawFlaggedLine (juce::Graphics&, int64_t sample, juce::Colour, const juce::String& label, bool flagAtTop, bool flagToRight, bool drawLine) const;
    MinMax getRange (int64_t first, int64_t last) const;

    const Wave* wave = nullptr;
    std::vector<MinMax> blocks;
    float peak = 1.0f;
    double samplesPerPixel = 1.0;
    double firstVisibleSample = 0.0;

    std::optional<int64_t> cursor;
    std::optional<int64_t> selStart, selEnd;
    std::optional<Markers> markers;
    bool markersEditable = false;
    int draggedMarkers = 0; // MarkerBit mask, all lines superimposed at the grab point
    int64_t markerDragOrigin = 0;
    Markers markersAtGrab;
    bool markersMoved = false;
    bool ignoringPress = false;
    bool selectionEnabled = false;
    bool dragSelecting = false;
    int64_t dragOrigin = 0;
    juce::Component::SafePointer<juce::Button> extraButton;
    int extraButtonWidth = 0;

    juce::ScrollBar scrollBar { false };
    juce::DrawableButton playButton { "play", juce::DrawableButton::ImageOnButtonBackground };
    juce::TextButton inButton { "i" };
    juce::TextButton outButton { "o" };
    juce::TextButton allButton { "all" };
    juce::TextButton zoomInButton { "+" };
    juce::TextButton zoomOutButton { "-" };
};
} // namespace drumhits::ui
