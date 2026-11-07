#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include "core/controller/Controller.h"
#include "core/controller/OfflineResampler.h"
#include "ui/WavePoolSort.h"

using namespace drumhits;
using Catch::Approx;

namespace
{
constexpr float center = 0.70710678f;

juce::File writeWav (const juce::File& dir, const juce::String& name, int numChannels, double rate, int numSamples, float value)
{
    auto file = dir.getChildFile (name);
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (file));
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate (rate)
                                                      .withNumChannels (numChannels)
                                                      .withBitsPerSample (24));
    REQUIRE (writer != nullptr);

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), value, numSamples);
    REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, numSamples));
    return file;
}

struct Fixture
{
    juce::TemporaryFile tempDir { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drumhits_tests_" + juce::String (juce::Random::getSystemRandom().nextInt64())) };
    juce::File dir = tempDir.getFile();

    std::unique_ptr<Engine> engine { std::make_unique<Engine> (256) };
    Controller controller { *engine };
    std::vector<std::vector<float>> out = std::vector<std::vector<float>> (2 * maxOutputs);

    Fixture()
    {
        dir.createDirectory();
        engine->prepare (48000.0);
        controller.setSampleRate (48000.0);
    }

    ~Fixture()
    {
        engine.reset();
        dir.deleteRecursively();
    }

    void render (int numSamples)
    {
        std::vector<float*> ptrs;
        for (auto& ch : out)
        {
            ch.assign (static_cast<size_t> (numSamples), 0.0f);
            ptrs.push_back (ch.data());
        }
        engine->process (ptrs.data(), static_cast<int> (ptrs.size()), numSamples);
    }

    int loadId (const juce::File& file)
    {
        auto r = controller.loadWave (file);
        REQUIRE (r.status == LoadWaveResult::Status::loaded);
        return r.waveId;
    }
};

juce::ValueTree readPatchTree (const juce::MemoryBlock& block)
{
    juce::MemoryInputStream input (block, false);
    input.readInt();
    input.readInt();
    return juce::ValueTree::readFromStream (input);
}
} // namespace

TEST_CASE ("Controller loads mono and stereo waves as stereo at the host rate")
{
    Fixture f;
    const int mono = f.loadId (writeWav (f.dir, "mono.wav", 1, 48000.0, 480, 0.5f));
    const int stereo = f.loadId (writeWav (f.dir, "stereo.wav", 2, 48000.0, 960, 0.25f));
    const int resampled = f.loadId (writeWav (f.dir, "slow.wav", 1, 24000.0, 240, 0.5f));

    REQUIRE (f.controller.getWaveLength (mono) == 480);
    REQUIRE (f.controller.getWaveLength (stereo) == 960);
    REQUIRE (f.controller.getWaveLength (resampled) == 480);
    REQUIRE (f.controller.getWaveName (mono) == "mono.wav");
    REQUIRE (f.controller.getWaveIds().size() == 3);
}

TEST_CASE ("Offline conversion preserves level and rejects frequencies above the new Nyquist limit")
{
    constexpr int length = 4096;
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> passband (length), stopband (length), constant (length, 0.75f);
    for (int i = 0; i < length; ++i)
    {
        passband[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * pi * 0.10 * i));
        stopband[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * pi * 0.35 * i));
    }
    const auto low = resampleOffline (passband.data(), length, length / 2, 2.0);
    const auto high = resampleOffline (stopband.data(), length, length / 2, 2.0);
    const auto dc = resampleOffline (constant.data(), length, length / 2, 2.0);
    double lowPower = 0.0, highPower = 0.0;
    for (size_t i = 64; i < low.size() - 64; ++i)
    {
        lowPower += low[i] * low[i];
        highPower += high[i] * high[i];
    }
    REQUIRE (lowPower / static_cast<double> (low.size() - 128) == Approx (0.5).margin (0.01));
    REQUIRE (highPower / lowPower < 0.000001);
    REQUIRE (dc.front() == Approx (0.75f).margin (1e-6));
    REQUIRE (dc.back() == Approx (0.75f).margin (1e-6));
}

TEST_CASE ("Sample-rate changes reload waves and preserve hit time and metadata")
{
    Fixture f;
    const auto file = writeWav (f.dir, "rate.wav", 1, 48000.0, 100, 0.5f);
    const int wave = f.loadId (file);
    const int slot = f.controller.addHit (0, wave);
    auto props = *f.controller.getHitProperties (0, slot);
    props.startPos = 10;
    props.endPos = 90;
    props.fadeInLength = 12;
    props.fadeOutLength = 14;
    props.semitones = 4;
    REQUIRE (f.controller.setHitProperties (0, slot, props));
    f.controller.setHitName (0, slot, "Custom");
    const auto* oldWave = f.controller.getWave (wave);
    REQUIRE (f.controller.setSampleRate (96000.0).wasOk());
    REQUIRE (f.controller.getWave (wave) != oldWave);
    REQUIRE (f.controller.getWaveLength (wave) == 200);
    auto updated = *f.controller.getHitProperties (0, slot);
    REQUIRE (updated.startPos == 20);
    REQUIRE (updated.endPos == 180);
    REQUIRE (updated.fadeInLength == 24);
    REQUIRE (updated.fadeOutLength == 28);
    REQUIRE (updated.semitones == 4);
    REQUIRE (f.controller.getHitName (0, slot) == "Custom");
    REQUIRE (updated.wave == f.controller.getWave (wave));
    REQUIRE (f.controller.getWaveFiles (wave) == std::vector<juce::File> { file });
    REQUIRE (f.controller.setSampleRate (24000.0).wasOk());
    REQUIRE (f.controller.getWaveLength (wave) == 50);
    updated = *f.controller.getHitProperties (0, slot);
    REQUIRE (updated.startPos == 5);
    REQUIRE (updated.endPos == 45);
    REQUIRE (updated.fadeInLength == 6);
    REQUIRE (updated.fadeOutLength == 7);
}

