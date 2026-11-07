#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"

using Catch::Approx;

struct DrumHitsProcessorTestAccess
{
    static void applyRate (DrumHitsProcessor& processor) { processor.handleAsyncUpdate(); }
    static void maintenance (DrumHitsProcessor& processor) { processor.timerCallback(); }
};

namespace
{
juce::File makeTestWave (const juce::File& dir)
{
    const auto file = dir.getChildFile ("processor.wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
        .withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
    REQUIRE (writer != nullptr);
    juce::AudioBuffer<float> data (1, 200);
    data.clear();
    juce::FloatVectorOperations::fill (data.getWritePointer (0), 0.5f, 200);
    REQUIRE (writer->writeFromAudioSampleBuffer (data, 0, 200));
    return file;
}
}

TEST_CASE ("Processor exposes four independent stereo output buses")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DrumHitsProcessor processor;
    REQUIRE (processor.getBusCount (false) == 4);
    auto layout = processor.getBusesLayout();
    REQUIRE (layout.outputBuses[0] == juce::AudioChannelSet::stereo());
    REQUIRE (layout.outputBuses[1].isDisabled());
    layout.outputBuses.set (2, juce::AudioChannelSet::stereo());
    REQUIRE (processor.isBusesLayoutSupported (layout));
    REQUIRE (processor.setBusesLayout (layout));
    auto invalid = layout;
    invalid.outputBuses.set (1, juce::AudioChannelSet::mono());
    REQUIRE_FALSE (processor.isBusesLayoutSupported (invalid));
    invalid = layout;
    invalid.outputBuses.set (0, juce::AudioChannelSet::disabled());
    REQUIRE_FALSE (processor.isBusesLayoutSupported (invalid));

    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_processor_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    processor.prepareToPlay (48000.0, 64);
    DrumHitsProcessorTestAccess::applyRate (processor);
    auto& controller = processor.getController();
    drumhits::DrumProperties drum;
    drum.note = 36;
    drum.output = 2;
    controller.setDrumProperties (0, drum);
    const auto loaded = controller.loadWave (makeTestWave (dir));
    REQUIRE (loaded.status == drumhits::LoadWaveResult::Status::loaded);
    REQUIRE (controller.addHit (0, loaded.waveId) >= 0);
    juce::AudioBuffer<float> buffer (4, 64);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
    processor.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 0) == Approx (0.0f).margin (1e-7));
    REQUIRE (buffer.getSample (1, 0) == Approx (0.0f).margin (1e-7));
    REQUIRE (buffer.getSample (2, 0) == Approx (0.5f * 0.70710678f).margin (1e-4));
    REQUIRE (buffer.getSample (3, 0) == Approx (0.5f * 0.70710678f).margin (1e-4));
    controller.resetVoices();
    drum.output = 1;
    controller.setDrumProperties (0, drum);
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
    processor.processBlock (buffer, midi);
    for (int channel = 0; channel < 4; ++channel)
        REQUIRE (buffer.getMagnitude (channel, 0, 64) == Approx (0.0f).margin (1e-7));
    REQUIRE (dir.deleteRecursively());
}

TEST_CASE ("Processor mutes during asynchronous rate conversion and reports missing waves")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DrumHitsProcessor processor;
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_processor_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    processor.prepareToPlay (48000.0, 64);
    DrumHitsProcessorTestAccess::applyRate (processor);
    auto& controller = processor.getController();
    const auto file = makeTestWave (dir);
    const auto loaded = controller.loadWave (file);
    REQUIRE (loaded.status == drumhits::LoadWaveResult::Status::loaded);
    REQUIRE (controller.addHit (0, loaded.waveId) >= 0);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    REQUIRE (file.deleteFile());
    processor.prepareToPlay (96000.0, 64);
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
    processor.processBlock (buffer, midi);
    REQUIRE (buffer.getMagnitude (0, 0, 64) == Approx (0.0f).margin (1e-7));
    int missing = 0;
    juce::String error;
    processor.onWavesReloaded = [&] (const juce::String& e, int count) { error = e; missing = count; };
    DrumHitsProcessorTestAccess::applyRate (processor);
    DrumHitsProcessorTestAccess::maintenance (processor);
    REQUIRE (missing == 1);
    REQUIRE (error.contains ("processor.wav"));
    REQUIRE (controller.isWaveMissing (loaded.waveId));
    REQUIRE (controller.getWaveLength (loaded.waveId) == 400);
    REQUIRE (dir.deleteRecursively());
}

