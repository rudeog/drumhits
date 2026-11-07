#include "core/controller/Controller.h"
#include "core/controller/OfflineResampler.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

namespace drumhits
{
namespace
{
// Reads a WAV file into stereo float data at the target sample rate.
bool readWaveFile (const juce::File& file, double targetRate,
                   std::vector<float>& left, std::vector<float>& right, juce::String& error)
{
    juce::WavAudioFormat format;
    auto stream = file.createInputStream();
    std::unique_ptr<juce::AudioFormatReader> reader (stream != nullptr ? format.createReaderFor (stream.release(), true) : nullptr);

    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName() + " as a WAV file";
        return false;
    }

    if (reader->numChannels < 1 || reader->numChannels > 2)
    {
        error = file.getFileName() + " has an unsupported channel count (" + juce::String (static_cast<int> (reader->numChannels)) + ")";
        return false;
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = file.getFileName() + " is empty or too long";
        return false;
    }

    const auto length = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (2, length);
    buffer.clear();
    if (! reader->read (&buffer, 0, length, 0, true, true))
    {
        error = "Failed to read samples from " + file.getFileName();
        return false;
    }

    // All waves are stereo internally; mono is duplicated.
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, length);

    const auto sourceRate = reader->sampleRate;
    if (! std::isfinite (sourceRate) || sourceRate <= 0.0)
    {
        error = file.getFileName() + " has an invalid sample rate";
        return false;
    }

    if (std::abs (sourceRate - targetRate) < 1e-6)
    {
        left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
        right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
        return true;
    }

    const double ratio = sourceRate / targetRate;
    const auto outputSize = std::ceil (static_cast<double> (length) / ratio);
    if (! std::isfinite (outputSize) || outputSize > std::numeric_limits<int>::max())
    {
        error = file.getFileName() + " is too long at the target sample rate";
        return false;
    }
    const auto outLength = std::max (1, static_cast<int> (outputSize));
    left = resampleOffline (buffer.getReadPointer (0), length, outLength, ratio);
    right = resampleOffline (buffer.getReadPointer (1), length, outLength, ratio);
    return true;
}
} // namespace

Controller::Controller (Engine& engineToControl)
    : engine (engineToControl)
{
}

Controller::~Controller()
{
    // Messages that never reached the engine still hold wave references.
    for (auto& m : pending)
    {
        if (m.hitProperties.wave != nullptr)
            m.hitProperties.wave->release();
        if (m.wave != nullptr)
            m.wave->release();
    }
}

juce::Result Controller::setSampleRate (double newSampleRate)
{
    if (! std::isfinite (newSampleRate) || newSampleRate <= 0.0)
        return juce::Result::fail ("Invalid sample rate");
    if (newSampleRate == sampleRate)
        return juce::Result::ok();

    const double ratio = newSampleRate / sampleRate;
    for (const auto& entry : waves)
    {
        int64_t maxLength = entry->wave->numSamples();
        for (const auto& drum : drums)
            for (const auto& hit : drum.hits)
                if (hit.used && hit.waveId == entry->id)
                    maxLength = std::max (maxLength, hit.properties.endPos);
        if (! std::isfinite (ratio) || static_cast<double> (maxLength) * ratio + 1.0 > std::numeric_limits<int>::max())
            return juce::Result::fail ("Wave is too long at the new sample rate: " + waveDisplayName (*entry));
    }
    const auto scale = [ratio] (int64_t value)
    {
        return static_cast<int64_t> (std::llround (static_cast<double> (value) * ratio));
    };
    juce::StringArray failures;

    for (auto& entry : waves)
    {
        std::vector<float> left, right;
        size_t selected = entry->files.size();
        juce::String errors;
        for (size_t i = 0; i < entry->files.size(); ++i)
        {
            juce::String error;
            if (readWaveFile (entry->files[i], newSampleRate, left, right, error))
            {
                selected = i;
                break;
            }
            errors << entry->files[i].getFullPathName() << ": " << error << "\n";
        }

        const bool missing = selected == entry->files.size();
        int64_t requiredLength = missing ? std::max<int64_t> (1, scale (entry->wave->numSamples()))
                                         : static_cast<int64_t> (left.size());
        for (const auto& drum : drums)
            for (const auto& hit : drum.hits)
                if (hit.used && hit.waveId == entry->id)
                    requiredLength = std::max (requiredLength, std::max (scale (hit.properties.endPos),
                                                                         scale (hit.properties.startPos) + 1));

        left.resize (static_cast<size_t> (requiredLength), 0.0f);
        right.resize (static_cast<size_t> (requiredLength), 0.0f);
        auto retired = std::make_unique<WaveEntry>();
        retired->wave = std::move (entry->wave);
        retired->wave->release();
        garbage.push_back (std::move (retired));
        entry->wave = std::make_unique<Wave> (std::move (left), std::move (right));
        entry->missing = missing;
        if (missing)
            failures.add (waveDisplayName (*entry) + ": " + (errors.isEmpty() ? "No saved paths" : errors.trim()));
        else
            std::rotate (entry->files.begin(), entry->files.begin() + static_cast<std::ptrdiff_t> (selected),
                         entry->files.begin() + static_cast<std::ptrdiff_t> (selected) + 1);

        for (int d = 0; d < maxDrums; ++d)
            for (int h = 0; h < maxHitsPerDrum; ++h)
            {
                auto& hit = drums[static_cast<size_t> (d)].hits[static_cast<size_t> (h)];
                if (! hit.used || hit.waveId != entry->id)
                    continue;
                auto props = hit.properties;
                props.startPos = scale (props.startPos);
                props.endPos = std::max (props.startPos + 1, scale (props.endPos));
                props.fadeInLength = scale (props.fadeInLength);
                props.fadeOutLength = scale (props.fadeOutLength);
                setHitProperties (d, h, props);
            }
    }
    sampleRate = newSampleRate;
    return failures.isEmpty() ? juce::Result::ok() : juce::Result::fail (failures.joinIntoString ("\n"));
}

