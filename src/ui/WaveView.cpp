#include "ui/WaveView.h"

#include <algorithm>
#include <cmath>

namespace drumhits::ui
{
namespace
{
constexpr int barHeight = 16;
constexpr int buttonWidth = 24;
constexpr int flagSize = 12;
} // namespace

WaveView::WaveView()
{
    scrollBar.addListener (this);
    addAndMakeVisible (scrollBar);

    zoomInButton.onClick = [this] { zoomIn(); };
    zoomOutButton.onClick = [this] { zoomOut(); };
    inButton.onClick = [this] { setIn(); };
    outButton.onClick = [this] { setOut(); };
    allButton.onClick = [this] { selectAll(); };
    for (auto* b : { &inButton, &outButton, &allButton })
        addChildComponent (b);

    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);

    juce::Path arrow;
    arrow.addTriangle (0.0f, 0.0f, 0.0f, 1.0f, 0.9f, 0.5f);
    juce::DrawablePath normal, over;
    normal.setPath (arrow);
    normal.setFill (juce::Colour (0xff2fbf4f));
    over.setPath (arrow);
    over.setFill (juce::Colour (0xff5fe07f));
    playButton.setImages (&normal, &over);
    playButton.setEdgeIndent (4);
    playButton.setTooltip ("Play");
    playButton.onClick = [this] { if (onPlay) onPlay(); };
    addAndMakeVisible (playButton);
}

void WaveView::setWave (const Wave* newWave)
{
    wave = newWave;
    playButton.setEnabled (wave != nullptr);
    zoomInButton.setEnabled (wave != nullptr);
    zoomOutButton.setEnabled (wave != nullptr);
    scrollBar.setEnabled (wave != nullptr && wave->numSamples() > getViewWidth());
    inButton.setEnabled (wave != nullptr);
    outButton.setEnabled (wave != nullptr);
    allButton.setEnabled (wave != nullptr);
    cursor.reset();
    selStart.reset();
    selEnd.reset();
    blocks.clear();
    peak = 1.0f;

    if (wave != nullptr && wave->numSamples() > 0)
    {
        const auto n = wave->numSamples();
        const float* l = wave->leftData();
        const float* r = wave->rightData();
        blocks.reserve (static_cast<size_t> (n / blockSize + 1));
        float p = 0.0f;

        for (int64_t start = 0; start < n; start += blockSize)
        {
            const auto end = std::min (n, start + blockSize);
            MinMax mm { l[start], l[start] };
            for (auto i = start; i < end; ++i)
            {
                mm.lo = std::min ({ mm.lo, l[i], r[i] });
                mm.hi = std::max ({ mm.hi, l[i], r[i] });
            }
            p = std::max ({ p, std::abs (mm.lo), std::abs (mm.hi) });
            blocks.push_back (mm);
        }
        if (p > 0.0f)
            peak = p;
    }

    zoomToFit();
}

void WaveView::setExtraButton (juce::Button* button, int width)
{
    if (extraButton != nullptr)
        removeChildComponent (extraButton.getComponent());
    extraButton = button;
    extraButtonWidth = std::max (0, width);
    if (extraButton != nullptr)
        addAndMakeVisible (extraButton.getComponent());
    resized();
}

juce::Rectangle<int> WaveView::waveArea() const
{
    return getLocalBounds().withTrimmedBottom (barHeight + 2);
}

double WaveView::getFitSamplesPerPixel() const
{
    if (wave == nullptr || wave->numSamples() <= 0)
        return 1.0;
    return std::max (1.0, static_cast<double> (wave->numSamples()) / getViewWidth());
}

void WaveView::zoomToFit()
{
    samplesPerPixel = getFitSamplesPerPixel();
    firstVisibleSample = 0.0;
    updateScrollBar();
    repaint();
}

void WaveView::setSamplesPerPixel (double value, double anchorSample, double anchorFraction)
{
    samplesPerPixel = std::clamp (value, 1.0, getFitSamplesPerPixel());
    firstVisibleSample = anchorSample - anchorFraction * getViewWidth() * samplesPerPixel;
    updateScrollBar();
    repaint();
}

void WaveView::zoomBy (double factor)
{
    // Keep the cursor where it is on screen; fall back to the centre when it is off screen.
    double anchor = firstVisibleSample + 0.5 * getViewWidth() * samplesPerPixel;
    double fraction = 0.5;

    if (cursor.has_value())
    {
        const double f = (static_cast<double> (*cursor) - firstVisibleSample) / (getViewWidth() * samplesPerPixel);
        if (f >= 0.0 && f <= 1.0)
        {
            anchor = static_cast<double> (*cursor);
            fraction = f;
        }
    }
    setSamplesPerPixel (samplesPerPixel * factor, anchor, fraction);
}

