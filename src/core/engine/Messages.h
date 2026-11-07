#pragma once

#include <cstdint>

#include "core/engine/Wave.h"

namespace drumhits
{
enum class PlayMode : uint8_t
{
    regular,
    randomRoundRobin
};

// Slots and outputs are 0-based inside the engine. Choke values are 0 (none) or 1-8.
struct DrumProperties
{
    int output = 0;
    PlayMode playMode = PlayMode::regular;
    int note = -1; // MIDI note, -1 = unassigned
    int choke = 0;
    uint16_t chokedByMask = 0; // bit g set = choked by group g (g in 1..8)
    float volumeDb = 0.0f;
    float pan = 0.0f; // -1 (left) .. 1 (right)
    int maxPoly = 0; // 0 = no max
};

struct HitProperties
{
    Wave* wave = nullptr; // a reference is transferred to the engine with the message
    int64_t startPos = 0;
    int64_t endPos = 0;
    int64_t fadeInLength = 0; // source samples, measured from start
    int64_t fadeOutLength = 0; // source samples, measured from end
    float volumeDb = 0.0f;
    int semitones = 0;
    int cents = 0;
    int velocityMin = 0;
    int velocityMax = 127;
    int velocitySensitivity = 100; // percent: 0 = volume ignores velocity, 100 = fully scaled by velocity / 127
};

enum class MessageType : uint8_t
{
    noteOn, // audition
    setDrumProperties,
    createHit,
    clearHit,
    setHitProperties,
    playWave, // preview
    auditionHit,
    resetVoices,
    allNotesOff // panic
};

// Plain fixed-size message passed through the control queue.
struct Message
{
    MessageType type = MessageType::noteOn;
    int drum = 0;
    int hit = 0;
    int value = 0; // note
    int velocity = 0;
    DrumProperties drumProperties;
    HitProperties hitProperties;
    Wave* wave = nullptr; // preview wave (reference transferred)
    int64_t previewStart = 0;
    int64_t previewEnd = 0;
    int64_t previewFadeIn = 0; // source samples, measured from start
    int64_t previewFadeOut = 0; // source samples, measured from end

    static Message makeNoteOn (int note, int velocity)
    {
        Message m;
        m.type = MessageType::noteOn;
        m.value = note;
        m.velocity = velocity;
        return m;
    }

    static Message makeAuditionHit (int drum, int hit, int velocity)
    {
        Message m;
        m.type = MessageType::auditionHit;
        m.drum = drum;
        m.hit = hit;
        m.velocity = velocity;
        return m;
    }

    static Message makeAllNotesOff()
    {
        Message m;
        m.type = MessageType::allNotesOff;
        return m;
    }

    static Message makeResetVoices()
    {
        Message m;
        m.type = MessageType::resetVoices;
        return m;
    }

    static Message makeSetDrumProperties (int drum, const DrumProperties& props)
    {
        Message m;
        m.type = MessageType::setDrumProperties;
        m.drum = drum;
        m.drumProperties = props;
        return m;
    }

    static Message makeCreateHit (int drum, int hit)
    {
        Message m;
        m.type = MessageType::createHit;
        m.drum = drum;
        m.hit = hit;
        return m;
    }

    static Message makeClearHit (int drum, int hit)
    {
        Message m;
        m.type = MessageType::clearHit;
        m.drum = drum;
        m.hit = hit;
        return m;
    }

    // The caller must have taken a reference on props.wave for this message.
    static Message makeSetHitProperties (int drum, int hit, const HitProperties& props)
    {
        Message m;
        m.type = MessageType::setHitProperties;
        m.drum = drum;
        m.hit = hit;
        m.hitProperties = props;
        return m;
    }

    // The caller must have taken a reference on wave for this message.
    static Message makePlayWave (Wave* wave, int64_t start, int64_t end, int64_t fadeIn = 0, int64_t fadeOut = 0)
    {
        Message m;
        m.type = MessageType::playWave;
        m.wave = wave;
        m.previewStart = start;
        m.previewEnd = end;
        m.previewFadeIn = fadeIn;
        m.previewFadeOut = fadeOut;
        return m;
    }
};
} // namespace drumhits