// --- Waves ---

Controller::WaveEntry* Controller::findWave (int waveId) noexcept
{
    for (auto& w : waves)
        if (w->id == waveId)
            return w.get();
    return nullptr;
}

const Controller::WaveEntry* Controller::findWave (int waveId) const noexcept
{
    return const_cast<Controller*> (this)->findWave (waveId);
}

LoadWaveResult Controller::loadWave (const juce::File& file)
{
    LoadWaveResult result;
    const auto absoluteFile = juce::File (file.getFullPathName());

    for (const auto& w : waves)
    {
        if (! w->files.empty() && w->files.front() == absoluteFile)
        {
            result.status = LoadWaveResult::Status::alreadyLoaded;
            result.waveId = w->id;
            result.error = absoluteFile.getFileName() + " is already loaded";
            return result;
        }
    }

    std::vector<float> left, right;
    if (! readWaveFile (absoluteFile, sampleRate, left, right, result.error))
        return result;

    auto entry = std::make_unique<WaveEntry>();
    entry->id = nextWaveId++;
    entry->files = { absoluteFile };
    entry->wave = std::make_unique<Wave> (std::move (left), std::move (right));

    result.status = LoadWaveResult::Status::loaded;
    result.waveId = entry->id;
    waves.push_back (std::move (entry));
    return result;
}

juce::Result Controller::replaceWaveFile (int waveId, const juce::File& file)
{
    auto* entry = findWave (waveId);
    if (entry == nullptr)
        return juce::Result::fail ("Unknown wave");

    const auto absoluteFile = juce::File (file.getFullPathName());
    auto replacementPaths = std::vector<juce::File> { absoluteFile };
    if (entry->missing)
        for (const auto& oldFile : entry->files)
            if (oldFile != absoluteFile)
                replacementPaths.push_back (oldFile);

    std::vector<float> left, right;
    juce::String error;
    if (! readWaveFile (absoluteFile, sampleRate, left, right, error))
        return juce::Result::fail (error);

    int64_t requiredLength = static_cast<int64_t> (left.size());
    for (const auto& drum : drums)
        for (const auto& hit : drum.hits)
            if (hit.used && hit.waveId == waveId)
                requiredLength = std::max ({ requiredLength, hit.properties.startPos, hit.properties.endPos });

    if (requiredLength > std::numeric_limits<int>::max())
        return juce::Result::fail ("Hit range is too long to preserve");

    left.resize (static_cast<size_t> (requiredLength), 0.0f);
    right.resize (static_cast<size_t> (requiredLength), 0.0f);
    auto replacement = std::make_unique<Wave> (std::move (left), std::move (right));

    auto retiredWave = std::make_unique<WaveEntry>();
    retiredWave->wave = std::move (entry->wave);
    retiredWave->wave->release();
    garbage.push_back (std::move (retiredWave));

    entry->wave = std::move (replacement);
    entry->files = std::move (replacementPaths);
    entry->missing = false;

    for (int drum = 0; drum < maxDrums; ++drum)
        for (int slot = 0; slot < maxHitsPerDrum; ++slot)
        {
            auto& hit = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (slot)];
            if (! hit.used || hit.waveId != waveId)
                continue;

            hit.properties.wave = entry->wave.get();
            entry->wave->addRef();
            send (Message::makeSetHitProperties (drum, slot, hit.properties));
        }

    return juce::Result::ok();
}

bool Controller::renameWave (int waveId, const juce::String& name)
{
    auto* entry = findWave (waveId);
    if (entry == nullptr)
        return false;

    const auto oldDefaultName = waveDisplayName (*entry);
    entry->name = name;
    const auto newDefaultName = waveDisplayName (*entry);

    for (auto& drum : drums)
        for (auto& hit : drum.hits)
            if (hit.used && hit.waveId == waveId && hit.name == oldDefaultName)
                hit.name = newDefaultName;

    return true;
}