TEST_CASE ("A failed sample-rate reload becomes missing with scaled hit data")
{
    Fixture f;
    const auto file = writeWav (f.dir, "gone.wav", 1, 48000.0, 100, 0.5f);
    const int wave = f.loadId (file);
    const int slot = f.controller.addHit (0, wave);
    auto props = *f.controller.getHitProperties (0, slot);
    props.startPos = 10;
    props.endPos = 90;
    props.fadeInLength = 5;
    REQUIRE (f.controller.setHitProperties (0, slot, props));
    REQUIRE (file.deleteFile());
    const auto result = f.controller.setSampleRate (96000.0);
    REQUIRE (result.failed());
    REQUIRE (result.getErrorMessage().contains ("gone.wav"));
    REQUIRE (f.controller.getSampleRate() == Approx (96000.0));
    REQUIRE (f.controller.isWaveMissing (wave));
    REQUIRE (f.controller.getWave (wave) == nullptr);
    REQUIRE (f.controller.getWaveFiles (wave) == std::vector<juce::File> { file });
    const auto missing = *f.controller.getHitProperties (0, slot);
    REQUIRE (missing.startPos == 20);
    REQUIRE (missing.endPos == 180);
    REQUIRE (missing.fadeInLength == 10);
    REQUIRE (missing.wave == nullptr);
    const auto replacement = writeWav (f.dir, "resolved.wav", 1, 96000.0, 200, 0.25f);
    REQUIRE (f.controller.replaceWaveFile (wave, replacement).wasOk());
    REQUIRE (f.controller.getWaveFiles (wave) == std::vector<juce::File> { replacement, file });
    REQUIRE (f.controller.getHitProperties (0, slot)->endPos == 180);
}

TEST_CASE ("Controller reports failures and duplicate loads")
{
    Fixture f;
    auto missing = f.controller.loadWave (f.dir.getChildFile ("nope.wav"));
    REQUIRE (missing.status == LoadWaveResult::Status::failed);
    REQUIRE (missing.error.isNotEmpty());

    auto notWav = f.dir.getChildFile ("bad.wav");
    REQUIRE (notWav.replaceWithText ("this is not a wave file"));
    REQUIRE (f.controller.loadWave (notWav).status == LoadWaveResult::Status::failed);

    auto file = writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f);
    auto first = f.controller.loadWave (file);
    auto second = f.controller.loadWave (file);
    REQUIRE (second.status == LoadWaveResult::Status::alreadyLoaded);
    REQUIRE (second.waveId == first.waveId);
    REQUIRE (f.controller.getWaveIds().size() == 1);
}

TEST_CASE ("A hit created through the controller plays in the engine")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));

    DrumProperties props;
    props.note = 36;
    f.controller.setDrumProperties (0, props);
    const int slot = f.controller.addHit (0, wave);
    REQUIRE (slot == 0);
    REQUIRE (f.controller.getHitName (0, slot) == "a.wav");
    REQUIRE (f.controller.getWaveUsageCount (wave) == 1);

    f.controller.sendNote (36, 127);
    f.render (200);
    REQUIRE (f.out[0][0] == Approx (0.5f * center).margin (1e-4));
    REQUIRE (f.out[1][0] == Approx (0.5f * center).margin (1e-4));
    REQUIRE (f.out[0][99] == Approx (0.5f * center).margin (1e-4));
    REQUIRE (f.out[0][100] == 0.0f);
}

TEST_CASE ("Controller auditions the selected hit in message order")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "audition.wav", 1, 48000.0, 100, 0.5f));
    const int slot = f.controller.addHit (0, wave);
    auto props = *f.controller.getHitProperties (0, slot);
    props.volumeDb = -6.0206f;
    props.velocityMax = 1;
    REQUIRE (f.controller.setHitProperties (0, slot, props));
    f.controller.auditionHit (0, slot, 100);
    REQUIRE (f.controller.removeHit (0, slot));
    f.render (8);
    REQUIRE (f.out[0][0] == Approx (0.5f * 0.5f * center * 100.0f / 127.0f).margin (1e-4));
    REQUIRE (f.engine->activeVoiceCount() == 1);
    f.controller.sendAllNotesOff();
    f.render (200);
    REQUIRE (f.engine->activeVoiceCount() == 0);
}

TEST_CASE ("Hits can use a portion of a wave and are limited to 16 per drum")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));

    NewHit portion;
    portion.startPos = 10;
    portion.endPos = 30;
    REQUIRE (f.controller.addHit (0, wave, portion) == 0);
    REQUIRE (f.controller.getHitProperties (0, 0)->endPos == 30);

    NewHit bad;
    bad.startPos = 50;
    bad.endPos = 40;
    REQUIRE (f.controller.addHit (0, wave, bad) == -1);
    bad.startPos = 0;
    bad.endPos = 101;
    REQUIRE (f.controller.addHit (0, wave, bad) == -1);
    REQUIRE (f.controller.addHit (0, 999) == -1);
    REQUIRE (f.controller.addHit (16, wave) == -1);

    for (int i = 1; i < maxHitsPerDrum; ++i)
        REQUIRE (f.controller.addHit (0, wave) == i);
    REQUIRE (f.controller.addHit (0, wave) == -1);
    REQUIRE (f.controller.getHitCount (0) == maxHitsPerDrum);
}

TEST_CASE ("Removed hit slots are reused but the display order stays stable")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    for (int i = 0; i < 3; ++i)
        f.controller.addHit (0, wave);

    REQUIRE (f.controller.removeHit (0, 1));
    REQUIRE_FALSE (f.controller.removeHit (0, 1));
    REQUIRE (f.controller.addHit (0, wave) == 1); // engine slot reused
    REQUIRE (f.controller.getHitSlots (0) == std::vector<int> { 0, 2, 1 }); // but displayed last
}