void WaveView::zoomIn()
{
    zoomBy (0.5);
}

void WaveView::zoomOut()
{
    zoomBy (2.0);
}

void WaveView::setSelectionEnabled (bool enabled)
{
    selectionEnabled = enabled;
    if (! enabled)
        clearSelection();
    updateButtons();
}

void WaveView::setMarkersEditable (bool editable)
{
    markersEditable = editable;
    updateButtons();
}

void WaveView::updateButtons()
{
    inButton.setVisible (selectionEnabled || markersEditable);
    outButton.setVisible (selectionEnabled || markersEditable);
    allButton.setVisible (selectionEnabled);
    const bool hasWave = wave != nullptr;
    playButton.setEnabled (hasWave);
    zoomInButton.setEnabled (hasWave);
    zoomOutButton.setEnabled (hasWave);
    inButton.setEnabled (hasWave);
    outButton.setEnabled (hasWave);
    allButton.setEnabled (hasWave);
    resized();
}

void WaveView::setMarkers (std::optional<Markers> newMarkers)
{
    markers = newMarkers;
    repaint();
}

void WaveView::setCursor (std::optional<int64_t> position)
{
    if (position.has_value() && wave != nullptr)
        position = std::clamp<int64_t> (*position, 0, std::max<int64_t> (0, wave->numSamples() - 1));
    if (position == cursor)
        return;
    cursor = position;
    repaint();
    if (onCursorChanged)
        onCursorChanged();
}

std::optional<std::pair<int64_t, int64_t>> WaveView::getSelection() const
{
    if (selStart.has_value() && selEnd.has_value())
        return std::make_pair (std::min (*selStart, *selEnd), std::max (*selStart, *selEnd));
    return std::nullopt;
}