bool Controller::hasWave (int waveId) const
{
    return findWave (waveId) != nullptr;
}

juce::String Controller::waveDisplayName (const WaveEntry& entry) const
{
    return entry.name.isNotEmpty() ? entry.name : (entry.files.empty() ? juce::String() : entry.files.front().getFileName());
}

juce::String Controller::getWaveName (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr ? waveDisplayName (*w) : juce::String();
}

juce::File Controller::getWaveFile (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr && ! w->files.empty() ? w->files.front() : juce::File();
}

std::vector<juce::File> Controller::getWaveFiles (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr ? w->files : std::vector<juce::File>();
}

int Controller::purgeUnusedWaves()
{
    int removed = 0;
    for (const int id : getWaveIds())
        if (getWaveUsageCount (id) == 0 && removeWave (id))
            ++removed;
    return removed;
}

int64_t Controller::getWaveLength (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr ? w->wave->numSamples() : 0;
}

const Wave* Controller::getWave (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr && ! w->missing ? w->wave.get() : nullptr;
}

int Controller::getWaveUsageCount (int waveId) const
{
    int count = 0;
    for (const auto& d : drums)
        for (const auto& h : d.hits)
            count += (h.used && h.waveId == waveId) ? 1 : 0;
    return count;
}

std::vector<int> Controller::getWaveIds() const
{
    std::vector<int> ids;
    for (const auto& w : waves)
        ids.push_back (w->id);
    return ids;
}

bool Controller::isWaveMissing (int waveId) const
{
    const auto* w = findWave (waveId);
    return w != nullptr && w->missing;
}

int Controller::getMissingWaveCount() const
{
    return static_cast<int> (std::count_if (waves.begin(), waves.end(), [] (const auto& w) { return w->missing; }));
}

bool Controller::removeWave (int waveId)
{
    const auto it = std::find_if (waves.begin(), waves.end(), [=] (const auto& w) { return w->id == waveId; });
    if (it == waves.end())
        return false;

    for (int d = 0; d < maxDrums; ++d)
        for (int h = 0; h < maxHitsPerDrum; ++h)
            if (drums[static_cast<size_t> (d)].hits[static_cast<size_t> (h)].used
                && drums[static_cast<size_t> (d)].hits[static_cast<size_t> (h)].waveId == waveId)
                clearHitInternal (d, h);

    // Drop the controller's own reference. The wave is freed once the engine releases its references too.
    (*it)->removed = true;
    (*it)->wave->release();
    garbage.push_back (std::move (*it));
    waves.erase (it);
    return true;
}

void Controller::collectGarbage()
{
    flushPendingMessages();
    std::erase_if (garbage, [] (const auto& w) { return w->wave->referenceCount() <= 0; });
}

// --- Drums ---

int Controller::defaultDrumNote (int drum) const noexcept
{
    if (drum >= 0 && drum < static_cast<int> (defaultDrumNotes.size()))
        return defaultDrumNotes[static_cast<size_t> (drum)];

    return 36 + 2 * drum;
}

void Controller::setDefaultDrumNotes (std::vector<int> notes)
{
    notes.resize (std::min (notes.size(), static_cast<size_t> (maxDrums)));
    defaultDrumNotes = std::move (notes);
}

void Controller::initialiseDefaultDrums()
{
    for (int d = 0; d < maxDrums; ++d)
    {
        DrumProperties properties;
        properties.note = defaultDrumNote (d);
        setDrumProperties (d, properties);
    }
}

void Controller::setDrumName (int drum, const juce::String& name)
{
    if (validDrum (drum))
        drums[static_cast<size_t> (drum)].name = name;
}

juce::String Controller::getDrumName (int drum) const
{
    return validDrum (drum) ? drums[static_cast<size_t> (drum)].name : juce::String();
}

void Controller::setDrumProperties (int drum, const DrumProperties& properties)
{
    if (! validDrum (drum))
        return;

    auto& d = drums[static_cast<size_t> (drum)];
    d.properties = properties;
    d.properties.output = std::clamp (d.properties.output, 0, maxOutputs - 1);
    d.properties.choke = std::clamp (d.properties.choke, 0, maxChokeGroups);
    d.properties.maxPoly = std::max (0, d.properties.maxPoly);
    send (Message::makeSetDrumProperties (drum, d.properties));
}

DrumProperties Controller::getDrumProperties (int drum) const
{
    return validDrum (drum) ? drums[static_cast<size_t> (drum)].properties : DrumProperties {};
}

bool Controller::resetDrum (int drum)
{
    if (! validDrum (drum))
        return false;

    for (const int slot : getHitSlots (drum))
        removeHit (drum, slot);

    setDrumName (drum, {});
    DrumProperties properties;
    properties.note = defaultDrumNote (drum);
    setDrumProperties (drum, properties);
    return true;
}

