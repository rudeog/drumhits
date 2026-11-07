#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr int maintenanceIntervalMs = 50;
constexpr int garbageCollectionIntervalMs = 10000;
}

DrumHitsProcessor::DrumHitsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output 1", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output 2", juce::AudioChannelSet::stereo(), false)
                                        .withOutput ("Output 3", juce::AudioChannelSet::stereo(), false)
                                        .withOutput ("Output 4", juce::AudioChannelSet::stereo(), false)),
      engine (std::make_unique<drumhits::Engine>()),
      controller (*engine)
{
    controller.setDefaultDrumNotes (preferences.getDefaultDrumNotes());
    controller.initialiseDefaultDrums();
    startTimer (maintenanceIntervalMs);
}

DrumHitsProcessor::~DrumHitsProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    engine.reset();
}

void DrumHitsProcessor::prepareToPlay (double sampleRate, int)
{
    engine->prepare (sampleRate);
    requestedRate.store (sampleRate, std::memory_order_relaxed);
    requestedGeneration.fetch_add (1, std::memory_order_release);
    triggerAsyncUpdate();
}

void DrumHitsProcessor::handleAsyncUpdate()
{
    const auto generation = requestedGeneration.load (std::memory_order_acquire);
    const auto rate = requestedRate.load (std::memory_order_relaxed);
    const auto result = controller.setSampleRate (rate);
    sampleRateError = result.wasOk() ? juce::String() : result.getErrorMessage();
    if (sampleRateError.isNotEmpty())
        juce::Logger::writeToLog ("Sample-rate reload failed:\n" + sampleRateError);
    prepared = true;

    if (pendingState.getSize() > 0)
    {
        // State restored before the sample rate was known; apply it now so waves load at the right rate.
        juce::MemoryBlock state;
        state.swapWith (pendingState);
        applyState (state);
    }

    if (onWavesReloaded)
        onWavesReloaded (sampleRateError, controller.getMissingWaveCount());

    if (std::abs (controller.getSampleRate() - rate) < 1e-6)
    {
        controller.resetVoices();
        convertedGeneration = generation;
        finishRateChangeWhenReady();
    }

    if (requestedGeneration.load (std::memory_order_acquire) != generation)
        triggerAsyncUpdate();
}

void DrumHitsProcessor::finishRateChangeWhenReady()
{
    controller.flushPendingMessages();
    // The audio thread drains the queued reset and all preceding configuration before rendering.
    if (! controller.hasPendingMessages()
        && convertedGeneration == requestedGeneration.load (std::memory_order_acquire))
        readyGeneration.store (convertedGeneration, std::memory_order_release);
}

void DrumHitsProcessor::timerCallback()
{
    finishRateChangeWhenReady();
    if (++maintenanceTicks >= garbageCollectionIntervalMs / maintenanceIntervalMs)
    {
        maintenanceTicks = 0;
        controller.collectGarbage();
    }
}

void DrumHitsProcessor::applyState (const juce::MemoryBlock& state)
{
    controller.collectGarbage();
    const auto result = controller.loadState (state.getData(), state.getSize());
    if (result.failed())
        juce::Logger::writeToLog (result.getErrorMessage());
    else if (const int missing = controller.getMissingWaveCount(); missing > 0 && onMissingWavesLoaded)
    {
        const auto notify = onMissingWavesLoaded;
        juce::MessageManager::callAsync ([notify, missing] { notify (missing); });
    }
}

// Controller access must stay on the message thread, but hosts may call state methods elsewhere.
void DrumHitsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto save = [this, &destData] { destData = controller.saveState(); };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        save();
    else
        juce::MessageManager::callSync (save);
}

void DrumHitsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const juce::MemoryBlock state (data, static_cast<size_t> (sizeInBytes));
    const auto apply = [this, state]
    {
        if (prepared)
            applyState (state);
        else
            pendingState = state;
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

void DrumHitsProcessor::releaseResources() {}

bool DrumHitsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() != 0 || layouts.outputBuses.size() != drumhits::maxOutputs
        || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
        if (! layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void DrumHitsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    std::array<float*, 2 * drumhits::maxOutputs> channels {};
    for (int bus = 0; bus < drumhits::maxOutputs; ++bus)
        if (getChannelCountOfBus (false, bus) == 2)
            for (int channel = 0; channel < 2; ++channel)
                channels[static_cast<size_t> (2 * bus + channel)] =
                    buffer.getWritePointer (getChannelIndexInProcessBlockBuffer (false, bus, channel));

    const bool ready = readyGeneration.load (std::memory_order_acquire)
                       == requestedGeneration.load (std::memory_order_acquire);
    int numEvents = 0;
    if (ready)
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (! message.isNoteOnOrOff() || numEvents >= static_cast<int> (noteEvents.size()))
                continue;

            noteEvents[static_cast<size_t> (numEvents++)] = { metadata.samplePosition,
                                                              message.getNoteNumber(),
                                                              message.getVelocity(),
                                                              message.isNoteOn() };
        }
    }
    midi.clear();

    engine->process (channels.data(), static_cast<int> (channels.size()), buffer.getNumSamples(),
                     noteEvents.data(), numEvents);
    if (! ready)
        buffer.clear();
}

juce::AudioProcessorEditor* DrumHitsProcessor::createEditor()
{
    return new DrumHitsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumHitsProcessor();
}