TEST_CASE ("Setting hit properties validates and clamps")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    f.controller.addHit (0, wave);

    auto props = *f.controller.getHitProperties (0, 0);
    props.startPos = 10;
    props.endPos = 30;
    props.fadeInLength = 15;
    props.fadeOutLength = 15;
    props.velocityMin = 90;
    props.velocityMax = 50;
    REQUIRE (f.controller.setHitProperties (0, 0, props));

    auto stored = *f.controller.getHitProperties (0, 0);
    REQUIRE (stored.fadeInLength == 15);
    REQUIRE (stored.fadeOutLength == 5); // clamped to fit the 20 sample span
    REQUIRE (stored.velocityMax == 90);

    props.endPos = 500;
    REQUIRE_FALSE (f.controller.setHitProperties (0, 0, props));
    REQUIRE_FALSE (f.controller.setHitProperties (0, 5, props));
}

TEST_CASE ("Removing a wave removes its hits and the wave is garbage collected after the engine lets go")
{
    Fixture f;
    const int a = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    const int b = f.loadId (writeWav (f.dir, "b.wav", 1, 48000.0, 100, 0.25f));
    DrumProperties props;
    props.note = 36;
    f.controller.setDrumProperties (0, props);
    f.controller.addHit (0, a);
    f.controller.addHit (0, b);
    f.controller.sendNote (36, 127);
    f.render (10);

    REQUIRE (f.controller.removeWave (a));
    REQUIRE_FALSE (f.controller.removeWave (a));
    REQUIRE_FALSE (f.controller.hasWave (a));
    REQUIRE (f.controller.getHitCount (0) == 1);

    // The engine still holds references (a playing cursor), so the wave stays alive.
    f.controller.collectGarbage();
    f.render (10);
    f.controller.collectGarbage();

    f.render (200); // the cursor finishes
    f.controller.collectGarbage();
    f.render (1);

    f.controller.sendNote (36, 127);
    f.render (10);
    REQUIRE (f.out[0][0] == Approx (0.25f * center).margin (1e-4)); // only b remains
}

TEST_CASE ("Drum output assignments are limited to the four available outputs")
{
    Fixture f;
    DrumProperties props;
    for (int output = 0; output < maxOutputs; ++output)
    {
        props.output = output;
        f.controller.setDrumProperties (0, props);
        REQUIRE (f.controller.getDrumProperties (0).output == output);
    }
    props.output = maxOutputs;
    f.controller.setDrumProperties (0, props);
    REQUIRE (f.controller.getDrumProperties (0).output == maxOutputs - 1);
    props.output = -1;
    f.controller.setDrumProperties (0, props);
    REQUIRE (f.controller.getDrumProperties (0).output == 0);
    REQUIRE (f.controller.getDrumProperties (99).output == 0);
}

TEST_CASE ("Messages that do not fit in the engine queue are retried in order")
{
    // A tiny queue forces the controller to buffer messages.
    juce::TemporaryFile tempDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drumhits_tests_q"));
    auto dir = tempDir.getFile();
    dir.createDirectory();

    Engine engine { 4 };
    Controller controller { engine };
    engine.prepare (48000.0);
    controller.setSampleRate (48000.0);

    auto r = controller.loadWave (writeWav (dir, "a.wav", 1, 48000.0, 100, 0.5f));
    DrumProperties props;
    props.note = 36;
    controller.setDrumProperties (0, props);
    for (int i = 0; i < 3; ++i)
        controller.addHit (0, r.waveId); // 7 messages, queue holds 4

    std::vector<float> l (64), rr (64);
    float* chans[2] = { l.data(), rr.data() };
    for (int i = 0; i < 4; ++i)
    {
        engine.process (chans, 2, 64);
        controller.flushPendingMessages();
    }

    controller.sendNote (36, 127);
    engine.process (chans, 2, 64);
    REQUIRE (l[0] == Approx (3.0f * 0.5f * center).margin (1e-4)); // all three hits arrived

    dir.deleteRecursively();
}

TEST_CASE ("The bundled test waves load and play from two drums")
{
    Fixture f;
    const juce::File testData (DRUMHITS_TESTDATA_DIR);
    const int bass = f.loadId (testData.getChildFile ("bass1.wav"));
    const int stick = f.loadId (testData.getChildFile ("stick1.wav"));
    REQUIRE (f.controller.getWaveLength (bass) > 0);
    REQUIRE (f.controller.getWaveLength (stick) > 0);

    DrumProperties d0, d1;
    d0.note = 36;
    d1.note = 38;
    f.controller.setDrumProperties (0, d0);
    f.controller.setDrumProperties (1, d1);
    REQUIRE (f.controller.addHit (0, bass) == 0);
    REQUIRE (f.controller.addHit (1, stick) == 0);

    auto peakAfterNote = [&] (int note)
    {
        f.controller.sendNote (note, 127);
        f.render (4800);
        float peak = 0.0f;
        for (float s : f.out[0])
            peak = std::max (peak, std::abs (s));
        return peak;
    };

    REQUIRE (peakAfterNote (36) > 0.01f);
    REQUIRE (peakAfterNote (38) > 0.01f);
}

TEST_CASE ("Default drums start at C2 and rise by whole tones")
{
    Fixture f;
    f.controller.initialiseDefaultDrums();
    REQUIRE (f.controller.getDrumProperties (0).note == 36);
    REQUIRE (f.controller.getDrumProperties (1).note == 38);
    REQUIRE (f.controller.getDrumProperties (2).note == 40);
    REQUIRE (f.controller.getDrumProperties (15).note == 66);

    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    f.controller.addHit (1, wave);
    f.controller.sendNote (38, 127);
    f.render (16);
    REQUIRE (f.out[0][0] > 0.1f);
}

