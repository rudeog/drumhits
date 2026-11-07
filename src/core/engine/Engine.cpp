#include "core/engine/Engine.h"

#include <algorithm>
#include <cmath>

namespace drumhits
{
namespace
{
constexpr double fastFadeSeconds = 0.002;
constexpr double pi = 3.14159265358979323846;

float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db / 20.0f);
}

float catmullRom (const float* data, int64_t numSamples, int64_t index, float frac) noexcept
{
    auto at = [&] (int64_t i) { return data[std::clamp<int64_t> (i, 0, numSamples - 1)]; };
    const float p0 = at (index - 1);
    const float p1 = at (index);
    const float p2 = at (index + 1);
    const float p3 = at (index + 2);
    return p1 + 0.5f * frac * (p2 - p0 + frac * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + frac * (3.0f * (p1 - p2) + p3 - p0)));
}

bool isValidHit (const HitProperties& h) noexcept
{
    if (h.wave == nullptr)
        return false;
    const auto n = h.wave->numSamples();
    return n > 0 && h.startPos >= 0 && h.endPos <= n && h.startPos < h.endPos;
}
} // namespace

Engine::Engine (size_t messageQueueCapacity)
    : queue (messageQueueCapacity)
{
    prepare (sampleRate);
}

Engine::~Engine()
{
    for (auto& c : cursors)
        if (c.active)
            freeCursor (c);

    for (auto& d : drums)
        for (auto& h : d.hits)
            if (h.props.wave != nullptr)
                h.props.wave->release();

    Message m;
    while (queue.pop (m))
    {
        if (m.hitProperties.wave != nullptr)
            m.hitProperties.wave->release();
        if (m.wave != nullptr)
            m.wave->release();
    }
}

void Engine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    killStep = static_cast<float> (1.0 / std::max (1.0, sampleRate * fastFadeSeconds));
}

int Engine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& c : cursors)
        n += c.active ? 1 : 0;
    return n;
}

uint32_t Engine::nextRandom() noexcept
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

Engine::Cursor* Engine::allocateCursor() noexcept
{
    for (auto& c : cursors)
        if (! c.active)
            return &c;
    return nullptr;
}

void Engine::beginKill (Cursor& cursor) noexcept
{
    cursor.killing = true;
}

void Engine::freeCursor (Cursor& cursor) noexcept
{
    if (cursor.wave != nullptr)
        cursor.wave->release();
    cursor = Cursor {};
}

void Engine::handleMessage (const Message& m)
{
    switch (m.type)
    {
        case MessageType::noteOn:
            handleNote (m.value, m.velocity);
            break;

        case MessageType::setDrumProperties:
            if (m.drum >= 0 && m.drum < maxDrums)
                drums[static_cast<size_t> (m.drum)].props = m.drumProperties;
            break;

        case MessageType::createHit:
            if (m.drum >= 0 && m.drum < maxDrums && m.hit >= 0 && m.hit < maxHitsPerDrum)
            {
                auto& h = drums[static_cast<size_t> (m.drum)].hits[static_cast<size_t> (m.hit)];
                if (! h.exists)
                    h.exists = true;
            }
            break;

        case MessageType::clearHit:
            if (m.drum >= 0 && m.drum < maxDrums && m.hit >= 0 && m.hit < maxHitsPerDrum)
            {
                auto& d = drums[static_cast<size_t> (m.drum)];
                auto& h = d.hits[static_cast<size_t> (m.hit)];
                if (h.props.wave != nullptr)
                    h.props.wave->release();
                h = Hit {};
                d.playedMask = static_cast<uint16_t> (d.playedMask & ~(1u << m.hit));
            }
            break;

        case MessageType::setHitProperties:
        {
            Hit* h = nullptr;
            if (m.drum >= 0 && m.drum < maxDrums && m.hit >= 0 && m.hit < maxHitsPerDrum)
            {
                h = &drums[static_cast<size_t> (m.drum)].hits[static_cast<size_t> (m.hit)];
                if (! h->exists)
                    h = nullptr;
            }

            if (h == nullptr)
            {
                // Invalid target: drop the reference that came with the message.
                if (m.hitProperties.wave != nullptr)
                    m.hitProperties.wave->release();
                break;
            }

            if (h->props.wave != nullptr)
                h->props.wave->release();
            h->props = m.hitProperties;
            break;
        }

        case MessageType::playWave:
            startPreview (m.wave, m.previewStart, m.previewEnd, m.previewFadeIn, m.previewFadeOut);
            break;

        case MessageType::auditionHit:
            if (m.drum >= 0 && m.drum < maxDrums && m.hit >= 0 && m.hit < maxHitsPerDrum)
            {
                const auto& drum = drums[static_cast<size_t> (m.drum)];
                const auto& hit = drum.hits[static_cast<size_t> (m.hit)];
                if (hit.exists && isValidHit (hit.props))
                    // A drum index of -1 keeps this cursor out of drum polyphony and choke.
                    startCursor (&drum.props, -1, hit.props, m.velocity);
            }
            break;

        case MessageType::allNotesOff:
            for (auto& c : cursors)
                if (c.active)
                    beginKill (c);
            break;

        case MessageType::resetVoices:
            for (auto& c : cursors)
                if (c.active)
                    freeCursor (c);
            break;
    }
}

