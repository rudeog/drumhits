#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "ui/MainTab.h"

namespace drumhits::ui
{
struct MainTabTestAccess
{
    static const juce::Label& name (const MainTab& tab) { return tab.waveNameLabel; }
    static juce::Label& path (MainTab& tab) { return tab.wavePathLabel; }
    static const WaveView& wave (const MainTab& tab) { return tab.hitWave; }
    static const HitDetailsPanel& details (const MainTab& tab) { return tab.details; }
    static void selectDrum (MainTab& tab, int drum) { tab.selectDrum (drum); }
};
}

TEST_CASE ("Hit details show the selected wave name and primary path")
{
    using namespace drumhits;
    using namespace drumhits::ui;
    juce::ScopedJuceInitialiser_GUI gui;
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_main_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    const auto file = dir.getChildFile ("wave.wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
        .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
    REQUIRE (writer != nullptr);
    juce::AudioBuffer<float> data (1, 100);
    data.clear();
    REQUIRE (writer->writeFromAudioSampleBuffer (data, 0, 100));
    writer.reset();

    auto engine = std::make_unique<Engine>();
    Controller controller (*engine);
    REQUIRE (controller.setSampleRate (48000.0).wasOk());
    const auto loaded = controller.loadWave (file);
    REQUIRE (loaded.status == LoadWaveResult::Status::loaded);
    const int slot = controller.addHit (0, loaded.waveId);
    REQUIRE (slot >= 0);

    {
        MainTab tab (controller);
        tab.showHit (0, slot);
        REQUIRE (MainTabTestAccess::name (tab).isVisible());
        REQUIRE (MainTabTestAccess::name (tab).getText() == "Wave: wave.wav");
        REQUIRE (MainTabTestAccess::path (tab).getText() == "Path: " + file.getFullPathName());
        REQUIRE (MainTabTestAccess::path (tab).getTooltip() == file.getFullPathName());
        for (const auto bounds : { juce::Rectangle<int> (0, 0, 700, 460),
                                   juce::Rectangle<int> (0, 0, 900, 610) })
        {
            tab.setBounds (bounds);
            REQUIRE (MainTabTestAccess::details (tab).getBottom() <= MainTabTestAccess::name (tab).getY());
            REQUIRE (MainTabTestAccess::name (tab).getBottom() <= MainTabTestAccess::path (tab).getY());
            REQUIRE (MainTabTestAccess::path (tab).getBottom() <= MainTabTestAccess::wave (tab).getY());
            REQUIRE (MainTabTestAccess::wave (tab).getHeight() >= 56);
            for (auto* child : MainTabTestAccess::wave (tab).getChildren())
                REQUIRE (child->getName() != "To drum...");
        }
        REQUIRE (controller.renameWave (loaded.waveId, "Renamed"));
        tab.refresh();
        REQUIRE (MainTabTestAccess::name (tab).getText() == "Wave: Renamed");
        REQUIRE (file.deleteFile());
        REQUIRE (controller.setSampleRate (96000.0).failed());
        tab.refresh();
        REQUIRE (MainTabTestAccess::name (tab).isVisible());
        REQUIRE (MainTabTestAccess::path (tab).getText() == "Path: " + file.getFullPathName());
        REQUIRE_FALSE (MainTabTestAccess::wave (tab).isVisible());
        MainTabTestAccess::selectDrum (tab, 1);
        REQUIRE_FALSE (MainTabTestAccess::name (tab).isVisible());
        REQUIRE_FALSE (MainTabTestAccess::path (tab).isVisible());
    }

    engine.reset();
    REQUIRE (dir.deleteRecursively());
}
