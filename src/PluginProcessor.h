#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include "core/controller/Controller.h"
#include "core/engine/Engine.h"
#include "core/preferences/Preferences.h"

class DrumHitsProcessor final : public juce::AudioProcessor,
                                private juce::AsyncUpdater,
                                private juce::Timer
{
public:
    DrumHitsProcessor();
    ~DrumHitsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    drumhits::Controller& getController() noexcept { return controller; }
    drumhits::Preferences& getPreferences() noexcept { return preferences; }
    std::function<void (int)> onMissingWavesLoaded;
    std::function<void (const juce::String&, int)> onWavesReloaded;
    const juce::String& getSampleRateError() const noexcept { return sampleRateError; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    friend struct DrumHitsProcessorTestAccess;
    void applyState (const juce::MemoryBlock& state);
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void finishRateChangeWhenReady();

    drumhits::Preferences preferences { drumhits::Preferences::getDefaultFile() };
    std::unique_ptr<drumhits::Engine> engine;
    drumhits::Controller controller;
    bool prepared = false;
    std::atomic<double> requestedRate { 44100.0 };
    std::atomic<uint64_t> requestedGeneration { 0 };
    std::atomic<uint64_t> readyGeneration { 0 };
    uint64_t convertedGeneration = 0;
    int maintenanceTicks = 0;
    juce::String sampleRateError;
    juce::MemoryBlock pendingState;
    std::array<drumhits::NoteEvent, 1024> noteEvents;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumHitsProcessor)
};