TEST_CASE ("Patch state round-trips through save and load")
{
    Fixture f;
    f.controller.initialiseDefaultDrums();
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 1000, 0.5f));

    DrumProperties dp;
    dp.note = 50;
    dp.choke = 2;
    dp.chokedByMask = 0x6;
    dp.pan = -0.5f;
    dp.volumeDb = -3.0f;
    dp.output = 3;
    f.controller.setDrumProperties (3, dp);
    f.controller.setDrumName (3, "Snare");

    NewHit nh;
    nh.startPos = 100;
    nh.endPos = 900;
    const int slot = f.controller.addHit (3, wave, nh);
    auto props = *f.controller.getHitProperties (3, slot);
    props.fadeInLength = 10;
    props.semitones = 2;
    props.velocityMax = 90;
    REQUIRE (f.controller.setHitProperties (3, slot, props));
    f.controller.setHitName (3, slot, "Custom");

    const auto state = f.controller.saveState();

    Engine engine2 { 256 };
    Controller c2 { engine2 };
    c2.setSampleRate (48000.0);
    REQUIRE (c2.loadState (state.getData(), state.getSize()).wasOk());

    REQUIRE_FALSE (readPatchTree (state).hasProperty ("outputCount"));
    REQUIRE (c2.getDrumName (3) == "Snare");
    const auto d = c2.getDrumProperties (3);
    REQUIRE (d.note == 50);
    REQUIRE (d.choke == 2);
    REQUIRE (d.chokedByMask == 0x6);
    REQUIRE (d.pan == Approx (-0.5f));
    REQUIRE (d.output == 3);
    REQUIRE (c2.getHitCount (3) == 1);

    const int s2 = c2.getHitSlots (3)[0];
    const auto h = *c2.getHitProperties (3, s2);
    REQUIRE (h.startPos == 100);
    REQUIRE (h.endPos == 900);
    REQUIRE (h.fadeInLength == 10);
    REQUIRE (h.semitones == 2);
    REQUIRE (h.velocityMax == 90);
    REQUIRE (c2.getHitName (3, s2) == "Custom");
    REQUIRE (c2.getWaveIds().size() == 1);
}

TEST_CASE ("Loading state rescales positions and keeps hits for missing waves")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 1000, 0.5f));
    NewHit nh;
    nh.startPos = 100;
    nh.endPos = 900;
    nh.properties.fadeInLength = 12;
    nh.properties.fadeOutLength = 24;
    nh.properties.volumeDb = -6.0f;
    nh.properties.semitones = 3;
    nh.properties.cents = 25;
    nh.properties.velocityMin = 20;
    nh.properties.velocityMax = 110;
    nh.properties.velocitySensitivity = 70;
    const int originalSlot = f.controller.addHit (0, wave, nh);
    f.controller.setHitName (0, originalSlot, "Saved hit");
    const auto state = f.controller.saveState();
    f.dir.getChildFile ("a.wav").deleteFile();

    Engine engine2 { 256 };
    Controller c2 { engine2 };
    c2.setSampleRate (96000.0);
    REQUIRE (c2.loadState (state.getData(), state.getSize()).wasOk());

    const int id = c2.getWaveIds()[0];
    REQUIRE (c2.isWaveMissing (id));
    REQUIRE (c2.getMissingWaveCount() == 1);
    const auto h = *c2.getHitProperties (0, c2.getHitSlots (0)[0]);
    REQUIRE (h.startPos == 200);
    REQUIRE (h.endPos == 1800);
    REQUIRE (h.wave == nullptr);
    REQUIRE (h.fadeInLength == 24);
    REQUIRE (h.fadeOutLength == 48);
    REQUIRE (h.volumeDb == Approx (-6.0f));
    REQUIRE (h.semitones == 3);
    REQUIRE (h.cents == 25);
    REQUIRE (h.velocityMin == 20);
    REQUIRE (h.velocityMax == 110);
    REQUIRE (h.velocitySensitivity == 70);
    REQUIRE (c2.getHitName (0, c2.getHitSlots (0)[0]) == "Saved hit");
    REQUIRE (c2.getWave (id) == nullptr);
    REQUIRE (c2.getWaveLength (id) > 0);
    REQUIRE_FALSE (c2.normalizeHit (0, c2.getHitSlots (0)[0]));

    DrumProperties drum;
    drum.note = 36;
    c2.setDrumProperties (0, drum);
    c2.sendNote (36, 127);
    c2.previewWave (id, 0, c2.getWaveLength (id));
    c2.auditionHit (0, c2.getHitSlots (0)[0], 100);
    std::array<float, 32> left {}, right {};
    float* channels[] { left.data(), right.data() };
    engine2.process (channels, 2, static_cast<int> (left.size()));
    REQUIRE (std::all_of (left.begin(), left.end(), [] (float sample) { return sample == 0.0f; }));
    REQUIRE (std::all_of (right.begin(), right.end(), [] (float sample) { return sample == 0.0f; }));

    SavePatchOptions saveOptions;
    REQUIRE (c2.savePatch (f.dir.getChildFile ("missing.dhpatch"), saveOptions).failed());

    const auto resolvedFile = writeWav (f.dir, "resolved.wav", 1, 96000.0, 1000, 0.3f);
    REQUIRE (c2.replaceWaveFile (id, resolvedFile).wasOk());
    REQUIRE (c2.getWaveFiles (id) == std::vector<juce::File> { resolvedFile, f.dir.getChildFile ("a.wav") });
    REQUIRE (c2.getMissingWaveCount() == 0);
    REQUIRE_FALSE (c2.isWaveMissing (id));
    REQUIRE (c2.getHitWaveId (0, c2.getHitSlots (0)[0]) == id);
    const auto resolvedHit = *c2.getHitProperties (0, c2.getHitSlots (0)[0]);
    REQUIRE (resolvedHit.wave == c2.getWave (id));
    REQUIRE (resolvedHit.startPos == 200);
    REQUIRE (resolvedHit.endPos == 1800);
    REQUIRE (resolvedHit.fadeInLength == 24);
    REQUIRE (resolvedHit.fadeOutLength == 48);
    REQUIRE (resolvedHit.volumeDb == Approx (-6.0f));
    REQUIRE (c2.savePatch (f.dir.getChildFile ("resolved.dhpatch"), saveOptions).wasOk());

    // Existing state is untouched by invalid data.
    const char junk[] = "not a patch";
    REQUIRE (c2.loadState (junk, sizeof (junk)).failed());
    REQUIRE (c2.getHitCount (0) == 1);
}

