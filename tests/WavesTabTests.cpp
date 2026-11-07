#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "ui/WavesTab.h"

namespace drumhits::ui
{
struct WavesTabTestAccess
{
    static juce::PopupMenu menu (WavesTab& tab, const std::vector<int>& ids,
                                 std::vector<std::pair<int, int>>& destinations)
    {
        return tab.makeActionsMenu (ids, destinations);
    }

    static juce::Result add (WavesTab& tab, int drum, const std::vector<int>& ids)
    {
        return tab.addWavesToDrum (drum, ids);
    }

    static void selectPortion (WavesTab& tab, int wave, int64_t start, int64_t end)
    {
        tab.showWave (wave, start, end);
    }

    static void addPortion (WavesTab& tab, int drum) { tab.addSelectionToDrum (drum); }
    static WaveView& waveView (WavesTab& tab) { return tab.waveView; }
    static juce::Button& toDrum (WavesTab& tab) { return tab.toDrumButton; }
    static void deselect (WavesTab& tab) { tab.table.deselectAllRows(); }
    static void selectBoth (WavesTab& tab)
    {
        tab.table.selectRow (0);
        tab.table.selectRow (1, true, false);
    }
};
}

namespace
{
juce::File writeWave (const juce::File& dir, const juce::String& name, int length)
{
    const auto file = dir.getChildFile (name);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
        .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
    REQUIRE (writer != nullptr);
    juce::AudioBuffer<float> data (1, length);
    data.clear();
    REQUIRE (writer->writeFromAudioSampleBuffer (data, 0, length));
    return file;
}

const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, const juce::String& text)
{
    juce::PopupMenu::MenuItemIterator iterator (menu);
    while (iterator.next())
        if (iterator.getItem().text == text)
            return &iterator.getItem();
    return nullptr;
}
}