bool Controller::duplicateDrum (int sourceDrum, int targetDrum)
{
    if (! validDrum (sourceDrum) || ! validDrum (targetDrum) || sourceDrum == targetDrum
        || getHitCount (targetDrum) != 0)
        return false;

    const auto sourceSlots = getHitSlots (sourceDrum);
    for (const int slot : sourceSlots)
        if (! validHit (sourceDrum, slot) || findWave (getHitWaveId (sourceDrum, slot)) == nullptr)
            return false;

    auto name = getDrumName (sourceDrum);
    if (name.isEmpty())
        name = "Drum " + juce::String (sourceDrum + 1);
    setDrumName (targetDrum, name + " copy");
    setDrumProperties (targetDrum, getDrumProperties (sourceDrum));

    for (const int slot : sourceSlots)
        if (copyHit (sourceDrum, slot, targetDrum) < 0)
            return false;

    return true;
}

// --- Hits ---

bool Controller::validHit (int drum, int hitSlot) const noexcept
{
    return validDrum (drum) && hitSlot >= 0 && hitSlot < maxHitsPerDrum
           && drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)].used;
}

int Controller::addHit (int drum, int waveId, const NewHit& newHit)
{
    auto* wave = findWave (waveId);
    if (! validDrum (drum) || wave == nullptr)
        return -1;

    const auto length = wave->wave->numSamples();
    const auto start = newHit.startPos;
    const auto end = newHit.endPos < 0 ? length : newHit.endPos;
    if (start < 0 || end > length || start >= end)
        return -1;

    auto& d = drums[static_cast<size_t> (drum)];
    int slot = -1;
    for (int i = 0; i < maxHitsPerDrum; ++i)
    {
        if (! d.hits[static_cast<size_t> (i)].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    auto& hit = d.hits[static_cast<size_t> (slot)];
    hit.used = true;
    hit.waveId = waveId;
    hit.name = waveDisplayName (*wave);
    hit.properties = newHit.properties;
    hit.properties.wave = wave->missing ? nullptr : wave->wave.get();
    hit.properties.startPos = start;
    hit.properties.endPos = end;
    d.order.push_back (slot);

    send (Message::makeCreateHit (drum, slot));
    if (! wave->missing)
        wave->wave->addRef(); // the reference travels with the message to the engine
    send (Message::makeSetHitProperties (drum, slot, hit.properties));
    return slot;
}

void Controller::clearHitInternal (int drum, int hitSlot)
{
    auto& d = drums[static_cast<size_t> (drum)];
    d.hits[static_cast<size_t> (hitSlot)] = HitEntry {};
    std::erase (d.order, hitSlot);
    send (Message::makeClearHit (drum, hitSlot));
}

bool Controller::removeHit (int drum, int hitSlot)
{
    if (! validHit (drum, hitSlot))
        return false;
    clearHitInternal (drum, hitSlot);
    return true;
}

int Controller::copyHit (int drum, int hitSlot, int targetDrum)
{
    if (! validHit (drum, hitSlot) || ! validDrum (targetDrum))
        return -1;

    const auto& source = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)];
    NewHit copy;
    copy.startPos = source.properties.startPos;
    copy.endPos = source.properties.endPos;
    copy.properties = source.properties;

    const auto name = source.name;
    const int slot = addHit (targetDrum, source.waveId, copy);
    if (slot >= 0)
        setHitName (targetDrum, slot, name);
    return slot;
}

int Controller::duplicateHit (int drum, int hitSlot)
{
    if (! validHit (drum, hitSlot))
        return -1;

    const auto& source = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)];
    NewHit copy;
    copy.startPos = source.properties.startPos;
    copy.endPos = source.properties.endPos;
    copy.properties = source.properties;
    const auto sourceName = source.name;

    // Split "Name (3)" into "Name" and 3. An unsuffixed name counts as number 1.
    const auto parse = [] (const juce::String& name)
    {
        const auto t = name.trim();
        if (t.endsWithChar (')'))
        {
            const int open = t.lastIndexOfChar ('(');
            const auto digits = open > 0 ? t.substring (open + 1, t.length() - 1) : juce::String();
            if (open > 0 && t[open - 1] == ' ' && digits.isNotEmpty() && digits.containsOnly ("0123456789") && digits.length() < 6)
                return std::make_pair (t.substring (0, open - 1).trim(), digits.getIntValue());
        }
        return std::make_pair (t, 1);
    };

    const auto base = parse (sourceName).first;
    int highest = 1;
    for (const int slot : drums[static_cast<size_t> (drum)].order)
    {
        const auto [otherBase, number] = parse (drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (slot)].name);
        if (otherBase == base)
            highest = std::max (highest, number);
    }

    const int newSlot = addHit (drum, source.waveId, copy);
    if (newSlot >= 0)
        setHitName (drum, newSlot, base + " (" + juce::String (highest + 1) + ")");
    return newSlot;
}

