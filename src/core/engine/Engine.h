#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core/Limits.h"
#include "core/engine/Messages.h"
#include "core/engine/SpscQueue.h"

namespace drumhits
{
struct NoteEvent
{
    int sampleOffset = 0; // within the block, events must be ordered by offset
    int note = 0;
    int velocity = 0;
    bool isNoteOn = true; // note-off is currently ignored
};

// Real-time engine. Everything except pushMessage() and the lastNote accessors
// must be called from the audio thread. It never allocates after construction.
class Engine
{
public:
    explicit Engine (size_t messageQueueCapacity = 4096);
    ~Engine();

    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void prepare (double sampleRate);

    // Producer side of the control queue (single UI thread). Returns false if full.
    bool pushMessage (const Message& message) { return queue.push (message); }

    // Drains the control queue, handles notes, then renders. Output channels are
    // cleared and then filled: channels 2*n and 2*n+1 are stereo output n.
    void process (float* const* channels, int numChannels, int numSamples,
                  const NoteEvent* events = nullptr, int numEvents = 0);

    int activeVoiceCount() const noexcept;
    void seedRandom (uint32_t seed) noexcept { rngState = seed != 0 ? seed : 1u; }

    // Last note-on received (for MIDI learn); polled by the UI thread.
    int lastNote() const noexcept { return lastNoteValue.load (std::memory_order_relaxed); }
    // Incremented on every note-on. A UI consumer remembers the value it last saw and treats any
    // change as a new note, so several consumers can poll independently without resetting anything.
    uint32_t noteCounter() const noexcept { return noteCount.load (std::memory_order_relaxed); }

    // Notes received since the previous call: bit (note % 64) of word (note / 64). Clears the flags,
    // so there must be a single consumer (the drum grid, for flashing pads).
    std::array<uint64_t, 2> takeReceivedNotes() noexcept
    {
        return { receivedNotes[0].exchange (0, std::memory_order_relaxed),
                 receivedNotes[1].exchange (0, std::memory_order_relaxed) };
    }

private:
    struct Hit
    {
        bool exists = false;
        HitProperties props;
    };

    struct Drum
    {
        DrumProperties props;
        uint16_t playedMask = 0;
        std::array<Hit, maxHitsPerDrum> hits;
    };

    struct Cursor
    {
        bool active = false;
        bool killing = false;
        Wave* wave = nullptr;
        double position = 0.0;
        int64_t endPos = 0;
        int64_t startPos = 0;
        int64_t fadeInLength = 0;
        int64_t fadeOutLength = 0;
        double rate = 1.0;
        float gainL = 1.0f;
        float gainR = 1.0f;
        float killGain = 1.0f;
        int output = 0;
        int drum = -1;
        int choke = 0;
        uint16_t chokedByMask = 0;
        uint64_t age = 0;
    };

    struct Candidate
    {
        int drum;
        int hit;
    };

    void handleMessage (const Message& message);
    void handleNote (int note, int velocity);
    void startCursor (const DrumProperties* drum, int drumIndex, const HitProperties& hit, int velocity);
    void startPreview (Wave* wave, int64_t start, int64_t end, int64_t fadeIn, int64_t fadeOut);
    Cursor* allocateCursor() noexcept;
    void beginKill (Cursor& cursor) noexcept;
    void freeCursor (Cursor& cursor) noexcept;
    void renderSegment (float* const* channels, int numChannels, int start, int count);
    uint32_t nextRandom() noexcept;

    SpscQueue<Message> queue;
    std::array<Drum, maxDrums> drums;
    std::array<Cursor, maxPolyphony> cursors;
    std::array<Candidate, maxDrums * maxHitsPerDrum> candidates;

    double sampleRate = 44100.0;
    float killStep = 1.0f;
    uint64_t ageCounter = 0;
    uint32_t rngState = 0x9e3779b9u;

    std::atomic<int> lastNoteValue { -1 };
    std::atomic<uint32_t> noteCount { 0 };
    // this is a bitmap of 128 received notes (1 bit per note)
    std::array<std::atomic<uint64_t>, 2> receivedNotes {};
};
} // namespace drumhits