void WaveView::selectionChanged()
{
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void WaveView::clearSelection()
{
    if (! selStart.has_value() && ! selEnd.has_value())
        return;
    selStart.reset();
    selEnd.reset();
    selectionChanged();
}

void WaveView::setSelection (int64_t start, int64_t end)
{
    if (! selectionEnabled || wave == nullptr)
        return;
    selStart = std::clamp<int64_t> (std::min (start, end), 0, wave->numSamples());
    selEnd = std::clamp<int64_t> (std::max (start, end), 0, wave->numSamples());
    selectionChanged();
}

void WaveView::selectAll()
{
    if (! selectionEnabled || wave == nullptr || wave->numSamples() <= 0)
        return;
    selStart = 0;
    selEnd = wave->numSamples();
    selectionChanged();
}

void WaveView::setIn()
{
    if (markersEditable && markers.has_value() && cursor.has_value())
    {
        markers->start = std::min (*cursor, markers->end - 1);
        markers->fadeInLength = std::min (markers->fadeInLength, markers->end - markers->start);
        repaint();
        if (onMarkersChanged)
            onMarkersChanged();
        return;
    }
    if (! selectionEnabled || ! cursor.has_value())
        return;
    selStart = *cursor;
    if (! selEnd.has_value() || *selStart > *selEnd)
        selEnd = wave != nullptr ? wave->numSamples() : *cursor; // no end given: end of the wave
    selectionChanged();
}

void WaveView::setOut()
{
    if (markersEditable && markers.has_value() && cursor.has_value())
    {
        markers->end = std::max (*cursor, markers->start + 1);
        markers->fadeOutLength = std::min (markers->fadeOutLength, markers->end - markers->start);
        repaint();
        if (onMarkersChanged)
            onMarkersChanged();
        return;
    }
    if (! selectionEnabled || ! cursor.has_value())
        return;
    selEnd = *cursor;
    if (! selStart.has_value() || *selStart > *selEnd)
        selStart = 0; // no start given: start of the wave
    selectionChanged();
}

int64_t WaveView::sampleAt (int x) const
{
    const auto n = wave != nullptr ? wave->numSamples() : 0;
    const auto s = static_cast<int64_t> (std::floor (firstVisibleSample + (x - waveArea().getX()) * samplesPerPixel));
    return std::clamp<int64_t> (s, 0, std::max<int64_t> (0, n - 1));
}

float WaveView::xForSample (int64_t sample) const
{
    return static_cast<float> (waveArea().getX() + (static_cast<double> (sample) - firstVisibleSample) / samplesPerPixel);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    dragSelecting = false;
    draggedMarkers = 0;
    markersMoved = false;
    ignoringPress = false;
    if (wave == nullptr || wave->numSamples() <= 0 || e.mods.isPopupMenu() || ! waveArea().contains (e.getPosition()))
        return;

    if (markersEditable && markers.has_value())
    {
        draggedMarkers = hitTestMarkers (e.getPosition());
        if (draggedMarkers != 0)
        {
            markerDragOrigin = sampleAt (e.x);
            markersAtGrab = *markers;
            return;
        }

        // The strip holding the fade flags never moves the cursor, so a near miss is harmless.
        if (e.y < waveArea().getY() + flagSize)
        {
            ignoringPress = true;
            return;
        }
    }
    dragOrigin = sampleAt (e.x);
    setCursor (dragOrigin);
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (wave == nullptr || wave->numSamples() <= 0 || e.mods.isPopupMenu() || ! e.mouseWasDraggedSinceMouseDown())
        return;

    if (ignoringPress)
        return;

    if (draggedMarkers != 0)
    {
        dragMarkers (sampleAt (e.x) - markerDragOrigin);
        return;
    }

    const auto pos = sampleAt (e.x);
    setCursor (pos);

    if (! selectionEnabled)
        return;
    if (! dragSelecting && e.getDistanceFromDragStartX() == 0)
        return;

    dragSelecting = true;
    selStart = std::min (dragOrigin, pos);
    selEnd = std::max (dragOrigin, pos);
    selectionChanged();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    const bool changed = draggedMarkers != 0 && markersMoved;
    draggedMarkers = 0;
    markersMoved = false;
    if (changed && onMarkersChanged)
        onMarkersChanged();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent&)
{
    selectAll();
}

int WaveView::markerPosition (int bit) const
{
    int64_t sample = 0;
    switch (bit)
    {
        case startBit: sample = markers->start; break;
        case endBit: sample = markers->end; break;
        case fadeInBit: sample = markers->start + markers->fadeInLength; break;
        default: sample = markers->end - markers->fadeOutLength; break;
    }

    // Matches drawFlaggedLine: a marker at the end of the wave is drawn on the last pixel column.
    const auto x = static_cast<int> (xForSample (sample));
    const auto right = waveArea().getRight();
    return x == right ? right - 1 : x;
}

// A flag grabs only its own marker. On a start/end line, both are grabbed when superimposed.
// Fade markers have no line and are moved by their flag only.
int WaveView::hitTestMarkers (juce::Point<int> p) const
{
    constexpr int grab = 4;
    const auto area = waveArea();
    constexpr int bits[] = { startBit, endBit, fadeInBit, fadeOutBit };

    for (const int bit : bits)
    {
        const int x = markerPosition (bit);
        const bool top = bit == fadeInBit || bit == fadeOutBit;
        const bool right = bit == startBit || bit == fadeInBit;
        const juce::Rectangle<int> flag (right ? x : x + 1 - flagSize, top ? area.getY() : area.getBottom() - flagSize, flagSize, flagSize);
        if (flag.contains (p))
            return bit;
    }

    int best = 0, bestDistance = grab + 1;
    for (const int bit : { startBit, endBit })
    {
        const int distance = std::abs (p.x - markerPosition (bit));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = bit;
        }
    }

    if (best == 0)
        return 0;

    int mask = 0;
    const int bx = markerPosition (best);
    for (const int bit : { startBit, endBit })
        if (std::abs (markerPosition (bit) - bx) <= 1)
            mask |= bit;
    return mask;
}

void WaveView::dragMarkers (int64_t delta)
{
    const auto n = wave->numSamples();
    const auto& o = markersAtGrab;
    const bool s = (draggedMarkers & startBit) != 0, e = (draggedMarkers & endBit) != 0;
    Markers m = o;

    if (s || e)
    {
        if (s && e)
            delta = std::clamp<int64_t> (delta, -o.start, n - o.end);
        m.start = s ? o.start + delta : o.start;
        m.end = e ? o.end + delta : o.end;
        if (s && ! e)
            m.start = std::clamp<int64_t> (m.start, 0, o.end - 1);
        if (e && ! s)
            m.end = std::clamp<int64_t> (m.end, o.start + 1, n);

        // Fade lengths are kept, so the fade lines travel with their edge.
        const auto span = m.end - m.start;
        m.fadeInLength = std::min (o.fadeInLength, span);
        m.fadeOutLength = std::min (o.fadeOutLength, span - m.fadeInLength);
    }
    else if ((draggedMarkers & fadeInBit) != 0)
    {
        const auto pos = std::clamp<int64_t> (o.start + o.fadeInLength + delta, o.start, o.end - o.fadeOutLength);
        m.fadeInLength = pos - o.start;
    }
    else if ((draggedMarkers & fadeOutBit) != 0)
    {
        const auto pos = std::clamp<int64_t> (o.end - o.fadeOutLength + delta, o.start + o.fadeInLength, o.end);
        m.fadeOutLength = o.end - pos;
    }

    markers = m;
    markersMoved = true;
    repaint();
}