bool Controller::normalizeHit (int drum, int hitSlot)
{
    if (! validHit (drum, hitSlot))
        return false;

    const auto& hit = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)];
    const auto* wave = getWave (hit.waveId);
    if (wave == nullptr)
        return false;

    const auto start = std::clamp<int64_t> (hit.properties.startPos, 0, wave->numSamples());
    const auto end = std::clamp<int64_t> (hit.properties.endPos, start, wave->numSamples());
    float peak = 0.0f;
    for (auto i = start; i < end; ++i)
        peak = std::max ({ peak, std::abs (wave->leftData()[i]), std::abs (wave->rightData()[i]) });

    if (peak <= 0.0f)
        return false;

    auto props = hit.properties;
    props.volumeDb = std::clamp (-20.0f * std::log10 (peak), -60.0f, 12.0f);
    return setHitProperties (drum, hitSlot, props);
}

bool Controller::moveHits (int drum, const std::vector<int>& hitSlots, bool up)
{
    if (! validDrum (drum))
        return false;

    auto& order = drums[static_cast<size_t> (drum)].order;
    const auto selected = [&] (int slot) { return std::find (hitSlots.begin(), hitSlots.end(), slot) != hitSlots.end(); };
    bool changed = false;

    if (up)
    {
        for (size_t i = 1; i < order.size(); ++i)
            if (selected (order[i]) && ! selected (order[i - 1]))
            {
                std::swap (order[i], order[i - 1]);
                changed = true;
            }
    }
    else
    {
        for (size_t i = order.size(); i-- > 1;)
            if (selected (order[i - 1]) && ! selected (order[i]))
            {
                std::swap (order[i], order[i - 1]);
                changed = true;
            }
    }
    return changed;
}

void Controller::splitVelocityRange (int drum)
{
    if (! validDrum (drum))
        return;

    const auto order = drums[static_cast<size_t> (drum)].order;
    const int count = static_cast<int> (order.size());

    for (int i = 0; i < count; ++i)
    {
        const int band = count - 1 - i;
        auto props = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (order[static_cast<size_t> (i)])].properties;
        props.velocityMin = band * 128 / count;
        props.velocityMax = (band + 1) * 128 / count - 1;
        setHitProperties (drum, order[static_cast<size_t> (i)], props);
    }
}

int Controller::getHitCount (int drum) const
{
    return validDrum (drum) ? static_cast<int> (drums[static_cast<size_t> (drum)].order.size()) : 0;
}

std::vector<int> Controller::getHitSlots (int drum) const
{
    return validDrum (drum) ? drums[static_cast<size_t> (drum)].order : std::vector<int> {};
}

std::optional<HitProperties> Controller::getHitProperties (int drum, int hitSlot) const
{
    if (! validHit (drum, hitSlot))
        return std::nullopt;
    return drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)].properties;
}

int Controller::getHitWaveId (int drum, int hitSlot) const
{
    return validHit (drum, hitSlot) ? drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)].waveId : -1;
}

juce::String Controller::getHitName (int drum, int hitSlot) const
{
    return validHit (drum, hitSlot) ? drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)].name : juce::String();
}

void Controller::setHitName (int drum, int hitSlot, const juce::String& name)
{
    if (validHit (drum, hitSlot))
        drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)].name = name;
}

bool Controller::setHitProperties (int drum, int hitSlot, const HitProperties& properties)
{
    if (! validHit (drum, hitSlot))
        return false;

    auto& hit = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)];
    auto* wave = findWave (hit.waveId);
    const auto length = wave->wave->numSamples();
    if (properties.startPos < 0 || properties.endPos > length || properties.startPos >= properties.endPos)
        return false;

    auto updated = properties;
    updated.wave = wave->missing ? nullptr : wave->wave.get();
    updated.fadeInLength = std::max<int64_t> (0, updated.fadeInLength);
    updated.fadeOutLength = std::max<int64_t> (0, updated.fadeOutLength);
    updated.velocityMin = std::clamp (updated.velocityMin, 0, 127);
    updated.velocityMax = std::clamp (updated.velocityMax, updated.velocityMin, 127);
    updated.velocitySensitivity = std::clamp (updated.velocitySensitivity, 0, 100);

    // Fades are offsets from start and end and must fit in the hit's span.
    const auto span = updated.endPos - updated.startPos;
    updated.fadeInLength = std::min (updated.fadeInLength, span);
    updated.fadeOutLength = std::min (updated.fadeOutLength, span - updated.fadeInLength);

    hit.properties = updated;
    if (! wave->missing)
        wave->wave->addRef();
    send (Message::makeSetHitProperties (drum, hitSlot, updated));
    return true;
}

bool Controller::setHitWave (int drum, int hitSlot, int waveId)
{
    if (! validHit (drum, hitSlot))
        return false;

    auto* wave = findWave (waveId);
    if (wave == nullptr || wave->missing || wave->wave->numSamples() <= 0)
        return false;

    auto& hit = drums[static_cast<size_t> (drum)].hits[static_cast<size_t> (hitSlot)];
    if (hit.waveId == waveId)
        return true;

    auto properties = hit.properties;
    properties.startPos = 0;
    properties.endPos = wave->wave->numSamples();

    const int oldWaveId = hit.waveId;
    hit.waveId = waveId;
    if (! setHitProperties (drum, hitSlot, properties))
    {
        hit.waveId = oldWaveId;
        return false;
    }

    return true;
}

