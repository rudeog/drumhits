#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "core/engine/Engine.h"
#include "core/engine/SpscQueue.h"

using namespace drumhits;
using Catch::Approx;

namespace
{
constexpr float center = 0.70710678f;

struct Fixture
{
    // Waves are declared first so they outlive the engine, which releases its references on destruction.
    std::vector<std::unique_ptr<Wave>> waves;
    Engine engine { 256 };
    std::vector<std::vector<float>> out = std::vector<std::vector<float>> (2 * maxOutputs);

    Fixture() { engine.prepare (44100.0); }

    Wave* makeWave (float value, int numSamples)
    {
        waves.push_back (std::make_unique<Wave> (std::vector<float> (static_cast<size_t> (numSamples), value),
                                                 std::vector<float> (static_cast<size_t> (numSamples), value)));
        return waves.back().get();
    }

    void configureDrum (int drum, int note, DrumProperties props = {})
    {
        props.note = note;
        REQUIRE (engine.pushMessage (Message::makeSetDrumProperties (drum, props)));
    }

    void addHit (int drum, int hit, Wave* wave, HitProperties props = {})
    {
        props.wave = wave;
        props.startPos = 0;
        props.endPos = wave->numSamples();
        wave->addRef();
        REQUIRE (engine.pushMessage (Message::makeCreateHit (drum, hit)));
        REQUIRE (engine.pushMessage (Message::makeSetHitProperties (drum, hit, props)));
    }

    void noteOn (int note, int velocity = 127) { REQUIRE (engine.pushMessage (Message::makeNoteOn (note, velocity))); }

    void render (int numSamples, const std::vector<NoteEvent>& events = {})
    {
        std::vector<float*> ptrs;
        for (auto& ch : out)
        {
            ch.assign (static_cast<size_t> (numSamples), 0.0f);
            ptrs.push_back (ch.data());
        }
        engine.process (ptrs.data(), static_cast<int> (ptrs.size()), numSamples, events.data(), static_cast<int> (events.size()));
    }
};
} // namespace

TEST_CASE ("SpscQueue preserves order and reports full/empty")
{
    SpscQueue<int> q (3);
    int v = 0;
    REQUIRE_FALSE (q.pop (v));

    int pushed = 0;
    while (q.push (pushed))
        ++pushed;
    REQUIRE (pushed >= 3);
    REQUIRE_FALSE (q.push (99));

    for (int i = 0; i < pushed; ++i)
    {
        REQUIRE (q.pop (v));
        REQUIRE (v == i);
    }
    REQUIRE_FALSE (q.pop (v));
}

TEST_CASE ("A note plays its hit at center pan with -3 dB and then finishes")
{
    Fixture f;
    f.configureDrum (0, 36);
    f.addHit (0, 0, f.makeWave (1.0f, 100));
    f.noteOn (36);
    f.render (64);

    REQUIRE (f.out[0][0] == Approx (center));
    REQUIRE (f.out[1][0] == Approx (center));
    REQUIRE (f.out[0][63] == Approx (center));
    REQUIRE (f.engine.activeVoiceCount() == 1);

    f.render (64);
    REQUIRE (f.out[0][35] == Approx (center));
    REQUIRE (f.out[0][36] == 0.0f);
    REQUIRE (f.engine.activeVoiceCount() == 0);
}

TEST_CASE ("Pan and volume are applied")
{
    Fixture f;
    DrumProperties d;
    d.pan = -1.0f;
    d.volumeDb = -6.0206f;
    f.configureDrum (0, 36, d);
    f.addHit (0, 0, f.makeWave (1.0f, 100));
    f.noteOn (36);
    f.render (8);
    REQUIRE (f.out[0][0] == Approx (0.5f).margin (1e-3));
    REQUIRE (f.out[1][0] == Approx (0.0f).margin (1e-6));
}