void WaveView::updateScrollBar()
{
    const double total = wave != nullptr ? static_cast<double> (wave->numSamples()) : 0.0;
    const double visible = getViewWidth() * samplesPerPixel;
    firstVisibleSample = std::clamp (firstVisibleSample, 0.0, std::max (0.0, total - visible));
    scrollBar.setRangeLimits (0.0, std::max (total, 1.0));
    scrollBar.setCurrentRange (firstVisibleSample, visible, juce::dontSendNotification);
    scrollBar.setEnabled (total > visible);
}

void WaveView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    firstVisibleSample = newRangeStart;
    repaint();
}

void WaveView::resized()
{
    auto bottom = getLocalBounds().removeFromBottom (barHeight);
    zoomInButton.setBounds (bottom.removeFromRight (buttonWidth));
    zoomOutButton.setBounds (bottom.removeFromRight (buttonWidth));
    if (selectionEnabled)
        allButton.setBounds (bottom.removeFromRight (buttonWidth + 8));
    if (selectionEnabled || markersEditable)
    {
        outButton.setBounds (bottom.removeFromRight (buttonWidth));
        inButton.setBounds (bottom.removeFromRight (buttonWidth));
    }
    playButton.setBounds (bottom.removeFromLeft (buttonWidth));
    if (extraButton != nullptr)
    {
        bottom.removeFromLeft (4);
        extraButton->setBounds (bottom.removeFromLeft (extraButtonWidth));
        bottom.removeFromLeft (4);
    }
    scrollBar.setBounds (bottom.withTrimmedRight (4));

    // Keep the same view when possible, but never be zoomed out further than fit.
    samplesPerPixel = std::clamp (samplesPerPixel, 1.0, getFitSamplesPerPixel());
    updateScrollBar();
}

void WaveView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    if (wave == nullptr)
        return;

    if (std::abs (d.deltaX) > std::abs (d.deltaY))
    {
        firstVisibleSample -= d.deltaX * getViewWidth() * samplesPerPixel * 0.2;
        updateScrollBar();
        repaint();
        return;
    }

    if (d.deltaY != 0.0f)
        zoomBy (std::pow (0.5, d.deltaY * 2.0));
}

WaveView::MinMax WaveView::getRange (int64_t first, int64_t last) const
{
    const auto n = wave->numSamples();
    first = std::clamp<int64_t> (first, 0, n - 1);
    last = std::clamp<int64_t> (last, first, n - 1);

    MinMax mm { 0.0f, 0.0f };
    if (last - first >= 2 * blockSize)
    {
        const auto b0 = (first + blockSize - 1) / blockSize;
        const auto b1 = (last + 1) / blockSize; // exclusive
        mm = { blocks[static_cast<size_t> (b0)].lo, blocks[static_cast<size_t> (b0)].hi };
        for (auto b = b0; b < b1; ++b)
        {
            mm.lo = std::min (mm.lo, blocks[static_cast<size_t> (b)].lo);
            mm.hi = std::max (mm.hi, blocks[static_cast<size_t> (b)].hi);
        }
        return mm;
    }

    const float* l = wave->leftData();
    const float* r = wave->rightData();
    mm = { l[first], l[first] };
    for (auto i = first; i <= last; ++i)
    {
        mm.lo = std::min ({ mm.lo, l[i], r[i] });
        mm.hi = std::max ({ mm.hi, l[i], r[i] });
    }
    return mm;
}