// --- Patch state ---

namespace
{
constexpr juce::uint32 patchMagic = 0x44485054; // 'DHPT'
constexpr int patchVersion = 1;
} // namespace

juce::MemoryBlock Controller::saveState() const
{
    std::vector<int> ids;
    for (const auto& w : waves)
        ids.push_back (w->id);
    return serialise (ids, nullptr);
}

// Writes the given waves (in controller order). Path overrides replace a wave's paths in the output.
juce::MemoryBlock Controller::serialise (const std::vector<int>& waveIdsToSave,
                                         const std::map<int, juce::StringArray>* pathOverrides) const
{
    juce::ValueTree patch ("Patch");
    patch.setProperty ("sampleRate", sampleRate, nullptr);

    for (const auto& w : waves)
    {
        if (std::find (waveIdsToSave.begin(), waveIdsToSave.end(), w->id) == waveIdsToSave.end())
            continue;

        juce::StringArray paths;
        if (pathOverrides != nullptr && pathOverrides->count (w->id) > 0)
            paths = pathOverrides->at (w->id);
        else
            for (const auto& f : w->files)
                paths.add (f.getFullPathName());

        juce::ValueTree wave ("Wave");
        wave.setProperty ("id", w->id, nullptr);
        wave.setProperty ("name", w->name, nullptr);
        for (const auto& path : paths)
        {
            juce::ValueTree node ("Path");
            node.setProperty ("value", path, nullptr);
            wave.appendChild (node, nullptr);
        }
        wave.setProperty ("length", w->wave->numSamples(), nullptr);
        patch.appendChild (wave, nullptr);
    }

    for (int i = 0; i < maxDrums; ++i)
    {
        const auto& d = drums[static_cast<size_t> (i)];
        juce::ValueTree drum ("Drum");
        drum.setProperty ("name", d.name, nullptr);
        drum.setProperty ("output", d.properties.output, nullptr);
        drum.setProperty ("playMode", static_cast<int> (d.properties.playMode), nullptr);
        drum.setProperty ("note", d.properties.note, nullptr);
        drum.setProperty ("choke", d.properties.choke, nullptr);
        drum.setProperty ("chokedBy", static_cast<int> (d.properties.chokedByMask), nullptr);
        drum.setProperty ("volumeDb", d.properties.volumeDb, nullptr);
        drum.setProperty ("pan", d.properties.pan, nullptr);
        drum.setProperty ("maxPoly", d.properties.maxPoly, nullptr);

        for (const int slot : d.order)
        {
            const auto& h = d.hits[static_cast<size_t> (slot)];
            juce::ValueTree hit ("Hit");
            hit.setProperty ("name", h.name, nullptr);
            hit.setProperty ("wave", h.waveId, nullptr);
            hit.setProperty ("start", h.properties.startPos, nullptr);
            hit.setProperty ("end", h.properties.endPos, nullptr);
            hit.setProperty ("fadeIn", h.properties.fadeInLength, nullptr);
            hit.setProperty ("fadeOut", h.properties.fadeOutLength, nullptr);
            hit.setProperty ("volumeDb", h.properties.volumeDb, nullptr);
            hit.setProperty ("semitones", h.properties.semitones, nullptr);
            hit.setProperty ("cents", h.properties.cents, nullptr);
            hit.setProperty ("velMin", h.properties.velocityMin, nullptr);
            hit.setProperty ("velMax", h.properties.velocityMax, nullptr);
            hit.setProperty ("velSens", h.properties.velocitySensitivity, nullptr);
            drum.appendChild (hit, nullptr);
        }
        patch.appendChild (drum, nullptr);
    }

    juce::MemoryBlock block;
    juce::MemoryOutputStream out (block, false);
    out.writeInt (static_cast<int> (patchMagic));
    out.writeInt (patchVersion);
    patch.writeToStream (out);
    return block;
}

void Controller::resetPatch()
{
    for (const int id : getWaveIds())
        removeWave (id);

    for (int d = 0; d < maxDrums; ++d)
        drums[static_cast<size_t> (d)].name = {};

    initialiseDefaultDrums();
}

