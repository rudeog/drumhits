#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/Limits.h"
#include "core/engine/Engine.h"

namespace drumhits
{
struct LoadWaveResult
{
    enum class Status
    {
        loaded,
        alreadyLoaded, // waveId refers to the existing wave
        failed
    };

    Status status = Status::failed;
    int waveId = -1;
    juce::String error;
};

// Parameters for creating a hit. Positions are in samples of the loaded wave
// (at the host sample rate). A negative end position means "end of the wave".
struct NewHit
{
    int64_t startPos = 0;
    int64_t endPos = -1;
    HitProperties properties; // wave, start and end are filled in by the controller
};

struct SavePatchOptions
{
    bool purgeUnused = false; // only used waves are saved, and unused waves are purged from the controller afterwards
    bool consolidate = false; // copy waves next to the patch and reference them by relative path
};

// Owns waves and a copy of the engine state, and configures the engine through its
// message queue. Call from a single (UI) thread only.
//
// Lifetime: the controller owns the wave objects, and the engine releases its wave
// references on destruction, so the controller must be destroyed AFTER the engine.
class Controller
{
public:
    explicit Controller (Engine& engineToControl);
    ~Controller();

    Controller (const Controller&) = delete;
    Controller& operator= (const Controller&) = delete;

    // Reloads waves at the new rate and rescales hit positions. Failed reloads become missing waves.
    juce::Result setSampleRate (double newSampleRate);
    double getSampleRate() const noexcept { return sampleRate; }

    // --- Waves ---
    LoadWaveResult loadWave (const juce::File& file);
    // Replaces a Wave Pool wave's audio and primary path without changing its ID. All hits referencing it
    // are updated to use the replacement. Existing hit sample positions are preserved; shorter
    // replacements are padded with silence through the highest hit start/end. If the wave was missing,
    // its prior paths are retained as fallbacks; otherwise prior paths are discarded.
    juce::Result replaceWaveFile (int waveId, const juce::File& file);
    // Sets a custom display name. Empty restores the filename-derived name. Hits that still use
    // the previous default wave name are updated; custom hit names are preserved.
    bool renameWave (int waveId, const juce::String& name);
    bool hasWave (int waveId) const;
    juce::String getWaveName (int waveId) const;
    // The primary (first) path of the wave.
    juce::File getWaveFile (int waveId) const;

    // All known paths of the wave, primary first. The others are fallbacks.
    std::vector<juce::File> getWaveFiles (int waveId) const;
    int64_t getWaveLength (int waveId) const;
    int getWaveUsageCount (int waveId) const;
    std::vector<int> getWaveIds() const;

    // Read-only sample data for display. Null if unknown or missing; invalid after replacement or collection.
    const Wave* getWave (int waveId) const;

    // True if the wave's file could not be loaded. Missing waves are
    // silent placeholders so that hits keep their data.
    bool isWaveMissing (int waveId) const;
    int getMissingWaveCount() const;

    // Removes the wave and every hit that uses it. The wave is freed later by collectGarbage().
    bool removeWave (int waveId);

    // Removes every wave that no hit uses. Returns the number of waves removed.
    int purgeUnusedWaves();

    // Frees removed waves that the engine no longer references. Call periodically from the UI thread.
    void collectGarbage();

    // --- Drums ---
    // Default MIDI note of a drum: the drum's entry in the configured list, otherwise drum 0 is
    // C2 (36) and each following drum is a whole tone higher.
    int defaultDrumNote (int drum) const noexcept;

    // Sets the notes used for the drums of new patches (drum 0 first). Does not change existing drums.
    void setDefaultDrumNotes (std::vector<int> notes);

    // Sends the default configuration for all drums to the engine. Call once after the
    // engine has been constructed.
    void initialiseDefaultDrums();

    void setDrumName (int drum, const juce::String& name);
    juce::String getDrumName (int drum) const;
    void setDrumProperties (int drum, const DrumProperties& properties);
    DrumProperties getDrumProperties (int drum) const;
    // Clears a drum's hits and name, and restores default drum settings and MIDI note.
    bool resetDrum (int drum);
    // Copies settings and hits to an empty target drum and appends " copy" to the visible drum name.
    bool duplicateDrum (int sourceDrum, int targetDrum);

    // --- Hits ---
    // Creates a hit in the first free engine slot of the drum. Returns the slot, or -1 on failure
    // (invalid drum or wave, invalid range, or the drum already has maxHitsPerDrum hits).
    int addHit (int drum, int waveId, const NewHit& hit = {});
    bool removeHit (int drum, int hitSlot);

    // Copies a hit (properties and name) to the end of another drum's hits. Returns the new slot, or -1 on failure.
    int copyHit (int drum, int hitSlot, int targetDrum);
    int getHitCount (int drum) const;

    // Copies a hit into the same drum (appended to the list). The name gets a " (n)" suffix, where n is one
    // more than the highest suffix among the drum's hits with the same base name. Returns the new slot, or -1.
    int duplicateHit (int drum, int hitSlot);

    // Sets the hit volume so the hit's start-end portion peaks at 0 dB (limited to the -60..12 dB volume
    // range). Returns false if the hit is invalid or its portion is silent.
    bool normalizeHit (int drum, int hitSlot);