TEST_CASE ("Hit audition renders the selected hit like a note")
{
    Fixture note;
    Fixture audition;
    for (auto* f : { &note, &audition })
    {
        DrumProperties drum;
        drum.output = 1;
        drum.pan = 0.4f;
        drum.volumeDb = -3.0f;
        f->configureDrum (0, 36, drum);
        std::vector<float> samples;
        for (int i = 0; i < 100; ++i)
            samples.push_back (static_cast<float> (i) / 100.0f);
        f->waves.push_back (std::make_unique<Wave> (samples, samples));
        auto* wave = f->waves.back().get();
        f->addHit (0, 0, wave);

        HitProperties hit;
        hit.wave = wave;
        hit.startPos = 10;
        hit.endPos = 80;
        hit.fadeInLength = 8;
        hit.fadeOutLength = 12;
        hit.volumeDb = -4.0f;
        hit.semitones = 7;
        hit.cents = 25;
        hit.velocitySensitivity = 70;
        wave->addRef();
        REQUIRE (f->engine.pushMessage (Message::makeSetHitProperties (0, 0, hit)));
    }

    note.noteOn (36, 100);
    REQUIRE (audition.engine.pushMessage (Message::makeAuditionHit (0, 0, 100)));
    note.render (80);
    audition.render (80);
    for (size_t channel = 0; channel < note.out.size(); ++channel)
        for (size_t sample = 0; sample < note.out[channel].size(); ++sample)
            REQUIRE (audition.out[channel][sample] == Approx (note.out[channel][sample]).margin (1e-6));
    REQUIRE (audition.out[2][10] > 0.0f);
    REQUIRE (audition.out[0][10] == 0.0f);
    REQUIRE (audition.engine.noteCounter() == 0);
}

TEST_CASE ("Hit audition bypasses hit selection and is isolated from choke and drum polyphony")
{
    Fixture f;
    DrumProperties d;
    d.playMode = PlayMode::randomRoundRobin;
    d.choke = 1;
    d.chokedByMask = 1u << 1;
    d.maxPoly = 1;
    f.configureDrum (0, 36, d);
    HitProperties selected;
    selected.velocityMax = 20;
    selected.velocitySensitivity = 0;
    f.addHit (0, 0, f.makeWave (0.2f, 10000), selected);
    HitProperties other;
    other.velocityMin = 21;
    other.velocitySensitivity = 0;
    f.addHit (0, 1, f.makeWave (0.4f, 10000), other);

    f.noteOn (36, 100);
    f.render (16);
    REQUIRE (f.out[0][15] == Approx (0.4f * center));
    REQUIRE (f.engine.pushMessage (Message::makeAuditionHit (0, 0, 100)));
    f.render (200);
    REQUIRE (f.engine.activeVoiceCount() == 2);
    REQUIRE (f.out[0][199] == Approx (0.6f * center));

    f.noteOn (36, 100);
    f.render (200);
    REQUIRE (f.engine.activeVoiceCount() == 2);
    REQUIRE (f.out[0][199] == Approx (0.6f * center));

    REQUIRE (f.engine.pushMessage (Message::makeAllNotesOff()));
    f.render (200);
    REQUIRE (f.engine.activeVoiceCount() == 0);
}

TEST_CASE ("Hit audition does not advance round robin")
{
    Fixture baseline;
    Fixture audition;
    for (auto* f : { &baseline, &audition })
    {
        DrumProperties d;
        d.playMode = PlayMode::randomRoundRobin;
        f->configureDrum (0, 36, d);
        f->addHit (0, 0, f->makeWave (0.2f, 20));
        f->addHit (0, 1, f->makeWave (0.4f, 20));
    }

    REQUIRE (audition.engine.pushMessage (Message::makeAuditionHit (0, 0, 127)));
    audition.render (20);
    for (int i = 0; i < 2; ++i)
    {
        baseline.noteOn (36);
        audition.noteOn (36);
        baseline.render (20);
        audition.render (20);
        REQUIRE (audition.out[0][0] == Approx (baseline.out[0][0]));
    }
}

TEST_CASE ("Notes on unassigned notes or hits with no wave do nothing")
{
    Fixture f;
    f.configureDrum (0, 36);
    REQUIRE (f.engine.pushMessage (Message::makeCreateHit (0, 0)));
    f.noteOn (36);
    f.noteOn (40);
    f.render (16);
    REQUIRE (f.engine.activeVoiceCount() == 0);
    REQUIRE (f.engine.lastNote() == 40);
    REQUIRE (f.engine.noteCounter() == 2);

    auto received = f.engine.takeReceivedNotes();
    REQUIRE (received[0] == ((uint64_t { 1 } << 36) | (uint64_t { 1 } << 40)));
    REQUIRE (received[1] == 0);
    received = f.engine.takeReceivedNotes();
    REQUIRE (received[0] == 0);
    REQUIRE (f.engine.noteCounter() == 2); // the counter is never reset
}