juce::Result Controller::loadState (const void* data, size_t size, const juce::File& fallbackDirectory,
                                  const juce::File& patchDirectory)
{
    juce::MemoryInputStream in (data, size, false);
    if (in.readInt() != static_cast<int> (patchMagic))
        return juce::Result::fail ("Not a DrumHits patch");

    const int version = in.readInt();
    if (version < 1 || version > patchVersion)
        return juce::Result::fail ("Unsupported patch version " + juce::String (version));

    const auto patch = juce::ValueTree::readFromStream (in);
    if (! patch.hasType ("Patch"))
        return juce::Result::fail ("Corrupt patch data");

    resetPatch();

    const double savedRate = static_cast<double> (patch.getProperty ("sampleRate", sampleRate));
    const double ratio = savedRate > 0.0 ? sampleRate / savedRate : 1.0;
    const auto scale = [ratio] (int64_t v) { return static_cast<int64_t> (std::llround (static_cast<double> (v) * ratio)); };

    juce::String problems;
    std::map<int, int> waveIds; // saved id -> new id

    for (const auto& w : patch)
    {
        if (! w.hasType ("Wave"))
            continue;

        // Saved paths, in priority order. Relative paths are relative to the patch directory.
        juce::StringArray savedPaths;
        for (const auto& node : w)
            if (node.hasType ("Path"))
                savedPaths.add (node.getProperty ("value").toString());
        if (savedPaths.isEmpty() && w.hasProperty ("path"))
            savedPaths.add (w.getProperty ("path").toString());

        const auto base = patchDirectory != juce::File() ? patchDirectory : juce::File::getCurrentWorkingDirectory();
        std::vector<juce::File> candidates;
        for (const auto& p : savedPaths)
        {
            if (p.isEmpty())
                continue;
            const auto f = juce::File::isAbsolutePath (p) ? juce::File (p) : base.getChildFile (p);
            if (std::find (candidates.begin(), candidates.end(), f) == candidates.end())
                candidates.push_back (f);
        }

        if (fallbackDirectory != juce::File() && ! candidates.empty())
        {
            const auto inFallback = fallbackDirectory.getChildFile (candidates.front().getFileName());
            if (inFallback.existsAsFile()
                && std::find (candidates.begin(), candidates.end(), inFallback) == candidates.end())
                candidates.push_back (inFallback);
        }

        int newId = -1;
        for (size_t i = 0; i < candidates.size() && newId < 0; ++i)
        {
            if (! candidates[i].existsAsFile())
                continue;

            const auto result = loadWave (candidates[i]);
            if (result.status != LoadWaveResult::Status::failed)
            {
                newId = result.waveId;
                if (result.status == LoadWaveResult::Status::loaded)
                {
                    // The path that worked becomes primary; the rest stay as fallbacks.
                    auto ordered = candidates;
                    std::rotate (ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t> (i), ordered.begin() + static_cast<std::ptrdiff_t> (i) + 1);
                    findWave (newId)->files = std::move (ordered);
                }
            }
            else
                problems << result.error << "\n";
        }

        if (newId < 0)
        {
            const auto length = static_cast<size_t> (std::max<int64_t> (1, scale (w.getProperty ("length"))));
            auto entry = std::make_unique<WaveEntry>();
            entry->id = nextWaveId++;
            entry->files = candidates;
            entry->missing = true;
            entry->wave = std::make_unique<Wave> (std::vector<float> (length, 0.0f), std::vector<float> (length, 0.0f));
            newId = entry->id;
            waves.push_back (std::move (entry));
            problems << "Missing file " << (savedPaths.isEmpty() ? juce::String ("(no path)") : savedPaths[0]) << "\n";
        }

        findWave (newId)->name = w.getProperty ("name").toString();
        waveIds[static_cast<int> (w.getProperty ("id"))] = newId;
    }

    int drumIndex = 0;
    for (const auto& d : patch)
    {
        if (! d.hasType ("Drum") || drumIndex >= maxDrums)
            continue;

        const int drum = drumIndex++;
        setDrumName (drum, d.getProperty ("name").toString());

        DrumProperties p;
        p.output = d.getProperty ("output", 0);
        p.playMode = static_cast<int> (d.getProperty ("playMode", 0)) == 1 ? PlayMode::randomRoundRobin : PlayMode::regular;
        p.note = d.getProperty ("note", -1);
        p.choke = d.getProperty ("choke", 0);
        p.chokedByMask = static_cast<uint16_t> (static_cast<int> (d.getProperty ("chokedBy", 0)));
        p.volumeDb = static_cast<float> (static_cast<double> (d.getProperty ("volumeDb", 0.0)));
        p.pan = static_cast<float> (static_cast<double> (d.getProperty ("pan", 0.0)));
        p.maxPoly = d.getProperty ("maxPoly", 0);
        setDrumProperties (drum, p);

        for (const auto& h : d)
        {
            if (! h.hasType ("Hit"))
                continue;

            const auto it = waveIds.find (static_cast<int> (h.getProperty ("wave", -1)));
            if (it == waveIds.end())
                continue;

            const auto length = findWave (it->second)->wave->numSamples();
            HitProperties props;
            props.startPos = std::clamp<int64_t> (scale (h.getProperty ("start")), 0, length - 1);
            props.endPos = std::clamp<int64_t> (scale (h.getProperty ("end")), props.startPos + 1, length);
            props.fadeInLength = scale (h.getProperty ("fadeIn"));
            props.fadeOutLength = scale (h.getProperty ("fadeOut"));
            props.volumeDb = static_cast<float> (static_cast<double> (h.getProperty ("volumeDb", 0.0)));
            props.semitones = h.getProperty ("semitones", 0);
            props.cents = h.getProperty ("cents", 0);
            props.velocityMin = h.getProperty ("velMin", 0);
            props.velocityMax = h.getProperty ("velMax", 127);
            props.velocitySensitivity = h.getProperty ("velSens", 100);

            const int slot = addHit (drum, it->second);
            if (slot < 0)
                continue;

            setHitProperties (drum, slot, props);
            setHitName (drum, slot, h.getProperty ("name").toString());
        }
    }

    if (problems.isNotEmpty())
        juce::Logger::writeToLog (problems.trim());

    return juce::Result::ok();
}