TEST_CASE ("Duplicate numbers the copy after the highest suffix")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    const int a = f.controller.addHit (0, wave);
    f.controller.setHitName (0, a, "Snare");

    const int b = f.controller.duplicateHit (0, a);
    REQUIRE (f.controller.getHitName (0, b) == "Snare (2)");
    const int c = f.controller.duplicateHit (0, a);
    REQUIRE (f.controller.getHitName (0, c) == "Snare (3)");
    const int d = f.controller.duplicateHit (0, b);
    REQUIRE (f.controller.getHitName (0, d) == "Snare (4)");
    REQUIRE (f.controller.getHitCount (0) == 4);
}

TEST_CASE ("Normalize sets the volume so the hit portion peaks at 0 dB")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    const int slot = f.controller.addHit (0, wave);
    REQUIRE (f.controller.normalizeHit (0, slot));
    REQUIRE (f.controller.getHitProperties (0, slot)->volumeDb == Approx (6.0206f).margin (0.01));
}

TEST_CASE ("Hits can be moved up and down")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    const int a = f.controller.addHit (0, wave);
    const int b = f.controller.addHit (0, wave);
    const int c = f.controller.addHit (0, wave);

    REQUIRE_FALSE (f.controller.moveHits (0, { a }, true));
    REQUIRE (f.controller.moveHits (0, { c }, true));
    REQUIRE (f.controller.getHitSlots (0) == std::vector<int> { a, c, b });
    REQUIRE (f.controller.moveHits (0, { a, c }, false));
    REQUIRE (f.controller.getHitSlots (0) == std::vector<int> { b, a, c });
    REQUIRE_FALSE (f.controller.moveHits (0, { a, c }, false));
}

TEST_CASE ("Velocity split gives the first hit the highest band")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f));
    const int a = f.controller.addHit (0, wave);
    const int b = f.controller.addHit (0, wave);
    const int c = f.controller.addHit (0, wave);
    f.controller.splitVelocityRange (0);

    const auto range = [&] (int slot)
    {
        const auto p = *f.controller.getHitProperties (0, slot);
        return std::make_pair (p.velocityMin, p.velocityMax);
    };
    REQUIRE (range (a) == std::make_pair (85, 127));
    REQUIRE (range (b) == std::make_pair (42, 84));
    REQUIRE (range (c) == std::make_pair (0, 41));
}

TEST_CASE ("Patch files resolve relative and fallback paths in order")
{
    Fixture f;
    const auto wavFile = writeWav (f.dir, "a.wav", 1, 48000.0, 1000, 0.5f);
    const int wave = f.loadId (wavFile);
    f.controller.addHit (0, wave);

    SavePatchOptions options;
    options.consolidate = true;
    const auto patch = f.dir.getChildFile ("sub").getChildFile ("p.dhpatch");
    REQUIRE (f.dir.getChildFile ("sub").createDirectory());
    REQUIRE (f.controller.savePatch (patch, options).wasOk());

    const auto copied = f.dir.getChildFile ("sub").getChildFile ("p_dh_data").getChildFile ("a.wav");
    REQUIRE (copied.existsAsFile());
    REQUIRE (f.controller.getWaveFile (wave) == copied);

    Engine engine2 { 256 };
    Controller c2 { engine2 };
    c2.setSampleRate (48000.0);
    REQUIRE (c2.loadPatch (patch).wasOk());
    REQUIRE (c2.getWaveFile (c2.getWaveIds()[0]) == copied);
    REQUIRE (c2.getHitCount (0) == 1);

    // First path missing: the second one loads and is promoted, the first is kept as a fallback.
    const auto gone = f.dir.getChildFile ("gone.wav");
    juce::ValueTree tree ("Patch");
    juce::ValueTree w ("Wave");
    w.setProperty ("id", 1, nullptr);
    for (const auto& path : { gone.getFullPathName(), juce::String ("sub/p_dh_data/a.wav") })
    {
        juce::ValueTree node ("Path");
        node.setProperty ("value", path, nullptr);
        w.appendChild (node, nullptr);
    }
    tree.appendChild (w, nullptr);
    juce::MemoryBlock block;
    juce::MemoryOutputStream out (block, false);
    out.writeInt (0x44485054);
    out.writeInt (1);
    tree.writeToStream (out);

    Engine engine3 { 256 };
    Controller c3 { engine3 };
    c3.setSampleRate (48000.0);
    REQUIRE (c3.loadState (block.getData(), block.getSize(), {}, f.dir).wasOk());
    const auto files = c3.getWaveFiles (c3.getWaveIds()[0]);
    REQUIRE (! c3.isWaveMissing (c3.getWaveIds()[0]));
    REQUIRE (files.size() == 2);
    REQUIRE (files[0] == copied);
    REQUIRE (files[1] == gone);

    // Host state keeps every absolute path.
    const auto state = c3.saveState();
    const auto hostTree = readPatchTree (state);
    for (const auto& node : hostTree)
        if (node.hasType ("Wave"))
            for (const auto& path : node)
                if (path.hasType ("Path"))
                    REQUIRE (juce::File::isAbsolutePath (path.getProperty ("value").toString()));

    Engine engine4 { 256 };
    Controller c4 { engine4 };
    c4.setSampleRate (48000.0);
    REQUIRE (c4.loadState (state.getData(), state.getSize()).wasOk());
    REQUIRE (c4.getWaveFiles (c4.getWaveIds()[0]) == files);

    const auto fallbackDir = f.dir.getChildFile ("fallback");
    REQUIRE (fallbackDir.createDirectory());
    const auto badCandidate = f.dir.getChildFile ("bad.wav");
    REQUIRE (badCandidate.replaceWithText ("not a wave"));
    const auto fallbackFile = writeWav (fallbackDir, "bad.wav", 1, 48000.0, 100, 0.4f);
    juce::ValueTree fallbackPatch ("Patch");
    juce::ValueTree fallbackWave ("Wave");
    fallbackWave.setProperty ("id", 1, nullptr);
    fallbackWave.setProperty ("length", 100, nullptr);
    juce::ValueTree badPath ("Path");
    badPath.setProperty ("value", "bad.wav", nullptr);
    fallbackWave.appendChild (badPath, nullptr);
    fallbackPatch.appendChild (fallbackWave, nullptr);
    juce::MemoryBlock fallbackState;
    juce::MemoryOutputStream fallbackOut (fallbackState, false);
    fallbackOut.writeInt (0x44485054);
    fallbackOut.writeInt (1);
    fallbackPatch.writeToStream (fallbackOut);

    Engine engine5 { 256 };
    Controller c5 { engine5 };
    c5.setSampleRate (48000.0);
    REQUIRE (c5.loadState (fallbackState.getData(), fallbackState.getSize(), fallbackDir, f.dir).wasOk());
    const auto fallbackPaths = c5.getWaveFiles (c5.getWaveIds()[0]);
    REQUIRE (fallbackPaths == std::vector<juce::File> { fallbackFile, badCandidate });
    writeWav (f.dir, "bad.wav", 1, 48000.0, 100, 0.6f);
    REQUIRE (fallbackFile.deleteFile());
    REQUIRE (c5.setSampleRate (96000.0).wasOk());
    REQUIRE (c5.getWaveFiles (c5.getWaveIds()[0]) == std::vector<juce::File> { badCandidate, fallbackFile });
    REQUIRE (c5.getWaveLength (c5.getWaveIds()[0]) == 200);
}