void Engine::startCursor (const DrumProperties* drum, int drumIndex, const HitProperties& hit, int velocity)
{
    auto* c = allocateCursor();
    if (c == nullptr)
        return;

    const float theta = (std::clamp (drum->pan, -1.0f, 1.0f) + 1.0f) * static_cast<float> (pi) * 0.25f;
    const float sensitivity = static_cast<float> (std::clamp (hit.velocitySensitivity, 0, 100)) * 0.01f;
    const float velocityScale = 1.0f - sensitivity + sensitivity * static_cast<float> (std::clamp (velocity, 0, 127)) / 127.0f;
    const float gain = dbToGain (drum->volumeDb + hit.volumeDb) * velocityScale;

    hit.wave->addRef();
    c->active = true;
    c->wave = hit.wave;
    c->position = static_cast<double> (hit.startPos);
    c->startPos = hit.startPos;
    c->endPos = hit.endPos;
    c->fadeInLength = hit.fadeInLength;
    c->fadeOutLength = hit.fadeOutLength;
    c->rate = std::pow (2.0, (hit.semitones + hit.cents / 100.0) / 12.0);
    c->gainL = gain * std::cos (theta);
    c->gainR = gain * std::sin (theta);
    c->output = std::clamp (drum->output, 0, maxOutputs - 1);
    c->drum = drumIndex;
    c->choke = drum->choke;
    c->chokedByMask = drumIndex < 0 ? 0 : drum->chokedByMask;
    c->age = ++ageCounter;
}

void Engine::startPreview (Wave* wave, int64_t start, int64_t end, int64_t fadeIn, int64_t fadeOut)
{
    auto* c = wave != nullptr ? allocateCursor() : nullptr;
    const auto n = wave != nullptr ? wave->numSamples() : 0;
    start = std::max<int64_t> (start, 0);
    end = std::min (end, n);

    if (c == nullptr || start >= end)
    {
        if (wave != nullptr)
            wave->release();
        return;
    }

    // The message's reference becomes the cursor's reference.
    c->active = true;
    c->wave = wave;
    c->position = static_cast<double> (start);
    c->startPos = start;
    c->endPos = end;
    c->fadeInLength = std::max<int64_t> (fadeIn, 0);
    c->fadeOutLength = std::max<int64_t> (fadeOut, 0);
    c->output = 0;
    c->age = ++ageCounter;
}

void Engine::handleNote (int note, int velocity)
{
    lastNoteValue.store (note, std::memory_order_relaxed);
    noteCount.fetch_add (1, std::memory_order_relaxed);
    if (note >= 0 && note < 128)
        receivedNotes[static_cast<size_t> (note >> 6)].fetch_or (uint64_t { 1 } << (note & 63), std::memory_order_relaxed);
    velocity = std::clamp (velocity, 0, 127);

    int numCandidates = 0;

    for (int d = 0; d < maxDrums; ++d)
    {
        auto& drum = drums[static_cast<size_t> (d)];
        if (drum.props.note != note)
            continue;

        int eligible[maxHitsPerDrum];
        int numEligible = 0;
        uint16_t eligibleMask = 0;

        for (int h = 0; h < maxHitsPerDrum; ++h)
        {
            const auto& hit = drum.hits[static_cast<size_t> (h)];
            if (hit.exists && isValidHit (hit.props) && velocity >= hit.props.velocityMin && velocity <= hit.props.velocityMax)
            {
                eligible[numEligible++] = h;
                eligibleMask = static_cast<uint16_t> (eligibleMask | (1u << h));
            }
        }

        if (numEligible == 0)
            continue;

        if (drum.props.playMode == PlayMode::regular)
        {
            for (int i = 0; i < numEligible; ++i)
                candidates[static_cast<size_t> (numCandidates++)] = { d, eligible[i] };
        }
        else
        {
            if ((drum.playedMask & eligibleMask) == eligibleMask)
                drum.playedMask = static_cast<uint16_t> (drum.playedMask & ~eligibleMask);

            int unplayed[maxHitsPerDrum];
            int numUnplayed = 0;
            for (int i = 0; i < numEligible; ++i)
                if ((drum.playedMask & (1u << eligible[i])) == 0)
                    unplayed[numUnplayed++] = eligible[i];

            const int pick = unplayed[nextRandom() % static_cast<uint32_t> (numUnplayed)];
            drum.playedMask = static_cast<uint16_t> (drum.playedMask | (1u << pick));
            candidates[static_cast<size_t> (numCandidates++)] = { d, pick };
        }
    }

    if (numCandidates == 0)
        return;

    // Choke: kill voices choked by any of the triggered drums' choke values.
    uint16_t triggeredChokes = 0;
    int perDrumCandidates[maxDrums] = {};
    for (int i = 0; i < numCandidates; ++i)
    {
        const auto d = candidates[static_cast<size_t> (i)].drum;
        ++perDrumCandidates[d];
        const auto choke = drums[static_cast<size_t> (d)].props.choke;
        if (choke >= 1 && choke <= maxChokeGroups)
            triggeredChokes = static_cast<uint16_t> (triggeredChokes | (1u << choke));
    }

    if (triggeredChokes != 0)
        for (auto& c : cursors)
            if (c.active && ! c.killing && (c.chokedByMask & triggeredChokes) != 0)
                beginKill (c);

    // Per-drum max polyphony: steal the oldest voices of that drum.
    int keep[maxDrums] = {};
    for (int d = 0; d < maxDrums; ++d)
    {
        const int maxPoly = drums[static_cast<size_t> (d)].props.maxPoly;
        keep[d] = maxPoly > 0 ? std::min (perDrumCandidates[d], maxPoly) : perDrumCandidates[d];

        if (maxPoly <= 0 || perDrumCandidates[d] == 0)
            continue;

        int count = 0;
        for (const auto& c : cursors)
            count += (c.active && ! c.killing && c.drum == d) ? 1 : 0;

        while (count > 0 && count + keep[d] > maxPoly)
        {
            Cursor* oldest = nullptr;
            for (auto& c : cursors)
                if (c.active && ! c.killing && c.drum == d && (oldest == nullptr || c.age < oldest->age))
                    oldest = &c;
            beginKill (*oldest);
            --count;
        }
    }

    // Start the surviving candidates. The global voice limit stops new hits.
    int started[maxDrums] = {};
    for (int i = 0; i < numCandidates; ++i)
    {
        const auto cand = candidates[static_cast<size_t> (i)];
        if (started[cand.drum] >= keep[cand.drum])
            continue;
        if (allocateCursor() == nullptr)
            break;

        ++started[cand.drum];
        const auto& drum = drums[static_cast<size_t> (cand.drum)];
        startCursor (&drum.props, cand.drum, drum.hits[static_cast<size_t> (cand.hit)].props, velocity);
    }
}