// --- Patch files ---

juce::Result Controller::loadPatch (const juce::File& patchFile, const juce::File& fallbackDirectory)
{
    juce::MemoryBlock data;
    if (! patchFile.existsAsFile() || ! patchFile.loadFileAsData (data))
        return juce::Result::fail ("Could not read " + patchFile.getFullPathName());

    return loadState (data.getData(), data.getSize(), fallbackDirectory, patchFile.getParentDirectory());
}

juce::Result Controller::savePatch (const juce::File& patchFile, const SavePatchOptions& options)
{
    for (const auto& w : waves)
        if (w->missing || w->files.empty() || ! w->files.front().existsAsFile())
            return juce::Result::fail ("Cannot save: the file for wave \"" + waveDisplayName (*w) + "\" is missing");

    std::vector<int> ids;
    for (const auto& w : waves)
        if (! options.purgeUnused || getWaveUsageCount (w->id) > 0)
            ids.push_back (w->id);

    const auto patchDir = patchFile.getParentDirectory();
    std::map<int, juce::StringArray> overrides;
    std::map<int, juce::File> newFiles;

    if (options.consolidate)
    {
        const auto dataDir = patchFile.getSiblingFile (patchFile.getFileNameWithoutExtension() + "_dh_data");
        if (! dataDir.createDirectory())
            return juce::Result::fail ("Could not create " + dataDir.getFullPathName());

        std::set<juce::String> usedNames;
        for (const int id : ids)
        {
            const auto source = findWave (id)->files.front();
            const auto stem = source.getFileNameWithoutExtension();
            const auto ext = source.getFileExtension();

            auto name = source.getFileName();
            for (int n = 1; usedNames.count (name.toLowerCase()) > 0; ++n)
                name = stem + "_" + juce::String (n) + ext;
            usedNames.insert (name.toLowerCase());

            const auto target = dataDir.getChildFile (name);
            if (! source.copyFileTo (target))
                return juce::Result::fail ("Could not copy " + source.getFullPathName() + " to " + target.getFullPathName());

            newFiles[id] = target;
            overrides[id] = juce::StringArray (target.getRelativePathFrom (patchDir));
        }
    }

    const auto data = serialise (ids, options.consolidate ? &overrides : nullptr);
    if (! patchFile.replaceWithData (data.getData(), data.getSize()))
        return juce::Result::fail ("Could not write " + patchFile.getFullPathName());

    // Only now is the live state changed.
    for (const auto& [id, file] : newFiles)
        findWave (id)->files = { file };

    if (options.purgeUnused)
        purgeUnusedWaves();

    return juce::Result::ok();
}

// --- Engine interaction ---

void Controller::sendNote (int note, int velocity)
{
    send (Message::makeNoteOn (note, velocity)); // audition notes may be dropped if the queue is full
}

void Controller::auditionHit (int drum, int hitSlot, int velocity)
{
    if (validHit (drum, hitSlot))
        send (Message::makeAuditionHit (drum, hitSlot, velocity));
}

void Controller::previewWave (int waveId, int64_t start, int64_t end, int64_t fadeIn, int64_t fadeOut)
{
    auto* entry = findWave (waveId);
    if (entry == nullptr || entry->removed || entry->missing || entry->wave == nullptr)
        return;

    entry->wave->addRef(); // reference for the message
    send (Message::makePlayWave (entry->wave.get(), start, end, fadeIn, fadeOut));
}

void Controller::sendAllNotesOff()
{
    send (Message::makeAllNotesOff());
}

void Controller::resetVoices()
{
    send (Message::makeResetVoices());
}

void Controller::send (const Message& message)
{
    flushPendingMessages();

    const bool droppable = message.type == MessageType::noteOn || message.type == MessageType::playWave
                           || message.type == MessageType::auditionHit;
    if (pending.empty() && engine.pushMessage (message))
        return;

    if (droppable)
    {
        if (message.wave != nullptr)
            message.wave->release();
        return;
    }

    pending.push_back (message);
}

void Controller::flushPendingMessages()
{
    while (! pending.empty() && engine.pushMessage (pending.front()))
        pending.pop_front();
}
} // namespace drumhits