TEST_CASE ("Velocity range selects hits and regular mode layers all matches")
{
    Fixture f;
    f.configureDrum (0, 36);
    HitProperties low;
    low.velocityMax = 63;
    low.velocitySensitivity = 0;
    HitProperties high;
    high.velocityMin = 64;
    high.velocitySensitivity = 0;
    HitProperties all;
    all.velocitySensitivity = 0;
    f.addHit (0, 0, f.makeWave (0.1f, 100), low);
    f.addHit (0, 1, f.makeWave (0.2f, 100), high);
    f.addHit (0, 2, f.makeWave (0.4f, 100), all);

    f.noteOn (36, 30);
    f.render (4);
    REQUIRE (f.out[0][0] == Approx ((0.1f + 0.4f) * center));

    f.render (200);
    f.noteOn (36, 100);
    f.render (4);
    REQUIRE (f.out[0][0] == Approx ((0.2f + 0.4f) * center));
}

TEST_CASE ("Random round robin plays every hit once before repeating")
{
    Fixture f;
    f.engine.seedRandom (1234);
    DrumProperties d;
    d.playMode = PlayMode::randomRoundRobin;
    f.configureDrum (0, 36, d);
    const float values[] = { 0.1f, 0.2f, 0.4f };
    for (int i = 0; i < 3; ++i)
        f.addHit (0, i, f.makeWave (values[i], 4));

    for (int round = 0; round < 3; ++round)
    {
        std::vector<float> seen;
        for (int i = 0; i < 3; ++i)
        {
            f.noteOn (36);
            f.render (8);
            seen.push_back (f.out[0][0] / center);
        }
        for (float v : values)
        {
            int count = 0;
            for (float s : seen)
                count += (s == Approx (v)) ? 1 : 0;
            REQUIRE (count == 1);
        }
    }
}

TEST_CASE ("Random round robin cycles each velocity layer independently")
{
    Fixture f;
    f.engine.seedRandom (99);
    DrumProperties d;
    d.playMode = PlayMode::randomRoundRobin;
    f.configureDrum (0, 36, d);
    const float values[] = { 0.1f, 0.2f, 0.4f, 0.8f };
    for (int i = 0; i < 4; ++i)
    {
        HitProperties h;
        h.velocitySensitivity = 0;
        if (i < 2)
            h.velocityMax = 99;
        else
            h.velocityMin = 100;
        f.addHit (0, i, f.makeWave (values[i] / 2, 4), h);
    }

    auto play = [&] (int velocity)
    {
        f.noteOn (36, velocity);
        f.render (8);
        return f.out[0][0] / center;
    };

    // Interleave the layers; each must alternate its own two hits and never touch the other layer's.
    float lowPrev = 0.0f, highPrev = 0.0f;
    for (int round = 0; round < 6; ++round)
    {
        const float low = play (50);
        const float high = play (120);
        const bool lowOk = low == Approx (values[0] / 2) || low == Approx (values[1] / 2);
        const bool highOk = high == Approx (values[2] / 2) || high == Approx (values[3] / 2);
        REQUIRE (lowOk);
        REQUIRE (highOk);

        // Within each pair of notes, both hits of the layer must have played once.
        if (round % 2 == 1)
        {
            REQUIRE (low != Approx (lowPrev));
            REQUIRE (high != Approx (highPrev));
        }
        lowPrev = low;
        highPrev = high;
    }

    // Extra low notes must not disturb the high layer's cycle.
    const float first = play (120);
    for (int i = 0; i < 3; ++i)
        play (50);
    const float second = play (120);
    REQUIRE (second != Approx (first));
}

TEST_CASE ("Choke fades out voices choked by the triggered drum's choke value")
{
    Fixture f;
    DrumProperties open;
    open.chokedByMask = 1u << 1;
    f.configureDrum (0, 36, open);
    DrumProperties closed;
    closed.choke = 1;
    f.configureDrum (1, 38, closed);
    f.addHit (0, 0, f.makeWave (1.0f, 10000));
    f.addHit (1, 0, f.makeWave (1.0f, 10000));

    f.noteOn (36);
    f.render (16);
    REQUIRE (f.engine.activeVoiceCount() == 1);

    f.noteOn (38);
    f.render (16);
    REQUIRE (f.engine.activeVoiceCount() == 2); // choked voice still fading
    // The choked voice fades out instead of cutting off.
    REQUIRE (f.out[0][0] == Approx (2.0f * center));
    REQUIRE (f.out[0][15] < f.out[0][0]);
    REQUIRE (f.out[0][15] > center);

    f.render (200);
    REQUIRE (f.engine.activeVoiceCount() == 1);
}