void Engine::renderSegment (float* const* channels, int numChannels, int start, int count)
{
    for (auto& c : cursors)
    {
        if (! c.active)
            continue;

        float* outL = (2 * c.output + 1 < numChannels && channels[2 * c.output] != nullptr
                       && channels[2 * c.output + 1] != nullptr) ? channels[2 * c.output] + start : nullptr;
        float* outR = outL != nullptr ? channels[2 * c.output + 1] + start : nullptr;

        const auto n = c.wave->numSamples();
        const float* srcL = c.wave->leftData();
        const float* srcR = c.wave->rightData();
        bool finished = false;

        for (int i = 0; i < count; ++i)
        {
            if (c.position >= static_cast<double> (c.endPos))
            {
                finished = true;
                break;
            }

            const auto index = static_cast<int64_t> (c.position);
            const auto frac = static_cast<float> (c.position - static_cast<double> (index));

            float gain = 1.0f;
            if (c.fadeInLength > 0)
                gain = std::min (gain, static_cast<float> (c.position - static_cast<double> (c.startPos)) / static_cast<float> (c.fadeInLength));
            if (c.fadeOutLength > 0)
                gain = std::min (gain, static_cast<float> (static_cast<double> (c.endPos) - c.position) / static_cast<float> (c.fadeOutLength));

            if (c.killing)
            {
                gain *= c.killGain;
                c.killGain -= killStep;
                if (c.killGain <= 0.0f)
                    finished = true;
            }

            if (outL != nullptr)
            {
                outL[i] += catmullRom (srcL, n, index, frac) * c.gainL * gain;
                outR[i] += catmullRom (srcR, n, index, frac) * c.gainR * gain;
            }

            c.position += c.rate;
            if (finished)
                break;
        }

        if (finished || c.position >= static_cast<double> (c.endPos))
            freeCursor (c);
    }
}

void Engine::process (float* const* channels, int numChannels, int numSamples,
                      const NoteEvent* events, int numEvents)
{
    for (int ch = 0; ch < numChannels; ++ch)
        if (channels[ch] != nullptr)
            std::fill (channels[ch], channels[ch] + numSamples, 0.0f);

    Message message;
    while (queue.pop (message))
        handleMessage (message);

    int pos = 0;
    int eventIndex = 0;
    while (pos < numSamples)
    {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset <= pos)
        {
            if (events[eventIndex].isNoteOn)
                handleNote (events[eventIndex].note, events[eventIndex].velocity);
            ++eventIndex;
        }

        const int next = eventIndex < numEvents ? std::min (numSamples, events[eventIndex].sampleOffset) : numSamples;
        renderSegment (channels, numChannels, pos, next - pos);
        pos = next;
    }
}
} // namespace drumhits