TEST_CASE ("Save patch rejects missing waves, names collisions and defers state changes")
{
    Fixture f;
    const auto sub = f.dir.getChildFile ("other");
    REQUIRE (sub.createDirectory());
    const int a = f.loadId (writeWav (f.dir, "same.wav", 1, 48000.0, 500, 0.5f));
    const int b = f.loadId (writeWav (sub, "same.wav", 1, 48000.0, 500, 0.25f));
    f.controller.addHit (0, a);

    const auto patch = f.dir.getChildFile ("kit.dhpatch");

    SavePatchOptions options;
    options.consolidate = true;
    options.purgeUnused = true;
    // Failing to write leaves the controller untouched.
    const auto blocked = f.dir.getChildFile ("blocked");
    REQUIRE (blocked.createDirectory());
    REQUIRE (f.controller.savePatch (blocked, options).failed());
    REQUIRE (f.controller.getWaveFile (a) == f.dir.getChildFile ("same.wav"));
    REQUIRE (f.controller.getWaveIds().size() == 2);
    REQUIRE (f.dir.getChildFile ("blocked_dh_data").getChildFile ("same.wav").existsAsFile());

    options.purgeUnused = false;
    REQUIRE (f.controller.savePatch (patch, options).wasOk());
    REQUIRE (f.dir.getChildFile ("kit_dh_data").getChildFile ("same.wav").existsAsFile());
    REQUIRE (f.dir.getChildFile ("kit_dh_data").getChildFile ("same_1.wav").existsAsFile());

    auto patchData = juce::MemoryBlock();
    REQUIRE (patch.loadFileAsData (patchData));
    const auto patchTree = readPatchTree (patchData);
    int savedWaves = 0;
    for (const auto& node : patchTree)
    {
        if (! node.hasType ("Wave"))
            continue;
        ++savedWaves;
        REQUIRE (node.getNumChildren() == 1);
        const auto relativePath = node.getChild (0).getProperty ("value").toString();
        REQUIRE_FALSE (juce::File::isAbsolutePath (relativePath));
        REQUIRE (patch.getParentDirectory().getChildFile (relativePath).existsAsFile());
    }
    REQUIRE (savedWaves == 2);

    options.purgeUnused = true;
    REQUIRE (f.controller.savePatch (patch, options).wasOk());
    REQUIRE (f.controller.getWaveIds().size() == 1);
    REQUIRE (f.controller.hasWave (a));
    REQUIRE (! f.controller.hasWave (b));

    f.controller.getWaveFile (a).deleteFile();
    REQUIRE (f.controller.savePatch (patch, options).failed());
}

TEST_CASE ("Consolidation copy failure leaves controller unchanged and keeps earlier copies")
{
    Fixture f;
    const auto aFile = writeWav (f.dir, "a.wav", 1, 48000.0, 100, 0.5f);
    const auto bFile = writeWav (f.dir, "b.wav", 1, 48000.0, 100, 0.25f);
    const int a = f.loadId (aFile);
    const int b = f.loadId (bFile);
    const auto patch = f.dir.getChildFile ("copy_failure.dhpatch");
    const auto dataDir = f.dir.getChildFile ("copy_failure_dh_data");
    REQUIRE (dataDir.createDirectory());
    const auto blockedTarget = dataDir.getChildFile ("b.wav");
    REQUIRE (blockedTarget.createDirectory());
    REQUIRE (blockedTarget.getChildFile ("keep").replaceWithText ("prevent directory replacement"));

    SavePatchOptions options;
    options.consolidate = true;
    REQUIRE (f.controller.savePatch (patch, options).failed());
    REQUIRE (dataDir.getChildFile ("a.wav").existsAsFile());
    REQUIRE (f.controller.getWaveFile (a) == aFile);
    REQUIRE (f.controller.getWaveFile (b) == bFile);
    REQUIRE_FALSE (patch.existsAsFile());
}