    // Moves the given hits one place up (towards the start of the list) or down, keeping their relative
    // order. Hits already at the edge stay put. Returns true if the order changed.
    bool moveHits (int drum, const std::vector<int>& hitSlots, bool up);

    // Divides the velocity range 0-127 evenly between the drum's hits. The first hit in the list gets
    // the highest band and the last gets the lowest.
    void splitVelocityRange (int drum);

    // Slots of the drum's hits in display order (the order they were added).
    std::vector<int> getHitSlots (int drum) const;
    std::optional<HitProperties> getHitProperties (int drum, int hitSlot) const;
    int getHitWaveId (int drum, int hitSlot) const;
    juce::String getHitName (int drum, int hitSlot) const;
    void setHitName (int drum, int hitSlot, const juce::String& name);

    // Updates a hit's properties. The wave in the given properties is ignored.
    bool setHitProperties (int drum, int hitSlot, const HitProperties& properties);
    // Changes a hit's wave and resets the range to the full wave; other properties are preserved and fades clamped.
    // Selecting the current wave leaves the hit unchanged.
    bool setHitWave (int drum, int hitSlot, int waveId);

    // --- Patch state ---
    // Serializes the whole patch (header + ValueTree) for host project state. Waves are referenced
    // by all of their absolute paths.
    juce::MemoryBlock saveState() const;

    // Replaces the whole patch. Nothing changes if the data is invalid. Waves are loaded at the
    // current sample rate, so call setSampleRate() first. A wave's paths are tried in order and the
    // first that loads becomes its primary path. Relative paths are resolved against patchDirectory.
    // If none loads, the primary filename is looked up in fallbackDirectory (e.g. the DAW project
    // directory) before the wave becomes a placeholder.
    juce::Result loadState (const void* data, size_t size, const juce::File& fallbackDirectory = {},
                            const juce::File& patchDirectory = {});

    // --- Patch files ---
    // Loads a patch file; relative wave paths are resolved against the file's directory.
    juce::Result loadPatch (const juce::File& patchFile, const juce::File& fallbackDirectory = {});

    // Writes a patch file. Fails, leaving the controller unchanged, if any wave file is missing or if
    // copying or writing fails (files already copied stay in place). On success the controller's wave
    // paths are updated to the consolidated files and unused waves are purged if requested.
    juce::Result savePatch (const juce::File& patchFile, const SavePatchOptions& options);

    // --- Engine interaction ---
    void sendNote (int note, int velocity);
    // Auditions the selected hit with drum gain, pan and output, independently of choke and polyphony.
    // Like audition notes, this may be dropped if the queue is full.
    void auditionHit (int drum, int hitSlot, int velocity);

    // Incoming MIDI (and audition) notes, for MIDI learn and pad flashing. See Engine::noteCounter()
    // and Engine::takeReceivedNotes().
    int getLastNote() const noexcept { return engine.lastNote(); }
    uint32_t getNoteCounter() const noexcept { return engine.noteCounter(); }
    std::array<uint64_t, 2> takeReceivedNotes() noexcept { return engine.takeReceivedNotes(); }

    // Plays a portion of a wave (positions in samples, fades measured from start and from end).
    // May be dropped if the queue is full.
    void previewWave (int waveId, int64_t start, int64_t end, int64_t fadeIn = 0, int64_t fadeOut = 0);

    // Panic: tells the engine to stop every playing cursor. Never dropped when the queue is full.
    void sendAllNotesOff();

    // Retries messages the engine's queue had no room for. Call periodically from the UI thread.
    void flushPendingMessages();
    bool hasPendingMessages() const noexcept { return ! pending.empty(); }
    void resetVoices();

private:
    struct WaveEntry
    {
        int id = -1;
        std::vector<juce::File> files; // absolute, primary first
        juce::String name; // custom name, empty = use the filename
        std::unique_ptr<Wave> wave;
        bool removed = false;
        bool missing = false;
    };

    struct HitEntry
    {
        bool used = false;
        int waveId = -1;
        juce::String name;
        HitProperties properties;
    };

    struct DrumEntry
    {
        juce::String name;
        DrumProperties properties;
        std::array<HitEntry, maxHitsPerDrum> hits;
        std::vector<int> order;
    };

    WaveEntry* findWave (int waveId) noexcept;
    const WaveEntry* findWave (int waveId) const noexcept;
    static bool validDrum (int drum) noexcept { return drum >= 0 && drum < maxDrums; }
    bool validHit (int drum, int hitSlot) const noexcept;
    void send (const Message& message);
    void clearHitInternal (int drum, int hitSlot);
    juce::String waveDisplayName (const WaveEntry& entry) const;
    juce::MemoryBlock serialise (const std::vector<int>& waveIdsToSave,
                                 const std::map<int, juce::StringArray>* pathOverrides) const;
    void resetPatch();

    Engine& engine;
    double sampleRate = 44100.0;
    int nextWaveId = 1;
    std::vector<int> defaultDrumNotes;
    std::vector<std::unique_ptr<WaveEntry>> waves;
    std::vector<std::unique_ptr<WaveEntry>> garbage;
    std::array<DrumEntry, maxDrums> drums;
    std::deque<Message> pending;
};
} // namespace drumhits