TEST_CASE ("Each host bus routes independently for every auxiliary bus layout")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DrumHitsProcessor processor;
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_processor_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    processor.prepareToPlay (48000.0, 64);
    DrumHitsProcessorTestAccess::applyRate (processor);
    auto& controller = processor.getController();
    const auto loaded = controller.loadWave (makeTestWave (dir));
    REQUIRE (loaded.status == drumhits::LoadWaveResult::Status::loaded);
    REQUIRE (controller.addHit (0, loaded.waveId) >= 0);

    for (int mask = 0; mask < 8; ++mask)
    {
        auto layout = processor.getBusesLayout();
        for (int bus = 1; bus < 4; ++bus)
            layout.outputBuses.set (bus, (mask & (1 << (bus - 1))) != 0
                                             ? juce::AudioChannelSet::stereo()
                                             : juce::AudioChannelSet::disabled());
        REQUIRE (processor.setBusesLayout (layout));
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), 16);
        for (int target = 0; target < 4; ++target)
        {
            controller.resetVoices();
            drumhits::DrumProperties drum;
            drum.note = 36;
            drum.output = target;
            controller.setDrumProperties (0, drum);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
            processor.processBlock (buffer, midi);

            for (int bus = 0; bus < 4; ++bus)
                if (processor.getChannelCountOfBus (false, bus) == 2)
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const int index = processor.getChannelIndexInProcessBlockBuffer (false, bus, channel);
                        const float expected = bus == target ? 0.5f * 0.70710678f : 0.0f;
                        REQUIRE (buffer.getSample (index, 0) == Approx (expected).margin (1e-4));
                    }
        }
    }
    REQUIRE (dir.deleteRecursively());
}

TEST_CASE ("Processor waits for queued configuration before resuming after a rate change")
{
    juce::ScopedJuceInitialiser_GUI gui;
    DrumHitsProcessor processor;
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("drumhits_processor_" + juce::String (juce::Random::getSystemRandom().nextInt64())));
    const auto dir = tempDir.getFile();
    REQUIRE (dir.createDirectory());
    processor.prepareToPlay (48000.0, 64);
    DrumHitsProcessorTestAccess::applyRate (processor);
    auto& controller = processor.getController();
    drumhits::DrumProperties drum;
    drum.note = 36;
    controller.setDrumProperties (0, drum);
    const auto loaded = controller.loadWave (makeTestWave (dir));
    REQUIRE (loaded.status == drumhits::LoadWaveResult::Status::loaded);
    const int slot = controller.addHit (0, loaded.waveId);
    REQUIRE (slot >= 0);
    const auto props = *controller.getHitProperties (0, slot);
    for (int i = 0; i < 9000; ++i)
        REQUIRE (controller.setHitProperties (0, slot, props));
    REQUIRE (controller.hasPendingMessages());
    processor.prepareToPlay (96000.0, 64);
    DrumHitsProcessorTestAccess::applyRate (processor);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
    processor.processBlock (buffer, midi);
    REQUIRE (buffer.getMagnitude (0, 0, 64) == Approx (0.0f).margin (1e-7));
    DrumHitsProcessorTestAccess::maintenance (processor);
    REQUIRE_FALSE (controller.hasPendingMessages());
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (127)), 0);
    processor.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 0) == Approx (0.5f * 0.70710678f).margin (1e-4));
    REQUIRE (dir.deleteRecursively());
}