void WaveView::paint (juce::Graphics& g)
{
    const auto area = waveArea();
    g.setColour (juce::Colour (0xff1b1b1b));
    g.fillRect (area);

    if (const auto sel = getSelection())
    {
        const float x0 = std::max (xForSample (sel->first), static_cast<float> (area.getX()));
        const float x1 = std::min (xForSample (sel->second), static_cast<float> (area.getRight()));
        if (x1 > x0)
        {
            g.setColour (juce::Colour (0xff2f6fa8).withAlpha (0.45f));
            g.fillRect (x0, static_cast<float> (area.getY()), x1 - x0, static_cast<float> (area.getHeight()));
        }
    }

    const float midY = static_cast<float> (area.getCentreY());
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (area.getCentreY(), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));

    if (wave == nullptr || wave->numSamples() <= 0)
        return;

    const auto n = wave->numSamples();
    const float halfHeight = static_cast<float> (area.getHeight()) * 0.5f - 2.0f;
    const float scale = halfHeight / peak;

    g.setColour (juce::Colour (0xff5fb4e8));
    for (int x = 0; x < area.getWidth(); ++x)
    {
        const double s0 = firstVisibleSample + x * samplesPerPixel;
        const double s1 = s0 + samplesPerPixel;
        if (s0 >= static_cast<double> (n))
            break;

        const auto mm = getRange (static_cast<int64_t> (s0), static_cast<int64_t> (std::ceil (s1)) - 1);
        const float top = midY - mm.hi * scale;
        const float bottom = midY - mm.lo * scale;
        g.fillRect (static_cast<float> (area.getX() + x), top, 1.0f, std::max (1.0f, bottom - top));
    }

    if (markers.has_value())
    {
        const auto fadeInEnd = markers->start + markers->fadeInLength;
        const auto fadeOutStart = markers->end - markers->fadeOutLength;
        const float top = static_cast<float> (area.getY());
        const float bottom = static_cast<float> (area.getBottom());

        // Dim the parts of the wave outside the hit.
        const float left = static_cast<float> (area.getX());
        const float right = static_cast<float> (area.getRight());
        const float sx = std::clamp (xForSample (markers->start), left, right);
        const float ex = std::clamp (xForSample (markers->end), left, right);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (left, top, sx - left, bottom - top);
        g.fillRect (ex, top, right - ex, bottom - top);

        g.setColour (juce::Colours::orange.withAlpha (0.9f));
        if (markers->fadeInLength > 0)
            g.drawLine (xForSample (markers->start), bottom, xForSample (fadeInEnd), top, 1.0f);
        if (markers->fadeOutLength > 0)
            g.drawLine (xForSample (fadeOutStart), top, xForSample (markers->end), bottom, 1.0f);

        drawFlaggedLine (g, markers->start, juce::Colours::limegreen, "i", false, true, true);
        drawFlaggedLine (g, markers->end, juce::Colours::tomato, "o", false, false, true);
        drawFlaggedLine (g, fadeInEnd, juce::Colours::orange, "f", true, true, false);
        drawFlaggedLine (g, fadeOutStart, juce::Colours::orange, "f", true, false, false);
    }

    if (cursor.has_value())
    {
        const float cx = xForSample (*cursor);
        if (cx >= static_cast<float> (area.getX()) && cx < static_cast<float> (area.getRight()))
        {
            g.setColour (juce::Colours::yellow);
            g.fillRect (std::floor (cx), static_cast<float> (area.getY()), 1.0f, static_cast<float> (area.getHeight()));
        }
    }
}

void WaveView::drawFlaggedLine (juce::Graphics& g, int64_t sample, juce::Colour colour, const juce::String& label,
                                bool flagAtTop, bool flagToRight, bool drawLine) const
{
    const auto area = waveArea();
    float x = std::floor (xForSample (sample));
    if (x < static_cast<float> (area.getX()) || x > static_cast<float> (area.getRight()))
        return;

    // A marker at the very end of the wave sits on the right edge; keep it inside the drawn area.
    x = std::min (x, static_cast<float> (area.getRight() - 1));

    g.setColour (colour);
    if (drawLine)
        g.fillRect (x, static_cast<float> (area.getY()), 1.0f, static_cast<float> (area.getHeight()));

    const float flagX = flagToRight ? x : x + 1.0f - static_cast<float> (flagSize);
    const float flagY = flagAtTop ? static_cast<float> (area.getY()) : static_cast<float> (area.getBottom() - flagSize);
    g.fillRect (flagX, flagY, static_cast<float> (flagSize), static_cast<float> (flagSize));
    g.setColour (juce::Colours::black);
    g.setFont (juce::FontOptions (static_cast<float> (flagSize) - 2.0f, juce::Font::bold));
    g.drawText (label, juce::Rectangle<float> (flagX, flagY, static_cast<float> (flagSize), static_cast<float> (flagSize)), juce::Justification::centred, false);
}
} // namespace drumhits::ui