TEST_CASE ("A drum choked by its own value retriggers cleanly")
{
    Fixture f;
    DrumProperties d;
    d.choke = 2;
    d.chokedByMask = 1u << 2;
    f.configureDrum (0, 36, d);
    f.addHit (0, 0, f.makeWave (1.0f, 10000));

    f.noteOn (36);
    f.render (16);
    f.noteOn (36);
    f.render (200);
    REQUIRE (f.engine.activeVoiceCount() == 1);
}

TEST_CASE ("Per-drum max polyphony steals the oldest voices")
{
    Fixture f;
    DrumProperties d;
    d.maxPoly = 2;
    f.configureDrum (0, 36, d);
    f.addHit (0, 0, f.makeWave (0.1f, 10000));

    for (int i = 0; i < 4; ++i)
    {
        f.noteOn (36);
        f.render (300);
    }
    REQUIRE (f.engine.activeVoiceCount() == 2);
}

TEST_CASE ("The global voice limit stops new hits from starting")
{
    Fixture f;
    for (int d = 0; d < maxDrums; ++d)
    {
        f.configureDrum (d, 36);
        for (int h = 0; h < 8; ++h)
            f.addHit (d, h, f.makeWave (0.01f, 10000));
    }

    f.noteOn (36);
    f.render (16);
    REQUIRE (f.engine.activeVoiceCount() == maxPolyphony);

    f.noteOn (36);
    f.render (16);
    REQUIRE (f.engine.activeVoiceCount() == maxPolyphony);
}

TEST_CASE ("Each drum can route to any of the four outputs")
{
    Fixture f;
    for (int output = 0; output < maxOutputs; ++output)
    {
        DrumProperties d;
        d.output = output;
        f.configureDrum (output, 36 + output, d);
        f.addHit (output, 0, f.makeWave (1.0f, 16));
    }
    for (int output = 0; output < maxOutputs; ++output)
    {
        f.noteOn (36 + output);
        f.render (16);
        for (int channel = 0; channel < 2 * maxOutputs; ++channel)
            REQUIRE (f.out[static_cast<size_t> (channel)][0]
                     == Approx (channel / 2 == output ? center : 0.0f).margin (1e-6));
    }
}

TEST_CASE ("Invalid slots are ignored and message references are released")
{
    Fixture f;
    auto* wave = f.makeWave (1.0f, 10);
    REQUIRE (wave->referenceCount() == 1);

    HitProperties props;
    props.wave = wave;
    props.endPos = 10;
    wave->addRef();
    REQUIRE (f.engine.pushMessage (Message::makeSetHitProperties (3, 5, props))); // hit not created
    wave->addRef();
    REQUIRE (f.engine.pushMessage (Message::makeSetHitProperties (99, -1, props)));
    REQUIRE (f.engine.pushMessage (Message::makeSetDrumProperties (-1, {})));
    REQUIRE (f.engine.pushMessage (Message::makeCreateHit (0, 99)));
    REQUIRE (f.engine.pushMessage (Message::makeClearHit (-4, 0)));
    f.render (8);
    REQUIRE (wave->referenceCount() == 1);
}

TEST_CASE ("Wave reference counts follow hits and cursors")
{
    Fixture f;
    auto* wave = f.makeWave (1.0f, 20);
    f.configureDrum (0, 36);
    f.addHit (0, 0, wave);
    f.render (4);
    REQUIRE (wave->referenceCount() == 2); // controller + hit

    f.noteOn (36);
    f.render (4);
    REQUIRE (wave->referenceCount() == 3); // + cursor

    f.render (64);
    REQUIRE (wave->referenceCount() == 2);

    REQUIRE (f.engine.pushMessage (Message::makeClearHit (0, 0)));
    f.render (4);
    REQUIRE (wave->referenceCount() == 1);
}

TEST_CASE ("A cursor keeps playing its original wave when the hit is repointed")
{
    Fixture f;
    f.configureDrum (0, 36);
    auto* a = f.makeWave (1.0f, 1000);
    auto* b = f.makeWave (0.5f, 1000);
    f.addHit (0, 0, a);
    f.noteOn (36);
    f.render (8);

    HitProperties props;
    props.wave = b;
    props.endPos = 1000;
    b->addRef();
    REQUIRE (f.engine.pushMessage (Message::makeSetHitProperties (0, 0, props)));
    f.render (8);
    REQUIRE (f.out[0][0] == Approx (center));
    REQUIRE (a->referenceCount() == 2); // controller + cursor
}