TEST_CASE ("Replacing a wave preserves identity, ranges and custom hit names")
{
    Fixture f;
    const auto original = writeWav (f.dir, "original.wav", 1, 48000.0, 100, 0.5f);
    const auto longer = writeWav (f.dir, "longer.wav", 1, 48000.0, 200, 0.25f);
    const auto shorter = writeWav (f.dir, "shorter.wav", 1, 48000.0, 50, 0.75f);
    const int wave = f.loadId (original);
    NewHit hit;
    hit.startPos = 20;
    hit.endPos = 90;
    const int slot = f.controller.addHit (0, wave, hit);
    f.controller.setHitName (0, slot, "A custom hit");
    const auto oldWave = f.controller.getWave (wave);

    REQUIRE (f.controller.replaceWaveFile (wave, longer).wasOk());
    REQUIRE (f.controller.getHitWaveId (0, slot) == wave);
    REQUIRE (f.controller.getWave (wave) != oldWave);
    REQUIRE (f.controller.getWaveLength (wave) == 200);
    REQUIRE (f.controller.getWaveFiles (wave) == std::vector<juce::File> { longer });
    REQUIRE (f.controller.getHitProperties (0, slot)->startPos == 20);
    REQUIRE (f.controller.getHitProperties (0, slot)->endPos == 90);
    REQUIRE (f.controller.getHitProperties (0, slot)->wave == f.controller.getWave (wave));
    REQUIRE (f.controller.getHitName (0, slot) == "A custom hit");

    DrumProperties drum;
    drum.note = 36;
    f.controller.setDrumProperties (0, drum);
    f.controller.sendNote (36, 127);
    f.render (10);
    REQUIRE (f.out[0][0] == Approx (0.25f * center).margin (1e-4));

    REQUIRE (f.controller.replaceWaveFile (wave, shorter).wasOk());
    REQUIRE (f.controller.getWaveLength (wave) == 90);
    REQUIRE (f.controller.getWaveFiles (wave) == std::vector<juce::File> { shorter });
    REQUIRE (f.controller.getHitProperties (0, slot)->startPos == 20);
    REQUIRE (f.controller.getHitProperties (0, slot)->endPos == 90);
    REQUIRE (f.controller.getWave (wave)->leftData()[60] == 0.0f);

    const auto oldPaths = f.controller.getWaveFiles (wave);
    REQUIRE (f.controller.replaceWaveFile (wave, f.dir.getChildFile ("not_a_wave.wav")).failed());
    REQUIRE (f.controller.getWaveFiles (wave) == oldPaths);
    REQUIRE (f.controller.getHitWaveId (0, slot) == wave);
}

TEST_CASE ("Renaming a wave updates default hit names but preserves custom names")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "drum.wav", 1, 48000.0, 100, 0.5f));
    const int defaultSlot = f.controller.addHit (0, wave);
    const int customSlot = f.controller.addHit (0, wave);
    f.controller.setHitName (0, customSlot, "My hit");

    REQUIRE (f.controller.renameWave (wave, "Kick"));
    REQUIRE (f.controller.getWaveName (wave) == "Kick");
    REQUIRE (f.controller.getHitName (0, defaultSlot) == "Kick");
    REQUIRE (f.controller.getHitName (0, customSlot) == "My hit");

    REQUIRE (f.controller.renameWave (wave, {}));
    REQUIRE (f.controller.getWaveName (wave) == "drum.wav");
    REQUIRE (f.controller.getHitName (0, defaultSlot) == "drum.wav");
    REQUIRE_FALSE (f.controller.renameWave (999, "Missing"));
}

TEST_CASE ("Selecting a different hit wave uses its full range and preserves other properties")
{
    Fixture f;
    const int original = f.loadId (writeWav (f.dir, "original.wav", 1, 48000.0, 100, 0.5f));
    const int shorter = f.loadId (writeWav (f.dir, "shorter.wav", 1, 48000.0, 40, 0.25f));
    const int longer = f.loadId (writeWav (f.dir, "longer.wav", 1, 48000.0, 200, 0.75f));
    const int slot = f.controller.addHit (0, original);
    auto properties = *f.controller.getHitProperties (0, slot);
    properties.startPos = 20;
    properties.endPos = 90;
    properties.fadeInLength = 30;
    properties.fadeOutLength = 30;
    properties.volumeDb = -4.0f;
    properties.semitones = 3;
    REQUIRE (f.controller.setHitProperties (0, slot, properties));
    f.controller.setHitName (0, slot, "Custom name");

    REQUIRE (f.controller.setHitWave (0, slot, original));
    REQUIRE (f.controller.getHitProperties (0, slot)->startPos == 20);
    REQUIRE (f.controller.getHitProperties (0, slot)->endPos == 90);
    REQUIRE (f.controller.getHitProperties (0, slot)->fadeOutLength == 30);
    REQUIRE_FALSE (f.controller.setHitWave (0, slot, -1));
    REQUIRE (f.controller.getHitWaveId (0, slot) == original);

    REQUIRE (f.controller.setHitWave (0, slot, shorter));
    const auto updated = *f.controller.getHitProperties (0, slot);
    REQUIRE (f.controller.getHitWaveId (0, slot) == shorter);
    REQUIRE (updated.startPos == 0);
    REQUIRE (updated.endPos == 40);
    REQUIRE (updated.fadeInLength == 30);
    REQUIRE (updated.fadeOutLength == 10);
    REQUIRE (updated.volumeDb == Approx (-4.0f));
    REQUIRE (updated.semitones == 3);
    REQUIRE (updated.wave == f.controller.getWave (shorter));
    REQUIRE (f.controller.getHitName (0, slot) == "Custom name");

    REQUIRE (f.controller.setHitWave (0, slot, longer));
    const auto longerProperties = *f.controller.getHitProperties (0, slot);
    REQUIRE (longerProperties.startPos == 0);
    REQUIRE (longerProperties.endPos == 200);
    REQUIRE (longerProperties.fadeInLength == 30);
    REQUIRE (longerProperties.fadeOutLength == 10);
    REQUIRE (longerProperties.wave == f.controller.getWave (longer));
}