TEST_CASE ("Wave Pool actions menu handles usage and whole-wave additions")
{
    using namespace drumhits;
    using namespace drumhits::ui;
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_waves_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    auto engine = std::make_unique<Engine>();
    Controller controller (*engine);
    REQUIRE (controller.setSampleRate (48000.0).wasOk());
    const auto file1 = writeWave (dir, "one.wav", 100);
    const auto file2 = writeWave (dir, "two.wav", 200);
    const auto one = controller.loadWave (file1);
    const auto two = controller.loadWave (file2);
    REQUIRE (one.status == LoadWaveResult::Status::loaded);
    REQUIRE (two.status == LoadWaveResult::Status::loaded);
    WavesTab tab (controller);
    auto& view = WavesTabTestAccess::waveView (tab);
    auto& toDrum = WavesTabTestAccess::toDrum (tab);
    REQUIRE (toDrum.getParentComponent() == &view);
    REQUIRE_FALSE (toDrum.isEnabled());
    for (const auto bounds : { juce::Rectangle<int> (0, 0, 684, 420),
                               juce::Rectangle<int> (0, 0, 900, 610) })
    {
        tab.setBounds (bounds);
        REQUIRE (view.getBottom() == tab.getHeight() - 8);
        juce::Component* playButton = nullptr;
        juce::ScrollBar* scrollbar = nullptr;
        for (auto* child : view.getChildren())
        {
            if (child->getName() == "play")
                playButton = child;
            if (auto* bar = dynamic_cast<juce::ScrollBar*> (child))
                scrollbar = bar;
            if (child != &toDrum && child->isVisible())
                REQUIRE_FALSE (child->getBounds().intersects (toDrum.getBounds()));
        }
        REQUIRE (playButton != nullptr);
        REQUIRE (scrollbar != nullptr);
        REQUIRE (toDrum.getX() >= playButton->getRight());
        REQUIRE (toDrum.getY() == playButton->getY());
        REQUIRE (toDrum.getWidth() > 0);
        REQUIRE (scrollbar->getWidth() > 0);
    }
    std::vector<std::pair<int, int>> destinations;
    auto menu = WavesTabTestAccess::menu (tab, {}, destinations);
    REQUIRE_FALSE (findItem (menu, "To drum")->isEnabled);
    REQUIRE_FALSE (findItem (menu, "Usage")->isEnabled);

    menu = WavesTabTestAccess::menu (tab, { one.waveId }, destinations);
    REQUIRE (findItem (menu, "To drum")->isEnabled);
    REQUIRE (findItem (menu, "Usage")->isEnabled);
    REQUIRE (findItem (*findItem (menu, "Usage")->subMenu, "No hits use this wave") != nullptr);

    controller.setDrumName (0, "Kick");
    const int slot = controller.addHit (0, one.waveId);
    REQUIRE (slot >= 0);
    controller.setHitName (0, slot, "Kick hit");
    destinations.clear();
    menu = WavesTabTestAccess::menu (tab, { one.waveId }, destinations);
    REQUIRE (destinations == std::vector<std::pair<int, int>> { { 0, slot } });
    const auto* usageDrum = findItem (*findItem (menu, "Usage")->subMenu, "1 - Kick");
    REQUIRE (usageDrum != nullptr);
    const auto* usageHit = findItem (*usageDrum->subMenu, "Kick hit");
    REQUIRE (usageHit != nullptr);
    REQUIRE (usageHit->itemID == 1000);

    for (int i = 1; i < maxHitsPerDrum - 1; ++i)
        REQUIRE (controller.addHit (0, one.waveId) >= 0);
    destinations.clear();
    menu = WavesTabTestAccess::menu (tab, { one.waveId, two.waveId }, destinations);
    REQUIRE (findItem (menu, "To drum")->isEnabled);
    REQUIRE_FALSE (findItem (menu, "Usage")->isEnabled);
    const auto& drums = *findItem (menu, "To drum")->subMenu;
    REQUIRE_FALSE (findItem (drums, "1 - Kick")->isEnabled);
    REQUIRE (findItem (drums, "2")->isEnabled);
    REQUIRE (WavesTabTestAccess::add (tab, 0, { one.waveId, two.waveId }).failed());
    REQUIRE (controller.getHitCount (0) == maxHitsPerDrum - 1);
    REQUIRE (WavesTabTestAccess::add (tab, 0, { two.waveId }).wasOk());
    REQUIRE (controller.getHitCount (0) == maxHitsPerDrum);

    WavesTabTestAccess::selectPortion (tab, one.waveId, 10, 30);
    REQUIRE (toDrum.isEnabled());
    REQUIRE (WavesTabTestAccess::add (tab, 1, { one.waveId }).wasOk());
    const auto whole = *controller.getHitProperties (1, controller.getHitSlots (1).front());
    REQUIRE (whole.startPos == 0);
    REQUIRE (whole.endPos == 100);
    WavesTabTestAccess::addPortion (tab, 2);
    const auto portion = *controller.getHitProperties (2, controller.getHitSlots (2).front());
    REQUIRE (portion.startPos == 10);
    REQUIRE (portion.endPos == 30);
    REQUIRE (WavesTabTestAccess::add (tab, 3, { one.waveId, two.waveId }).wasOk());
    const auto slots = controller.getHitSlots (3);
    REQUIRE (slots.size() == 2);
    REQUIRE (controller.getHitWaveId (3, slots[0]) == one.waveId);
    REQUIRE (controller.getHitWaveId (3, slots[1]) == two.waveId);
    REQUIRE (controller.getHitProperties (3, slots[1])->endPos == 200);
    WavesTabTestAccess::selectBoth (tab);
    REQUIRE_FALSE (toDrum.isEnabled());
    WavesTabTestAccess::deselect (tab);
    REQUIRE_FALSE (toDrum.isEnabled());

    REQUIRE (file2.deleteFile());
    REQUIRE (controller.setSampleRate (96000.0).failed());
    REQUIRE (controller.isWaveMissing (two.waveId));
    tab.refresh();
    WavesTabTestAccess::selectPortion (tab, two.waveId, 10, 30);
    REQUIRE_FALSE (toDrum.isEnabled());
    destinations.clear();
    menu = WavesTabTestAccess::menu (tab, { one.waveId, two.waveId }, destinations);
    REQUIRE_FALSE (findItem (menu, "To drum")->isEnabled);
    destinations.clear();
    menu = WavesTabTestAccess::menu (tab, { two.waveId }, destinations);
    REQUIRE_FALSE (findItem (menu, "To drum")->isEnabled);
    REQUIRE (findItem (menu, "Usage")->isEnabled);
    REQUIRE_FALSE (destinations.empty());
    REQUIRE (WavesTabTestAccess::add (tab, 4, { one.waveId, two.waveId }).failed());
    REQUIRE (controller.getHitCount (4) == 0);
    REQUIRE (dir.deleteRecursively());
    engine.reset();
}