TEST_CASE ("Notes start at their sample offset within the block")
{
    Fixture f;
    f.configureDrum (0, 36);
    f.addHit (0, 0, f.makeWave (1.0f, 100));
    f.render (1);

    f.render (32, { { 10, 36, 127, true } });
    REQUIRE (f.out[0][9] == 0.0f);
    REQUIRE (f.out[0][10] == Approx (center));
}

TEST_CASE ("Note-off is ignored")
{
    Fixture f;
    f.configureDrum (0, 36);
    f.addHit (0, 0, f.makeWave (1.0f, 100));
    f.render (32, { { 0, 36, 127, true }, { 4, 36, 0, false } });
    REQUIRE (f.out[0][31] == Approx (center));
}

TEST_CASE ("Pitch changes playback speed")
{
    Fixture f;
    f.configureDrum (0, 36);
    HitProperties p;
    p.semitones = 12;
    f.addHit (0, 0, f.makeWave (1.0f, 100), p);
    f.noteOn (36);
    f.render (200);

    int nonZero = 0;
    for (float s : f.out[0])
        nonZero += s != 0.0f ? 1 : 0;
    REQUIRE (nonZero == 50);
}

TEST_CASE ("Fades are linear, measured in source samples from start and end")
{
    Fixture f;
    f.configureDrum (0, 36);
    HitProperties p;
    p.fadeInLength = 10;
    p.fadeOutLength = 10;
    f.addHit (0, 0, f.makeWave (1.0f, 100), p);
    f.noteOn (36);
    f.render (120);

    REQUIRE (f.out[0][0] == Approx (0.0f).margin (1e-6));
    REQUIRE (f.out[0][5] == Approx (0.5f * center));
    REQUIRE (f.out[0][50] == Approx (center));
    REQUIRE (f.out[0][95] == Approx (0.5f * center));
}

TEST_CASE ("Start and end positions select a portion of the wave")
{
    Fixture f;
    f.configureDrum (0, 36);
    auto* wave = f.makeWave (1.0f, 100);
    HitProperties p;
    p.wave = wave;
    p.startPos = 20;
    p.endPos = 30;
    wave->addRef();
    REQUIRE (f.engine.pushMessage (Message::makeCreateHit (0, 0)));
    REQUIRE (f.engine.pushMessage (Message::makeSetHitProperties (0, 0, p)));
    f.noteOn (36);
    f.render (40);

    int nonZero = 0;
    for (float s : f.out[0])
        nonZero += s != 0.0f ? 1 : 0;
    REQUIRE (nonZero == 10);
}

TEST_CASE ("Preview plays a portion of a wave on output 1 and releases its reference")
{
    Fixture f;
    auto* wave = f.makeWave (1.0f, 100);
    wave->addRef();
    REQUIRE (f.engine.pushMessage (Message::makePlayWave (wave, 10, 20)));
    f.render (40);

    int nonZero = 0;
    for (float s : f.out[0])
        nonZero += s != 0.0f ? 1 : 0;
    REQUIRE (nonZero == 10);
    REQUIRE (f.out[0][0] == Approx (1.0f));
    REQUIRE (f.engine.activeVoiceCount() == 0);
    REQUIRE (wave->referenceCount() == 1);
}

TEST_CASE ("Preview applies fade in and fade out")
{
    Fixture f;
    auto* wave = f.makeWave (1.0f, 100);
    wave->addRef();
    REQUIRE (f.engine.pushMessage (Message::makePlayWave (wave, 10, 30, 10, 10)));
    f.render (40);

    REQUIRE (f.out[0][0] == Approx (0.0f));
    REQUIRE (f.out[0][5] == Approx (0.5f).margin (0.02));
    REQUIRE (f.out[0][10] == Approx (1.0f));
    REQUIRE (f.out[0][15] == Approx (0.5f).margin (0.02));
}

TEST_CASE ("Velocity sensitivity scales the hit volume")
{
    auto level = [] (int sensitivity, int velocity)
    {
        Fixture f;
        f.configureDrum (0, 36);
        HitProperties h;
        h.velocitySensitivity = sensitivity;
        f.addHit (0, 0, f.makeWave (1.0f, 100), h);
        f.noteOn (36, velocity);
        f.render (4);
        return f.out[0][0] / center;
    };

    REQUIRE (level (100, 127) == Approx (1.0f));
    REQUIRE (level (100, 64) == Approx (64.0f / 127.0f));
    REQUIRE (level (100, 0) == Approx (0.0f).margin (1e-6));
    REQUIRE (level (0, 1) == Approx (1.0f));
    REQUIRE (level (50, 0) == Approx (0.5f));
    REQUIRE (level (50, 127) == Approx (1.0f));
}