TEST_CASE ("Drum duplication copies settings and hits to empty targets")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "hit.wav", 1, 48000.0, 100, 0.5f));
    f.controller.setDrumName (0, "Kick");
    DrumProperties properties;
    properties.note = 42;
    properties.choke = 3;
    properties.chokedByMask = 1u << 2;
    properties.volumeDb = -6.0f;
    f.controller.setDrumProperties (0, properties);
    const int sourceSlot = f.controller.addHit (0, wave);
    f.controller.setHitName (0, sourceSlot, "Low kick");
    for (int i = 1; i < maxHitsPerDrum; ++i)
        REQUIRE (f.controller.addHit (0, wave) >= 0);

    REQUIRE (f.controller.duplicateDrum (0, 1));
    REQUIRE (f.controller.getDrumName (1) == "Kick copy");
    REQUIRE (f.controller.getDrumProperties (1).note == 42);
    REQUIRE (f.controller.getDrumProperties (1).choke == 3);
    REQUIRE (f.controller.getDrumProperties (1).chokedByMask == (1u << 2));
    REQUIRE (f.controller.getDrumProperties (1).volumeDb == Approx (-6.0f));
    REQUIRE (f.controller.getHitCount (1) == maxHitsPerDrum);
    const int copiedSlot = f.controller.getHitSlots (1).front();
    REQUIRE (f.controller.getHitWaveId (1, copiedSlot) == wave);
    REQUIRE (f.controller.getHitName (1, copiedSlot) == "Low kick");

    REQUIRE_FALSE (f.controller.duplicateDrum (0, 1));
    REQUIRE (f.controller.getHitCount (1) == maxHitsPerDrum);

    f.controller.setDrumName (2, "Aux");
    DrumProperties emptyDrumProperties;
    emptyDrumProperties.note = 54;
    emptyDrumProperties.volumeDb = -3.0f;
    f.controller.setDrumProperties (2, emptyDrumProperties);
    REQUIRE (f.controller.duplicateDrum (2, 3));
    REQUIRE (f.controller.getDrumName (3) == "Aux copy");
    REQUIRE (f.controller.getDrumProperties (3).note == 54);
    REQUIRE (f.controller.getDrumProperties (3).volumeDb == Approx (-3.0f));
    REQUIRE (f.controller.getHitCount (3) == 0);
}

TEST_CASE ("Resetting a drum removes hits and restores all defaults")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "hit.wav", 1, 48000.0, 100, 0.5f));
    f.controller.setDefaultDrumNotes ({ 38, 40 });

    DrumProperties properties;
    properties.note = 55;
    properties.output = 2;
    properties.playMode = PlayMode::randomRoundRobin;
    properties.choke = 4;
    properties.chokedByMask = 0xff;
    properties.volumeDb = -8.0f;
    properties.pan = 0.5f;
    properties.maxPoly = 3;
    f.controller.setDrumName (1, "Custom");
    f.controller.setDrumProperties (1, properties);
    REQUIRE (f.controller.addHit (1, wave) >= 0);

    REQUIRE (f.controller.resetDrum (1));
    REQUIRE (f.controller.getHitCount (1) == 0);
    REQUIRE (f.controller.getHitSlots (1).empty());
    REQUIRE (f.controller.getDrumName (1).isEmpty());
    const auto reset = f.controller.getDrumProperties (1);
    REQUIRE (reset.note == 40);
    REQUIRE (reset.output == 0);
    REQUIRE (reset.playMode == PlayMode::regular);
    REQUIRE (reset.choke == 0);
    REQUIRE (reset.chokedByMask == 0);
    REQUIRE (reset.volumeDb == 0.0f);
    REQUIRE (reset.pan == 0.0f);
    REQUIRE (reset.maxPoly == 0);
    REQUIRE_FALSE (f.controller.resetDrum (maxDrums));
}

TEST_CASE ("Copying hits stops when target drum reaches capacity")
{
    Fixture f;
    const int wave = f.loadId (writeWav (f.dir, "hit.wav", 1, 48000.0, 100, 0.5f));
    const int first = f.controller.addHit (0, wave);
    const int second = f.controller.addHit (0, wave);
    for (int i = 0; i < maxHitsPerDrum - 1; ++i)
        REQUIRE (f.controller.addHit (1, wave) >= 0);

    REQUIRE (f.controller.copyHit (0, first, 1) >= 0);
    REQUIRE (f.controller.copyHit (0, second, 1) == -1);
    REQUIRE (f.controller.getHitCount (1) == maxHitsPerDrum);
}

TEST_CASE ("Wave Pool sorting orders every column in both directions")
{
    using namespace drumhits::ui;
    const std::vector<WavePoolSortEntry> input {
        { 1, "wave10", "/b.wav", 2, true },
        { 2, "Wave2", "/c.wav", 10, false },
        { 3, "wave1", "/a.wav", 0, false }
    };
    const auto sortedIds = [&input] (WavePoolSortColumn column, bool forwards)
    {
        auto entries = input;
        sortWavePoolEntries (entries, column, forwards);
        std::vector<int> ids;
        for (const auto& entry : entries)
            ids.push_back (entry.id);
        return ids;
    };

    REQUIRE (sortedIds (WavePoolSortColumn::none, true) == std::vector<int> { 1, 2, 3 });
    REQUIRE (sortedIds (WavePoolSortColumn::name, true) == std::vector<int> { 3, 2, 1 });
    REQUIRE (sortedIds (WavePoolSortColumn::name, false) == std::vector<int> { 1, 2, 3 });
    REQUIRE (sortedIds (WavePoolSortColumn::path, true) == std::vector<int> { 3, 1, 2 });
    REQUIRE (sortedIds (WavePoolSortColumn::path, false) == std::vector<int> { 2, 1, 3 });
    REQUIRE (sortedIds (WavePoolSortColumn::usage, true) == std::vector<int> { 3, 1, 2 });
    REQUIRE (sortedIds (WavePoolSortColumn::usage, false) == std::vector<int> { 2, 1, 3 });
    REQUIRE (sortedIds (WavePoolSortColumn::status, true) == std::vector<int> { 2, 3, 1 });
    REQUIRE (sortedIds (WavePoolSortColumn::status, false) == std::vector<int> { 1, 2, 3 });
}

TEST_CASE ("Purge unused waves removes only unreferenced waves")
{
    Fixture f;
    const int used = f.loadId (writeWav (f.dir, "used.wav", 1, 48000.0, 100, 0.5f));
    const int unused = f.loadId (writeWav (f.dir, "unused.wav", 1, 48000.0, 100, 0.25f));
    f.controller.addHit (0, used);

    REQUIRE (f.controller.purgeUnusedWaves() == 1);
    REQUIRE (f.controller.hasWave (used));
    REQUIRE_FALSE (f.controller.hasWave (unused));
}
